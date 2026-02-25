#include "unity.h"

void test_return_true() { TEST_ASSERT(1); }

void setUp() { }

void tearDown() { }

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_return_true);
    return UNITY_END();
}
