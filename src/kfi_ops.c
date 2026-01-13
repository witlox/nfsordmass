#include "kfi_verbs_compat.h"
#include "kfi_internal.h"
#include <linux/slab.h>
#include <linux/uio.h>
#include <rdma/kfi/mr.h>

/*
 * Helper functions for individual operations
 */

int kfi_do_send(struct kfi_qp *kqp, const struct ib_send_wr *wr)
{
    struct kfi_mr *kmr;
    void *desc = NULL;
    ssize_t ret;
    int i;

    if (wr->num_sge > 1) {
        /* Vectored send */
        struct kvec iov[KFI_MAX_SGE];
        void *descs[KFI_MAX_SGE];

        if (wr->num_sge > KFI_MAX_SGE) {
            pr_err("kfi_do_send: num_sge %d exceeds max %d\n",
                   wr->num_sge, KFI_MAX_SGE);
            return -EINVAL;
        }

        for (i = 0; i < wr->num_sge; i++) {
            iov[i].iov_base = (void *)(uintptr_t)wr->sg_list[i].addr;
            iov[i].iov_len = wr->sg_list[i].length;

            kmr = (struct kfi_mr *)(uintptr_t)wr->sg_list[i].lkey;
            descs[i] = kfi_mr_desc(kmr->kfi_mr);
        }

        ret = kfi_sendv(kqp->ep, iov, descs, wr->num_sge,
                        0, /* kfi_addr */
                        (void *)wr->wr_id);
    } else {
        /* Single segment */
        void *buf = (void *)(uintptr_t)wr->sg_list[0].addr;
        size_t len = wr->sg_list[0].length;

        kmr = (struct kfi_mr *)(uintptr_t)wr->sg_list[0].lkey;
        desc = kfi_mr_desc(kmr->kfi_mr);

        ret = kfi_send(kqp->ep, buf, len, desc,
                       0, /* kfi_addr */
                       (void *)wr->wr_id);
    }

    if (ret < 0 && ret != -KFI_EAGAIN) {
        pr_err("kfi_send failed: %zd\n", ret);
        return (int)ret;
    }

    return (ret == -KFI_EAGAIN) ? -EAGAIN : 0;
}

int kfi_do_rdma_read(struct kfi_qp *kqp, const struct ib_send_wr *wr)
{
    struct ib_rdma_wr *rdma_wr = container_of(wr, struct ib_rdma_wr, wr);
    struct kfi_mr *kmr;
    void *desc = NULL;
    ssize_t ret;
    int i;

    if (wr->num_sge > 1) {
        /* Vectored read */
        struct kvec iov[KFI_MAX_SGE];
        void *descs[KFI_MAX_SGE];

        if (wr->num_sge > KFI_MAX_SGE) {
            pr_err("kfi_do_rdma_read: num_sge %d exceeds max %d\n",
                   wr->num_sge, KFI_MAX_SGE);
            return -EINVAL;
        }

        for (i = 0; i < wr->num_sge; i++) {
            iov[i].iov_base = (void *)(uintptr_t)wr->sg_list[i].addr;
            iov[i].iov_len = wr->sg_list[i].length;

            kmr = (struct kfi_mr *)(uintptr_t)wr->sg_list[i].lkey;
            descs[i] = kfi_mr_desc(kmr->kfi_mr);
        }

        ret = kfi_readv(kqp->ep, iov, descs, wr->num_sge,
                        0, /* kfi_addr */
                        rdma_wr->remote_addr,
                        rdma_wr->rkey,
                        (void *)wr->wr_id);
    } else {
        /* Single segment */
        void *buf = (void *)(uintptr_t)wr->sg_list[0].addr;
        size_t len = wr->sg_list[0].length;

        kmr = (struct kfi_mr *)(uintptr_t)wr->sg_list[0].lkey;
        desc = kfi_mr_desc(kmr->kfi_mr);

        ret = kfi_read(kqp->ep, buf, len, desc,
                       0, /* kfi_addr */
                       rdma_wr->remote_addr,
                       rdma_wr->rkey,
                       (void *)wr->wr_id);
    }

    if (ret < 0 && ret != -KFI_EAGAIN) {
        pr_err("kfi_read failed: %zd\n", ret);
        return (int)ret;
    }

    return (ret == -KFI_EAGAIN) ? -EAGAIN : 0;
}

