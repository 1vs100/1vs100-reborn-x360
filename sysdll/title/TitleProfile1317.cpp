#include "TitleProfile.h"
#include "CommonSignatures.h"

/* v2.0.1317.0 sites, by SiteId. */


static const BYTE kXSessionCreate[16] = {
	0x7D, 0x88, 0x02, 0xA6, 0x4B, 0xF3, 0x05, 0x2D, 0x94, 0x21, 0xFF, 0x20, 0x7C, 0x7F, 0x1B, 0x78};
static const BYTE kQNetLeaveHandler[16] = {
	0x7D, 0x88, 0x02, 0xA6, 0x48, 0x2B, 0x98, 0x49, 0x94, 0x21, 0xFF, 0x90, 0x7C, 0x7F, 0x1B, 0x78};
static const BYTE kSessionUpdate[16] = {
	0x7D, 0x88, 0x02, 0xA6, 0x4B, 0xF8, 0x73, 0x21, 0x94, 0x21, 0xFF, 0x80, 0x7C, 0x7F, 0x1B, 0x78};
static const BYTE kMainPump[16] = {
	0x7D, 0x88, 0x02, 0xA6, 0x48, 0x3B, 0x1A, 0x1D, 0x94, 0x21, 0xFF, 0x70, 0x7C, 0x7E, 0x1B, 0x78};
static const BYTE kQNetSendData[16] = {
	0x7D, 0x88, 0x02, 0xA6, 0x48, 0x2B, 0x7B, 0x31, 0x3D, 0x80, 0xFF, 0xFF, 0x61, 0x8C, 0x7F, 0x10};
static const BYTE kAccountGetUserInfo[16] = {
	0x7D, 0x88, 0x02, 0xA6, 0x48, 0x04, 0x3F, 0x89, 0x94, 0x21, 0xFC, 0xD0, 0x3B, 0xE0, 0x00, 0x00};
static const BYTE kXuluWideDebug[16] = {
	0x7D, 0x88, 0x02, 0xA6, 0x91, 0x81, 0xFF, 0xF8, 0xFB, 0xE1, 0xFF, 0xF0, 0x94, 0x21, 0xFF, 0x90};
/* Lua log writer: print thunks branch here. */
static const BYTE kLuaLogWrite[16] = {
	0x7D, 0x88, 0x02, 0xA6, 0x48, 0x2A, 0x3A, 0x95, 0x94, 0x21, 0xFA, 0xC0, 0x89, 0x63, 0x00, 0x0C};
/* XuiShell's `beq skip` around the "CONSOLE IS UNSECURED" banner */
static const BYTE kUnsecuredBannerBranch[4] = {0x41, 0x82, 0x00, 0x74};

/* Two Grid RSA moduli, 172 chars plus NUL each. */
static const char kGridMicrosoftModulusA[] =
	"yi/w2Dz5eowpNtzvjD4DpsclmI6zZ0uhFBS6JID5EqpQ+iKxCAHV6qHyO1r5XVXylInRDnVgllY64lanVPIZXXGnxdSunC5JN3HUgt9vP1m98DSxIcMtmj2I//CS+FPypGceIBqRdbgmLgNduxkYpcsTc1eHwDSktZXmiM74OJE=";
static const char kGridMicrosoftModulusB[] =
	"2RlsdD3EejqXi3c6iOEp4GqXA3NoG3MyX1uspoABzV3SrUXuIHLPqVsj5pH+51nSltF0lgLeSIKg0Vk01+pJMBNfeKhx7ZjIzx08sP6zGo7C2YvpZpc+SJyGoT8H0Ser1TgUIiUYU9XduTVGDBAJYR9r+39kbscD8fYuQbWfO9k=";
typedef char GridMicrosoftModulusAMustBe172Chars[(sizeof(kGridMicrosoftModulusA) == 173) ? 1 : -1];
typedef char GridMicrosoftModulusBMustBe172Chars[(sizeof(kGridMicrosoftModulusB) == 173) ? 1 : -1];

static const BYTE kContentIdPush[4] = {0x4B, 0xFF, 0xF3, 0x71}; /* bl sub_821B2078 */
/* Name length check */
static const BYTE kContentNameLen[12] = {
	0x55, 0x6B, 0x00, 0x3E, 0x2B, 0x0B, 0x00, 0x2A, 0x41, 0x9A, 0x00, 0x20};

/* Loader-called or compared entries; each occurs once in the image. */
static const BYTE kQNetLeaveErrorHandler[16] = {
	0x7D, 0x88, 0x02, 0xA6, 0x48, 0x2B, 0xA5, 0xED, 0x94, 0x21, 0xFF, 0x80, 0x3D, 0x60, 0x82, 0x00};
static const BYTE kSessionMemberNew[16] = {
	0x7D, 0x88, 0x02, 0xA6, 0x4B, 0xF8, 0x3D, 0x51, 0x94, 0x21, 0xFF, 0x50, 0x7C, 0x7E, 0x1B, 0x78};
