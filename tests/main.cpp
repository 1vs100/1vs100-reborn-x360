#include "check.h"

int g_Failures = 0;

void TestText();
void TestPpc();
void TestPpcDecode();
void TestIni();
void TestFormat();
void TestTitleProfile();
void TestMmProtocol();
void TestLogConfig();
void TestLauncherConfig();

int main()
{
	TestText();
	TestPpc();
	TestPpcDecode();
	TestIni();
	TestFormat();
	TestTitleProfile();
	TestMmProtocol();
	TestLogConfig();
	TestLauncherConfig();
	if (g_Failures)
		printf("%d check(s) failed\n", g_Failures);
	else
		printf("all checks passed\n");
	return g_Failures ? 1 : 0;
}
