#include "check.h"
#include "../sysdll/matchmaking/Protocol.h"
#include <string.h>

/* Fixtures from the spec (version 2); no server captures in the repo. */

static void TestWriterAndFrames()
{
	BYTE buf[64];
	MmWriter w;
	static const BYTE join[] = {
		0x02,                                           /* player count */
		0xE0, 0x00, 0x06, 0x01, 0x48, 0x4D, 0x4B, 0x37, /* user 0 xuid */
		0x00, 0x02, 'P', '1',                           /* gamertag "P1" */
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, /* user 1 xuid */
		0x00, 0x00,                                     /* empty gamertag */
	};
	MatchmakingLocalPlayer players[2];
	memset(players, 0, sizeof(players));
	players[0].xuid = 0xE0000601484D4B37ull;
	strcpy(players[0].gamertag, "P1");
	players[1].xuid = 2;
	MmWriter_Init(&w, buf, sizeof(buf));
	MmEncodeJoin(&w, players, 2);
	CHECK(!w.overflow);
	CHECK_EQ_HEX(MmWriter_Length(&w), sizeof(join));
	CHECK(memcmp(buf, join, sizeof(join)) == 0);

	/* overflow: stops writing, keeps what fitted, reports once */
	MmWriter_Init(&w, buf, 12);
	MmEncodeJoin(&w, players, 1);
	CHECK(w.overflow);
	CHECK_EQ_HEX(MmWriter_Length(&w), 11);

	/* no request names a session: HEARTBEAT / LEAVE / RELAY_POLL are the xuid, AVATAR_GET the peer */
	MmWriter_Init(&w, buf, sizeof(buf));
	MmEncodeXuid(&w, 0x0102030405060708ull);
	CHECK_EQ_HEX(MmWriter_Length(&w), 8);
	CHECK_EQ_HEX(MmGetU64(buf), 0x0102030405060708ull);
	MmWriter_Init(&w, buf, sizeof(buf));
	MmEncodeAvatarGet(&w, 7);
	CHECK_EQ_HEX(MmWriter_Length(&w), 8);

	{
		BYTE frame[16];
		BYTE version = 0, type = 0;
		DWORD len = 0;
		static const BYTE payload[] = {1, 2, 3};
		/* version 2 header: version u8, type u8, length u32 */
		CHECK_EQ_HEX(MmFrame(frame, sizeof(frame), MM_TYPE_HEARTBEAT, payload, 3), 9);
		CHECK_EQ_HEX(frame[0], 0x02);
		CHECK_EQ_HEX(frame[1], 0x03);
		CHECK_EQ_HEX(MmGetU32(frame + 2), 3);
		CHECK(memcmp(frame + 6, payload, 3) == 0);
		MmParseHeader(frame, &version, &type, &len);
		CHECK_EQ_HEX(version, MM_PROTOCOL_VERSION);
		CHECK_EQ_HEX(type, MM_TYPE_HEARTBEAT);
		CHECK_EQ_HEX(len, 3);
		CHECK_EQ_HEX(MmFrame(frame, 8, MM_TYPE_HEARTBEAT, payload, 3), 0); /* does not fit */
		/* a REJECT from a version 3 server: the version is read before anything else */
		{
			static const BYTE reject[] = {0x03, 0x11, 0, 0, 0, 1, MM_REJECT_VERSION};
			MmParseHeader(reject, &version, &type, &len);
			CHECK_EQ_HEX(version, 3);
			CHECK_EQ_HEX(type, MM_TYPE_REJECT);
			CHECK_EQ_HEX(len, 1);
		}
	}

	/* relay put: targets and data length */
	{
		BYTE out[64];
		const ULONGLONG targets[2] = {0x1122334455667788ull, 2};
		static const BYTE data[] = {0xAA, 0xBB};
		MmWriter_Init(&w, out, sizeof(out));
		MmEncodeRelayPut(&w, 9, targets, 2, data, 2);
		CHECK(!w.overflow);
		CHECK_EQ_HEX(MmWriter_Length(&w), 8 + 2 + 16 + 2 + 2);
		CHECK_EQ_HEX(MmGetU64(out), 9);
		CHECK_EQ_HEX(MmGetU16(out + 8), 2);
		CHECK_EQ_HEX(MmGetU64(out + 10), 0x1122334455667788ull);
		CHECK_EQ_HEX(MmGetU16(out + 26), 2);
		CHECK_EQ_HEX(out[28], 0xAA);
	}
}

