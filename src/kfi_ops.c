#include "kfi_verbs_compat.h"
#include <linux/slab.h>

/*
 * Translate ib_send_wr to kfabric operations
 * This is complex because verbs uses chained work requests
 * while kfabric uses individual operations
 */
int kfi_post_send(struct ib_qp *qp,
                  const struct ib_send_wr *wr,
                  const struct ib_send_wr **bad_wr)
{
    struct kfi_qp *kqp = container_of(qp, struct kfi_qp, qp);
    const struct ib_send_wr *cur_wr;
    int ret = 0;
    unsigned long flags;
    
    if (!kqp || !kqp->ep) {
        if (bad_wr)
            *bad_wr = wr;
        return -EINVAL;
    }
    
    spin_lock_irqsave(&kqp->sq_lock, flags);
    
    /* Process each work request in the chain */
    for (cur_wr = wr; cur_wr; cur_wr = cur_wr->next) {
        switch (cur_wr->opcode) {
        case IB_WR_SEND:
            ret = kfi_do_send(kqp, cur_wr);
            break;
            
        case IB_WR_RDMA_WRITE:
        case IB_WR_RDMA_WRITE_WITH_IMM:
            ret = kfi_do_rdma_write(kqp, cur_wr);
            break;
            
        case IB_WR_RDMA_READ:
            ret = kfi_do_rdma_read(kqp, cur_wr);
            break;
            
        case IB_WR_SEND_WITH_INV:
            /* CXI doesn't have invalidate semantics like IB
             * Need to handle this differently */
            ret = kfi_do_send_with_inv(kqp, cur_wr);
            break;
            
        default:
            pr_warn("kfi_post_send: unsupported opcode %d\n",
                    cur_wr->opcode);
            ret = -EOPNOTSUPP;
        }
        
        if (ret) {
            if (bad_wr)
                *bad_wr = cur_wr;
            goto out_unlock;
        }
    }
    
out_unlock:
    spin_unlock_irqrestore(&kqp->sq_lock, flags);
    return ret;
}

static int kfi_do_rdma_write(struct kfi_qp *kqp,
                              const struct ib_send_wr *wr)
{
    struct ib_rdma_wr *rdma_wr = container_of(wr, struct ib_rdma_wr, wr);
    struct kfi_mr *kmr;
    void *desc = NULL;
    ssize_t ret;
    int i;
    
    /* For scatter-gather, we need to handle multiple segments
     * kfabric typically expects contiguous buffers, so we may need
     * to either:
     * 1. Issue multiple kfi_write calls
     * 2. Use kfi_writev for vectored I/O
     * 3. Copy to temp buffer (AVOID - performance killer)
     */
    
    if (wr->num_sge > 1) {
        /* Use vectored write */
        struct iovec iov[wr->num_sge];
        void *descs[wr->num_sge];
        
        for (i = 0; i < wr->num_sge; i++) {
            iov[i].iov_base = (void *)(uintptr_t)wr->sg_list[i].addr;
            iov[i].iov_len = wr->sg_list[i].length;
            
            kmr = (struct kfi_mr *)wr->sg_list[i].lkey;
            descs[i] = kfi_mr_desc(kmr->kfi_mr);
        }
        
        ret = kfi_writev(kqp->ep, iov, descs, wr->num_sge,
                         0, /* kfi_addr - need to resolve this */
                         rdma_wr->remote_addr,
                         rdma_wr->rkey,
                         (void *)wr->wr_id);
    } else {
        /* Single segment - fast path */
        void *buf = (void *)(uintptr_t)wr->sg_list[0].addr;
        size_t len = wr->sg_list[0].length;
        
        kmr = (struct kfi_mr *)wr->sg_list[0].lkey;
        desc = kfi_mr_desc(kmr->kfi_mr);
        
        ret = kfi_write(kqp->ep, buf, len, desc,
                        0, /* kfi_addr */
                        rdma_wr->remote_addr,
                        rdma_wr->rkey,
                        (void *)wr->wr_id);
    }
    
    if (ret < 0 && ret != -KFI_EAGAIN) {
        pr_err("kfi_write failed: %zd\n", ret);
        return (int)ret;
    }
    
    /* -KFI_EAGAIN means resource temporarily unavailable
     * In verbs this would block, in kfi we need to handle it */
    if (ret == -KFI_EAGAIN) {
        /* TODO: Implement flow control / retry logic */
        return -EAGAIN;
    }
    
    return 0;
}

