#ifndef _LINUX_XPRTRDMA_KFI_COMPAT_H
#define _LINUX_XPRTRDMA_KFI_COMPAT_H

#include <rdma/ib_verbs.h>
#include <rdma/kfi/fabric.h>
#include <rdma/kfi/endpoint.h>
#include <rdma/kfi/domain.h>
#include <rdma/kfi/cq.h>
#include <linux/sunrpc/xprt.h>

/* Include kfabric errno definitions */
#include "kfi_errno.h"

/*
 * Compatibility header for kfabric
 *
 * This header provides basic kfabric includes.
 * Actual structure definitions are in kfi_internal.h to avoid duplication.
 */

/*
 * XPRT_TRANSPORT_RDMA compatibility
 * Some kernels define this in sunrpc/xprt.h, others don't.
 * The value 256 (0x100) is the standard RDMA transport identifier.
 */
#ifndef XPRT_TRANSPORT_RDMA
#define XPRT_TRANSPORT_RDMA     256
#endif

#endif /* _LINUX_XPRTRDMA_KFI_COMPAT_H */
