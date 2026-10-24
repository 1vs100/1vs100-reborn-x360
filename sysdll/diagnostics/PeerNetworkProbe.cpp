/* Couch-join peer network watch, 1149. Log-only. Join marks members pending until a type-12 slot-state event validates them. */
#include "DiagnosticTables.h"
#include "../registry/PatchHelpers.h"
#include "../platform/Log.h"
#include "../title/TitleLayout.h"

#define PROBE_SESSION_SLOTS 0x17C     /* member per local slot */
#define PROBE_MEMBER_PENDING 0x80u
#define PROBE_EVENT_TYPE 4
#define PROBE_EVENT_TYPES 16
#define PROBE_SUMMARY_MS 30000

typedef int(__fastcall *SessionJoinLocal_t)(
	DWORD session, DWORD user, DWORD a3, DWORD a4, DWORD a5, DWORD a6, DWORD a7, DWORD a8);
typedef int(__fastcall *SessionPeerEvents_t)(DWORD session);
typedef int(__fastcall *PeerNextEvent_t)(DWORD peerNet, DWORD a2, DWORD *event);
typedef int(__fastcall *PeerTransportPump_t)(DWORD transport, DWORD a2);
typedef int(__fastcall *PeerStop_t)(DWORD peerNet, DWORD a4, DWORD reason, DWORD a6);
extern "C" void *_ReturnAddress(void);
#pragma intrinsic(_ReturnAddress)
static SessionJoinLocal_t g_JoinLocalOriginal = NULL;
static SessionPeerEvents_t g_PeerEventsOriginal = NULL;
static PeerNextEvent_t g_NextEventOriginal = NULL;
static PeerTransportPump_t g_TransportPumpOriginal = NULL;
static PeerStop_t g_PeerStopOriginal = NULL;
/* Type 7 carries old/new state and reason; any new state but started tears down. */
#define PROBE_EVENT_OLD_STATE 0x10
#define PROBE_EVENT_NEW_STATE 0x14
#define PROBE_EVENT_REASON 0x18

/* QNet thread only. */
static DWORD g_SlotMember[4];
static DWORD g_SlotFlags[4];
static DWORD g_NextEventCalls = 0;
static DWORD g_EventsByType[PROBE_EVENT_TYPES];
static int g_LastNextEventRc = 0;
static DWORD g_PumpCalls = 0;
static int g_LastPumpRc = 0;
static volatile LONG g_PumpRcChanges = 0;
static volatile LONG g_OtherEventLines = 0;
static volatile LONG g_OtherEvents = 0;
static DWORD g_NextSummaryTick = 0;

static VOID LogFirstCall(volatile DWORD *thread, const char *what)
{
	if (*thread == 0) {
		*thread = (DWORD)GetCurrentThreadId();
		LOG_INFO("qnet", "peer probe: %s first call thread=%08X", what, *thread);
	}
}

static VOID LogSessionHead(const char *why, DWORD session)
{
	LOG_INFO("qnet",
		"peer probe: %s session %08X state %u type %u xam %08X peernet %08X flags %08X count %u", why,
		session, *(volatile DWORD *)(session + Session::kState),
		*(volatile DWORD *)(session + Session::kType),
		*(volatile DWORD *)(session + Session::kXamHandle),
		*(volatile DWORD *)(session + Session::kPeerNet),
		*(volatile DWORD *)(session + Session::kFlags),
		*(volatile DWORD *)(session + Session::kMemberCount));
}

/* One line per changed slot. */
static VOID LogSlotChanges(const char *why, DWORD session)
{
	DWORD i;
	for (i = 0; i < 4; ++i) {
		DWORD member = *(volatile DWORD *)(session + PROBE_SESSION_SLOTS + 4 * i);
		DWORD flags = member ? *(volatile DWORD *)(member + Member::kFlags) : 0;
		if (member == g_SlotMember[i] && flags == g_SlotFlags[i])
			continue;
		LOG_INFO("qnet", "peer probe: %s slot %u member %08X flags %08X -> %08X%s%s count %u", why,
			i, member, g_SlotFlags[i], flags, (flags & PROBE_MEMBER_PENDING) ? " pending" : "",
			(flags & Member::kFlagValid) ? " valid" : "",
			*(volatile DWORD *)(session + Session::kMemberCount));
		g_SlotMember[i] = member;
		g_SlotFlags[i] = flags;
	}
}

