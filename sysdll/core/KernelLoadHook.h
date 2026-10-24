#pragma once
#include "../stdafx.h"

/* Kernel-phase patches and the XexLoadImage hooks that start the title passes. */
VOID SetupKernelLoadHook();

/* Detach: kernel exports point back at the kernel. */
VOID RemoveKernelLoadHook();
