/*
 * kfi_verbs_compat.c - Core translation between RDMA verbs and kfabric
 *
 * This file implements the verbs API surface that xprtrdma/svcrdma expect,
 * but backed by kfabric calls to CXI provider.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/idr.h>
#include <rdma/ib_verbs.h>
#include <rdma/kfi/fabric.h>
#include <rdma/kfi/domain.h>
#include <rdma/kfi/endpoint.h>

#include "../include/kfi_verbs_compat.h"
#include "../include/kfi_internal.h"

/* Global state */
static LIST_HEAD(kfi_device_list);
static DEFINE_MUTEX(kfi_device_mutex);
static struct idr qp_idr;
static DEFINE_SPINLOCK(qp_idr_lock);

/*
 * ============================================================================
 * DEVICE ENUMERATION
 * ============================================================================
 */

/**
 * kfi_get_devices - Enumerate available kfabric devices
 * @num_devices: Returns number of devices found
 *
 * This replaces ib_get_client_data() for device discovery.
 * In kfabric, we query for available CXI providers.
 */
struct ib_device **kfi_get_devices(int *num_devices)
{
    struct kfi_info *hints, *info, *cur;
    struct ib_device **devices = NULL;
    struct kfi_device *kdev;
    int count = 0, i = 0;
    int ret;

    /* Set up hints for CXI provider */
    hints = kfi_allocinfo();
    if (!hints)
        return NULL;

    hints->fabric_attr->prov_name = kstrdup("cxi", GFP_KERNEL);
    hints->caps = KFI_MSG | KFI_RMA | KFI_TAGGED;
    hints->mode = KFI_CONTEXT;
    hints->ep_attr->type = KFI_EP_RDM; /* Reliable datagram */

    /* Query available fabrics */
    ret = kfi_getinfo(KFI_VERSION(1, 0), NULL, NULL, 0, hints, &info);
    kfi_freeinfo(hints);
    
    if (ret) {
        pr_err("kfi_getinfo failed: %d\n", ret);
        return NULL;
    }

    /* Count devices */
    for (cur = info; cur; cur = cur->next)
        count++;

    if (count == 0) {
        kfi_freeinfo(info);
        return NULL;
    }

    /* Allocate device array */
    devices = kcalloc(count, sizeof(*devices), GFP_KERNEL);
    if (!devices) {
        kfi_freeinfo(info);
        return NULL;
    }

    /* Create device structures */
    mutex_lock(&kfi_device_mutex);
    for (cur = info; cur; cur = cur->next) {
        kdev = kzalloc(sizeof(*kdev), GFP_KERNEL);
        if (!kdev)
            continue;

        kdev->info = kfi_dupinfo(cur);
        strncpy(kdev->name, cur->fabric_attr->name, sizeof(kdev->name) - 1);
        
        /* Open fabric and domain */
        ret = kfi_fabric(cur->fabric_attr, &kdev->fabric, NULL);
        if (ret) {
            pr_err("kfi_fabric failed for %s: %d\n", kdev->name, ret);
            kfi_freeinfo(kdev->info);
            kfree(kdev);
            continue;
        }

        ret = kfi_domain(kdev->fabric, cur, &kdev->domain, NULL);
        if (ret) {
            pr_err("kfi_domain failed for %s: %d\n", kdev->name, ret);
            kfi_close(&kdev->fabric->fid);
            kfi_freeinfo(kdev->info);
            kfree(kdev);
            continue;
        }

        list_add_tail(&kdev->list, &kfi_device_list);
        devices[i++] = &kdev->ibdev;
    }
    mutex_unlock(&kfi_device_mutex);

    kfi_freeinfo(info);
    *num_devices = i;
    
    pr_info("kfi: Found %d CXI device(s)\n", i);
    return devices;
}
EXPORT_SYMBOL(kfi_get_devices);

/**
 * kfi_free_devices - Free device list
 */
void kfi_free_devices(struct ib_device **devices)
{
    kfree(devices);
}
EXPORT_SYMBOL(kfi_free_devices);

/*
 * ============================================================================
 * PROTECTION DOMAIN OPERATIONS
 * ============================================================================
 */

