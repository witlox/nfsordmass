/*
 * kfi_memory.c - Memory registration and management for kfabric NFS
 *
 * Implements memory region allocation, registration, and caching.
 * Critical for performance as NFS does frequent memory registrations.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/rbtree.h>
#include <rdma/ib_verbs.h>
#include <rdma/kfi/mr.h>

#include "kfi_internal.h"

/*
 * ============================================================================
 * MEMORY REGION ALLOCATION
 * ============================================================================
 */

/**
 * kfi_alloc_mr - Allocate memory region for fast registration
 * @pd: Protection domain
 * @mr_type: Type of MR (IB_MR_TYPE_MEM_REG, etc.)
 * @max_num_sg: Maximum number of SG entries
 *
 * This is the key function for NFS RDMA which uses Fast Memory Registration.
 * NFS typically registers/deregisters memory frequently for each I/O.
 */
struct ib_mr *kfi_alloc_mr(struct ib_pd *pd,
                            enum ib_mr_type mr_type,
                            u32 max_num_sg)
{
    struct kfi_pd *kpd = ibpd_to_kfi(pd);
    struct kfi_mr *kmr;
    u64 access;
    u32 ib_key;
    int ret;

    kfi_dbg("alloc_mr: type=%d max_sg=%u\n", mr_type, max_num_sg);

    /* Only support MEM_REG type for now */
    if (mr_type != IB_MR_TYPE_MEM_REG) {
        kfi_err("alloc_mr: Unsupported MR type %d\n", mr_type);
        return ERR_PTR(-EOPNOTSUPP);
    }

    kmr = kzalloc(sizeof(*kmr), GFP_KERNEL);
    if (!kmr)
        return ERR_PTR(-ENOMEM);

    kmr->pd = kpd;
    atomic_set(&kmr->usecnt, 1);

    /* Set up access flags
     * For CXI, we create an "empty" MR that will be populated later
     * via kfi_map_mr_sg()
     */
    access = KFI_READ | KFI_WRITE | KFI_REMOTE_READ | KFI_REMOTE_WRITE;

    /* Register with kfabric
     * For fast registration, we pass NULL buffer - actual mapping done later
     * kfi_mr_reg signature: (domain, buf, len, access, offset, requested_key, flags, mr, context, event)
     */
    ret = kfi_mr_reg(kpd->kfi_domain,
                     NULL, 0, /* No buffer yet */
                     access,
                     0, /* offset */
                     0, /* requested_key - let provider choose */
                     0, /* flags */
                     &kmr->kfi_mr,
                     NULL, /* context */
                     NULL); /* event */
    
    if (ret) {
        kfi_err("kfi_mr_reg failed: %d\n", ret);
        kfree(kmr);
        return ERR_PTR(ret);
    }

    /* Get the kfabric key and create IB-compatible 32-bit keys */
    u64 kfi_key = kfi_mr_key(kmr->kfi_mr);
    
    ret = kfi_key_register(kfi_key, &ib_key);
    if (ret) {
        kfi_err("kfi_key_register failed: %d\n", ret);
        kfi_close(&kmr->kfi_mr->fid);
        kfree(kmr);
        return ERR_PTR(ret);
    }

    kmr->lkey = ib_key;
    kmr->rkey = ib_key; /* For simplicity, same key for local/remote */
    
    atomic_inc(&kpd->usecnt);

    kfi_dbg("alloc_mr: success lkey=0x%x rkey=0x%x\n", kmr->lkey, kmr->rkey);
    return kfi_to_ibmr(kmr);
}
EXPORT_SYMBOL(kfi_alloc_mr);

/**
 * kfi_get_dma_mr - Get DMA memory region
 * @pd: Protection domain
 * @mr_access_flags: Access flags
 *
 * This creates an MR that covers all physical memory.
 * Used for simple DMA operations without explicit registration.
 */
struct ib_mr *kfi_get_dma_mr(struct ib_pd *pd, int mr_access_flags)
{
    struct kfi_pd *kpd = ibpd_to_kfi(pd);
    struct kfi_mr *kmr;
    u64 kfi_access;
    u32 ib_key;
    int ret;

