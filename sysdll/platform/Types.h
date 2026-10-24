#pragma once

/* XDK types on the console; a host shim so tests build the same sources. */

#ifdef _XBOX
#include "../stdafx.h"
#else
#include <stddef.h>
#include <stdint.h>
typedef void VOID;
typedef char CHAR;
typedef uint16_t WCHAR; /* 16-bit, as on the console; not the host's wchar_t */
typedef uint8_t BYTE;
typedef uint16_t WORD;
typedef int32_t LONG;
typedef uint32_t DWORD;
typedef uint64_t ULONGLONG;
typedef int BOOL;
typedef void *PVOID;
typedef uintptr_t DWORD_PTR;
#define TRUE 1
#define FALSE 0
#endif
