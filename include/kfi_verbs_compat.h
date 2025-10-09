#ifndef _LINUX_XPRTRDMA_KFI_COMPAT_H
#define _LINUX_XPRTRDMA_KFI_COMPAT_H

#include <rdma/kfi/fabric.h>
#include <rdma/kfi/endpoint.h>
#include <rdma/kfi/domain.h>
#include <rdma/kfi/cq.h>

/*
 * Compatibility structures that mimic ib_* structures
 * but are backed by kfabric
 */

struct kfi_device {
    struct kfid_fabric *fabric;
    struct kfid_domain *domain;
    struct kfi_info *info;
    char name[64];
};

struct kfi_pd {
    struct kfi_device *device;
    struct kfid_domain *kfi_domain;
    atomic_t usecnt;
};

struct kfi_qp {
    struct kfi_pd *pd;
    struct kfid_ep *ep;          // kfabric endpoint
    struct kfid_cq *send_cq;
    struct kfid_cq *recv_cq;
    void (*event_handler)(struct ib_event *, void *);
    void *qp_context;
    u32 qp_num;                  // Synthetic QP number
    enum ib_qp_state state;
    
    // CXI-specific
    struct kfi_cxi_auth_key *auth_key;
    spinlock_t sq_lock;
    spinlock_t rq_lock;
};

struct kfi_mr {
    struct kfi_pd *pd;
    struct kfid_mr *kfi_mr;
    u64 iova;                    // IO virtual address
    u64 length;
    u32 lkey;                    // Local key (synthesized)
    u32 rkey;                    // Remote key (synthesized from kfi 64-bit)
    atomic_t usecnt;
};

struct kfi_cq {
    struct kfi_device *device;
    struct kfid_cq *kfi_cq;
    void (*comp_handler)(struct ib_cq *, void *);
    void *cq_context;
    atomic_t usecnt;
    int cqe;                     // Number of CQ entries
    
    // CQ event processing
    struct workqueue_struct *comp_wq;
    struct work_struct comp_work;
};

/*
 * Work completion translation
 * Map kfi_cq_data_entry to ib_wc
 */
struct kfi_wc_xlate {
    u64 wr_id;
    enum ib_wc_status status;
    enum ib_wc_opcode opcode;
    u32 vendor_err;
    u32 byte_len;
};

#endif /* _LINUX_XPRTRDMA_KFI_COMPAT_H */
