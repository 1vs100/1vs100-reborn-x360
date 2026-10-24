#pragma once

/*
 * Hook code runs on title threads: no CRT calls there. #pragma deprecated + /we4995 turns a direct
 * call into a compile error; write a CRT-free replacement instead. Compiler-inserted helpers are
 * caught by the link audit.
 */
#ifdef _MSC_VER
#pragma deprecated(strlen, strcpy, strcat, strcmp, strncmp, strcpy_s, strcat_s, strncpy, strstr,   \
	strchr, strrchr, strtok, strtok_s, strpbrk, strspn, strcspn, sprintf, vsprintf, _vsnprintf,    \
	vsnprintf, _snprintf, snprintf, sprintf_s, vsprintf_s, printf, fprintf, vfprintf, puts, fputs, \
	malloc, calloc, realloc, free, _expand, _msize, atoi, atol, atof, strtol, strtoul, strtod,     \
	tolower, toupper, isalpha, isdigit, isspace, rand, srand, qsort, bsearch)
#endif