static const BYTE kSessionMemberLink[16] = {
	0x7D, 0x88, 0x02, 0xA6, 0x4B, 0xF9, 0x15, 0x5D, 0x94, 0x21, 0xFF, 0x80, 0x83, 0xE3, 0x00, 0xF8};
static const BYTE kSessionMemberUpd[16] = {
	0x7D, 0x88, 0x02, 0xA6, 0x4B, 0xF8, 0xEE, 0xC9, 0x94, 0x21, 0xFF, 0x00, 0x90, 0x81, 0x01, 0x1C};
static const BYTE kSessionRecompute[16] = {
	0x7D, 0x88, 0x02, 0xA6, 0x4B, 0xF8, 0xF3, 0x51, 0x94, 0x21, 0xFE, 0xD0, 0x81, 0x63, 0x01, 0xA4};
static const BYTE kSessionIdAssign[16] = {
	0x7D, 0x88, 0x02, 0xA6, 0x4B, 0xF8, 0x58, 0x15, 0x94, 0x21, 0xFF, 0x80, 0x54, 0xAB, 0x06, 0x3E};
static const BYTE kSessionIdRemove[16] = {
	0x7D, 0x88, 0x02, 0xA6, 0x4B, 0xF8, 0x57, 0x69, 0x94, 0x21, 0xFF, 0x90, 0x89, 0x64, 0x01, 0x8A};
static const BYTE kSessionMemberUnlink[16] = {
	0x7D, 0x88, 0x02, 0xA6, 0x4B, 0xF9, 0x14, 0xA5, 0x94, 0x21, 0xFF, 0x70, 0x7C, 0x7F, 0x1B, 0x78};
static const BYTE kMainThreadVtable[8] = {0x82, 0x14, 0x6A, 0xF0, 0x82, 0x14, 0x6D, 0x88};
static const BYTE kPostNetEvent[16] = {
	0x7D, 0x88, 0x02, 0xA6, 0x48, 0x36, 0x87, 0x9D, 0xE9, 0x81, 0xF0, 0x00, 0x94, 0x21, 0xE7, 0x10};
/* Leave error handler lbz of the command block. */
static const BYTE kQNetCmdBlockLoad[16] = {
	0x89, 0x1C, 0x05, 0xA4, 0x28, 0x08, 0x00, 0x00, 0x41, 0x82, 0x00, 0x08, 0x7D, 0x6A, 0x5B, 0x78};
/* Event queue load before post of event 18. */
static const BYTE kEventQueueLoad[16] = {
	0x80, 0x7B, 0x00, 0x78, 0x38, 0xA1, 0x00, 0x70, 0xA8, 0xFF, 0x00, 0x01, 0x38, 0x80, 0x00, 0x12};
static const BYTE kAvatarHandleData[16] = {
	0x7D, 0x88, 0x02, 0xA6, 0x48, 0x3B, 0xD2, 0xC9, 0x94, 0x21, 0xFF, 0x60, 0x3D, 0x60, 0x82, 0x9F};
static const BYTE kMassiveServerPort[16] = {
	0x7D, 0x88, 0x02, 0xA6, 0x4B, 0xFA, 0xED, 0xD1, 0x94, 0x21, 0xFF, 0x90, 0x7C, 0x7D, 0x1B, 0x78};
static const BYTE kSessionVtable[8] = {0x82, 0x56, 0x8E, 0x80, 0x82, 0x56, 0xCE, 0xF8};

