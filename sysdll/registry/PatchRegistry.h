#pragma once
#include "../stdafx.h"

/* Every patch is one row in its feature file table; the engine checks policy and switches, guards installs against faults, and records the outcome. */

enum PatchPhase {
	PHASE_KERNEL, /* loader start */
	PHASE_EARLY,  /* title thread xam load, before game code */
	PHASE_TITLE,  /* title loaded, profile selected */
};
#define PHASE_BIT(phase) (1u << (phase))
#define ON_KERNEL PHASE_BIT(PHASE_KERNEL)
#define ON_EARLY PHASE_BIT(PHASE_EARLY)
#define ON_TITLE PHASE_BIT(PHASE_TITLE)

struct PatchDescriptor;
typedef VOID (*PatchInstall)(const PatchDescriptor *self);

struct PatchDescriptor {
	const char *id;         /* stable manifest id */
	const char *switchName; /* disable word, stable */
	DWORD phases;           /* phases it runs in */
	const char *summary;    /* one line for docs/patches.md */
	PatchInstall install;   /* reports its own manifest lines */
	VOID (*reset)(VOID);    /* NULL when it keeps none */
	DWORD param;            /* shared installer data */
	PVOID target;           /* shared installer data */
};

struct PatchTable {
	const PatchDescriptor *rows;
	DWORD count;
};
#define PATCH_TABLE(rows) {rows, sizeof(rows) / sizeof(rows[0])}

/* In install order. */
extern const PatchTable *const kPatchTables[];
extern const DWORD kPatchTableCount;

/* Skipped rows report why=build/switch; a fault reports why=fault and the next row runs. */
VOID ApplyPatchPhase(PatchPhase phase);

/* Every row's reset. */
VOID ResetPatches();

/* Installing row, NULL outside; for the fault report. */
const char *TakeCurrentPatchId();

/* Title xam import: ordinal `param` to `target`. */
VOID InstallXamImport(const PatchDescriptor *self);
