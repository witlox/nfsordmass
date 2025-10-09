/*
 * Unit tests for key mapping
 */

#include <linux/module.h>
#include "../../src/kfi_key_mapping.c" /* Include implementation */

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
    
    pr_info("Registered 0x%llx -> 0x%x\n", kfi_key, ib_key);
    
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

static int __init test_key_mapping_init(void)
{
    int ret = 0;
    
    pr_info("=== Running key mapping unit tests ===\n");
    
    kfi_key_mapping_init();
    
    ret |= test_key_register_lookup();
    ret |= test_key_collision();
    
    kfi_key_mapping_cleanup();
    
    if (ret == 0)
        pr_info("=== All key mapping tests PASSED ===\n");
    else
        pr_err("=== Some key mapping tests FAILED ===\n");
    
    return ret;
}

static void __exit test_key_mapping_exit(void)
{
    pr_info("Key mapping tests unloaded\n");
}

module_init(test_key_mapping_init);
module_exit(test_key_mapping_exit);
