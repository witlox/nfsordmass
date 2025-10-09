/*
 * kfi_key_mapping.c - 32-bit/64-bit memory key translation
 *
 * CHALLENGE 3 MITIGATION: NFS uses 32-bit keys, CXI uses 64-bit.
 * Maintain bidirectional mapping.
 */

#include <linux/rbtree.h>
#include <linux/slab.h>
#include <linux/hash.h>
#include "../include/kfi_internal.h"

struct key_map_entry {
    u32 ib_key;        /* 32-bit key for NFS */
    u64 kfi_key;       /* 64-bit key from CXI */
    struct rb_node ib_node;   /* RB tree by ib_key */
    struct hlist_node kfi_node; /* Hash table by kfi_key */
    atomic_t refcount;
};

static struct rb_root ib_key_tree = RB_ROOT;
static DEFINE_SPINLOCK(ib_key_lock);

#define KEY_HASH_BITS 10
static DEFINE_HASHTABLE(kfi_key_hash, KEY_HASH_BITS);
static DEFINE_SPINLOCK(kfi_key_lock);

static atomic_t next_ib_key = ATOMIC_INIT(0x10000); /* Start at 64K */

/**
 * kfi_key_mapping_init - Initialize key mapping tables
 */
void kfi_key_mapping_init(void)
{
    hash_init(kfi_key_hash);
    pr_info("kfi_key_mapping: Initialized\n");
}

/**
 * kfi_key_register - Register a new key mapping
 * @kfi_key: 64-bit key from kfabric
 * @ib_key_out: Returns generated 32-bit key
 *
 * Returns: 0 on success, negative error code on failure
 */
int kfi_key_register(u64 kfi_key, u32 *ib_key_out)
{
    struct key_map_entry *entry;
    struct rb_node **new, *parent = NULL;
    u32 ib_key;
    unsigned long flags;

    entry = kzalloc(sizeof(*entry), GFP_KERNEL);
    if (!entry)
        return -ENOMEM;

    /* Generate unique 32-bit key */
    ib_key = atomic_inc_return(&next_ib_key);
    
    entry->ib_key = ib_key;
    entry->kfi_key = kfi_key;
    atomic_set(&entry->refcount, 1);

    /* Insert into IB key tree */
    spin_lock_irqsave(&ib_key_lock, flags);
    new = &ib_key_tree.rb_node;
    
    while (*new) {
        struct key_map_entry *this = rb_entry(*new, struct key_map_entry,
                                               ib_node);
        parent = *new;
        
        if (ib_key < this->ib_key)
            new = &((*new)->rb_left);
        else if (ib_key > this->ib_key)
            new = &((*new)->rb_right);
        else {
            /* Collision - should never happen with atomic counter */
            spin_unlock_irqrestore(&ib_key_lock, flags);
            kfree(entry);
            return -EEXIST;
        }
    }
    
    rb_link_node(&entry->ib_node, parent, new);
    rb_insert_color(&entry->ib_node, &ib_key_tree);
    spin_unlock_irqrestore(&ib_key_lock, flags);

    /* Insert into KFI key hash */
    spin_lock_irqsave(&kfi_key_lock, flags);
    hash_add(kfi_key_hash, &entry->kfi_node, kfi_key);
    spin_unlock_irqrestore(&kfi_key_lock, flags);

    *ib_key_out = ib_key;
    
    pr_debug("kfi_key_mapping: Registered 0x%llx -> 0x%x\n", kfi_key, ib_key);
    return 0;
}

/**
 * kfi_key_lookup_ib - Look up kfi key from IB key
 */
int kfi_key_lookup_ib(u32 ib_key, u64 *kfi_key_out)
{
    struct rb_node *node;
    struct key_map_entry *entry;
    unsigned long flags;

    spin_lock_irqsave(&ib_key_lock, flags);
    node = ib_key_tree.rb_node;
    
    while (node) {
        entry = rb_entry(node, struct key_map_entry, ib_node);
        
        if (ib_key < entry->ib_key)
            node = node->rb_left;
        else if (ib_key > entry->ib_key)
            node = node->rb_right;
        else {
            *kfi_key_out = entry->kfi_key;
            spin_unlock_irqrestore(&ib_key_lock, flags);
            return 0;
        }
    }
    
    spin_unlock_irqrestore(&ib_key_lock, flags);
    return -ENOENT;
}

/**
 * kfi_key_lookup_kfi - Look up IB key from kfi key
 */
int kfi_key_lookup_kfi(u64 kfi_key, u32 *ib_key_out)
{
    struct key_map_entry *entry;
    unsigned long flags;

    spin_lock_irqsave(&kfi_key_lock, flags);
    
    hash_for_each_possible(kfi_key_hash, entry, kfi_node, kfi_key) {
        if (entry->kfi_key == kfi_key) {
            *ib_key_out = entry->ib_key;
            spin_unlock_irqrestore(&kfi_key_lock, flags);
            return 0;
        }
    }
    
    spin_unlock_irqrestore(&kfi_key_lock, flags);
    return -ENOENT;
}

/**
 * kfi_key_unregister - Remove key mapping
 */
void kfi_key_unregister(u32 ib_key)
{
    struct key_map_entry *entry;
    struct rb_node *node;
    unsigned long flags;

    spin_lock_irqsave(&ib_key_lock, flags);
    node = ib_key_tree.rb_node;
    
    while (node) {
        entry = rb_entry(node, struct key_map_entry, ib_node);
        
        if (ib_key < entry->ib_key) {
            node = node->rb_left;
        } else if (ib_key > entry->ib_key) {
            node = node->rb_right;
        } else {
            /* Found it */
            rb_erase(&entry->ib_node, &ib_key_tree);
            spin_unlock_irqrestore(&ib_key_lock, flags);
            
            /* Remove from hash */
            spin_lock_irqsave(&kfi_key_lock, flags);
            hash_del(&entry->kfi_node);
            spin_unlock_irqrestore(&kfi_key_lock, flags);
            
            kfree(entry);
            pr_debug("kfi_key_mapping: Unregistered 0x%x\n", ib_key);
            return;
        }
    }
    
    spin_unlock_irqrestore(&ib_key_lock, flags);
}

/**
 * kfi_key_mapping_cleanup - Clean up all key mappings
 */
void kfi_key_mapping_cleanup(void)
{
    struct key_map_entry *entry;
    struct rb_node *node;
    unsigned long flags;

    spin_lock_irqsave(&ib_key_lock, flags);
    while ((node = rb_first(&ib_key_tree))) {
        entry = rb_entry(node, struct key_map_entry, ib_node);
        rb_erase(node, &ib_key_tree);
        
        spin_lock(&kfi_key_lock);
        hash_del(&entry->kfi_node);
        spin_unlock(&kfi_key_lock);
        
        kfree(entry);
    }
    spin_unlock_irqrestore(&ib_key_lock, flags);
    
    pr_info("kfi_key_mapping: Cleaned up\n");
}