/* Memory registration with proper CXI key management */
struct ib_mr *kfi_alloc_mr(struct ib_pd *pd,
                            enum ib_mr_type mr_type,
                            u32 max_num_sg)
{
    struct kfi_pd *kpd = container_of(pd, struct kfi_pd, pd);
    struct kfi_mr *kmr;
    int ret;
    
    kmr = kzalloc(sizeof(*kmr), GFP_KERNEL);
    if (!kmr)
        return ERR_PTR(-ENOMEM);
        
    kmr->pd = kpd;
    atomic_set(&kmr->usecnt, 1);
    
    /* For CXI, we need to handle memory registration differently
     * CXI uses 64-bit keys but NFS expects 32-bit keys
     * We'll need to maintain a mapping table */
    
    switch (mr_type) {
    case IB_MR_TYPE_MEM_REG:
        /* Standard memory registration */
        ret = kfi_mr_reg(kpd->kfi_domain,
                         NULL, 0, /* will be set later via map_mr_sg */
                         KFI_READ | KFI_WRITE | KFI_REMOTE_READ | KFI_REMOTE_WRITE,
                         0, /* offset */
                         0, /* requested_key - let provider choose */
                         0, /* flags */
                         &kmr->kfi_mr,
                         NULL);
        break;
        
    case IB_MR_TYPE_INTEGRITY:
        /* Integrity check MR - may not be supported by CXI */
        ret = -EOPNOTSUPP;
        break;
        
    default:
        ret = -EINVAL;
    }
    
    if (ret) {
        kfree(kmr);
        return ERR_PTR(ret);
    }
    
    /* Generate synthetic 32-bit keys from kfabric's 64-bit keys */
    kmr->lkey = (u32)kfi_mr_key(kmr->kfi_mr);
    kmr->rkey = kmr->lkey; /* For simplicity - may need separate mapping */
    
    return &kmr->mr;
}

/*
 * Batch work requests for efficiency
 */

#define MAX_BATCH_SIZE 16

struct kfi_batch_ctx {
    struct iovec iovs[MAX_BATCH_SIZE];
    void *descs[MAX_BATCH_SIZE];
    void *contexts[MAX_BATCH_SIZE];
    int count;
};

/**
 * kfi_batch_send - Batch multiple sends
 */
static int kfi_batch_send(struct kfi_qp *kqp, struct kfi_batch_ctx *batch)
{
    int i;
    ssize_t ret;

    if (batch->count == 0)
        return 0;

    /* Use FI_MORE for all but last */
    for (i = 0; i < batch->count - 1; i++) {
        ret = kfi_sendv(kqp->ep, &batch->iovs[i], &batch->descs[i], 1,
                        0, batch->contexts[i]);
        if (ret < 0 && ret != -KFI_EAGAIN) {
            pr_err("kfi_sendv[%d] failed: %zd\n", i, ret);
            return (int)ret;
        }
        /* Mark with FI_MORE to batch */
        kfi_tx_attr_set(kqp->ep, KFI_MORE);
    }

    /* Last one without FI_MORE - flushes batch */
    kfi_tx_attr_set(kqp->ep, 0);
    ret = kfi_sendv(kqp->ep, &batch->iovs[i], &batch->descs[i], 1,
                    0, batch->contexts[i]);

    if (ret < 0 && ret != -KFI_EAGAIN) {
        pr_err("kfi_sendv[%d] failed: %zd\n", i, ret);
        return (int)ret;
    }

    pr_debug("kfi: Batched %d sends\n", batch->count);
    return 0;
}
