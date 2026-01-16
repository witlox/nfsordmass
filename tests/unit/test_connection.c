/*
 * Unit tests for connection management and VNI parsing
 */

#include <linux/module.h>
#include <linux/string.h>
#include "kfi_verbs_compat.h"
#include "kfi_internal.h"

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Connection unit tests");

static int test_vni_parse_valid(void)
{
    uint16_t vni;
    int ret;

    pr_info("TEST: VNI parsing - valid cases\n");

    /* Test basic VNI */
    ret = kfi_parse_vni_from_options("vni=1000", &vni);
    if (ret || vni != 1000) {
        pr_err("FAIL: 'vni=1000' -> %d (ret=%d)\n", vni, ret);
        return -1;
    }
    pr_info("  'vni=1000' -> %d OK\n", vni);

    /* Test VNI with other options before */
    ret = kfi_parse_vni_from_options("proto=rdma,vni=2000,port=20049", &vni);
    if (ret || vni != 2000) {
        pr_err("FAIL: 'proto=rdma,vni=2000,port=20049' -> %d (ret=%d)\n", vni, ret);
        return -1;
    }
    pr_info("  'proto=rdma,vni=2000,port=20049' -> %d OK\n", vni);

    /* Test VNI at end */
    ret = kfi_parse_vni_from_options("port=20049,vni=3000", &vni);
    if (ret || vni != 3000) {
        pr_err("FAIL: 'port=20049,vni=3000' -> %d (ret=%d)\n", vni, ret);
        return -1;
    }
    pr_info("  'port=20049,vni=3000' -> %d OK\n", vni);

    /* Test minimum VNI */
    ret = kfi_parse_vni_from_options("vni=0", &vni);
    if (ret || vni != 0) {
        pr_err("FAIL: 'vni=0' -> %d (ret=%d)\n", vni, ret);
        return -1;
    }
    pr_info("  'vni=0' -> %d OK\n", vni);

    /* Test maximum VNI */
    ret = kfi_parse_vni_from_options("vni=65535", &vni);
    if (ret || vni != 65535) {
        pr_err("FAIL: 'vni=65535' -> %d (ret=%d)\n", vni, ret);
        return -1;
    }
    pr_info("  'vni=65535' -> %d OK\n", vni);

    pr_info("PASS: VNI parsing - valid cases\n");
    return 0;
}

static int test_vni_parse_invalid(void)
{
    uint16_t vni;
    int ret;

    pr_info("TEST: VNI parsing - invalid/missing cases\n");

    /* Test no VNI */
    ret = kfi_parse_vni_from_options("proto=rdma,port=20049", &vni);
    if (ret == 0) {
        pr_err("FAIL: Should fail with no VNI\n");
        return -1;
    }
    pr_info("  No VNI -> error (expected)\n");

    /* Test NULL options */
    ret = kfi_parse_vni_from_options(NULL, &vni);
    if (ret == 0) {
        pr_err("FAIL: Should fail with NULL options\n");
        return -1;
    }
    pr_info("  NULL options -> error (expected)\n");

    /* Test empty options */
    ret = kfi_parse_vni_from_options("", &vni);
    if (ret == 0) {
        pr_err("FAIL: Should fail with empty options\n");
        return -1;
    }
    pr_info("  Empty options -> error (expected)\n");

    pr_info("PASS: VNI parsing - invalid/missing cases\n");
    return 0;
}

static int test_auth_key_structure(void)
{
    struct kfi_cxi_auth_key auth_key;

    pr_info("TEST: Auth key structure\n");

    /* Initialize and verify structure */
    memset(&auth_key, 0, sizeof(auth_key));

    auth_key.vni = 1234;
    auth_key.service_id = 1;
    auth_key.traffic_class = 0;

    if (auth_key.vni != 1234) {
        pr_err("FAIL: VNI field mismatch\n");
        return -1;
    }

    if (auth_key.service_id != 1) {
        pr_err("FAIL: service_id field mismatch\n");
        return -1;
    }

    pr_info("  Auth key structure size: %zu bytes\n", sizeof(auth_key));
    pr_info("PASS: Auth key structure\n");
    return 0;
}

static int test_qp_state_transitions(void)
{
    pr_info("TEST: QP state constants\n");

    /* Verify IB QP states exist */
    if (IB_QPS_RESET != 0) {
        pr_info("  IB_QPS_RESET = %d\n", IB_QPS_RESET);
    }

    pr_info("  IB_QPS_INIT = %d\n", IB_QPS_INIT);
    pr_info("  IB_QPS_RTR = %d\n", IB_QPS_RTR);
    pr_info("  IB_QPS_RTS = %d\n", IB_QPS_RTS);
    pr_info("  IB_QPS_ERR = %d\n", IB_QPS_ERR);

    pr_info("PASS: QP state constants\n");
    return 0;
}

static int __init test_connection_init(void)
{
    int failures = 0;

    pr_info("=== Running connection unit tests ===\n");

    if (test_vni_parse_valid())
        failures++;
    if (test_vni_parse_invalid())
        failures++;
    if (test_auth_key_structure())
        failures++;
    if (test_qp_state_transitions())
        failures++;

    pr_info("=== Connection tests: %d failures ===\n", failures);

    /* Return error to prevent module staying loaded */
    return failures ? -EINVAL : -EAGAIN;
}

static void __exit test_connection_exit(void)
{
    pr_info("Connection tests unloaded\n");
}

module_init(test_connection_init);
module_exit(test_connection_exit);
