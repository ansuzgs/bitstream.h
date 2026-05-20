/*
 * test.h — Minimal test harness for bitstream.h
 *
 * Usage:
 *   Each test file defines a suite registration function that calls RUN_TEST().
 *   run_all.c collects all suites and prints a final summary.
 */
#ifndef TEST_H
#define TEST_H

#include <stdio.h>
#include <string.h>

/* counters: defined once in run_all.c */
extern int tests_run;
extern int tests_passed;
extern int tests_failed;

#define RUN_TEST(fn) \
	do { \
		printf("  %-50s ", #fn); \
		tests_run++; \
		fn(); \
		tests_passed++; \
		printf("OK\n"); \
	} while (0)

/*
 * In an assert() fires inside a test function the process aborts immediately,
 * so tests_failed is only useful if we later switch to setjmp-based recovery.
 * For now a failure = abort with a clear message from assert().
 */

#endif /* TEST_H */
