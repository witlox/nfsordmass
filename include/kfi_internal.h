/*
 * kfi_internal.h - Internal definitions for kfabric NFS RDMA transport
 *
 * This header contains shared structures, prototypes, and macros used
 * across the kfabric NFS implementation modules.
 */

#ifndef _KFI_INTERNAL_H
#define _KFI_INTERNAL_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/rbtree.h>
#include <linux/hashtable.h>
#include <linux/workqueue.h>
#include <rdma/ib_verbs.h>
#include <rdma/kfi/fabric.h>
#include <rdma/kfi/endpoint.h>
#include <rdma/kfi/domain.h>
#include <rdma/kfi/cq.h>
#include <rdma/kfi/mr.h>

/*
 * ============================================================================
 * CONSTANTS AND LIMITS
 * ============================================================================
 */

#define KFI_MAX_DEVICES         8
#define KFI_MAX_SGE             16      /* Max scatter-gather entries */
#define KFI_MAX_INLINE_DATA     512     /* Max inline data size */
#define KFI_DEFAULT_CQ_SIZE     1024
#define KFI_DEFAULT_QP_DEPTH    256
#define KFI_PROGRESS_INTERVAL   100     /* usec */

/* VNI defaults */
#define KFI_DEFAULT_VNI         0       /* 0 = use system default */
#define KFI_VNI_MAX             65535

/* Memory registration cache sizes */
#define KFI_MR_CACHE_SIZE       1024
#define KFI_MR_MAX_REGIONS      8192

/*
 * ============================================================================
 * DEVICE MANAGEMENT
 * ============================================================================
 */

/**
 * struct kfi_device - Represents a CXI device exposed as IB device
 * @ibdev: IB device structure (for compatibility)
 * @fabric: kfabric fabric handle
 * @domain: kfabric domain handle
 * @info: Fabric information from kfi_getinfo()
 * @name: Device name
 * @list: List entry for global device list
 * @mr_cache: Memory registration cache
 * @default_cq: Default CQ for progress thread
 * @progress_thread: Progress thread handle
 */
struct kfi_device {
    struct ib_device ibdev;
    struct kfid_fabric *fabric;
    struct kfid_domain *domain;
    struct kfi_info *info;
    char name[64];
    struct list_head list;
    
    /* Memory registration cache */
    struct kfi_mr_cache *mr_cache;
    
    /* Progress engine */
    struct kfid_cq *default_cq;
    struct task_struct *progress_thread;
};

/*
 * ============================================================================
 * PROTECTION DOMAIN
 * ============================================================================
 */

/**
 * struct kfi_pd - Protection domain
 * @pd: IB PD structure
 * @device: Parent device
 * @kfi_domain: kfabric domain
 * @usecnt: Usage counter
 */
struct kfi_pd {
    struct ib_pd pd;
    struct kfi_device *device;
    struct kfid_domain *kfi_domain;
    atomic_t usecnt;
};

/*
 * ============================================================================
 * COMPLETION QUEUE
 * ============================================================================
 */

/**
 * struct kfi_cq - Completion queue
 * @cq: IB CQ structure
 * @device: Parent device
 * @kfi_cq: kfabric CQ
 * @comp_handler: Completion handler callback
 * @cq_context: Context for completion handler
 * @usecnt: Usage counter
 * @cqe: Number of CQ entries
 * @comp_wq: Workqueue for async completions
 * @comp_work: Work item for async completions
 */
struct kfi_cq {
    struct ib_cq cq;
    struct kfi_device *device;
    struct kfid_cq *kfi_cq;
    void (*comp_handler)(struct ib_cq *, void *);
    void *cq_context;
    atomic_t usecnt;
    int cqe;
    
    /* Async completion support */
    struct workqueue_struct *comp_wq;
    struct work_struct comp_work;
};

/*
 * ============================================================================
 * QUEUE PAIR
 * ============================================================================
 */

/**
 * struct kfi_cxi_auth_key - CXI authentication credentials
 * @vni: Virtual Network Identifier
 * @service_id: CXI service ID
 * @traffic_class: Traffic class for QoS
 */
struct kfi_cxi_auth_key {
    uint16_t vni;
    uint16_t service_id;
    uint8_t traffic_class;
};

/**
 * struct kfi_qp - Queue pair
 * @qp: IB QP structure
 * @pd: Protection domain
 * @ep: kfabric endpoint
 * @send_cq: Send completion queue
 * @recv_cq: Receive completion queue
 * @av: Address vector for connections
 * @event_handler: Event handler callback
 * @qp_context: Context for event handler
 * @qp_num: Synthetic QP number
 * @state: Current QP state
 * @auth_key: CXI authentication credentials
 * @sq_lock: Send queue lock
 * @rq_lock: Receive queue lock
 * @vni_from_mount: VNI specified in mount options (0 = not set)
 * @send_flags: Flags for send operations
 */
