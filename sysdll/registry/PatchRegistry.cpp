#include "PatchRegistry.h"
#include "PatchHelpers.h"
#include "../config/PatchSwitches.h"
#include "../platform/Log.h"

static const char *volatile g_CurrentPatchId = NULL;

static VOID RunRow(const PatchDescriptor *row)
{
	if (PolicySkips(row->id))
		return;
	if (LoaderPatchDisabled(row->switchName)) {
		ReportPatch(row->id, "skipped", 0, "switch");
		return;
	}
	g_CurrentPatchId = row->id;
	__try {
		row->install(row);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		LOG_ERROR(
			"patch", "%s: exception %08X during install, continuing", row->id, GetExceptionCode());
		ReportPatch(row->id, "failed", 0, "fault");
	}
	g_CurrentPatchId = NULL;
}

VOID ApplyPatchPhase(PatchPhase phase)
{
	DWORD t = 0;
	while (t < kPatchTableCount) {
		const PatchTable *table = kPatchTables[t];
		DWORD r = 0;
		while (r < table->count) {
			if (table->rows[r].phases & PHASE_BIT(phase))
				RunRow(&table->rows[r]);
			r++;
		}
		t++;
	}
}

VOID ResetPatches()
{
	DWORD t = 0;
	while (t < kPatchTableCount) {
		const PatchTable *table = kPatchTables[t];
		DWORD r = 0;
		while (r < table->count) {
			if (table->rows[r].reset)
				table->rows[r].reset();
			r++;
		}
		t++;
	}
}

const char *TakeCurrentPatchId()
{
	return (const char *)InterlockedExchangePointer((PVOID *)&g_CurrentPatchId, NULL);
}

VOID InstallXamImport(const PatchDescriptor *self)
{
	HookXamImport(self->param, (DWORD)self->target, self->id);
}
