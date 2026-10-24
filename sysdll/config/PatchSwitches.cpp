#include "PatchSwitches.h"
#include "../platform/Log.h"
#include "../platform/Text.h"

static CHAR
	g_patchSwitches[REVIVAL_PATCH_SWITCHES_BYTES];

VOID SetPatchSwitches(const CHAR *text)
{
	DWORD got = Text_Length(text, sizeof(g_patchSwitches) - 1);
	/* Lower-case, separators to space, wrap in spaces so " name " matches whole words. */
	{
		CHAR tmp[sizeof(g_patchSwitches)];
		DWORD i, n = 0;
		tmp[n++] = ' ';
		for (i = 0; i < got && n < sizeof(tmp) - 2; i++) {
			CHAR c = Text_LowerAscii(text[i]);
			if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
				c = ' ';
			tmp[n++] = c;
		}
		tmp[n++] = ' ';
		tmp[n] = 0;
		for (i = 0; i <= n; i++)
			g_patchSwitches[i] = tmp[i];
	}
	LOG_INFO("cfg", "patch switches:%s", g_patchSwitches);
}

BOOL LoaderPatchDisabled(const char *name)
{
	CHAR key[48];
	DWORD n = 0;
	key[n++] = ' ';
	while (name[n - 1] && n < sizeof(key) - 2) {
		key[n] = name[n - 1];
		n++;
	}
	key[n++] = ' ';
	key[n] = 0;
	return Text_Contains(g_patchSwitches, key);
}