struct kfi_qp {
    struct ib_qp qp;
    struct kfi_pd *pd;
    struct kfid_ep *ep;
    struct ib_cq *send_cq;
    struct ib_cq *recv_cq;
    struct kfid_av *av;
    void (*event_handler)(struct ib_event *, void *);
    void *qp_context;
    u32 qp_num;
    enum ib_qp_state state;
    
    /* CXI-specific */
    struct kfi_cxi_auth_key *auth_key;
    uint16_t vni_from_mount;
    
    /* Locking */
    spinlock_t sq_lock;
    spinlock_t rq_lock;
    
    /* Send attributes */
    u32 send_flags;
};

/*
 * ============================================================================
 * MEMORY REGISTRATION
 * ============================================================================
 */

/**
 * struct kfi_mr - Memory region
 * @mr: IB MR structure
 * @pd: Protection domain
 * @kfi_mr: kfabric MR
 * @iova: IO virtual address
 * @length: Length of region
 * @lkey: Local key (32-bit)
 * @rkey: Remote key (32-bit)
 * @access_flags: Access permissions
 * @usecnt: Usage counter
 * @cache_entry: Entry in MR cache (if cached)
 */
struct kfi_mr {
    struct ib_mr mr;
    struct kfi_pd *pd;
    struct kfid_mr *kfi_mr;
    u64 iova;
    u64 length;
    u32 lkey;
    u32 rkey;
    u64 access_flags;
    atomic_t usecnt;
    void *cache_entry;
};

/**
 * struct kfi_mr_cache_entry - Entry in MR cache
 * @vaddr: Virtual address
 * @len: Length
 * @access: Access flags
 * @mr: Associated memory region
 * @refcount: Reference count
 * @node: RB tree node
 * @lru: LRU list entry
 * @last_used: Timestamp of last use
 */
struct kfi_mr_cache_entry {
    unsigned long vaddr;
    size_t len;
    u64 access;
    struct kfi_mr *mr;
    atomic_t refcount;
    struct rb_node node;
    struct list_head lru;
    unsigned long last_used;
};

/**
 * struct kfi_mr_cache - Memory registration cache
 * @root: RB tree root
 * @lru: LRU list head
 * @lock: Cache lock
 * @max_entries: Maximum cache entries
 * @current_entries: Current number of entries
 * @hits: Cache hit counter
 * @misses: Cache miss counter
 */
struct kfi_mr_cache {
    struct rb_root root;
    struct list_head lru;
    spinlock_t lock;
    int max_entries;
    atomic_t current_entries;
    atomic64_t hits;
    atomic64_t misses;
};

/*
 * ============================================================================
 * KEY MAPPING (32-bit <-> 64-bit)
 * ============================================================================
 */

/**
 * struct key_map_entry - Key mapping entry
 * @ib_key: 32-bit IB-style key
 * @kfi_key: 64-bit kfabric key
 * @ib_node: RB tree node indexed by ib_key
 * @kfi_node: Hash table node indexed by kfi_key
 * @refcount: Reference count
 */
struct key_map_entry {
    u32 ib_key;
    u64 kfi_key;
    struct rb_node ib_node;
    struct hlist_node kfi_node;
    atomic_t refcount;
};

/*
 * ============================================================================
 * PROGRESS ENGINE
 * ============================================================================
 */

/**
 * struct kfi_progress_thread - Progress thread context
 * @thread: Kernel thread
 * @device: Associated device
 * @should_stop: Stop flag
 * @wait_queue: Wait queue for wakeup
 */
struct kfi_progress_thread {
    struct task_struct *thread;
    struct kfi_device *device;
    atomic_t should_stop;
    wait_queue_head_t wait_queue;
};

/*
 * ============================================================================
 * WORK REQUEST BATCHING
 * ============================================================================
 */

#define KFI_MAX_BATCH_SIZE 16

/**
 * struct kfi_batch_ctx - Batching context for work requests
 * @iovs: IO vectors
 * @descs: Memory descriptors
 * @contexts: Context pointers
 * @count: Number of batched operations
 */
struct kfi_batch_ctx {
    struct iovec iovs[KFI_MAX_BATCH_SIZE];
    void *descs[KFI_MAX_BATCH_SIZE];
    void *contexts[KFI_MAX_BATCH_SIZE];
    int count;
};