/**
 * kfi_alloc_pd - Allocate protection domain
 * @device: Device to allocate PD on
 * @context: User context (unused)
 * @udata: User data (unused)
 *
 * In kfabric, the domain serves as the PD equivalent.
 */
struct ib_pd *kfi_alloc_pd(struct ib_device *device,
                            struct ib_ucontext *context,
                            struct ib_udata *udata)
{
    struct kfi_device *kdev = container_of(device, struct kfi_device, ibdev);
    struct kfi_pd *kpd;

    kpd = kzalloc(sizeof(*kpd), GFP_KERNEL);
    if (!kpd)
        return ERR_PTR(-ENOMEM);

    kpd->device = kdev;
    kpd->kfi_domain = kdev->domain;
    atomic_set(&kpd->usecnt, 0);

    pr_debug("kfi: Allocated PD\n");
    return &kpd->pd;
}
EXPORT_SYMBOL(kfi_alloc_pd);

/**
 * kfi_dealloc_pd - Free protection domain
 */
int kfi_dealloc_pd(struct ib_pd *pd)
{
    struct kfi_pd *kpd = container_of(pd, struct kfi_pd, pd);

    if (atomic_read(&kpd->usecnt)) {
        pr_err("kfi: Cannot dealloc PD with active resources\n");
        return -EBUSY;
    }

    kfree(kpd);
    pr_debug("kfi: Deallocated PD\n");
    return 0;
}
EXPORT_SYMBOL(kfi_dealloc_pd);

/*
 * ============================================================================
 * COMPLETION QUEUE OPERATIONS
 * ============================================================================
 */

/**
 * kfi_create_cq - Create completion queue
 * @device: Device to create CQ on
 * @cq_attr: CQ attributes
 * @context: User context
 * @udata: User data
 */
struct ib_cq *kfi_create_cq(struct ib_device *device,
                             const struct ib_cq_init_attr *cq_attr,
                             struct ib_ucontext *context,
                             struct ib_udata *udata)
{
    struct kfi_device *kdev = container_of(device, struct kfi_device, ibdev);
    struct kfi_cq *kcq;
    struct kfi_cq_attr attr = {
        .size = cq_attr->cqe,
        .format = KFI_CQ_FORMAT_DATA,
        .wait_obj = KFI_WAIT_NONE,
    };
    int ret;

    kcq = kzalloc(sizeof(*kcq), GFP_KERNEL);
    if (!kcq)
        return ERR_PTR(-ENOMEM);

    kcq->device = kdev;
    kcq->cqe = cq_attr->cqe;
    kcq->comp_handler = NULL; /* Set later by ib_req_notify_cq */
    atomic_set(&kcq->usecnt, 0);

    /* Create kfabric CQ */
    ret = kfi_cq_open(kdev->domain, &attr, &kcq->kfi_cq, NULL);
    if (ret) {
        pr_err("kfi_cq_open failed: %d\n", ret);
        kfree(kcq);
        return ERR_PTR(ret);
    }

    /* Initialize workqueue for async completions if needed */
    kcq->comp_wq = alloc_workqueue("kfi_comp_%p", WQ_HIGHPRI, 0, kcq);
    if (!kcq->comp_wq) {
        kfi_close(&kcq->kfi_cq->fid);
        kfree(kcq);
        return ERR_PTR(-ENOMEM);
    }
    INIT_WORK(&kcq->comp_work, kfi_cq_comp_worker);

    pr_debug("kfi: Created CQ with %d entries\n", cq_attr->cqe);
    return &kcq->cq;
}
EXPORT_SYMBOL(kfi_create_cq);

/**
 * kfi_destroy_cq - Destroy completion queue
 */
int kfi_destroy_cq(struct ib_cq *cq)
{
    struct kfi_cq *kcq = container_of(cq, struct kfi_cq, cq);

    if (atomic_read(&kcq->usecnt)) {
        pr_err("kfi: Cannot destroy CQ with active QPs\n");
        return -EBUSY;
    }

    destroy_workqueue(kcq->comp_wq);
    kfi_close(&kcq->kfi_cq->fid);
    kfree(kcq);
    
    pr_debug("kfi: Destroyed CQ\n");
    return 0;
}
EXPORT_SYMBOL(kfi_destroy_cq);

