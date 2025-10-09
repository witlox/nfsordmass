/*
 * Unit tests for verbs<->kfabric translation
 */

#include <linux/module.h>
#include "../../include/kfi_internal.h"

static int test_opcode_translation(void)
{
    pr_info("TEST: Opcode translation\n");
    
    /* Test IB_WR_SEND -> KFI_SEND */
    if (ib_opcode_to_kfi(IB_WR_SEND) != KFI_SEND) {
        pr_err("FAIL: IB_WR_SEND translation\n");
        return -1;
    }
    
    /* Test IB_WR_RDMA_WRITE -> KFI_WRITE */
    if (ib_opcode_to_kfi(IB_WR_RDMA_WRITE) != KFI_WRITE) {
        pr_err("FAIL: IB_WR_RDMA_WRITE translation\n");
        return -1;
    }
    
    /* Test IB_WR_RDMA_READ -> KFI_READ */
    if (ib_opcode_to_kfi(IB_WR_RDMA_READ) != KFI_READ) {
        pr_err("FAIL: IB_WR_RDMA_READ translation\n");
        return -1;
    }
    
    pr_info("PASS: Opcode translation\n");
    return 0;
}

static int test_status_translation(void)
{
    pr_info("TEST: Status translation\n");
    
    if (kfi_errno_to_ib_status(0) != IB_WC_SUCCESS) {
        pr_err("FAIL: Success status\n");
        return -1;
    }
    
    if (kfi_errno_to_ib_status(-KFI_ETRUNC) != IB_WC_LOC_LEN_ERR) {
        pr_err("FAIL: Truncation error status\n");
        return -1;
    }
    
    pr_info("PASS: Status translation\n");
    return 0;
}

static int __init test_translation_init(void)
{
    int ret = 0;
    
    pr_info("=== Running translation unit tests ===\n");
    
    ret |= test_opcode_translation();
    ret |= test_status_translation();
    
    if (ret == 0)
        pr_info("=== All translation tests PASSED ===\n");
    else
        pr_err("=== Some translation tests FAILED ===\n");
    
    return ret;
}

static void __exit test_translation_exit(void)
{
    pr_info("Translation tests unloaded\n");
}

module_init(test_translation_init);
module_exit(test_translation_exit);