static int __fastcall SessionJoinLocalHook(
	DWORD session, DWORD user, DWORD a3, DWORD a4, DWORD a5, DWORD a6, DWORD a7, DWORD a8)
{
	static volatile DWORD thread = 0;
	int hr;
	LogFirstCall(&thread, "local join");
	LogSessionHead("local join before", session);
	LOG_INFO("qnet", "peer probe: local join user %u", user);
	hr = g_JoinLocalOriginal(session, user, a3, a4, a5, a6, a7, a8);
	LOG_INFO("qnet", "peer probe: local join user %u -> hr=%08X", user, (DWORD)hr);
	LogSlotChanges("local join", session);
	return hr;
}

static int __fastcall SessionPeerEventsHook(DWORD session)
{
	static volatile DWORD thread = 0;
	int rc;
	LogFirstCall(&thread, "event drain");
	rc = g_PeerEventsOriginal(session);
	LogSlotChanges("event drain", session);
	return rc;
}

/* The drain handles 1, 7-12; the rest count as other. */
static VOID LogSummary()
{
	DWORD now = GetTickCount(), i, other = 0;
	if (g_NextSummaryTick == 0)
		g_NextSummaryTick = now + PROBE_SUMMARY_MS;
	if ((LONG)(now - g_NextSummaryTick) < 0)
		return;
	g_NextSummaryTick = now + PROBE_SUMMARY_MS;
	for (i = 0; i < PROBE_EVENT_TYPES; ++i)
		if (i != 1 && (i < 7 || i > 12))
			other += g_EventsByType[i];
	LOG_INFO("qnet",
		"peer probe: 30 s: next-event calls %u rc %08X events 1:%u 7:%u 8:%u 9:%u 10:%u 11:%u "
		"12:%u other:%u; transport pumps %u rc %08X",
		g_NextEventCalls, (DWORD)g_LastNextEventRc, g_EventsByType[1], g_EventsByType[7],
		g_EventsByType[8], g_EventsByType[9], g_EventsByType[10], g_EventsByType[11],
		g_EventsByType[12], other, g_PumpCalls, (DWORD)g_LastPumpRc);
	for (i = 0; i < PROBE_EVENT_TYPES; ++i)
		g_EventsByType[i] = 0;
	g_NextEventCalls = 0;
	g_PumpCalls = 0;
}

static int __fastcall PeerNextEventHook(DWORD peerNet, DWORD a2, DWORD *event)
{
	static volatile DWORD thread = 0;
	int rc;
	LogFirstCall(&thread, "next event");
	rc = g_NextEventOriginal(peerNet, a2, event);
	g_NextEventCalls++;
	g_LastNextEventRc = rc;
	if (event && *event) {
		DWORD type = *(volatile DWORD *)(*event + PROBE_EVENT_TYPE);
		g_EventsByType[type < PROBE_EVENT_TYPES ? type : 0]++; /* out of range */
		/* Type 12 always; the rest sampled. */
		if (type == 12 || Log_First(&g_OtherEventLines, 32) || Log_Every(&g_OtherEvents, 64))
			LOG_INFO("qnet", "peer probe: peernet %08X event type %u rc %08X", peerNet, type,
				(DWORD)rc);
		if (type == 7)
			LOG_INFO("qnet", "peer probe: state change %u -> %u reason %08X",
				*(volatile DWORD *)(*event + PROBE_EVENT_OLD_STATE),
				*(volatile DWORD *)(*event + PROBE_EVENT_NEW_STATE),
				*(volatile DWORD *)(*event + PROBE_EVENT_REASON));
	}
	LogSummary();
	return rc;
}

