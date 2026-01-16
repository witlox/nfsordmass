/*
 * kfi_errno.h - kfabric error code definitions
 *
 * These error codes mirror libfabric (OFI) error codes for use in
 * the kernel fabric interface. They are used for consistent error
 * handling across the kfabric NFS RDMA transport.
 */

#ifndef _KFI_ERRNO_H
#define _KFI_ERRNO_H

/*
 * kfabric-specific error codes
 * These are offset from standard errno values to avoid conflicts.
 * The actual kfabric implementation may define these differently;
 * this provides compatibility definitions.
 */

#define KFI_ERRNO_OFFSET    256

#define KFI_SUCCESS         0
#define KFI_EAGAIN          (KFI_ERRNO_OFFSET + 1)   /* Resource temporarily unavailable */
#define KFI_EACCES          (KFI_ERRNO_OFFSET + 2)   /* Permission denied */
#define KFI_ECANCELED       (KFI_ERRNO_OFFSET + 3)   /* Operation canceled */
#define KFI_EINVAL          (KFI_ERRNO_OFFSET + 4)   /* Invalid argument */
#define KFI_ENOMEM          (KFI_ERRNO_OFFSET + 5)   /* Out of memory */
#define KFI_ENODATA         (KFI_ERRNO_OFFSET + 6)   /* No data available */
#define KFI_EMSGSIZE        (KFI_ERRNO_OFFSET + 7)   /* Message too long */
#define KFI_ENOSYS          (KFI_ERRNO_OFFSET + 8)   /* Function not implemented */
#define KFI_ENOENT          (KFI_ERRNO_OFFSET + 9)   /* No such entry */
#define KFI_EBUSY           (KFI_ERRNO_OFFSET + 10)  /* Device or resource busy */
#define KFI_ENETDOWN        (KFI_ERRNO_OFFSET + 11)  /* Network is down */
#define KFI_ENETUNREACH     (KFI_ERRNO_OFFSET + 12)  /* Network is unreachable */
#define KFI_ECONNREFUSED    (KFI_ERRNO_OFFSET + 13)  /* Connection refused */
#define KFI_ECONNRESET      (KFI_ERRNO_OFFSET + 14)  /* Connection reset by peer */
#define KFI_ETIMEDOUT       (KFI_ERRNO_OFFSET + 15)  /* Connection timed out */
#define KFI_ENOTCONN        (KFI_ERRNO_OFFSET + 16)  /* Transport endpoint not connected */

/* Provider-specific error codes (CXI) */
#define KFI_ERRNO_PROV_OFFSET   512

#define KKFI_ETRUNC         (KFI_ERRNO_PROV_OFFSET + 1)  /* Message truncated */
#define KFI_EOVERRUN        (KFI_ERRNO_PROV_OFFSET + 2)  /* Queue overrun */
#define KFI_EOTHER          (KFI_ERRNO_PROV_OFFSET + 3)  /* Unspecified error */

#endif /* _KFI_ERRNO_H */
