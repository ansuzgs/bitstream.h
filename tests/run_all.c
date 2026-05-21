/*
 * run_all.c — Collects and runs every test suite.
 *
 * Add new suites here as: void suite_xxx(void); + call in main().
 */
#include "test.h"
#include <stdio.h>

/* Global counters (declared extern in test.h */
int tests_run = 0;
int tests_passed = 0;
int tests_failed = 0;

/* Suite registration functions, defined in each test_*.c */
void suite_reader(void);
void suite_writer(void);
void suite_regression(void);
void suite_integration(void);
void suite_peek_skip(void);
void suite_lsb(void);

int main(void) {
    printf("bitstream.h - test suite\n");
    printf("========================\n\n");

    suite_reader();
    suite_writer();
    suite_regression();
    suite_integration();
    suite_peek_skip();
    suite_lsb();

    /* Summary */
    printf("========================\n\n");
    printf("%d/%d tests passed.\n", tests_passed, tests_run);

    if (tests_passed == tests_run) {
        printf("All tests pased.\n");
    }

    return (tests_passed == tests_run) ? 0 : 1;
}