static int __fastcall PeerTransportPumpHook(DWORD transport, DWORD a2)
{
	static volatile DWORD thread = 0;
	int rc;
	LogFirstCall(&thread, "transport pump");
	rc = g_TransportPumpOriginal(transport, a2);
	g_PumpCalls++;
	if (rc != g_LastPumpRc && Log_First(&g_PumpRcChanges, 16))
		LOG_INFO("qnet", "peer probe: transport %08X pump rc %08X -> %08X", transport,
			(DWORD)g_LastPumpRc, (DWORD)rc);
	g_LastPumpRc = rc;
	return rc;
}

/* Every stop request with its caller. */
static int __fastcall PeerStopHook(DWORD peerNet, DWORD a4, DWORD reason, DWORD a6)
{
	DWORD caller = (DWORD)_ReturnAddress();
	/* Heuristic chain: words above the frame pointing into title code. */
	volatile DWORD probe = 0;
	const DWORD *sp = (const DWORD *)&probe;
	DWORD found[12], n = 0, i = 0;
	while (i < 1024 && n < 12) {
		DWORD v = sp[i];
		if (v >= 0x82100000u && v < 0x82600000u && (v & 3) == 0)
			found[n++] = v;
		i++;
	}
	while (n < 12)
		found[n++] = 0;
	LOG_WARN("qnet", "peer probe: peer network %08X stop requested by %08X reason %08X (%u, %u)",
		peerNet, caller, reason, a4, a6);
	LOG_WARN("qnet",
		"peer probe: stop stack %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X",
		found[0], found[1], found[2], found[3], found[4], found[5], found[6], found[7], found[8],
		found[9], found[10], found[11]);
	return g_PeerStopOriginal(peerNet, a4, reason, a6);
}

static VOID InstallPeerStopLog(const PatchDescriptor *self)
{
	HOOK_SITE(SITE_PEER_STOP, PeerStopHook, g_PeerStopOriginal, self->id);
}

static VOID InstallJoinLocalLog(const PatchDescriptor *self)
{
	HOOK_SITE(SITE_SESSION_JOIN_LOCAL, SessionJoinLocalHook, g_JoinLocalOriginal, self->id);
}

static VOID InstallPeerEventsLog(const PatchDescriptor *self)
{
	HOOK_SITE(SITE_SESSION_PEER_EVENTS, SessionPeerEventsHook, g_PeerEventsOriginal, self->id);
}

static VOID InstallNextEventLog(const PatchDescriptor *self)
{
	HOOK_SITE(SITE_PEER_NEXT_EVENT, PeerNextEventHook, g_NextEventOriginal, self->id);
}

static VOID InstallTransportPumpLog(const PatchDescriptor *self)
{
	HOOK_SITE(SITE_PEER_TRANSPORT_PUMP, PeerTransportPumpHook, g_TransportPumpOriginal, self->id);
}

/* Reset per title. */
static VOID ResetPeerNetworkProbe()
{
	DWORD i;
	for (i = 0; i < 4; ++i) {
		g_SlotMember[i] = 0;
		g_SlotFlags[i] = 0;
	}
	for (i = 0; i < PROBE_EVENT_TYPES; ++i)
		g_EventsByType[i] = 0;
	g_NextEventCalls = 0;
	g_LastNextEventRc = 0;
	g_PumpCalls = 0;
	g_LastPumpRc = 0;
	g_PumpRcChanges = 0;
	g_OtherEventLines = 0;
	g_OtherEvents = 0;
	g_NextSummaryTick = 0;
}

static const PatchDescriptor kRows[] = {
	{"session local join log", "session_join_local_log", ON_TITLE,
		"Logs the local join: type, handle, peer network, slots",
		InstallJoinLocalLog, ResetPeerNetworkProbe},
	{"session peer event log", "session_peer_events_log", ON_TITLE,
		"Logs slot flag changes per event drain",
		InstallPeerEventsLog},
	{"peer network event log", "peer_next_event_log", ON_TITLE,
		"Logs peer-network event types and a 30 s summary", InstallNextEventLog},
	{"peer transport pump log", "peer_transport_pump_log", ON_TITLE,
		"Logs transport pump result changes",
		InstallTransportPumpLog},
	{"peer network stop log", "peer_stop_log", ON_TITLE,
		"Logs every stop request with caller and reason",
		InstallPeerStopLog},
};
const PatchTable kPeerNetworkProbePatches = PATCH_TABLE(kRows);
