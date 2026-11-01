// Tiny assertion helpers for the C unit tests
#ifndef H_CHECK
#define H_CHECK

#include <stdio.h>
#include <stdlib.h>
#include "globals.h"
#include "worldgen.h"

static int check_failures = 0;

#define CHECK(cond) do { if (!(cond)) { \
  printf("  %s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); check_failures ++; } } while (0)

#define RUN(test) do { int before = check_failures; printf("%s\n", #test); test(); \
  if (check_failures != before) printf("  -> FAILED\n"); } while (0)

// bareiron_run takes the server's tables from alloc_large; a test that calls
// its functions without running it gets them here, before main
static void *check_alloc (size_t size) { return calloc(1, size); }
__attribute__((constructor)) static void check_tables (void) {
  if (tables_alloc(check_alloc)) abort();
}

#define CHECK_DONE() (check_failures ? (printf("%d check(s) failed\n", check_failures), 1) : 0)

#endif
