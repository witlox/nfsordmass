/*
 * Unit tests for memory registration and MR cache
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include "kfi_verbs_compat.h"
#include "kfi_internal.h"

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Memory registration unit tests");

static int test_mr_cache_create_destroy(void)
{
    struct kfi_mr_cache *cache;

    pr_info("TEST: MR cache create/destroy\n");

    cache = kfi_mr_cache_create(64);
    if (!cache) {
        pr_err("FAIL: kfi_mr_cache_create returned NULL\n");
        return -1;
    }

    pr_info("  Created cache with max_entries=64\n");

    kfi_mr_cache_destroy(cache);

    pr_info("PASS: MR cache create/destroy\n");
    return 0;
}

static int test_mr_cache_stats(void)
{
    struct kfi_mr_cache *cache;

    pr_info("TEST: MR cache stats\n");

    cache = kfi_mr_cache_create(32);
    if (!cache) {
        pr_err("FAIL: Cache creation failed\n");
        return -1;
    }

    /* Check initial stats */
    if (atomic_read(&cache->current_entries) != 0) {
        pr_err("FAIL: Initial entries not zero\n");
        kfi_mr_cache_destroy(cache);
        return -1;
    }

    if (atomic64_read(&cache->hits) != 0 || atomic64_read(&cache->misses) != 0) {
        pr_err("FAIL: Initial hit/miss counters not zero\n");
        kfi_mr_cache_destroy(cache);
        return -1;
    }

    pr_info("  Initial stats verified\n");

    kfi_mr_cache_destroy(cache);

    pr_info("PASS: MR cache stats\n");
    return 0;
}

static int test_batch_context(void)
{
    struct kfi_batch_ctx batch;

    pr_info("TEST: Batch context initialization\n");

    kfi_batch_init(&batch);

    if (batch.count != 0) {
        pr_err("FAIL: Batch count not zero after init\n");
        return -1;
    }

    pr_info("PASS: Batch context initialization\n");
    return 0;
}

static int test_memory_constants(void)
{
    pr_info("TEST: Memory constants\n");

    /* Verify constants are reasonable */
    if (KFI_MAX_SGE < 1 || KFI_MAX_SGE > 256) {
        pr_err("FAIL: KFI_MAX_SGE out of range: %d\n", KFI_MAX_SGE);
        return -1;
    }
    pr_info("  KFI_MAX_SGE = %d\n", KFI_MAX_SGE);

    if (KFI_MAX_INLINE_DATA < 64 || KFI_MAX_INLINE_DATA > 4096) {
        pr_err("FAIL: KFI_MAX_INLINE_DATA out of range: %d\n", KFI_MAX_INLINE_DATA);
        return -1;
    }
    pr_info("  KFI_MAX_INLINE_DATA = %d\n", KFI_MAX_INLINE_DATA);

    if (KFI_MR_CACHE_SIZE < 1) {
        pr_err("FAIL: KFI_MR_CACHE_SIZE too small: %d\n", KFI_MR_CACHE_SIZE);
        return -1;
    }
    pr_info("  KFI_MR_CACHE_SIZE = %d\n", KFI_MR_CACHE_SIZE);

    if (KFI_MR_MAX_REGIONS < 1) {
        pr_err("FAIL: KFI_MR_MAX_REGIONS too small: %d\n", KFI_MR_MAX_REGIONS);
        return -1;
    }
    pr_info("  KFI_MR_MAX_REGIONS = %d\n", KFI_MR_MAX_REGIONS);

    pr_info("PASS: Memory constants\n");
    return 0;
}

static int test_vni_constants(void)
{
    pr_info("TEST: VNI constants\n");

    if (KFI_DEFAULT_VNI > KFI_VNI_MAX) {
        pr_err("FAIL: DEFAULT_VNI > VNI_MAX\n");
        return -1;
    }
    pr_info("  KFI_DEFAULT_VNI = %d\n", KFI_DEFAULT_VNI);
    pr_info("  KFI_VNI_MAX = %d\n", KFI_VNI_MAX);

    pr_info("PASS: VNI constants\n");
    return 0;
}

static int __init test_memory_init(void)
{
    int failures = 0;

    pr_info("=== Running memory unit tests ===\n");

    if (test_memory_constants())
        failures++;
    if (test_vni_constants())
        failures++;
    if (test_batch_context())
        failures++;
    if (test_mr_cache_create_destroy())
        failures++;
    if (test_mr_cache_stats())
        failures++;

    pr_info("=== Memory tests: %d failures ===\n", failures);

    /* Return error to prevent module staying loaded */
    return failures ? -EINVAL : -EAGAIN;
}

static void __exit test_memory_exit(void)
{
    pr_info("Memory tests unloaded\n");
}

module_init(test_memory_init);
module_exit(test_memory_exit);