static const SiteSpec kSites[] = {
	SITE(SITE_XNET_SERVER_TO_IN_ADDR, 0x824B05E0u, kSig_XNetServerToInAddr),
	SITE(SITE_SBG_RAW_RESOLVE, 0x825C9B30u, kSig_StdPrologueR30R31),
	SITE(SITE_GRID_PUBKEY_A, 0x820A2420u, kGridMicrosoftModulusA),
	SITE(SITE_GRID_PUBKEY_B, 0x820A24D0u, kGridMicrosoftModulusB),
	SITE(SITE_XSESSION_CREATE, 0x825C3CD0u, kXSessionCreate),
	SITE(SITE_QNET_LEAVE_HANDLER, 0x8223A9D0u, kQNetLeaveHandler),
	SITE_REF(SITE_QNET_LEAVE_ERROR_HANDLER, 0x82239C28u, kQNetLeaveErrorHandler),
	SITE_REF(SITE_QNET_CMD_BLOCK_LOAD, 0x82239C60u, kQNetCmdBlockLoad),
	SITE(SITE_QNET_START_GAME_HANDLER, 0x8223AB58u, kSig_StdPrologueR30R31),
	SITE(SITE_QNET_END_GAME_HANDLER, 0x8223AC48u, kSig_StdPrologueR30R31),
	SITE(SITE_SESSION_UPDATE, 0x8256CEF8u, kSessionUpdate),
	SITE_REF(SITE_SESSION_MEMBER_NEW, 0x825704B0u, kSessionMemberNew),
	SITE_REF(SITE_SESSION_MEMBER_LINK, 0x82562CB8u, kSessionMemberLink),
	SITE_REF(SITE_SESSION_MEMBER_UPD, 0x82565338u, kSessionMemberUpd),
	SITE_REF(SITE_SESSION_RECOMPUTE, 0x82564EC0u, kSessionRecompute),
	SITE_REF(SITE_SESSION_ID_ASSIGN, 0x8256EA00u, kSessionIdAssign),
	SITE(SITE_QNET_SEND_DATA, 0x8223C6C0u, kQNetSendData),
	SITE_REF(SITE_SESSION_ID_REMOVE, 0x8256EAB0u, kSessionIdRemove),
	SITE_REF(SITE_SESSION_MEMBER_UNLINK, 0x82562D68u, kSessionMemberUnlink),
	SITE_REF(SITE_SESSION_CHAIN_UNLINK, 0x8256FAE0u, kSig_SessionChainUnlink),
	SITE_REF(SITE_SESSION_RELEASE, 0x82570578u, kSig_SessionRelease),
	SITE(SITE_MAIN_PUMP, 0x821427F8u, kMainPump),
	SITE_REF(SITE_MAIN_THREAD_VTABLE, 0x82001F18u, kMainThreadVtable),
	SITE_REF(SITE_POST_NET_EVENT, 0x8218BA70u, kPostNetEvent),
	SITE_REF(SITE_EVENT_QUEUE_LOAD, 0x8213FD24u, kEventQueueLoad),
	SITE(SITE_AVATAR_REMOTE_REQUEST, 0x82137338u, kSig_StdPrologueR30R31),
	SITE_REF(SITE_AVATAR_HANDLE_DATA, 0x82136F40u, kAvatarHandleData),
	SITE(SITE_XULU_WIDE_DEBUG, 0x828D6210u, kXuluWideDebug),
	SITE(SITE_LUA_LOG_WRITE, 0x82250770u, kLuaLogWrite),
	SITE(SITE_PTNTP_DEBUG_TRAP, 0x82370E50u, kSig_PtntpDebugTrap),
	SITE(SITE_UNSECURED_BANNER_BRANCH, 0x822293DCu, kUnsecuredBannerBranch),
	SITE(SITE_CONTENT_DEVICE_ID_ARG, 0x821BB2E0u, kSig_ContentDeviceIdArg),
	SITE(SITE_CONTENT_TITLE_ID_WRAPPER, 0x8236BE88u, kSig_ContentTitleIdArg),
	SITE_ADDR(SITE_TEXT_PADDING_CAVE, 0x82955D7Cu),
	SITE(SITE_CONTENT_ID_PUSH_SITE, 0x821B2D08u, kContentIdPush),
	SITE_REF(SITE_CONTENT_ID_PUSH_RET, 0x821B2D0Cu, kSig_ContentIdPushRet),
	SITE_REF(SITE_LUA_PUSH_STRING, 0x82155648u, kSig_LuaPushString),
	SITE_REF(SITE_LUA_SET_FIELD, 0x82155BF8u, kSig_LuaSetField),
	SITE(SITE_CONTENT_NAME_LEN, 0x821BB38Cu, kContentNameLen),
	SITE(SITE_ACCOUNT_GET_USER_INFO, 0x824B0288u, kAccountGetUserInfo),
	SITE_REF(SITE_SESSION_VTABLE, 0x8209CC50u, kSessionVtable),
	SITE(SITE_MASSIVE_SERVER_PORT, 0x82545448u,
		kMassiveServerPort), /* NetworkManager::GetServerPortU16 */
};

/* Absent in this build. SBG connect works with sbgRawResolve alone; Grid URL comes from engineconfig.xml. */
/*   SITE_NULLSUB2_LOGGER
 *   SITE_GRID_CONFIG_GETTER
 *   SITE_MASSIVE_INITIALIZE
 *   SITE_MASSIVE_DNS_THREAD_START
 *   SITE_NULLSUB1_LOGGER
 *   SITE_NULLSUB5_LOGGER
 *   SITE_LUA_PRINT_SINK
 *   SITE_LUA_ARG_COUNT
 *   SITE_LUA_TO_LSTRING
 *   SITE_SBG_SESSION_RESOLVE
 *   SITE_XLSP_LOOKUP
 *   SITE_GRID_DEFAULT_URL
 *   SITE_NTP_XNET_CONNECT_CALL
 *   SITE_AVATAR_SYSTEM_INIT
 *   SITE_AVATAR_INIT_PRE_STEP
 *   SITE_AVATAR_INIT_DEVICE
 *   SITE_AVATAR_INIT_ANIMS
 */

/* Skipped by decision. */
static const PolicyOff kPolicyOff[] = {
	/* 1149 investigation only; sites not derived for 1317. */
	{"GridTrace", "1149 investigation only"},
};

/* extern: const has internal linkage in C++. */
extern const TitleProfile kTitleProfile1317 = {
	"v2.0.1317.0",
	0x584A07E4u,
	0x4BC5B844u,
	0x584A07D1u /* base title id, not execution id */,
	kSites,
	sizeof(kSites) / sizeof(kSites[0]),
	kPolicyOff,
	sizeof(kPolicyOff) / sizeof(kPolicyOff[0]),
};