/*
 * ============================================================================
 * QUEUE PAIR OPERATIONS
 * ============================================================================
 */

/**
 * kfi_create_qp - Create queue pair
 * @pd: Protection domain
 * @init_attr: QP initialization attributes
 */
struct ib_qp *kfi_create_qp(struct ib_pd *pd,
                             struct ib_qp_init_attr *init_attr)
{
    struct kfi_pd *kpd = container_of(pd, struct kfi_pd, pd);
    struct kfi_qp *kqp;
    struct kfi_info *hints;
    int ret;

    kqp = kzalloc(sizeof(*kqp), GFP_KERNEL);
    if (!kqp)
        return ERR_PTR(-ENOMEM);

    kqp->pd = kpd;
    kqp->send_cq = init_attr->send_cq;
    kqp->recv_cq = init_attr->recv_cq;
    kqp->event_handler = init_attr->event_handler;
    kqp->qp_context = init_attr->qp_context;
    kqp->state = IB_QPS_RESET;
    
    spin_lock_init(&kqp->sq_lock);
    spin_lock_init(&kqp->rq_lock);

    /* Allocate synthetic QP number */
    spin_lock(&qp_idr_lock);
    kqp->qp_num = idr_alloc(&qp_idr, kqp, 1, 0, GFP_ATOMIC);
    spin_unlock(&qp_idr_lock);
    
    if (kqp->qp_num < 0) {
        ret = kqp->qp_num;
        kfree(kqp);
        return ERR_PTR(ret);
    }

    /* Create kfabric endpoint */
    hints = kfi_dupinfo(kpd->device->info);
    hints->tx_attr->size = init_attr->cap.max_send_wr;
    hints->rx_attr->size = init_attr->cap.max_recv_wr;
    hints->ep_attr->tx_ctx_cnt = 1;
    hints->ep_attr->rx_ctx_cnt = 1;

    ret = kfi_endpoint(kpd->kfi_domain, hints, &kqp->ep, NULL);
    kfi_freeinfo(hints);
    
    if (ret) {
        pr_err("kfi_endpoint failed: %d\n", ret);
        spin_lock(&qp_idr_lock);
        idr_remove(&qp_idr, kqp->qp_num);
        spin_unlock(&qp_idr_lock);
        kfree(kqp);
        return ERR_PTR(ret);
    }

    /* Bind CQs to endpoint */
    struct kfi_cq *ksend_cq = container_of(init_attr->send_cq, 
                                            struct kfi_cq, cq);
    struct kfi_cq *krecv_cq = container_of(init_attr->recv_cq,
                                            struct kfi_cq, cq);

    ret = kfi_ep_bind(kqp->ep, &ksend_cq->kfi_cq->fid, KFI_TRANSMIT);
    if (ret) {
        pr_err("kfi_ep_bind(send_cq) failed: %d\n", ret);
        goto err_close_ep;
    }

    ret = kfi_ep_bind(kqp->ep, &krecv_cq->kfi_cq->fid, KFI_RECV);
    if (ret) {
        pr_err("kfi_ep_bind(recv_cq) failed: %d\n", ret);
        goto err_close_ep;
    }

    atomic_inc(&kpd->usecnt);
    atomic_inc(&ksend_cq->usecnt);
    atomic_inc(&krecv_cq->usecnt);

    pr_debug("kfi: Created QP %d\n", kqp->qp_num);
    return &kqp->qp;

err_close_ep:
    kfi_close(&kqp->ep->fid);
    spin_lock(&qp_idr_lock);
    idr_remove(&qp_idr, kqp->qp_num);
    spin_unlock(&qp_idr_lock);
    kfree(kqp);
    return ERR_PTR(ret);
}
EXPORT_SYMBOL(kfi_create_qp);

/**
 * kfi_modify_qp - Modify queue pair state
 * @qp: Queue pair to modify
 * @attr: New attributes
 * @attr_mask: Mask of attributes to modify
 * @udata: User data
 *
 * Critical for connection setup. Maps IB QP state machine to kfabric.
 */
