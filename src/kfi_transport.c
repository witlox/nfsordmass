#include <linux/sunrpc/xprt.h>
#include <linux/module.h>
#include "kfi_verbs_compat.h"
#include "kfi_internal.h"

/* Forward declaration */
static struct rpc_xprt *xs_setup_rdma_kfi(struct xprt_create *args);

static struct xprt_class xprt_rdma_kfi = {
    .list           = LIST_HEAD_INIT(xprt_rdma_kfi.list),
    .name           = "rdma_kfi",
    .owner          = THIS_MODULE,
    .ident          = XPRT_TRANSPORT_RDMA,
    .setup          = xs_setup_rdma_kfi,
    .netid          = { "rdma", "rdma6", "" },
};

/* Placeholder setup function - to be implemented */
static struct rpc_xprt *xs_setup_rdma_kfi(struct xprt_create *args)
{
    pr_err("xs_setup_rdma_kfi: not yet implemented\n");
    return ERR_PTR(-ENOSYS);
}

/* Forward declarations */
extern int kfi_verbs_compat_init(void);
extern void kfi_verbs_compat_exit(void);

static int __init xprt_rdma_kfi_init(void)
{
    int rc;

    /* Note: kfabric is loaded separately as a kernel module (kfabric.ko) */
    /* No kfi_init() function exists - kfabric initializes when loaded */

    /* Initialize kfabric compatibility layer */
    rc = kfi_verbs_compat_init();
    if (rc) {
        pr_err("kfi_verbs_compat_init failed: %d\n", rc);
        return rc;
    }

    /* Register with SUNRPC */
    rc = xprt_register_transport(&xprt_rdma_kfi);
    if (rc) {
        pr_err("xprt_register_transport failed: %d\n", rc);
        kfi_verbs_compat_exit();
        return rc;
    }

    pr_info("NFS RDMA kfabric transport loaded\n");
    return 0;
}

static void __exit xprt_rdma_kfi_exit(void)
{
    xprt_unregister_transport(&xprt_rdma_kfi);
    kfi_verbs_compat_exit();
    pr_info("NFS RDMA kfabric transport unloaded\n");
}

module_init(xprt_rdma_kfi_init);
module_exit(xprt_rdma_kfi_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pim Witlox");
MODULE_DESCRIPTION("NFS RDMA transport using kfabric for HPE Slingshot");
MODULE_ALIAS("svcrdma_kfi");
MODULE_ALIAS("xprtrdma_kfi");