    kfi_dbg("get_dma_mr: access=0x%x\n", mr_access_flags);

    kmr = kzalloc(sizeof(*kmr), GFP_KERNEL);
    if (!kmr)
        return ERR_PTR(-ENOMEM);

    kmr->pd = kpd;
    atomic_set(&kmr->usecnt, 1);
    kmr->access_flags = mr_access_flags;

    /* Translate access flags */
    kfi_access = ib_access_to_kfi(mr_access_flags);

    /* For DMA MR, we register entire address space
     * CXI may have restrictions here - check provider capabilities
     */
    ret = kfi_mr_reg(kpd->kfi_domain,
                     NULL, /* NULL = all memory */
                     SIZE_MAX, /* All addressable memory */
                     kfi_access,
                     0, /* offset */
                     0, /* Let provider choose key */
                     0, /* flags */
                     &kmr->kfi_mr,
                     NULL, /* context */
                     NULL); /* event */
    
    if (ret) {
        kfi_err("kfi_mr_reg (DMA) failed: %d\n", ret);
        kfree(kmr);
        return ERR_PTR(ret);
    }

    /* Register key mapping */
    u64 kfi_key = kfi_mr_key(kmr->kfi_mr);
    ret = kfi_key_register(kfi_key, &ib_key);
    if (ret) {
        kfi_close(&kmr->kfi_mr->fid);
        kfree(kmr);
        return ERR_PTR(ret);
    }

    kmr->lkey = ib_key;
    kmr->rkey = ib_key;
    kmr->iova = 0;
    kmr->length = SIZE_MAX;

    atomic_inc(&kpd->usecnt);

    kfi_dbg("get_dma_mr: success lkey=0x%x\n", kmr->lkey);
    return kfi_to_ibmr(kmr);
}
EXPORT_SYMBOL(kfi_get_dma_mr);

/**
 * kfi_reg_user_mr - Register user memory region
 * @pd: Protection domain
 * @start: Start address
 * @length: Length of region
 * @virt_addr: Virtual address
 * @mr_access_flags: Access flags
 * @udata: User data
 *
 * For user-space NFS clients (less common in kernel context).
 */
struct ib_mr *kfi_reg_user_mr(struct ib_pd *pd, u64 start, u64 length,
                               u64 virt_addr, int mr_access_flags,
                               struct ib_udata *udata)
{
    /* User MR not implemented yet - kernel NFS doesn't need this */
    kfi_err("reg_user_mr: Not implemented\n");
    return ERR_PTR(-EOPNOTSUPP);
}
EXPORT_SYMBOL(kfi_reg_user_mr);

/*
 * ============================================================================
 * MEMORY REGION MAPPING
 * ============================================================================
 */

/**
 * kfi_map_mr_sg - Map scatter-gather list to MR
 * @mr: Memory region (from kfi_alloc_mr)
 * @sg: Scatter-gather list
 * @sg_nents: Number of SG entries
 * @sg_offset: Offset into SG list
 * @page_size: Page size (typically PAGE_SIZE)
 *
 * This is called after kfi_alloc_mr() to actually register the memory.
 * NFS uses this for each I/O operation.
 */
