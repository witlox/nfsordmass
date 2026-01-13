#include "kfi_verbs_compat.h"
#include "kfi_internal.h"
#include <linux/sunrpc/svc_rdma.h>
#include <linux/slab.h>

/*
 * Server-side helper functions for kfabric operations
 * These handle incoming client requests and outgoing responses
 */

/**
 * svc_kfi_post_recv - Post receive work request for incoming data
 * @kqp: kfabric queue pair
 * @buf: Buffer to receive into
 * @len: Buffer length
 * @context: Context pointer for completion
 *
 * Returns: 0 on success, negative error on failure
 */
static int __maybe_unused svc_kfi_post_recv(struct kfi_qp *kqp, void *buf, size_t len, void *context)
{
    struct kfi_mr *kmr;
    void *desc;
    ssize_t ret;

    if (!kqp || !kqp->ep || !buf) {
        pr_err("svc_kfi_post_recv: invalid parameters\n");
        return -EINVAL;
    }

    /* Get memory region descriptor for the buffer */
    kmr = (struct kfi_mr *)context; /* Assume context includes MR */
    desc = kfi_mr_desc(kmr->kfi_mr);

    ret = kfi_recv(kqp->ep, buf, len, desc, 0, context);
    if (ret < 0 && ret != -KFI_EAGAIN) {
        pr_err("svc_kfi_post_recv: kfi_recv failed: %zd\n", ret);
        return (int)ret;
    }

    return (ret == -KFI_EAGAIN) ? -EAGAIN : 0;
}

/**
 * svc_kfi_post_send - Send response data to client
 * @kqp: kfabric queue pair
 * @buf: Buffer to send
 * @len: Buffer length
 * @context: Context pointer for completion
 *
 * Returns: 0 on success, negative error on failure
 */
static int __maybe_unused svc_kfi_post_send(struct kfi_qp *kqp, void *buf, size_t len, void *context)
{
    struct kfi_mr *kmr;
    void *desc;
    ssize_t ret;

    if (!kqp || !kqp->ep || !buf) {
        pr_err("svc_kfi_post_send: invalid parameters\n");
        return -EINVAL;
    }

    /* Get memory region descriptor for the buffer */
    kmr = (struct kfi_mr *)context; /* Assume context includes MR */
    desc = kfi_mr_desc(kmr->kfi_mr);

    ret = kfi_send(kqp->ep, buf, len, desc, 0, context);
    if (ret < 0 && ret != -KFI_EAGAIN) {
        pr_err("svc_kfi_post_send: kfi_send failed: %zd\n", ret);
        return (int)ret;
    }

    return (ret == -KFI_EAGAIN) ? -EAGAIN : 0;
}

/**
 * svc_kfi_rdma_read - Read data from client memory
 * @kqp: kfabric queue pair
 * @local_buf: Local buffer to read into
 * @len: Length to read
 * @remote_addr: Remote address to read from
 * @rkey: Remote key
 * @context: Context pointer for completion
 *
 * Returns: 0 on success, negative error on failure
 */
static int __maybe_unused svc_kfi_rdma_read(struct kfi_qp *kqp, void *local_buf, size_t len,
                                            u64 remote_addr, u32 rkey, void *context)
{
    struct kfi_mr *kmr;
    void *desc;
    ssize_t ret;

    if (!kqp || !kqp->ep || !local_buf) {
        pr_err("svc_kfi_rdma_read: invalid parameters\n");
        return -EINVAL;
    }

    /* Get memory region descriptor for local buffer */
    kmr = (struct kfi_mr *)context; /* Assume context includes MR */
    desc = kfi_mr_desc(kmr->kfi_mr);

    ret = kfi_read(kqp->ep, local_buf, len, desc, 0,
                   remote_addr, rkey, context);
    if (ret < 0 && ret != -KFI_EAGAIN) {
        pr_err("svc_kfi_rdma_read: kfi_read failed: %zd\n", ret);
        return (int)ret;
    }

    return (ret == -KFI_EAGAIN) ? -EAGAIN : 0;
}

/**
 * svc_kfi_rdma_write - Write data to client memory
 * @kqp: kfabric queue pair
 * @local_buf: Local buffer to write from
 * @len: Length to write
 * @remote_addr: Remote address to write to
 * @rkey: Remote key
 * @context: Context pointer for completion
 *
 * Returns: 0 on success, negative error on failure
 */
