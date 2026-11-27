/* platform: host-agnostic -- the tests' tiny harness: CHECK records a failure and continues */
#ifndef VF_CHECK_H
#define VF_CHECK_H
#include <stdio.h>
static int check_failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); check_failures++; } } while (0)
#define CHECK_EQ(a, b) do { long long _a = (long long)(a), _b = (long long)(b); if (_a != _b) { fprintf(stderr, "%s:%d: %s == %lld, want %lld\n", __FILE__, __LINE__, #a, _a, _b); check_failures++; } } while (0)
#define CHECK_DONE() (check_failures ? (fprintf(stderr, "%d failure(s)\n", check_failures), 1) : (printf("ok %s\n", __FILE__), 0))
#endif
