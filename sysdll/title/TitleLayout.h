#pragma once
#include "../stdafx.h"

/* Title objects and address space. Only fields the patches touch are named. Layouts are partly known: named offsets, not structs. */


const DWORD kTitleHeapBase = 0x40000000u; /* virtual heap */
const DWORD kTitleHeapEnd = 0x80000000u;
const DWORD kTitleImageEnd = 0x90000000u; /* images */
/* Sessions seen below this. */
const DWORD kSessionHeapEnd = 0x50000000u;

/* align is a power of two. */
inline BOOL IsTitleHeapPtr(DWORD p, DWORD align)
{
	return p >= kTitleHeapBase && p < kTitleHeapEnd && (p & (align - 1)) == 0;
}

/* Heap or image data. */
inline BOOL IsTitleDataPtr(DWORD p, DWORD align)
{
	return p >= kTitleHeapBase && p < kTitleImageEnd && (p & (align - 1)) == 0;
}

namespace QNetWrapper
{
const DWORD kCQNet = 4;
const DWORD kField16 = 16; /* cleared by LeaveGame when a3 != 0 */
} // namespace QNetWrapper

/* Offset differs by build; fields keep order and spacing. */
namespace QNetCmdBlock
{
const DWORD kPending = 0;
const DWORD kWaitKind = 4;      /* 3 StartGame, 6 EndGame, 8 LeaveGame, 0 none */
const DWORD kWaitState = 8;     /* session state waited for */
const DWORD kLeaveByte = 0xC;   /* cleared after LeaveGame */
const DWORD kLeaveDword = 0x10; /* cleared after LeaveGame */
} // namespace QNetCmdBlock

namespace CQNetVtbl
{
const DWORD kLeaveGame = 0x14;
const DWORD kStartGame = 24;
const DWORD kEndGame = 28;
const DWORD kIsHost = 44;
} // namespace CQNetVtbl

namespace Session
{
const DWORD kListener = 0xC;      /* listener object */
const DWORD kState = 0x14;        /* 3 / 6 seen in lobby / game */
const DWORD kType = 0x1C;         /* 0 is no peer network; QNet sessions are 2 */
const DWORD kXamHandle = 0x20;    /* XSession handle, null in the liveless session */
const DWORD kMaxPublic = 0xA8;
const DWORD kPublicUsed = 0xAA;
const DWORD kMemberCount = 0xB4;
const DWORD kIdTable = 0x100;     /* member id table */
const DWORD kVtblFindMember = 15; /* vtable index */
const DWORD kPeerNet = 0x190;     /* peer network object, null when type 0 */
const DWORD kFlags = 0x2E4;
/* Local member per local user (slot = XAM user index), NULL when none. */
const DWORD kLocalSlots = 0x17C;
/* Set by host completion; the drain skips the peer network until then. */
const DWORD kFlagXSessionJoined = 0x01000000u;
} // namespace Session

namespace SessionListenerVtbl
{
const DWORD kMemberAdded = 1;
const DWORD kMemberRemoved = 2;
} // namespace SessionListenerVtbl

namespace Member
{
const DWORD kTag = 0x64;    /* zeroed on removal */
const DWORD kId = 0x18A;    /* assigned by sessionIdAssign */
const DWORD kFlags = 0x18C;
const DWORD kFlagValid = 0x08000000u;
const DWORD kFlagLocal = 0x80000000u;
} // namespace Member

namespace QNetSendBlock
{ /* send argument block (r6) */
const DWORD kBufferLen = 8;
const DWORD kBuffer = 0xC; /* u8 type, u16 BE length, payload */
} // namespace QNetSendBlock
namespace AvatarRequestEntry
{
const DWORD kXuid = 8;
}