static void TestRoster()
{
	/* group 7: console A (self 1 and its second player 4), console B (2 "Bob"), console C (3 with
	 * an empty gamertag, and 5) */
	static const BYTE roster[] = {
		0x00, 0x03, /* 3 consoles */
		0x02,       /* A: 2 players */
		0, 0, 0, 0, 0, 0, 0, 0x01, 0x00, 0x05, 'S', 'e', 'l', 'f', '!', 0, 0, 0, 0, 0, 0, 0, 0x04,
		0x00, 0x00, 0x01,                                           /* B: 1 player */
		0, 0, 0, 0, 0, 0, 0, 0x02, 0x00, 0x03, 'B', 'o', 'b', 0x02, /* C: 2 players */
		0, 0, 0, 0, 0, 0, 0, 0x03, 0x00, 0x00, 0, 0, 0, 0, 0, 0, 0, 0x05, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x07, /* group id */
	};
	MatchmakingPeer peers[3];
	DWORD n = 99, group = 0;
	CHECK(MmDecodeRoster(roster, sizeof(roster), 1, peers, 3, &n, &group));
	CHECK_EQ_HEX(group, 7);
	CHECK_EQ_HEX(n, 3); /* own console skipped, every player of the other consoles */
	CHECK_EQ_HEX(peers[0].xuid, 2);
	CHECK(peers[0].gamertag[0] == 'B' && peers[0].gamertag[2] == 'b' && peers[0].gamertag[3] == 0);
	CHECK_EQ_HEX(peers[1].xuid, 3);
	CHECK(peers[1].gamertag[0] == 0);
	CHECK_EQ_HEX(peers[2].xuid, 5);
	/* self is found on any player of its console */
	CHECK(MmDecodeRoster(roster, sizeof(roster), 4, peers, 3, &n, &group));
	CHECK_EQ_HEX(n, 3);
	/* self found on the second player of a console: its first player is taken back out */
	CHECK(MmDecodeRoster(roster, sizeof(roster), 5, peers, 3, &n, &group));
	CHECK_EQ_HEX(n, 3);
	CHECK_EQ_HEX(peers[0].xuid, 1);
	CHECK_EQ_HEX(peers[1].xuid, 4);
	CHECK_EQ_HEX(peers[2].xuid, 2);
	/* capacity: later players dropped */
	CHECK(MmDecodeRoster(roster, sizeof(roster), 1, peers, 2, &n, &group));
	CHECK_EQ_HEX(n, 2);
	CHECK_EQ_HEX(peers[1].xuid, 3);
	/* truncated (group id cut): nothing to apply */
	CHECK(!MmDecodeRoster(roster, sizeof(roster) - 1, 1, peers, 3, &n, &group));
	CHECK(!MmDecodeRoster(roster, 1, 1, peers, 3, &n, &group));
	/* self 0 is never "self": every player, up to the cap */
	CHECK(MmDecodeRoster(roster, sizeof(roster), 0, peers, 3, &n, &group));
	CHECK_EQ_HEX(n, 3);
	CHECK_EQ_HEX(peers[2].xuid, 2);
	/* a console with 0 or more than 4 players is malformed */
	{
		static const BYTE zero[] = {0x00, 0x01, 0x00, 0, 0, 0, 1};
		static const BYTE five[] = {0x00, 0x01, 0x05, 0, 0, 0, 1};
		CHECK(!MmDecodeRoster(zero, sizeof(zero), 1, peers, 3, &n, &group));
		CHECK(!MmDecodeRoster(five, sizeof(five), 1, peers, 3, &n, &group));
	}
	/* an empty group is valid */
	{
		static const BYTE empty[] = {0x00, 0x00, 0, 0, 0, 9};
		CHECK(MmDecodeRoster(empty, sizeof(empty), 1, peers, 3, &n, &group));
		CHECK_EQ_HEX(n, 0);
		CHECK_EQ_HEX(group, 9);
	}
}

struct RelaySeen {
	DWORD count;
	ULONGLONG from[4];
	DWORD len[4];
};

static VOID Visit(VOID *ctx, ULONGLONG from, const BYTE *data, DWORD len)
{
	RelaySeen *seen = (RelaySeen *)ctx;
	(void)data;
	seen->from[seen->count] = from;
	seen->len[seen->count] = len;
	seen->count++;
}

static void TestRelayAndAvatar()
{
	static const BYTE relay[] = {
		0x00, 0x03, 0, 0, 0, 0, 0, 0, 0, 0x07, 0x00, 0x02, 0x11, 0x22, 0, 0, 0, 0, 0, 0, 0, 0x08,
		0x00, 0x01, 0x33, 0, 0, 0, 0, 0, 0, 0, 0x09, 0x00, 0x05,
		0x44, /* truncated: ends the reply */
	};
	RelaySeen seen;
	memset(&seen, 0, sizeof(seen));
	CHECK_EQ_HEX(MmDecodeRelayReply(relay, sizeof(relay), Visit, &seen), 2);
	CHECK_EQ_HEX(seen.from[0], 7);
	CHECK_EQ_HEX(seen.len[0], 2);
	CHECK_EQ_HEX(seen.from[1], 8);
	CHECK_EQ_HEX(seen.len[1], 1);

	{
		static BYTE reply[3 + AVATAR_METADATA_BYTES];
		const BYTE *blob = 0;
		reply[0] = 1;
		reply[1] = (BYTE)(AVATAR_METADATA_BYTES >> 8);
		reply[2] = (BYTE)AVATAR_METADATA_BYTES;
		reply[3] = 0x5A;
		CHECK(MmDecodeAvatarReply(reply, sizeof(reply), &blob));
		CHECK(blob == reply + 3);
		CHECK(!MmDecodeAvatarReply(reply, sizeof(reply) - 1, &blob)); /* incomplete */
		reply[0] = 0;
		CHECK(!MmDecodeAvatarReply(reply, sizeof(reply), &blob)); /* not found */
	}
	{
		static const BYTE ack[] = {0, 0, 0, 4};
		static const BYTE joinAck[] = {0, 0, 0, 29, 0, 0, 0, 8};
		DWORD population = 0, group = 0;
		CHECK_EQ_HEX(MmDecodePopulation(ack, 4), 4);
		CHECK_EQ_HEX(MmDecodePopulation(ack, 3), 0);
		CHECK(MmDecodeJoinAck(joinAck, sizeof(joinAck), &population, &group));
		CHECK_EQ_HEX(population, 29);
		CHECK_EQ_HEX(group, 8);
		CHECK(!MmDecodeJoinAck(joinAck, 7, &population, &group));
	}
}

void TestMmProtocol()
{
	TestWriterAndFrames();
	TestRoster();
	TestRelayAndAvatar();
}