int kfi_map_mr_sg(struct ib_mr *mr, struct scatterlist *sg,
                  int sg_nents, unsigned int *sg_offset,
                  unsigned int page_size)
{
    struct kfi_mr *kmr = ibmr_to_kfi(mr);
    struct iovec *iovs;
    struct scatterlist *sg_entry;
    int i, mapped = 0;
    int ret;

    kfi_dbg("map_mr_sg: nents=%d offset=%u pagesize=%u\n",
            sg_nents, sg_offset ? *sg_offset : 0, page_size);

    if (sg_nents > KFI_MAX_SGE) {
        kfi_err("map_mr_sg: Too many SG entries: %d > %d\n",
                sg_nents, KFI_MAX_SGE);
        return -EINVAL;
    }

    /* Convert scatter-gather list to iovec for kfabric */
    iovs = kcalloc(sg_nents, sizeof(*iovs), GFP_KERNEL);
    if (!iovs)
        return -ENOMEM;

    /* Walk the scatter-gather list */
    for_each_sg(sg, sg_entry, sg_nents, i) {
        void *addr;
        size_t len;

        len = sg_dma_len(sg_entry);
        if (len == 0)
            continue;

        /* Get kernel virtual address from SG entry */
        addr = sg_virt(sg_entry);
        
        /* Apply offset to first entry if specified */
        if (i == 0 && sg_offset && *sg_offset) {
            addr += *sg_offset;
            len -= *sg_offset;
        }

        iovs[mapped].iov_base = addr;
        iovs[mapped].iov_len = len;
        mapped++;

        kfi_dbg("  [%d] addr=%p len=%zu\n", mapped - 1, addr, len);
    }

    if (mapped == 0) {
        kfi_warn("map_mr_sg: No entries mapped\n");
        kfree(iovs);
        return 0;
    }

    /* Update the kfabric MR with the actual memory regions
     * NOTE: kfi_mr_regv doesn't exist in kfabric API
     * For now, register the first segment only. Full scatter-gather
     * support would require multiple MRs or provider-specific extensions
     */
    if (mapped > 0) {
        ret = kfi_mr_reg(kmr->pd->kfi_domain,
                         iovs[0].iov_base,
                         iovs[0].iov_len,
                         kmr->access_flags,
                         0, /* offset */
                         kfi_mr_key(kmr->kfi_mr), /* Use existing key */
                         0, /* flags */
                         &kmr->kfi_mr, /* Update in place */
                         NULL, /* context */
                         NULL); /* event */

        if (ret) {
            kfi_err("kfi_mr_reg failed: %d\n", ret);
            kfree(iovs);
            return ret;
        }
    }

    /* Calculate total length and base address */
    kmr->iova = (u64)(uintptr_t)iovs[0].iov_base;
    kmr->length = 0;
    for (i = 0; i < mapped; i++) {
        kmr->length += iovs[i].iov_len;
    }

    kfree(iovs);

    kfi_dbg("map_mr_sg: Mapped %d entries, total length=%llu\n",
            mapped, kmr->length);
    return mapped;
}
EXPORT_SYMBOL(kfi_map_mr_sg);

/*
 * ============================================================================
 * MEMORY REGION DEREGISTRATION
 * ============================================================================
 */

/**
 * kfi_dereg_mr - Deregister memory region
 * @mr: Memory region to deregister
 */
int kfi_dereg_mr(struct ib_mr *mr)
{
    struct kfi_mr *kmr = ibmr_to_kfi(mr);
    int ret;

    if (atomic_read(&kmr->usecnt) > 1) {
        kfi_warn("dereg_mr: MR still in use (usecnt=%d)\n",
                 atomic_read(&kmr->usecnt));
        return -EBUSY;
    }

    kfi_dbg("dereg_mr: lkey=0x%x rkey=0x%x\n", kmr->lkey, kmr->rkey);

    /* Unregister key mapping */
    kfi_key_unregister(kmr->lkey);

    /* Close kfabric MR */
    ret = kfi_close(&kmr->kfi_mr->fid);
    if (ret) {
        kfi_err("kfi_close(mr) failed: %d\n", ret);
        /* Continue anyway to free memory */
    }

    atomic_dec(&kmr->pd->usecnt);
    kfree(kmr);

    return 0;
}
EXPORT_SYMBOL(kfi_dereg_mr);

/*
 * ============================================================================
 * MEMORY WINDOWS (Optional - for advanced RDMA operations)
 * ============================================================================
 */

/**
 * kfi_alloc_mw - Allocate memory window
 * @pd: Protection domain
 * @type: Memory window type
 * @udata: User data
 *
 * Memory windows allow dynamic binding of MRs.
 * Not commonly used by NFS but included for completeness.
 */
struct ib_mw *kfi_alloc_mw(struct ib_pd *pd, enum ib_mw_type type,
                            struct ib_udata *udata)
{
    kfi_err("alloc_mw: Not implemented (CXI may not support MWs)\n");
    return ERR_PTR(-EOPNOTSUPP);
}
EXPORT_SYMBOL(kfi_alloc_mw);

/**
 * kfi_dealloc_mw - Deallocate memory window
 */
