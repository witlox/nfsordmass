/*
 * Unit tests for key mapping
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/random.h>
#include "kfi_internal.h"

/* Include the implementation for standalone test module */
#include "../../src/kfi_key_mapping.c"

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Key mapping unit tests");

static int test_key_register_lookup(void)
{
    u64 kfi_key = 0x123456789ABCDEF0ULL;
    u32 ib_key;
    u64 kfi_key_back;
    int ret;
    
    pr_info("TEST: Key register and lookup\n");
    
    /* Register */
    ret = kfi_key_register(kfi_key, &ib_key);
    if (ret) {
        pr_err("FAIL: kfi_key_register returned %d\n", ret);
        return -1;
    }
    
    pr_info("  Registered 0x%llx -> 0x%x\n", kfi_key, ib_key);

    /* Lookup IB->KFI */
    ret = kfi_key_lookup_ib(ib_key, &kfi_key_back);
    if (ret) {
        pr_err("FAIL: kfi_key_lookup_ib returned %d\n", ret);
        return -1;
    }
    
    if (kfi_key_back != kfi_key) {
        pr_err("FAIL: Key mismatch: 0x%llx != 0x%llx\n", 
               kfi_key_back, kfi_key);
        return -1;
    }
    
    /* Lookup KFI->IB */
    {
        u32 ib_key_back;
        ret = kfi_key_lookup_kfi(kfi_key, &ib_key_back);
        if (ret) {
            pr_err("FAIL: kfi_key_lookup_kfi returned %d\n", ret);
            return -1;
        }

        if (ib_key_back != ib_key) {
            pr_err("FAIL: IB key mismatch: 0x%x != 0x%x\n",
                   ib_key_back, ib_key);
            return -1;
        }
    }
    
    /* Cleanup */
    kfi_key_unregister(ib_key);
    
    /* Verify removal */
    ret = kfi_key_lookup_ib(ib_key, &kfi_key_back);
    if (ret != -ENOENT) {
        pr_err("FAIL: Key not removed properly\n");
        return -1;
    }
    
    pr_info("PASS: Key register and lookup\n");
    return 0;
}

static int test_key_collision(void)
{
    u64 kfi_key1 = 0x1111111111111111ULL;
    u64 kfi_key2 = 0x2222222222222222ULL;
    u32 ib_key1, ib_key2;
    int ret;
    
    pr_info("TEST: Key collision handling\n");
    
    ret = kfi_key_register(kfi_key1, &ib_key1);
    if (ret) {
        pr_err("FAIL: First key registration\n");
        return -1;
    }
    
    ret = kfi_key_register(kfi_key2, &ib_key2);
    if (ret) {
        pr_err("FAIL: Second key registration\n");
        kfi_key_unregister(ib_key1);
        return -1;
    }
    
    if (ib_key1 == ib_key2) {
        pr_err("FAIL: IB keys collided: 0x%x\n", ib_key1);
        kfi_key_unregister(ib_key1);
        return -1;
    }
    
    kfi_key_unregister(ib_key1);
    kfi_key_unregister(ib_key2);
    
    pr_info("PASS: Key collision handling\n");
    return 0;
}

static int test_key_stress(void)
{
    #define STRESS_COUNT 100
    u64 kfi_keys[STRESS_COUNT];
    u32 ib_keys[STRESS_COUNT];
    int i, ret;
    int failures = 0;

    pr_info("TEST: Key mapping stress test (%d keys)\n", STRESS_COUNT);

    /* Register many keys */
    for (i = 0; i < STRESS_COUNT; i++) {
        get_random_bytes(&kfi_keys[i], sizeof(u64));
        ret = kfi_key_register(kfi_keys[i], &ib_keys[i]);
        if (ret) {
            pr_err("FAIL: Registration %d failed: %d\n", i, ret);
            failures++;
            ib_keys[i] = 0; /* Mark as invalid */
        }
    }

    /* Verify all lookups */
    for (i = 0; i < STRESS_COUNT; i++) {
        u64 kfi_key_back;
        if (ib_keys[i] == 0)
            continue;

        ret = kfi_key_lookup_ib(ib_keys[i], &kfi_key_back);
        if (ret || kfi_key_back != kfi_keys[i]) {
            pr_err("FAIL: Lookup %d failed\n", i);
            failures++;
        }
    }

    /* Cleanup */
    for (i = 0; i < STRESS_COUNT; i++) {
        if (ib_keys[i] != 0)
            kfi_key_unregister(ib_keys[i]);
    }

    if (failures) {
        pr_err("FAIL: Stress test had %d failures\n", failures);
        return -1;
    }

    pr_info("PASS: Key mapping stress test\n");
    return 0;
    #undef STRESS_COUNT
}

static int test_key_double_unregister(void)
{
    u64 kfi_key = 0xDEADBEEFCAFEBABEULL;
    u32 ib_key;
    int ret;

    pr_info("TEST: Double unregister safety\n");

    ret = kfi_key_register(kfi_key, &ib_key);
    if (ret) {
        pr_err("FAIL: Registration failed\n");
        return -1;
    }

    /* First unregister */
    kfi_key_unregister(ib_key);

    /* Second unregister - should not crash */
    kfi_key_unregister(ib_key);

    pr_info("PASS: Double unregister safety\n");
    return 0;
}

static int test_key_lookup_invalid(void)
{
    u64 kfi_key;
    u32 ib_key;
    int ret;

    pr_info("TEST: Invalid key lookup\n");

    /* Lookup non-existent IB key */
    ret = kfi_key_lookup_ib(0xFFFFFFFF, &kfi_key);
    if (ret != -ENOENT) {
        pr_err("FAIL: Expected -ENOENT, got %d\n", ret);
        return -1;
    }

    /* Lookup non-existent KFI key */
    ret = kfi_key_lookup_kfi(0xFFFFFFFFFFFFFFFFULL, &ib_key);
    if (ret != -ENOENT) {
        pr_err("FAIL: Expected -ENOENT, got %d\n", ret);
        return -1;
    }

    pr_info("PASS: Invalid key lookup\n");
    return 0;
}

static int __init test_key_mapping_init(void)
{
    int failures = 0;

    pr_info("=== Running key mapping unit tests ===\n");
    
    kfi_key_mapping_init();
    
    if (test_key_register_lookup())
        failures++;
    if (test_key_collision())
        failures++;
    if (test_key_stress())
        failures++;
    if (test_key_double_unregister())
        failures++;
    if (test_key_lookup_invalid())
        failures++;

    kfi_key_mapping_cleanup();
    
    pr_info("=== Key mapping tests: %d failures ===\n", failures);

    /* Return error to prevent module staying loaded */
    return failures ? -EINVAL : -EAGAIN;
}

static void __exit test_key_mapping_exit(void)
{
    pr_info("Key mapping tests unloaded\n");
}

module_init(test_key_mapping_init);
module_exit(test_key_mapping_exit);