static int __maybe_unused svc_kfi_rdma_write(struct kfi_qp *kqp, void *local_buf, size_t len,
                                             u64 remote_addr, u32 rkey, void *context)
{
    struct kfi_mr *kmr;
    void *desc;
    ssize_t ret;

    if (!kqp || !kqp->ep || !local_buf) {
        pr_err("svc_kfi_rdma_write: invalid parameters\n");
        return -EINVAL;
    }

    /* Get memory region descriptor for local buffer */
    kmr = (struct kfi_mr *)context; /* Assume context includes MR */
    desc = kfi_mr_desc(kmr->kfi_mr);

    ret = kfi_write(kqp->ep, local_buf, len, desc, 0,
                    remote_addr, rkey, context);
    if (ret < 0 && ret != -KFI_EAGAIN) {
        pr_err("svc_kfi_rdma_write: kfi_write failed: %zd\n", ret);
        return (int)ret;
    }

    return (ret == -KFI_EAGAIN) ? -EAGAIN : 0;
}

/**
 * svc_kfi_poll_cq - Poll completion queue for completed operations
 * @kqp: kfabric queue pair
 * @wc: Array of work completions to fill
 * @num_entries: Maximum number of completions to poll
 *
 * Returns: Number of completions retrieved, or negative error
 */
static int __maybe_unused svc_kfi_poll_cq(struct kfi_qp *kqp, struct ib_wc *wc, int num_entries)
{
    struct kfi_cq_data_entry cq_entry[KFI_MAX_POLL_ENTRIES];
    struct kfi_cq *kcq;
    int poll_count, i;
    ssize_t ret;

    if (!kqp || !kqp->send_cq || !wc || num_entries <= 0) {
        return -EINVAL;
    }

    /* Get the kfi_cq from the IB CQ */
    kcq = container_of(kqp->send_cq, struct kfi_cq, cq);

    /* Limit to max poll entries */
    poll_count = num_entries > KFI_MAX_POLL_ENTRIES ?
                 KFI_MAX_POLL_ENTRIES : num_entries;

    ret = kfi_cq_read(kcq->kfi_cq, cq_entry, poll_count);
    if (ret < 0) {
        if (ret == -KFI_EAGAIN)
            return 0; /* No completions available */
        pr_err("svc_kfi_poll_cq: kfi_cq_read failed: %zd\n", ret);
        return (int)ret;
    }

    /* Convert kfabric completions to IB work completions */
    for (i = 0; i < ret; i++) {
        wc[i].wr_id = (u64)(uintptr_t)cq_entry[i].op_context;
        wc[i].status = IB_WC_SUCCESS;
        wc[i].byte_len = cq_entry[i].len;
        wc[i].wc_flags = 0;

        /* Determine operation type from flags */
        if (cq_entry[i].flags & KFI_SEND)
            wc[i].opcode = IB_WC_SEND;
        else if (cq_entry[i].flags & KFI_RECV)
            wc[i].opcode = IB_WC_RECV;
        else if (cq_entry[i].flags & KFI_READ)
            wc[i].opcode = IB_WC_RDMA_READ;
        else if (cq_entry[i].flags & KFI_WRITE)
            wc[i].opcode = IB_WC_RDMA_WRITE;
    }

    return (int)ret;
}

/**
 * svc_kfi_accept_connection - Accept incoming connection from client
 * @kqp: kfabric queue pair (endpoint)
 * @conn_param: Connection parameters
 *
 * Returns: 0 on success, negative error on failure
 *
 * Note: kfabric uses connectionless communication with address vectors,
 * so accept/reject semantics don't apply directly. This is a stub for
 * future connection management.
 */
static int __maybe_unused svc_kfi_accept_connection(struct kfi_qp *kqp, void *conn_param)
{
    if (!kqp || !kqp->ep) {
        pr_err("svc_kfi_accept_connection: invalid endpoint\n");
        return -EINVAL;
    }

    pr_debug("svc_kfi_accept_connection: connection accepted (no-op in kfabric)\n");
    return 0;
}

/**
 * svc_kfi_reject_connection - Reject incoming connection from client
 * @kqp: kfabric queue pair (endpoint)
 * @reason: Rejection reason
 *
 * Returns: 0 on success, negative error on failure
 *
 * Note: kfabric uses connectionless communication with address vectors,
 * so accept/reject semantics don't apply directly. This is a stub for
 * future connection management.
 */
static int __maybe_unused svc_kfi_reject_connection(struct kfi_qp *kqp, int reason)
{
    if (!kqp || !kqp->ep) {
        pr_err("svc_kfi_reject_connection: invalid endpoint\n");
        return -EINVAL;
    }

    pr_debug("svc_kfi_reject_connection: connection rejected (reason: %d, no-op in kfabric)\n", reason);
    return 0;
}
