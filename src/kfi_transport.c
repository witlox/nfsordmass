#include <linux/sunrpc/xprt.h>
#include "kfi_verbs_compat.h"

static struct xprt_class xprt_rdma_kfi = {
    .list           = LIST_HEAD_INIT(xprt_rdma_kfi.list),
    .name           = "rdma_kfi",
    .owner          = THIS_MODULE,
    .ident          = XPRT_TRANSPORT_RDMA,
    .setup          = xs_setup_rdma_kfi,
    .netid          = { "rdma", "rdma6", "" },
};

static int __init xprt_rdma_kfi_init(void)
{
    int rc;
    
    /* Initialize kfabric */
    rc = kfi_init();
    if (rc) {
        pr_err("kfi_init failed: %d\n", rc);
        return rc;
    }
    
    /* Register with SUNRPC */
    rc = xprt_register_transport(&xprt_rdma_kfi);
    if (rc) {
        pr_err("xprt_register_transport failed: %d\n", rc);
        return rc;
    }
    
    pr_info("NFS RDMA kfabric transport loaded\n");
    return 0;
}

static void __exit xprt_rdma_kfi_exit(void)
{
    xprt_unregister_transport(&xprt_rdma_kfi);
    /* Cleanup kfabric resources */
    pr_info("NFS RDMA kfabric transport unloaded\n");
}

module_init(xprt_rdma_kfi_init);
module_exit(xprt_rdma_kfi_exit);

MODULE_LICENSE("GPLv2");
MODULE_AUTHOR("Pim Witlox");
MODULE_DESCRIPTION("NFS RDMA transport using kfabric for HPE Slingshot");
MODULE_ALIAS("svcrdma_kfi");
MODULE_ALIAS("xprtrdma_kfi");
