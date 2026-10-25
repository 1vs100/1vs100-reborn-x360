#pragma once
#include "../platform/Types.h"

/* Per-build data. A site is one fact about a build. Patch code asks by id, never by build. */

enum SiteId {
	SITE_XNET_SERVER_TO_IN_ADDR,
	SITE_SBG_RAW_RESOLVE,
	SITE_GRID_PUBKEY_A,
	SITE_GRID_PUBKEY_B,
	SITE_XSESSION_CREATE,
	SITE_SESSION_HOST_COMPLETE, /* SessionTaskCompletionCreateHost */
	SITE_SESSION_UPDATE_REMOTES,
	SITE_QNET_LEAVE_HANDLER,
	SITE_QNET_LEAVE_ERROR_HANDLER,
	SITE_QNET_CMD_BLOCK_LOAD, /* error handler lbz of the command block */
	SITE_QNET_START_GAME_HANDLER,
	SITE_QNET_END_GAME_HANDLER,
	SITE_SESSION_UPDATE,
	SITE_SESSION_MEMBER_NEW,
	SITE_SESSION_MEMBER_LINK,
	SITE_SESSION_MEMBER_UPD,
	SITE_SESSION_RECOMPUTE,
	SITE_SESSION_ID_ASSIGN,
	SITE_QNET_SEND_DATA,
	SITE_SESSION_ID_REMOVE,
	SITE_SESSION_MEMBER_UNLINK,
	SITE_SESSION_CHAIN_UNLINK,
	SITE_SESSION_RELEASE,
	SITE_MAIN_PUMP,
	SITE_MAIN_THREAD_VTABLE,
	SITE_POST_NET_EVENT,
	SITE_EVENT_QUEUE_LOAD, /* title lwz of the queue */
	SITE_AVATAR_REMOTE_REQUEST,
	SITE_AVATAR_HANDLE_DATA,
	SITE_XULU_WIDE_DEBUG,
	SITE_LUA_LOG_WRITE,
	SITE_PTNTP_DEBUG_TRAP,
	SITE_UNSECURED_BANNER_BRANCH,
	SITE_CONTENT_DEVICE_ID_ARG,
	SITE_CONTENT_TITLE_ID_WRAPPER,
	SITE_TEXT_PADDING_CAVE,
	SITE_CONTENT_ID_PUSH_SITE,
	SITE_CONTENT_ID_PUSH_RET,
	SITE_LUA_PUSH_STRING,
	SITE_LUA_SET_FIELD,
	SITE_CONTENT_NAME_LEN,
	SITE_ACCOUNT_GET_USER_INFO,
	SITE_SESSION_VTABLE,
	SITE_NULLSUB2_LOGGER,
	SITE_GRID_CONFIG_GETTER,
	SITE_MASSIVE_INITIALIZE,
	SITE_MASSIVE_DNS_THREAD_START,
	SITE_MASSIVE_SERVER_PORT, /* NetworkManager::GetServerPortU16 */
	SITE_NULLSUB1_LOGGER,
	SITE_NULLSUB5_LOGGER,
	SITE_LUA_PRINT_SINK,
	SITE_LUA_ARG_COUNT,
	SITE_LUA_TO_LSTRING,
	SITE_SBG_SESSION_RESOLVE,
	SITE_XLSP_LOOKUP,
	SITE_GRID_DEFAULT_URL,
	SITE_NTP_XNET_CONNECT_CALL,
	SITE_AVATAR_SYSTEM_INIT,
	SITE_AVATAR_INIT_PRE_STEP,
	SITE_AVATAR_INIT_DEVICE,
	SITE_AVATAR_INIT_ANIMS,
	/* Grid dispatcher trace points, diagnostic */
	SITE_GRID_TRACE_NTP_TIME,
	SITE_GRID_TRACE_CLIENT_START,
	SITE_GRID_TRACE_CLIENT_SETUP,
	SITE_GRID_TRACE_USER_XUID,
	SITE_GRID_TRACE_REQUEST_LOOP,
	SITE_GRID_TRACE_SHOW_DATA,
	SITE_GRID_TRACE_RESULT_POST,
	SITE_GRID_TRACE_DECODE,
	SITE_GRID_TRACE_INFLATE,
	SITE_GRID_TRACE_PARSE,
	SITE_GRID_TRACE_PARSE_XML,
	SITE_GRID_TRACE_CHANNEL_LOOKUP,
	/* Local join and peer network trace points, diagnostic */
	SITE_SESSION_JOIN_LOCAL,
	SITE_SESSION_PEER_EVENTS,
	SITE_PEER_NEXT_EVENT,
	SITE_PEER_TRANSPORT_PUMP,
	SITE_PEER_STOP, /* stop request: target state 2 with a reason */
	SITE_COUNT
};

struct SiteSpec {
	SiteId id;
	DWORD address;
	const BYTE *expect; /* NULL when the patch checks its own bytes */
	DWORD expectSize;
	BOOL readOnly; /* never written */
};

/* Checked before each write. */
#define SITE(id, addr, sig) {id, addr, (const BYTE *)(sig), sizeof(sig), FALSE}
/* Checked once at select; a mismatch is dropped. */
#define SITE_REF(id, addr, sig) {id, addr, (const BYTE *)(sig), sizeof(sig), TRUE}
/* The patch checks the bytes itself. */
#define SITE_ADDR(id, addr) {id, addr, NULL, 0, FALSE}

/* A patch this build skips by decision, with its reason. */
struct PolicyOff {
	const char *patch;
	const char *reason;
};

struct TitleProfile {
	const char *name;
	DWORD titleId;
	DWORD timeDateStamp;
	DWORD contentTitleId; /* content filed under this, not titleId */
	const SiteSpec *sites;
	DWORD siteCount;
	const PolicyOff *policyOff;
	DWORD policyOffCount;
};

/* Refuses on out-of-range or duplicate ids, or addresses outside the image. */
/* Why `patch` is off; NULL when on. */
const char *TitleProfile_PolicyOff(const TitleProfile *profile, const char *patch);

BOOL TitleProfile_Index(const TitleProfile *profile, DWORD imageBase, DWORD imageSize,
	const SiteSpec **index, const char **why, DWORD *row);
