/* Minimal assertion harness for host-side firmware logic tests. */
#ifndef VV_HARNESS_H
#define VV_HARNESS_H

#include <stdio.h>
#include <string.h>

static int vv_passed = 0;
static int vv_failed = 0;

#define VV_CHECK(name, cond)                                          \
    do {                                                              \
        if (cond) { vv_passed++; printf("TEST PASS %s\n", name); }    \
        else { vv_failed++; printf("TEST FAIL %s (%s:%d)\n",          \
                                   name, __FILE__, __LINE__); }       \
    } while (0)

#define VV_EQ_U32(name, got, want)                                    \
    do {                                                              \
        unsigned long g = (unsigned long)(got);                       \
        unsigned long w = (unsigned long)(want);                      \
        if (g == w) { vv_passed++; printf("TEST PASS %s\n", name); }   \
        else { vv_failed++;                                           \
               printf("TEST FAIL %s: got %lu want %lu (%s:%d)\n",     \
                      name, g, w, __FILE__, __LINE__); }              \
    } while (0)

#define VV_REPORT()                                                   \
    do {                                                              \
        printf("%d passed, %d failed\n", vv_passed, vv_failed);       \
        return vv_failed == 0 ? 0 : 1;                                \
    } while (0)

#endif /* VV_HARNESS_H */
