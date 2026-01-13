#include <linux/sunrpc/svc_rdma.h>
#include <linux/sunrpc/svc_xprt.h>
#include <linux/module.h>
#include "kfi_verbs_compat.h"
#include "kfi_internal.h"

/* Forward declarations */
static struct svc_xprt *svc_rdma_kfi_create(struct svc_serv *serv,
                                            struct net *net,
                                            struct sockaddr *sa, int salen,
                                            int flags);
static void svc_rdma_kfi_close(struct svc_xprt *xprt);
static int svc_rdma_kfi_recvfrom(struct svc_rqst *rqstp);
static int svc_rdma_kfi_sendto(struct svc_rqst *rqstp);
static void svc_rdma_kfi_detach(struct svc_xprt *xprt);

static struct svc_xprt_ops svc_rdma_kfi_ops = {
    .xpo_create = svc_rdma_kfi_create,
    .xpo_recvfrom = svc_rdma_kfi_recvfrom,
    .xpo_sendto = svc_rdma_kfi_sendto,
    .xpo_detach = svc_rdma_kfi_detach,
    .xpo_free = svc_rdma_kfi_close,
    .xpo_has_wspace = NULL,
};

static struct svc_xprt_class svc_rdma_kfi_class = {
    .xcl_name = "rdma_kfi",
    .xcl_owner = THIS_MODULE,
    .xcl_ops = &svc_rdma_kfi_ops,
    .xcl_max_payload = RPCSVC_MAXPAYLOAD_RDMA,
    .xcl_ident = XPRT_TRANSPORT_RDMA,
};

/* Placeholder implementations */

static struct svc_xprt *svc_rdma_kfi_create(struct svc_serv *serv,
                                            struct net *net,
                                            struct sockaddr *sa, int salen,
                                            int flags)
{
    pr_err("svc_rdma_kfi_create: not yet implemented\n");
    return ERR_PTR(-ENOSYS);
}

static void svc_rdma_kfi_close(struct svc_xprt *xprt)
{
    pr_debug("svc_rdma_kfi_close: called\n");
}

static int svc_rdma_kfi_recvfrom(struct svc_rqst *rqstp)
{
    pr_err("svc_rdma_kfi_recvfrom: not yet implemented\n");
    return -ENOSYS;
}

static int svc_rdma_kfi_sendto(struct svc_rqst *rqstp)
{
    pr_err("svc_rdma_kfi_sendto: not yet implemented\n");
    return -ENOSYS;
}

static void svc_rdma_kfi_detach(struct svc_xprt *xprt)
{
    pr_debug("svc_rdma_kfi_detach: called\n");
}

/* Module initialization */
static int __init svc_rdma_kfi_init(void)
{
    int rc;

    pr_info("NFS/RDMA server kfabric transport module loading\n");

    /* Register the transport class */
    rc = svc_reg_xprt_class(&svc_rdma_kfi_class);
    if (rc) {
        pr_err("svc_reg_xprt_class failed: %d\n", rc);
        return rc;
    }

    pr_info("NFS/RDMA server kfabric transport registered\n");
    return 0;
}

static void __exit svc_rdma_kfi_exit(void)
{
    svc_unreg_xprt_class(&svc_rdma_kfi_class);
    pr_info("NFS/RDMA server kfabric transport unloaded\n");
}

module_init(svc_rdma_kfi_init);
module_exit(svc_rdma_kfi_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pim Witlox");
MODULE_DESCRIPTION("NFS/RDMA server transport using kfabric for HPE Slingshot");
MODULE_ALIAS("svcrdma_kfi");