int kfi_modify_qp(struct ib_qp *qp, struct ib_qp_attr *attr,
                   int attr_mask, struct ib_udata *udata)
{
    struct kfi_qp *kqp = container_of(qp, struct kfi_qp, qp);
    int ret = 0;

    pr_debug("kfi: modify_qp %d: state %d -> %d (mask 0x%x)\n",
             kqp->qp_num, kqp->state, attr->qp_state, attr_mask);

    /* Handle state transitions */
    if (attr_mask & IB_QP_STATE) {
        switch (attr->qp_state) {
        case IB_QPS_INIT:
            /* Get VNI authentication - CHALLENGE 5 MITIGATION */
            ret = kfi_get_auth_key(kqp);
            if (ret) {
                pr_err("kfi: Failed to get VNI auth: %d\n", ret);
                return ret;
            }
            kqp->state = IB_QPS_INIT;
            break;

        case IB_QPS_RTR: /* Ready to Receive */
            /* Set up address vector if needed */
            if (attr_mask & IB_QP_AV) {
                ret = kfi_setup_av(kqp, &attr->ah_attr);
                if (ret)
                    return ret;
            }
            kqp->state = IB_QPS_RTR;
            break;

        case IB_QPS_RTS: /* Ready to Send */
            /* Enable endpoint - CRITICAL */
            ret = kfi_enable(kqp->ep);
            if (ret) {
                pr_err("kfi_enable failed: %d\n", ret);
                return ret;
            }
            kqp->state = IB_QPS_RTS;
            pr_info("kfi: QP %d is now active\n", kqp->qp_num);
            break;

        case IB_QPS_ERR:
            kqp->state = IB_QPS_ERR;
            break;

        default:
            pr_warn("kfi: Unsupported QP state %d\n", attr->qp_state);
            return -EINVAL;
        }
    }

    return 0;
}
EXPORT_SYMBOL(kfi_modify_qp);

/**
 * kfi_destroy_qp - Destroy queue pair
 */
int kfi_destroy_qp(struct ib_qp *qp)
{
    struct kfi_qp *kqp = container_of(qp, struct kfi_qp, qp);
    struct kfi_cq *ksend_cq = container_of(kqp->send_cq, struct kfi_cq, cq);
    struct kfi_cq *krecv_cq = container_of(kqp->recv_cq, struct kfi_cq, cq);

    kfi_close(&kqp->ep->fid);
    
    spin_lock(&qp_idr_lock);
    idr_remove(&qp_idr, kqp->qp_num);
    spin_unlock(&qp_idr_lock);

    atomic_dec(&kqp->pd->usecnt);
    atomic_dec(&ksend_cq->usecnt);
    atomic_dec(&krecv_cq->usecnt);

    if (kqp->auth_key)
        kfree(kqp->auth_key);

    kfree(kqp);
    pr_debug("kfi: Destroyed QP\n");
    return 0;
}
EXPORT_SYMBOL(kfi_destroy_qp);

/*
 * ============================================================================
 * MODULE INIT/EXIT
 * ============================================================================
 */

static int __init kfi_verbs_compat_init(void)
{
    idr_init(&qp_idr);
    
    /* Initialize key mapping table - CHALLENGE 3 MITIGATION */
    kfi_key_mapping_init();
    
    pr_info("kfi_verbs_compat: Initialized\n");
    return 0;
}

static void __exit kfi_verbs_compat_exit(void)
{
    struct kfi_device *kdev, *tmp;

    /* Clean up all devices */
    mutex_lock(&kfi_device_mutex);
    list_for_each_entry_safe(kdev, tmp, &kfi_device_list, list) {
        kfi_close(&kdev->domain->fid);
        kfi_close(&kdev->fabric->fid);
        kfi_freeinfo(kdev->info);
        list_del(&kdev->list);
        kfree(kdev);
    }
    mutex_unlock(&kfi_device_mutex);

    kfi_key_mapping_cleanup();
    idr_destroy(&qp_idr);
    
    pr_info("kfi_verbs_compat: Cleaned up\n");
}

module_init(kfi_verbs_compat_init);
module_exit(kfi_verbs_compat_exit);
