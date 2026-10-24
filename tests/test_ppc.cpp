#include "check.h"
#include "../sysdll/platform/Ppc.h"

void TestPpc()
{
	/* Known encodings from the 1317 TitleID fix. */
	CHECK_EQ_HEX(Ppc_Branch(0x8236BE88u, 0x82955D8Cu, FALSE), 0x485E9F04u);
	CHECK_EQ_HEX(Ppc_Branch(0x82955D94u, 0x8236BE8Cu, FALSE), 0x4BA160F8u);
	/* link bit, zero distance */
	CHECK_EQ_HEX(Ppc_Branch(0x82000000u, 0x82000010u, TRUE), 0x48000011u);
	CHECK_EQ_HEX(Ppc_Branch(0x82000000u, 0x82000000u, FALSE), 0x48000000u);

	/* range: -32 MB reaches, +32 MB does not */
	CHECK(Ppc_InBranchRange(0x82000000u, 0x82000000u + 0x01FFFFFCu));
	CHECK(!Ppc_InBranchRange(0x82000000u, 0x82000000u + 0x02000000u));
	CHECK(Ppc_InBranchRange(0x82000000u, 0x82000000u - 0x02000000u));
	CHECK(!Ppc_InBranchRange(0x82000000u, 0x82000000u - 0x02000004u));
	CHECK_EQ_HEX(Ppc_Branch(0x82000000u, 0x80000000u, FALSE), 0x4A000000u);

	/* far jump: plain, carry (bit 15 set), linked */
	{
		DWORD j[4];
		Ppc_FarJump(j, 0x91234567u, FALSE);
		CHECK_EQ_HEX(j[0], 0x3D609123u);
		CHECK_EQ_HEX(j[1], 0x396B4567u);
		CHECK_EQ_HEX(j[2], 0x7D6903A6u);
		CHECK_EQ_HEX(j[3], 0x4E800420u);
		Ppc_FarJump(j, 0x9123C567u, TRUE);
		CHECK_EQ_HEX(j[0], 0x3D609124u); /* addi sign-extends 0xC567 */
		CHECK_EQ_HEX(j[1], 0x396BC567u);
		CHECK_EQ_HEX(j[3], 0x4E800421u);
		/* carry out of the high half must not spill into the register field */
		Ppc_FarJump(j, 0xFFFF8000u, FALSE);
		CHECK_EQ_HEX(j[0], 0x3D600000u);
	}

	{
		BYTE b[4];
		Ppc_PutWordBE(b, 0x485E9F04u);
		CHECK(b[0] == 0x48 && b[1] == 0x5E && b[2] == 0x9F && b[3] == 0x04);
	}
}

void TestPpcDecode()
{
	DWORD t = 0;
	/* round trip forward and backward, and the far end of the range */
	CHECK(Ppc_DecodeBranch(0x8236BE88u, Ppc_Branch(0x8236BE88u, 0x82955D8Cu, FALSE), &t));
	CHECK_EQ_HEX(t, 0x82955D8Cu);
	CHECK(Ppc_DecodeBranch(0x82955D94u, 0x4BA160F8u, &t));
	CHECK_EQ_HEX(t, 0x8236BE8Cu);
	CHECK(Ppc_DecodeBranch(0x82000000u, 0x4A000000u, &t));
	CHECK_EQ_HEX(t, 0x80000000u);
	/* bl, ba, a non-branch: not a hook jump */
	CHECK(!Ppc_DecodeBranch(0x82000000u, 0x48000011u, &t));
	CHECK(!Ppc_DecodeBranch(0x82000000u, 0x48000012u, &t));
	CHECK(!Ppc_DecodeBranch(0x82000000u, 0x7D8802A6u, &t));
}