int kfi_dealloc_mw(struct ib_mw *mw)
{
    return -EOPNOTSUPP;
}
EXPORT_SYMBOL(kfi_dealloc_mw);

/*
 * ============================================================================
 * MEMORY REGISTRATION CACHE
 * ============================================================================
 */

/**
 * kfi_mr_cache_create - Create MR cache
 * @max_entries: Maximum number of cached entries
 *
 * Caching MRs significantly improves performance by avoiding
 * repeated registration/deregistration of the same memory.
 */
struct kfi_mr_cache *kfi_mr_cache_create(int max_entries)
{
    struct kfi_mr_cache *cache;

    cache = kzalloc(sizeof(*cache), GFP_KERNEL);
    if (!cache)
        return NULL;

    cache->root = RB_ROOT;
    INIT_LIST_HEAD(&cache->lru);
    spin_lock_init(&cache->lock);
    cache->max_entries = max_entries;
    atomic_set(&cache->current_entries, 0);
    atomic64_set(&cache->hits, 0);
    atomic64_set(&cache->misses, 0);

    kfi_info("MR cache created (max_entries=%d)\n", max_entries);
    return cache;
}

/**
 * kfi_mr_cache_destroy - Destroy MR cache and free all entries
 */
void kfi_mr_cache_destroy(struct kfi_mr_cache *cache)
{
    struct kfi_mr_cache_entry *entry, *tmp;
    unsigned long flags;

    if (!cache)
        return;

    spin_lock_irqsave(&cache->lock, flags);
    
    /* Free all entries */
    list_for_each_entry_safe(entry, tmp, &cache->lru, lru) {
        rb_erase(&entry->node, &cache->root);
        list_del(&entry->lru);
        
        /* Deregister the MR */
        kfi_dereg_mr(kfi_to_ibmr(entry->mr));
        kfree(entry);
    }
    
    spin_unlock_irqrestore(&cache->lock, flags);

    s64 hits = atomic64_read(&cache->hits);
    s64 misses = atomic64_read(&cache->misses);
    s64 total = hits + misses;
    int hit_rate = total > 0 ? (int)((hits * 100) / total) : 0;

    kfi_info("MR cache destroyed (hits=%lld misses=%lld hit_rate=%d%%)\n",
             hits, misses, hit_rate);

    kfree(cache);
}

/**
 * kfi_mr_cache_get - Get MR from cache or create new
 * @cache: MR cache
 * @vaddr: Virtual address
 * @len: Length
 * @access: Access flags
 * @pd: Protection domain
 */
