#  NFS over RDMA for HPE Slingshot
NFS over RDMA for HPE Slingshot using the kfabric kernel fabric interface. 

## System Architecture Analysis
Current NFSoRDMA Stack, the existing Linux implementation has this layering:

┌─────────────────────────────────────┐
│     NFS Client/Server (VFS layer)   │
└─────────────────┬───────────────────┘
                  │
┌─────────────────▼───────────────────┐
│    SUNRPC (RPC transport layer)     │
└─────────────────┬───────────────────┘
                  │
┌─────────────────▼───────────────────┐
│  xprtrdma/svcrdma (RDMA transport)  │
│  - Memory registration              │
│  - Queue pair management            │
│  - RDMA READ/WRITE operations       │
└─────────────────┬───────────────────┘
                  │
┌─────────────────▼───────────────────┐
│    RDMA Core (ib_core, rdma_cm)     │
│    - Verbs API abstraction          │
└─────────────────┬───────────────────┘
                  │
┌─────────────────▼───────────────────┐
│  Hardware Driver (mlx5, cxgb4, etc) │
└─────────────────────────────────────┘

xprtrdma/svcrdma make direct calls to the RDMA Core verbs API (ib_post_send(), ib_post_recv(), ib_reg_mr(), etc.). 
These APIs don't exist for CXI.

## Target Architecture with kfabric

┌─────────────────────────────────────┐
│     NFS Client/Server (VFS layer)   │
└─────────────────┬───────────────────┘
                  │
┌─────────────────▼───────────────────┐
│    SUNRPC (RPC transport layer)     │
└─────────────────┬───────────────────┘
                  │
┌─────────────────▼───────────────────┐
│  NEW: xprtrdma_kfi / svcrdma_kfi    │ ← What we need to build
│  - Translate verbs semantics to kfi │
│  - Handle CXI-specific auth/VNI     │
└─────────────────┬───────────────────┘
                  │
┌─────────────────▼───────────────────┐
│        kfabric (kernel OFI)         │
└─────────────────┬───────────────────┘
                  │
┌─────────────────▼───────────────────┐
│    CXI Provider (kernel module)     │
└─────────────────┬───────────────────┘
                  │
┌─────────────────▼───────────────────┐
│    CXI Hardware Driver (cxi.ko)     │
└─────────────────────────────────────┘

NOTE: The [kfabric repository](https://github.com/ofiwg/kfabric) is marked as "experimental" with limited activity. 

### RDMA Verbs Operations Used by xprtrdma/svcrdma
```c
// Memory registration (from net/sunrpc/xprtrdma/)
struct ib_mr *ib_alloc_mr(struct ib_pd *pd, 
                          enum ib_mr_type mr_type,
                          u32 max_num_sg);
int ib_map_mr_sg(struct ib_mr *mr, 
                 struct scatterlist *sg,
                 int sg_nents, 
                 unsigned int *sg_offset,
                 unsigned int page_size);

// Queue pair operations
int ib_post_send(struct ib_qp *qp,
                 const struct ib_send_wr *send_wr,
                 const struct ib_send_wr **bad_send_wr);
int ib_post_recv(struct ib_qp *qp,
                 const struct ib_recv_wr *recv_wr,
                 const struct ib_recv_wr **bad_recv_wr);

// Completion queue polling
int ib_poll_cq(struct ib_cq *cq, int num_entries,
               struct ib_wc *wc);

// RDMA operations
struct ib_send_wr {
    u64 wr_id;
    struct ib_send_wr *next;
    struct ib_sge *sg_list;
    int num_sge;
    enum ib_wr_opcode opcode; // IB_WR_RDMA_READ, IB_WR_RDMA_WRITE, etc.
    ...
};
```

### kfabric Equivalents
```C
// Memory registration
int kfi_mr_reg(struct kfid_domain *domain,
               const void *buf, size_t len,
               uint64_t access, uint64_t offset,
               uint64_t requested_key,
               uint64_t flags,
               struct kfid_mr **mr, void *context);

// Endpoint operations  
ssize_t kfi_send(struct kfid_ep *ep, const void *buf,
                 size_t len, void *desc,
                 kfi_addr_t dest_addr, void *context);
                 
ssize_t kfi_recv(struct kfid_ep *ep, void *buf,
                 size_t len, void *desc,
                 kfi_addr_t src_addr, void *context);

// RDMA operations
ssize_t kfi_read(struct kfid_ep *ep, void *buf, size_t len,
                 void *desc, kfi_addr_t src_addr,
                 uint64_t addr, uint64_t key, void *context);
                 
ssize_t kfi_write(struct kfid_ep *ep, const void *buf, size_t len,
                  void *desc, kfi_addr_t dest_addr,
                  uint64_t addr, uint64_t key, void *context);

// Completion queue
ssize_t kfi_cq_read(struct kfid_cq *cq, void *buf, size_t count);
```

| Aspect | RDMA Verbs | kfabric | Impact |
| Connection model | Queue Pair (QP) centric | Endpoint centric | Moderate - abstraction shift |
| Work requests | Chained linked list | Single operations | Significant - affects batching |
| Memory keys | 32-bit rkey | 64-bit keys (CXI provider) | Moderate - data structure changes |
| Completion semantics | Work completion (WC) | CQ events with context | Significant - affects polling logic | 
| Addressing | LID-based (IB) / GID | Fabric addresses with VNI | Major - auth model different |