/*
 * ============================================================================
 * FUNCTION PROTOTYPES - Device Management (kfi_verbs_compat.c)
 * ============================================================================
 */

/* Device enumeration */
struct ib_device **kfi_get_devices(int *num_devices);
void kfi_free_devices(struct ib_device **devices);

/* Protection domain */
struct ib_pd *kfi_alloc_pd(struct ib_device *device,
                            struct ib_ucontext *context,
                            struct ib_udata *udata);
int kfi_dealloc_pd(struct ib_pd *pd);

/* Completion queue */
struct ib_cq *kfi_create_cq(struct ib_device *device,
                             const struct ib_cq_init_attr *cq_attr,
                             struct ib_ucontext *context,
                             struct ib_udata *udata);
int kfi_destroy_cq(struct ib_cq *cq);
void kfi_cq_comp_worker(struct work_struct *work);

/* Queue pair */
struct ib_qp *kfi_create_qp(struct ib_pd *pd,
                             struct ib_qp_init_attr *init_attr);
int kfi_modify_qp(struct ib_qp *qp, struct ib_qp_attr *attr,
                   int attr_mask, struct ib_udata *udata);
int kfi_destroy_qp(struct ib_qp *qp);

/*
 * ============================================================================
 * FUNCTION PROTOTYPES - Operations (kfi_ops.c)
 * ============================================================================
 */

/* Post operations */
int kfi_post_send(struct ib_qp *qp,
                  const struct ib_send_wr *wr,
                  const struct ib_send_wr **bad_wr);
int kfi_post_recv(struct ib_qp *qp,
                  const struct ib_recv_wr *wr,
                  const struct ib_recv_wr **bad_wr);

/* Completion polling */
int kfi_poll_cq(struct ib_cq *cq, int num_entries, struct ib_wc *wc);
int kfi_req_notify_cq(struct ib_cq *cq, enum ib_cq_notify_flags flags);

/* Helper functions */
int kfi_do_send(struct kfi_qp *kqp, const struct ib_send_wr *wr);
int kfi_do_rdma_write(struct kfi_qp *kqp, const struct ib_send_wr *wr);
int kfi_do_rdma_read(struct kfi_qp *kqp, const struct ib_send_wr *wr);
int kfi_do_recv(struct kfi_qp *kqp, const struct ib_recv_wr *wr);

/* Batching */
int kfi_batch_send(struct kfi_qp *kqp, struct kfi_batch_ctx *batch);
void kfi_batch_init(struct kfi_batch_ctx *batch);
int kfi_batch_add(struct kfi_batch_ctx *batch, const struct ib_send_wr *wr);

/*
 * ============================================================================
 * FUNCTION PROTOTYPES - Memory (kfi_memory.c)
 * ============================================================================
 */

/* Memory region allocation */
struct ib_mr *kfi_alloc_mr(struct ib_pd *pd,
                            enum ib_mr_type mr_type,
                            u32 max_num_sg);
struct ib_mr *kfi_get_dma_mr(struct ib_pd *pd, int mr_access_flags);
struct ib_mr *kfi_reg_user_mr(struct ib_pd *pd, u64 start, u64 length,
                               u64 virt_addr, int mr_access_flags,
                               struct ib_udata *udata);

/* Memory region operations */
int kfi_map_mr_sg(struct ib_mr *mr, struct scatterlist *sg,
                  int sg_nents, unsigned int *sg_offset,
                  unsigned int page_size);
int kfi_dereg_mr(struct ib_mr *mr);

/* Memory windows (if needed) */
struct ib_mw *kfi_alloc_mw(struct ib_pd *pd, enum ib_mw_type type,
                            struct ib_udata *udata);
int kfi_dealloc_mw(struct ib_mw *mw);

/* MR cache operations */
struct kfi_mr_cache *kfi_mr_cache_create(int max_entries);
void kfi_mr_cache_destroy(struct kfi_mr_cache *cache);
struct kfi_mr *kfi_mr_cache_get(struct kfi_mr_cache *cache,
                                 unsigned long vaddr, size_t len,
                                 u64 access, struct kfi_pd *pd);
void kfi_mr_cache_put(struct kfi_mr_cache *cache, struct kfi_mr *mr);
void kfi_mr_cache_flush(struct kfi_mr_cache *cache);

/*
 * ============================================================================
 * FUNCTION PROTOTYPES - Connection (kfi_connection.c)
 * ============================================================================
 */

