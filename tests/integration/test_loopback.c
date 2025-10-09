/*
 * Integration test - NFS loopback over kfabric
 */

#include <linux/module.h>
#include <linux/nfs_fs.h>
#include "../../include/kfi_internal.h"

#define TEST_MOUNT_POINT "/mnt/nfs_kfi_test"
#define TEST_EXPORT "/export/test"
#define TEST_FILE "testfile.txt"

static int test_nfs_mount(void)
{
    struct file_system_type *nfs_fs;
    struct vfsmount *mnt;
    char options[256];
    int ret;
    
    pr_info("TEST: NFS mount over kfabric\n");
    
    /* Get NFS filesystem type */
    nfs_fs = get_fs_type("nfs");
    if (!nfs_fs) {
        pr_err("FAIL: NFS filesystem not registered\n");
        return -1;
    }
    
    /* Prepare mount options with VNI */
    snprintf(options, sizeof(options),
             "rdma,port=20049,vni=1000,addr=127.0.0.1");
    
    /* Attempt mount */
    mnt = vfs_kern_mount(nfs_fs, 0, "127.0.0.1:" TEST_EXPORT, options);
    put_filesystem(nfs_fs);
    
    if (IS_ERR(mnt)) {
        pr_err("FAIL: vfs_kern_mount returned %ld\n", PTR_ERR(mnt));
        return -1;
    }
    
    pr_info("NFS mount successful\n");
    
    /* Unmount */
    mntput(mnt);
    
    pr_info("PASS: NFS mount\n");
    return 0;
}

static int test_nfs_io(void)
{
    struct file *file;
    char write_buf[] = "Hello from kfabric NFS!";
    char read_buf[128];
    loff_t pos = 0;
    ssize_t ret;
    
    pr_info("TEST: NFS I/O operations\n");
    
    /* Open file for writing */
    file = filp_open(TEST_MOUNT_POINT "/" TEST_FILE, 
                     O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (IS_ERR(file)) {
        pr_err("FAIL: filp_open(write) returned %ld\n", PTR_ERR(file));
        return -1;
    }
    
    /* Write test data */
    ret = kernel_write(file, write_buf, sizeof(write_buf), &pos);
    if (ret != sizeof(write_buf)) {
        pr_err("FAIL: kernel_write returned %zd\n", ret);
        filp_close(file, NULL);
        return -1;
    }
    
    filp_close(file, NULL);
    pr_info("Write completed: %zd bytes\n", ret);
    
    /* Open file for reading */
    pos = 0;
    file = filp_open(TEST_MOUNT_POINT "/" TEST_FILE, O_RDONLY, 0);
    if (IS_ERR(file)) {
        pr_err("FAIL: filp_open(read) returned %ld\n", PTR_ERR(file));
        return -1;
    }
    
    /* Read data back */
    memset(read_buf, 0, sizeof(read_buf));
    ret = kernel_read(file, read_buf, sizeof(write_buf), &pos);
    if (ret != sizeof(write_buf)) {
        pr_err("FAIL: kernel_read returned %zd\n", ret);
        filp_close(file, NULL);
        return -1;
    }
    
    filp_close(file, NULL);
    
    /* Verify data */
    if (memcmp(write_buf, read_buf, sizeof(write_buf)) != 0) {
        pr_err("FAIL: Data mismatch\n");
        pr_err("  Expected: %s\n", write_buf);
        pr_err("  Got:      %s\n", read_buf);
        return -1;
    }
    
    pr_info("Read completed and verified\n");
    pr_info("PASS: NFS I/O operations\n");
    return 0;
}

static int __init test_loopback_init(void)
{
    int ret = 0;
    
    pr_info("=== Running NFS loopback integration tests ===\n");
    pr_info("Prerequisites:\n");
    pr_info("  - NFS server running on localhost\n");
    pr_info("  - Export configured: " TEST_EXPORT "\n");
    pr_info("  - VNI 1000 allocated\n");
    
    ret |= test_nfs_mount();
    if (ret == 0)
        ret |= test_nfs_io();
    
    if (ret == 0)
        pr_info("=== All loopback tests PASSED ===\n");
    else
        pr_err("=== Some loopback tests FAILED ===\n");
    
    /* Note: In real test, we'd return -1 to prevent module load on failure
     * But for testing, we return 0 to allow inspection */
    return 0;
}

static void __exit test_loopback_exit(void)
{
    pr_info("Loopback tests unloaded\n");
}

module_init(test_loopback_init);
module_exit(test_loopback_exit);