struct kfi_mr *kfi_mr_cache_get(struct kfi_mr_cache *cache,
                                 unsigned long vaddr, size_t len,
                                 u64 access, struct kfi_pd *pd)
{
    struct kfi_mr_cache_entry *entry;
    struct rb_node *node;
    unsigned long flags;
    struct ib_mr *mr;

    spin_lock_irqsave(&cache->lock, flags);

    /* Search for existing entry */
    node = cache->root.rb_node;
    while (node) {
        entry = rb_entry(node, struct kfi_mr_cache_entry, node);

        if (vaddr < entry->vaddr) {
            node = node->rb_left;
        } else if (vaddr > entry->vaddr) {
            node = node->rb_right;
        } else if (len != entry->len) {
            /* Address matches but length differs - continue search */
            node = (len < entry->len) ? node->rb_left : node->rb_right;
        } else if (access != entry->access) {
            /* Need different access permissions - continue search */
            node = (access < entry->access) ? node->rb_left : node->rb_right;
        } else {
            /* Found exact match */
            atomic_inc(&entry->refcount);
            entry->last_used = jiffies;
            
            /* Move to head of LRU */
            list_del(&entry->lru);
            list_add(&entry->lru, &cache->lru);
            
            atomic64_inc(&cache->hits);
            spin_unlock_irqrestore(&cache->lock, flags);
            
            kfi_dbg("MR cache HIT: vaddr=0x%lx len=%zu\n", vaddr, len);
            return entry->mr;
        }
    }

    /* Cache miss - need to create new entry */
    atomic64_inc(&cache->misses);
    spin_unlock_irqrestore(&cache->lock, flags);

    kfi_dbg("MR cache MISS: vaddr=0x%lx len=%zu\n", vaddr, len);

    /* Create new MR */
    mr = kfi_get_dma_mr(kfi_to_ibpd(pd), (int)access);
    if (IS_ERR(mr))
        return ERR_CAST(mr);

    /* Create cache entry */
    entry = kzalloc(sizeof(*entry), GFP_KERNEL);
    if (!entry) {
        kfi_dereg_mr(mr);
        return ERR_PTR(-ENOMEM);
    }

    entry->vaddr = vaddr;
    entry->len = len;
    entry->access = access;
    entry->mr = ibmr_to_kfi(mr);
    atomic_set(&entry->refcount, 1);
    entry->last_used = jiffies;

    /* Insert into cache */
    spin_lock_irqsave(&cache->lock, flags);

    /* Check if cache is full */
    if (atomic_read(&cache->current_entries) >= cache->max_entries) {
        /* Evict LRU entry */
        struct kfi_mr_cache_entry *lru_entry;
        lru_entry = list_last_entry(&cache->lru,
                                     struct kfi_mr_cache_entry, lru);
        
        if (atomic_read(&lru_entry->refcount) == 0) {
            rb_erase(&lru_entry->node, &cache->root);
            list_del(&lru_entry->lru);
            atomic_dec(&cache->current_entries);
            
            spin_unlock_irqrestore(&cache->lock, flags);
            kfi_dereg_mr(kfi_to_ibmr(lru_entry->mr));
            kfree(lru_entry);
            spin_lock_irqsave(&cache->lock, flags);
            
            kfi_dbg("MR cache: Evicted LRU entry\n");
        }
    }

    /* Insert into RB tree */
    struct rb_node **new = &cache->root.rb_node, *parent = NULL;
    
    while (*new) {
        struct kfi_mr_cache_entry *this;
        this = rb_entry(*new, struct kfi_mr_cache_entry, node);
        parent = *new;

        if (vaddr < this->vaddr)
            new = &((*new)->rb_left);
        else
            new = &((*new)->rb_right);
    }

    rb_link_node(&entry->node, parent, new);
    rb_insert_color(&entry->node, &cache->root);
    
    /* Add to head of LRU */
    list_add(&entry->lru, &cache->lru);
    
    atomic_inc(&cache->current_entries);
    spin_unlock_irqrestore(&cache->lock, flags);

    return entry->mr;
}

/**
 * kfi_mr_cache_put - Release reference to cached MR
 */
void kfi_mr_cache_put(struct kfi_mr_cache *cache, struct kfi_mr *mr)
{
    struct kfi_mr_cache_entry *entry;
    unsigned long flags;

    if (!cache || !mr)
        return;

    entry = mr->cache_entry;
    if (!entry) {
        kfi_warn("mr_cache_put: MR not in cache\n");
        return;
    }

    spin_lock_irqsave(&cache->lock, flags);
    atomic_dec(&entry->refcount);
    spin_unlock_irqrestore(&cache->lock, flags);
}

/**
 * kfi_mr_cache_flush - Flush all entries from cache
 */
void kfi_mr_cache_flush(struct kfi_mr_cache *cache)
{
    struct kfi_mr_cache_entry *entry, *tmp;
    unsigned long flags;
    int flushed = 0;

    if (!cache)
        return;

    spin_lock_irqsave(&cache->lock, flags);
    
    list_for_each_entry_safe(entry, tmp, &cache->lru, lru) {
        if (atomic_read(&entry->refcount) == 0) {
            rb_erase(&entry->node, &cache->root);
            list_del(&entry->lru);
            atomic_dec(&cache->current_entries);
            
            spin_unlock_irqrestore(&cache->lock, flags);
            kfi_dereg_mr(kfi_to_ibmr(entry->mr));
            kfree(entry);
            flushed++;
            spin_lock_irqsave(&cache->lock, flags);
        }
    }
    
    spin_unlock_irqrestore(&cache->lock, flags);

    kfi_info("MR cache flushed: %d entries removed\n", flushed);
}

