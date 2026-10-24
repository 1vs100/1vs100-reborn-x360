#pragma once
#include <stdio.h>

/* CHECK records a failure and keeps going; main returns the count. */
extern int g_Failures;

#define CHECK(cond)                                                         \
	do {                                                                    \
		if (!(cond)) {                                                      \
			printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
			g_Failures++;                                                   \
		}                                                                   \
	} while (0)

#define CHECK_EQ_HEX(actual, expected)                                                            \
	do {                                                                                          \
		unsigned long long a_ = (unsigned long long)(actual);                                     \
		unsigned long long e_ = (unsigned long long)(expected);                                   \
		if (a_ != e_) {                                                                           \
			printf("%s:%d: %s = 0x%llX, expected 0x%llX\n", __FILE__, __LINE__, #actual, a_, e_); \
			g_Failures++;                                                                         \
		}                                                                                         \
	} while (0)
