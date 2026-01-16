/*
 * Unit tests for kfabric errno definitions
 */

#include <linux/module.h>
#include <linux/errno.h>
#include "kfi_errno.h"
#include "kfi_verbs_compat.h"

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Errno definition tests");

static int test_errno_values(void)
{
    pr_info("TEST: Errno value definitions\n");

    /* Verify KFI_SUCCESS is 0 */
    if (KFI_SUCCESS != 0) {
        pr_err("FAIL: KFI_SUCCESS should be 0, got %d\n", KFI_SUCCESS);
        return -1;
    }
    pr_info("  KFI_SUCCESS = %d\n", KFI_SUCCESS);

    /* Verify error codes are positive (used as -KFI_xxx) */
    if (KFI_EAGAIN <= 0) {
        pr_err("FAIL: KFI_EAGAIN should be positive\n");
        return -1;
    }
    pr_info("  KFI_EAGAIN = %d\n", KFI_EAGAIN);

    if (KFI_EACCES <= 0) {
        pr_err("FAIL: KFI_EACCES should be positive\n");
        return -1;
    }
    pr_info("  KFI_EACCES = %d\n", KFI_EACCES);

    if (KFI_ECANCELED <= 0) {
        pr_err("FAIL: KFI_ECANCELED should be positive\n");
        return -1;
    }
    pr_info("  KFI_ECANCELED = %d\n", KFI_ECANCELED);

    pr_info("PASS: Errno value definitions\n");
    return 0;
}

static int test_errno_no_conflict(void)
{
    pr_info("TEST: Errno no conflict with standard\n");

    /* KFI errors should not conflict with standard errno values */
    /* Standard errno values are typically < 256 */

    if (KFI_EAGAIN <= 256) {
        pr_warn("  WARNING: KFI_EAGAIN (%d) may conflict with standard errno\n",
                KFI_EAGAIN);
    }

    /* Verify they don't equal standard values */
    if (KFI_EAGAIN == EAGAIN) {
        pr_err("FAIL: KFI_EAGAIN == EAGAIN (potential confusion)\n");
        return -1;
    }

    pr_info("  KFI errors offset from standard: OK\n");
    pr_info("  KFI_ERRNO_OFFSET = %d\n", KFI_ERRNO_OFFSET);

    pr_info("PASS: Errno no conflict with standard\n");
    return 0;
}

static int test_provider_errno(void)
{
    pr_info("TEST: Provider-specific errno values\n");

    /* Verify provider errors are defined */
    if (KKFI_ETRUNC <= 0) {
        pr_err("FAIL: KKFI_ETRUNC should be positive\n");
        return -1;
    }
    pr_info("  KKFI_ETRUNC = %d\n", KKFI_ETRUNC);

    if (KFI_EOVERRUN <= 0) {
        pr_err("FAIL: KFI_EOVERRUN should be positive\n");
        return -1;
    }
    pr_info("  KFI_EOVERRUN = %d\n", KFI_EOVERRUN);

    /* Provider errors should be higher than base errors */
    if (KKFI_ETRUNC <= KFI_EAGAIN) {
        pr_err("FAIL: Provider errors should be > base errors\n");
        return -1;
    }

    pr_info("  KFI_ERRNO_PROV_OFFSET = %d\n", KFI_ERRNO_PROV_OFFSET);

    pr_info("PASS: Provider-specific errno values\n");
    return 0;
}

static int test_errno_uniqueness(void)
{
    int errors[] = {
        KFI_SUCCESS, KFI_EAGAIN, KFI_EACCES, KFI_ECANCELED,
        KFI_EINVAL, KFI_ENOMEM, KFI_ENODATA, KFI_EMSGSIZE,
        KFI_ENOSYS, KFI_ENOENT, KFI_EBUSY, KKFI_ETRUNC
    };
    int count = sizeof(errors) / sizeof(errors[0]);
    int i, j;

    pr_info("TEST: Errno uniqueness\n");

    for (i = 0; i < count; i++) {
        for (j = i + 1; j < count; j++) {
            if (errors[i] == errors[j] && errors[i] != 0) {
                pr_err("FAIL: Duplicate errno value: %d\n", errors[i]);
                return -1;
            }
        }
    }

    pr_info("  All %d error codes are unique\n", count);
    pr_info("PASS: Errno uniqueness\n");
    return 0;
}

static int __init test_errno_init(void)
{
    int failures = 0;

    pr_info("=== Running errno unit tests ===\n");

    if (test_errno_values())
        failures++;
    if (test_errno_no_conflict())
        failures++;
    if (test_provider_errno())
        failures++;
    if (test_errno_uniqueness())
        failures++;

    pr_info("=== Errno tests: %d failures ===\n", failures);

    /* Return error to prevent module staying loaded */
    return failures ? -EINVAL : -EAGAIN;
}

static void __exit test_errno_exit(void)
{
    pr_info("Errno tests unloaded\n");
}

module_init(test_errno_init);
module_exit(test_errno_exit);