int kfi_do_send_with_inv(struct kfi_qp *kqp, const struct ib_send_wr *wr)
{
    /* CXI doesn't have invalidate semantics like InfiniBand
     * For now, just do a regular send and log the invalidate request
     * TODO: Implement proper invalidation handling if needed
     */
    pr_debug("kfi_do_send_with_inv: invalidation not supported, doing regular send\n");
    return kfi_do_send(kqp, wr);
}

int kfi_do_recv(struct kfi_qp *kqp, const struct ib_recv_wr *wr)
{
    struct kfi_mr *kmr;
    void *desc = NULL;
    ssize_t ret;
    int i;

    if (wr->num_sge > 1) {
        /* Vectored receive */
        struct kvec iov[KFI_MAX_SGE];
        void *descs[KFI_MAX_SGE];

        if (wr->num_sge > KFI_MAX_SGE) {
            pr_err("kfi_do_recv: num_sge %d exceeds max %d\n",
                   wr->num_sge, KFI_MAX_SGE);
            return -EINVAL;
        }

        for (i = 0; i < wr->num_sge; i++) {
            iov[i].iov_base = (void *)(uintptr_t)wr->sg_list[i].addr;
            iov[i].iov_len = wr->sg_list[i].length;

            kmr = (struct kfi_mr *)(uintptr_t)wr->sg_list[i].lkey;
            descs[i] = kfi_mr_desc(kmr->kfi_mr);
        }

        ret = kfi_recvv(kqp->ep, iov, descs, wr->num_sge,
                        0, /* kfi_addr */
                        (void *)wr->wr_id);
    } else {
        /* Single segment */
        void *buf = (void *)(uintptr_t)wr->sg_list[0].addr;
        size_t len = wr->sg_list[0].length;

        kmr = (struct kfi_mr *)(uintptr_t)wr->sg_list[0].lkey;
        desc = kfi_mr_desc(kmr->kfi_mr);

        ret = kfi_recv(kqp->ep, buf, len, desc,
                       0, /* kfi_addr */
                       (void *)wr->wr_id);
    }

    if (ret < 0 && ret != -KFI_EAGAIN) {
        pr_err("kfi_recv failed: %zd\n", ret);
        return (int)ret;
    }

    return (ret == -KFI_EAGAIN) ? -EAGAIN : 0;
}

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

int kfi_do_rdma_write(struct kfi_qp *kqp,
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
        struct kvec iov[KFI_MAX_SGE];
        void *descs[KFI_MAX_SGE];

        if (wr->num_sge > KFI_MAX_SGE) {
            pr_err("kfi_do_rdma_write: num_sge %d exceeds max %d\n",
                   wr->num_sge, KFI_MAX_SGE);
            return -EINVAL;
        }

        for (i = 0; i < wr->num_sge; i++) {
            iov[i].iov_base = (void *)(uintptr_t)wr->sg_list[i].addr;
            iov[i].iov_len = wr->sg_list[i].length;
            
            kmr = (struct kfi_mr *)(uintptr_t)wr->sg_list[i].lkey;
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
        
        kmr = (struct kfi_mr *)(uintptr_t)wr->sg_list[0].lkey;
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

/* Note: kfi_alloc_mr is implemented in kfi_memory.c */

/*
 * Batch work requests for efficiency
 */

/**
 * kfi_batch_send - Batch multiple sends
 */
int kfi_batch_send(struct kfi_qp *kqp, struct kfi_batch_ctx *batch)
{
    int i;
    ssize_t ret;

    if (batch->count == 0)
        return 0;

    /* Send all operations in batch
     * Note: KFI_MORE flag would indicate batching but is not directly
     * supported in this kfabric API - batching happens implicitly
     */
    for (i = 0; i < batch->count; i++) {
        ret = kfi_sendv(kqp->ep, &batch->iovs[i], &batch->descs[i], 1,
                        0, batch->contexts[i]);
        if (ret < 0 && ret != -KFI_EAGAIN) {
            pr_err("kfi_sendv[%d] failed: %zd\n", i, ret);
            return (int)ret;
        }
    }

    ret = 0; /* Success if we got here */

    if (ret < 0 && ret != -KFI_EAGAIN) {
        pr_err("kfi_sendv[%d] failed: %zd\n", i, ret);
        return (int)ret;
    }

    pr_debug("kfi: Batched %d sends\n", batch->count);
    return 0;
}
