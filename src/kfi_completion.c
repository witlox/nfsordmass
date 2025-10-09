#include "kfi_verbs_compat.h"
#include "../include/kfi_internal.h"

/*
 * Poll completions and translate to ib_wc format
 * This is performance-critical code
 */
int kfi_poll_cq(struct ib_cq *cq, int num_entries, struct ib_wc *wc)
{
    struct kfi_cq *kcq = container_of(cq, struct kfi_cq, cq);
    struct kfi_cq_data_entry cq_entry[num_entries];
    ssize_t ret;
    int i, count = 0;
    
    ret = kfi_cq_read(kcq->kfi_cq, cq_entry, num_entries);
    if (ret < 0) {
        if (ret == -KFI_EAGAIN)
            return 0; /* No completions available */
        
        /* Check for CQ errors */
        struct kfi_cq_err_entry err_entry;
        ret = kfi_cq_readerr(kcq->kfi_cq, &err_entry, 0);
        if (ret == 1) {
            /* Translate error to ib_wc */
            wc[0].wr_id = (u64)err_entry.op_context;
            wc[0].status = kfi_errno_to_ib_status(err_entry.err);
            wc[0].vendor_err = err_entry.prov_errno;
            return 1;
        }
        return 0;
    }
    
    count = (int)ret;
    
    /* Translate each completion */
    for (i = 0; i < count; i++) {
        wc[i].wr_id = (u64)cq_entry[i].op_context;
        wc[i].status = IB_WC_SUCCESS;
        wc[i].byte_len = (u32)cq_entry[i].len;
        
        /* Map kfabric flags to IB opcode */
        if (cq_entry[i].flags & KFI_SEND)
            wc[i].opcode = IB_WC_SEND;
        else if (cq_entry[i].flags & KFI_RECV)
            wc[i].opcode = IB_WC_RECV;
        else if (cq_entry[i].flags & KFI_READ)
            wc[i].opcode = IB_WC_RDMA_READ;
        else if (cq_entry[i].flags & KFI_WRITE)
            wc[i].opcode = IB_WC_RDMA_WRITE;
        else
            wc[i].opcode = IB_WC_SEND; /* Default */
    }
    
    return count;
}

static enum ib_wc_status kfi_errno_to_ib_status(int kfi_err)
{
    switch (-kfi_err) {
    case KFI_SUCCESS:
        return IB_WC_SUCCESS;
    case KFI_ETRUNC:
        return IB_WC_LOC_LEN_ERR;
    case KFI_EACCES:
        return IB_WC_LOC_PROT_ERR;
    case KFI_ECANCELED:
        return IB_WC_WR_FLUSH_ERR;
    /* Add more mappings */
    default:
        return IB_WC_GENERAL_ERR;
    }
}
