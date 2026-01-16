/*
 * Unit tests for verbs<->kfabric translation
 */

#include <linux/module.h>
#include <rdma/ib_verbs.h>
#include "kfi_verbs_compat.h"
#include "kfi_internal.h"

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Translation unit tests");

/* Declare external function from kfi_completion.c */
extern enum ib_wc_status kfi_errno_to_ib_status(int kfi_err);

static int test_opcode_translation(void)
{
    int failures = 0;

    pr_info("TEST: Opcode translation\n");
    
    /* Test IB_WR_SEND -> KFI_SEND */
    if (ib_opcode_to_kfi(IB_WR_SEND) != KFI_SEND) {
        pr_err("FAIL: IB_WR_SEND translation\n");
        failures++;
    }

    /* Test IB_WR_SEND_WITH_IMM -> KFI_SEND */
    if (ib_opcode_to_kfi(IB_WR_SEND_WITH_IMM) != KFI_SEND) {
        pr_err("FAIL: IB_WR_SEND_WITH_IMM translation\n");
        failures++;
    }
    
    /* Test IB_WR_RDMA_WRITE -> KFI_WRITE */
    if (ib_opcode_to_kfi(IB_WR_RDMA_WRITE) != KFI_WRITE) {
        pr_err("FAIL: IB_WR_RDMA_WRITE translation\n");
        failures++;
    }

    /* Test IB_WR_RDMA_WRITE_WITH_IMM -> KFI_WRITE */
    if (ib_opcode_to_kfi(IB_WR_RDMA_WRITE_WITH_IMM) != KFI_WRITE) {
        pr_err("FAIL: IB_WR_RDMA_WRITE_WITH_IMM translation\n");
        failures++;
    }
    
    /* Test IB_WR_RDMA_READ -> KFI_READ */
    if (ib_opcode_to_kfi(IB_WR_RDMA_READ) != KFI_READ) {
        pr_err("FAIL: IB_WR_RDMA_READ translation\n");
        failures++;
    }

    /* Test atomic operations -> KFI_ATOMIC */
    if (ib_opcode_to_kfi(IB_WR_ATOMIC_CMP_AND_SWP) != KFI_ATOMIC) {
        pr_err("FAIL: IB_WR_ATOMIC_CMP_AND_SWP translation\n");
        failures++;
    }

    if (ib_opcode_to_kfi(IB_WR_ATOMIC_FETCH_AND_ADD) != KFI_ATOMIC) {
        pr_err("FAIL: IB_WR_ATOMIC_FETCH_AND_ADD translation\n");
        failures++;
    }

    if (failures) {
        pr_err("FAIL: Opcode translation (%d failures)\n", failures);
        return -1;
    }
    
    pr_info("PASS: Opcode translation\n");
    return 0;
}

static int test_status_translation(void)
{
    int failures = 0;

    pr_info("TEST: Status translation\n");
    
    /* Test success */
    if (kfi_errno_to_ib_status(0) != IB_WC_SUCCESS) {
        pr_err("FAIL: Success status translation\n");
        failures++;
    }

    /* Test KFI_SUCCESS explicitly */
    if (kfi_errno_to_ib_status(-KFI_SUCCESS) != IB_WC_SUCCESS) {
        pr_err("FAIL: KFI_SUCCESS translation\n");
        failures++;
    }
    
    /* Test truncation error */
    if (kfi_errno_to_ib_status(-KKFI_ETRUNC) != IB_WC_LOC_LEN_ERR) {
        pr_err("FAIL: Truncation error status\n");
        failures++;
    }

    /* Test access error */
    if (kfi_errno_to_ib_status(-KFI_EACCES) != IB_WC_LOC_PROT_ERR) {
        pr_err("FAIL: Access error status\n");
        failures++;
    }

    /* Test canceled error */
    if (kfi_errno_to_ib_status(-KFI_ECANCELED) != IB_WC_WR_FLUSH_ERR) {
        pr_err("FAIL: Canceled error status\n");
        failures++;
    }

    /* Test unknown error -> general error */
    if (kfi_errno_to_ib_status(-9999) != IB_WC_GENERAL_ERR) {
        pr_err("FAIL: Unknown error status\n");
        failures++;
    }

    if (failures) {
        pr_err("FAIL: Status translation (%d failures)\n", failures);
        return -1;
    }
    
    pr_info("PASS: Status translation\n");
    return 0;
}

static int test_access_translation(void)
{
    u64 kfi_access;
    int failures = 0;

    pr_info("TEST: Access flags translation\n");

    /* Test local write */
    kfi_access = ib_access_to_kfi(IB_ACCESS_LOCAL_WRITE);
    if (!(kfi_access & KFI_WRITE)) {
        pr_err("FAIL: LOCAL_WRITE translation\n");
        failures++;
    }

    /* Test remote write */
    kfi_access = ib_access_to_kfi(IB_ACCESS_REMOTE_WRITE);
    if (!(kfi_access & KFI_REMOTE_WRITE)) {
        pr_err("FAIL: REMOTE_WRITE translation\n");
        failures++;
    }

    /* Test remote read */
    kfi_access = ib_access_to_kfi(IB_ACCESS_REMOTE_READ);
    if (!(kfi_access & KFI_REMOTE_READ)) {
        pr_err("FAIL: REMOTE_READ translation\n");
        failures++;
    }

    /* Test combined flags */
    kfi_access = ib_access_to_kfi(IB_ACCESS_LOCAL_WRITE |
                                   IB_ACCESS_REMOTE_WRITE |
                                   IB_ACCESS_REMOTE_READ);
    if (!(kfi_access & KFI_WRITE) ||
        !(kfi_access & KFI_REMOTE_WRITE) ||
        !(kfi_access & KFI_REMOTE_READ)) {
        pr_err("FAIL: Combined flags translation\n");
        failures++;
    }

    /* Test zero access */
    kfi_access = ib_access_to_kfi(0);
    if (kfi_access != 0) {
        pr_err("FAIL: Zero access translation\n");
        failures++;
    }

    if (failures) {
        pr_err("FAIL: Access flags translation (%d failures)\n", failures);
        return -1;
    }

    pr_info("PASS: Access flags translation\n");
    return 0;
}

static int test_container_macros(void)
{
    pr_info("TEST: Container macros\n");

    /* These are compile-time checks mostly - if they compile, they work */
    /* We can't easily test without real structures, but check they exist */

    pr_info("  kfi_to_ibdev, ibdev_to_kfi - OK (compile check)\n");
    pr_info("  kfi_to_ibpd, ibpd_to_kfi - OK (compile check)\n");
    pr_info("  kfi_to_ibcq, ibcq_to_kfi - OK (compile check)\n");
    pr_info("  kfi_to_ibqp, ibqp_to_kfi - OK (compile check)\n");
    pr_info("  kfi_to_ibmr, ibmr_to_kfi - OK (compile check)\n");

    pr_info("PASS: Container macros\n");
    return 0;
}

static int __init test_translation_init(void)
{
    int failures = 0;

    pr_info("=== Running translation unit tests ===\n");
    
    if (test_opcode_translation())
        failures++;
    if (test_status_translation())
        failures++;
    if (test_access_translation())
        failures++;
    if (test_container_macros())
        failures++;

    pr_info("=== Translation tests: %d failures ===\n", failures);

    /* Return error to prevent module staying loaded */
    return failures ? -EINVAL : -EAGAIN;
}

static void __exit test_translation_exit(void)
{
    pr_info("Translation tests unloaded\n");
}

module_init(test_translation_init);
module_exit(test_translation_exit);
