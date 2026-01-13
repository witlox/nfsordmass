#include "kfi_verbs_compat.h"
#include "kfi_internal.h"
#include <linux/limits.h>
/* CXI driver headers would be needed for production CXI-specific features */
/* #include <linux/cxi/cxi.h> */

/*
 * Handle CXI VNI (Virtual Network Identifier) authentication
 * This is critical - CXI requires proper auth keys for isolation
 */

/**
 * kfi_query_default_vni - Query default VNI from CXI service
 * @vni: Pointer to store the VNI value
 *
 * Returns: 0 on success, negative error on failure
 *
 * TODO: In production, this should query the CXI service for the default VNI.
 * For now, returns KFI_DEFAULT_VNI.
 */
int kfi_query_default_vni(uint16_t *vni)
{
    if (!vni)
        return -EINVAL;

    *vni = KFI_DEFAULT_VNI;
    pr_debug("kfi_query_default_vni: returning default VNI %u\n", *vni);
    return 0;
}

/*
 * Create connection with proper CXI addressing
 */
int kfi_connect_ep(struct kfi_qp *kqp, struct sockaddr *remote_addr)
{
    struct kfi_av_attr av_attr = {
        .type = KFI_AV_TABLE,
        .count = 1,
    };
    struct kfid_av *av;
    kfi_addr_t fi_addr;
    int ret;
    
    /* Get authentication credentials */
    ret = kfi_get_auth_key(kqp);
    if (ret)
        return ret;
        
    /* Set up address vector for this connection */
    ret = kfi_av_open(kqp->pd->kfi_domain, &av_attr, &av, NULL);
    if (ret) {
        pr_err("kfi_av_open failed: %d\n", ret);
        return ret;
    }
    
    /* Insert remote address */
    ret = kfi_av_insert(av, remote_addr, 1, &fi_addr, 0, NULL);
    if (ret != 1) {
        pr_err("kfi_av_insert failed: %d\n", ret);
        kfi_close(&av->fid);
        return -EINVAL;
    }
    
    /* Bind endpoint to AV */
    ret = kfi_ep_bind(kqp->ep, &av->fid, 0);
    if (ret) {
        pr_err("kfi_ep_bind(av) failed: %d\n", ret);
        kfi_close(&av->fid);
        return ret;
    }
    
    /* Enable endpoint */
    ret = kfi_enable(kqp->ep);
    if (ret) {
        pr_err("kfi_enable failed: %d\n", ret);
        return ret;
    }
    
    kqp->state = IB_QPS_RTS; /* Mark as Ready To Send */
    return 0;
}

/*
 * VNI configuration via mount options
 */

/**
 * kfi_parse_vni_from_options - Parse VNI from mount options string
 * @options: Mount options string (e.g., "rdma,port=20049,vni=1234")
 * @vni_out: Returns parsed VNI
 */
int kfi_parse_vni_from_options(const char *options, uint16_t *vni_out)
{
    char *opt_copy, *opt, *p;
    int ret = -EINVAL;

    opt_copy = kstrdup(options, GFP_KERNEL);
    if (!opt_copy)
        return -ENOMEM;

    /* Parse comma-separated options */
    opt = opt_copy;
    while ((p = strsep(&opt, ",")) != NULL) {
        char *key, *value;

        if (*p == '\0')
            continue;

        key = p;
        value = strchr(p, '=');
        if (!value)
            continue;

        *value++ = '\0';

        if (strcmp(key, "vni") == 0) {
            unsigned long vni;
            ret = kstrtoul(value, 10, &vni);
            if (ret == 0 && vni <= U16_MAX) {
                *vni_out = (uint16_t)vni;
                pr_info("kfi: Parsed VNI=%u from mount options\n", *vni_out);
            }
            break;
        }
    }

    kfree(opt_copy);
    return ret;
}

/**
 * kfi_get_auth_key - Get authentication key (tries multiple sources)
 */
int kfi_get_auth_key(struct kfi_qp *kqp)
{
    uint16_t vni;
    int ret;

    kqp->auth_key = kzalloc(sizeof(*kqp->auth_key), GFP_KERNEL);
    if (!kqp->auth_key)
        return -ENOMEM;

    /* Priority 1: Mount option (if set via transport setup) */
    if (kqp->vni_from_mount != 0) {
        kqp->auth_key->vni = kqp->vni_from_mount;
        pr_info("kfi: Using VNI %u from mount option\n", kqp->auth_key->vni);
        return 0;
    }

    /* Priority 2: SLINGSHOT_VNIS environment variable */
    /* Note: In kernel, we'd need to read from /proc/self/environ or
     * have userspace pass this via netlink/ioctl */

    /* Priority 3: Query CXI service API */
    ret = kfi_query_default_vni(&vni);
    if (ret == 0) {
        kqp->auth_key->vni = vni;
        pr_info("kfi: Using VNI %u from CXI service\n", kqp->auth_key->vni);
        return 0;
    }

    pr_err("kfi: No VNI source available - connection will fail\n");
    kfree(kqp->auth_key);
    kqp->auth_key = NULL;
    return -EACCES;
}