/* Connection management */
int kfi_connect_ep(struct kfi_qp *kqp, struct sockaddr *remote_addr);
int kfi_setup_av(struct kfi_qp *kqp, struct rdma_ah_attr *ah_attr);
int kfi_get_auth_key(struct kfi_qp *kqp);
int kfi_query_default_vni(long *vni);

/* VNI parsing */
int kfi_parse_vni_from_options(const char *options, uint16_t *vni_out);

/*
 * ============================================================================
 * FUNCTION PROTOTYPES - Completion (kfi_completion.c)
 * ============================================================================
 */

/* Status translation */
enum ib_wc_status kfi_errno_to_ib_status(int kfi_err);
enum ib_wc_opcode kfi_flags_to_ib_opcode(uint64_t flags);

/*
 * ============================================================================
 * FUNCTION PROTOTYPES - Key Mapping (kfi_key_mapping.c)
 * ============================================================================
 */

void kfi_key_mapping_init(void);
void kfi_key_mapping_cleanup(void);
int kfi_key_register(u64 kfi_key, u32 *ib_key_out);
int kfi_key_lookup_ib(u32 ib_key, u64 *kfi_key_out);
int kfi_key_lookup_kfi(u64 kfi_key, u32 *ib_key_out);
void kfi_key_unregister(u32 ib_key);

/*
 * ============================================================================
 * FUNCTION PROTOTYPES - Progress Engine (kfi_progress.c)
 * ============================================================================
 */

int kfi_progress_start(struct kfi_device *device);
void kfi_progress_stop(struct kfi_device *device);
void kfi_progress_cleanup_all(void);

/*
 * ============================================================================
 * UTILITY MACROS
 * ============================================================================
 */

/* Convert between structures */
#define kfi_to_ibdev(kdev)      (&(kdev)->ibdev)
#define ibdev_to_kfi(ibdev)     container_of(ibdev, struct kfi_device, ibdev)

#define kfi_to_ibpd(kpd)        (&(kpd)->pd)
#define ibpd_to_kfi(pd)         container_of(pd, struct kfi_pd, pd)

#define kfi_to_ibcq(kcq)        (&(kcq)->cq)
#define ibcq_to_kfi(cq)         container_of(cq, struct kfi_cq, cq)

#define kfi_to_ibqp(kqp)        (&(kqp)->qp)
#define ibqp_to_kfi(qp)         container_of(qp, struct kfi_qp, qp)

#define kfi_to_ibmr(kmr)        (&(kmr)->mr)
#define ibmr_to_kfi(mr)         container_of(mr, struct kfi_mr, mr)

/* Opcode translation */
static inline uint64_t ib_opcode_to_kfi(enum ib_wr_opcode opcode)
{
    switch (opcode) {
    case IB_WR_SEND:
    case IB_WR_SEND_WITH_IMM:
        return KFI_SEND;
    case IB_WR_RDMA_WRITE:
    case IB_WR_RDMA_WRITE_WITH_IMM:
        return KFI_WRITE;
    case IB_WR_RDMA_READ:
        return KFI_READ;
    case IB_WR_ATOMIC_CMP_AND_SWP:
        return KFI_ATOMIC;
    case IB_WR_ATOMIC_FETCH_AND_ADD:
        return KFI_ATOMIC;
    default:
        return 0;
    }
}

/* Access flags translation */
static inline u64 ib_access_to_kfi(int ib_access)
{
    u64 kfi_access = 0;
    
    if (ib_access & IB_ACCESS_LOCAL_WRITE)
        kfi_access |= KFI_WRITE;
    if (ib_access & IB_ACCESS_REMOTE_WRITE)
        kfi_access |= KFI_REMOTE_WRITE;
    if (ib_access & IB_ACCESS_REMOTE_READ)
        kfi_access |= KFI_REMOTE_READ;
    if (ib_access & IB_ACCESS_REMOTE_ATOMIC)
        kfi_access |= KFI_REMOTE_WRITE; /* CXI doesn't have separate atomic flag */
        
    return kfi_access;
}

/* Debug printing */
#ifdef CONFIG_KFI_DEBUG
#define kfi_dbg(fmt, ...) pr_debug("kfi: " fmt, ##__VA_ARGS__)
#else
#define kfi_dbg(fmt, ...) do {} while (0)
#endif

#define kfi_info(fmt, ...) pr_info("kfi: " fmt, ##__VA_ARGS__)
#define kfi_warn(fmt, ...) pr_warn("kfi: " fmt, ##__VA_ARGS__)
#define kfi_err(fmt, ...)  pr_err("kfi: " fmt, ##__VA_ARGS__)

#endif /* _KFI_INTERNAL_H */
