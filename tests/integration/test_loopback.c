/*
 * Integration test - NFS loopback over kfabric
 *
 * NOTE: This test requires a properly configured environment:
 *   - NFS server running on localhost
 *   - Export configured and mounted
 *   - kfabric module loaded
 *   - VNI allocated (if using CXI)
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include "kfi_verbs_compat.h"
#include "kfi_internal.h"

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("NFS loopback integration tests");

#define TEST_MOUNT_POINT "/mnt/nfs_kfi_test"
#define TEST_EXPORT "/export/test"
#define TEST_FILE "testfile.txt"

/* Check if test environment is ready */
static int test_environment_check(void)
{
    struct path path;
    int ret;
    
    pr_info("TEST: Environment check\n");

    /* Check if mount point exists */
    ret = kern_path(TEST_MOUNT_POINT, LOOKUP_DIRECTORY, &path);
    if (ret) {
        pr_warn("  Mount point %s not found (error %d)\n", TEST_MOUNT_POINT, ret);
        pr_warn("  Skipping integration tests - environment not configured\n");
        return -ENOENT;
    }
    path_put(&path);

    pr_info("  Mount point exists: %s\n", TEST_MOUNT_POINT);
    pr_info("PASS: Environment check\n");
    return 0;
}

/* Test basic file operations */
static int test_file_create(void)
{
    struct file *file;
    char path_buf[256];

    pr_info("TEST: File creation\n");

    snprintf(path_buf, sizeof(path_buf), "%s/%s", TEST_MOUNT_POINT, TEST_FILE);

    file = filp_open(path_buf, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (IS_ERR(file)) {
        pr_err("FAIL: filp_open failed: %ld\n", PTR_ERR(file));
        return -1;
    }

    filp_close(file, NULL);

    pr_info("PASS: File creation\n");
    return 0;
}

static int test_file_write(void)
{
    struct file *file;
    char path_buf[256];
    char write_buf[] = "Hello from kfabric NFS test!";
    loff_t pos = 0;
    ssize_t ret;

    pr_info("TEST: File write\n");

    snprintf(path_buf, sizeof(path_buf), "%s/%s", TEST_MOUNT_POINT, TEST_FILE);

    file = filp_open(path_buf, O_WRONLY, 0);
    if (IS_ERR(file)) {
        pr_err("FAIL: filp_open(write) failed: %ld\n", PTR_ERR(file));
        return -1;
    }
    
    ret = kernel_write(file, write_buf, sizeof(write_buf) - 1, &pos);
    filp_close(file, NULL);

    if (ret != sizeof(write_buf) - 1) {
        pr_err("FAIL: kernel_write returned %zd, expected %zu\n",
               ret, sizeof(write_buf) - 1);
        return -1;
    }
    
    pr_info("  Wrote %zd bytes\n", ret);
    pr_info("PASS: File write\n");
    return 0;
}

static int test_file_read(void)
{
    struct file *file;
    char path_buf[256];
    char read_buf[128];
    loff_t pos = 0;
    ssize_t ret;
    
    pr_info("TEST: File read\n");

    snprintf(path_buf, sizeof(path_buf), "%s/%s", TEST_MOUNT_POINT, TEST_FILE);

    file = filp_open(path_buf, O_RDONLY, 0);
    if (IS_ERR(file)) {
        pr_err("FAIL: filp_open(read) failed: %ld\n", PTR_ERR(file));
        return -1;
    }
    
    memset(read_buf, 0, sizeof(read_buf));
    ret = kernel_read(file, read_buf, sizeof(read_buf) - 1, &pos);
    filp_close(file, NULL);

    if (ret < 0) {
        pr_err("FAIL: kernel_read returned %zd\n", ret);
        return -1;
    }
    
    pr_info("  Read %zd bytes: '%s'\n", ret, read_buf);
    pr_info("PASS: File read\n");
    return 0;
}

static int test_file_verify(void)
{
    struct file *file;
    char path_buf[256];
    char expected[] = "Hello from kfabric NFS test!";
    char read_buf[128];
    loff_t pos = 0;
    ssize_t ret;

    pr_info("TEST: Data verification\n");

    snprintf(path_buf, sizeof(path_buf), "%s/%s", TEST_MOUNT_POINT, TEST_FILE);

    file = filp_open(path_buf, O_RDONLY, 0);
    if (IS_ERR(file)) {
        pr_err("FAIL: filp_open failed: %ld\n", PTR_ERR(file));
        return -1;
    }
    
    memset(read_buf, 0, sizeof(read_buf));
    ret = kernel_read(file, read_buf, sizeof(expected) - 1, &pos);
    filp_close(file, NULL);

    if (ret != sizeof(expected) - 1) {
        pr_err("FAIL: Read size mismatch: %zd vs %zu\n", ret, sizeof(expected) - 1);
        return -1;
    }
    
    if (memcmp(expected, read_buf, sizeof(expected) - 1) != 0) {
        pr_err("FAIL: Data mismatch\n");
        pr_err("  Expected: '%s'\n", expected);
        pr_err("  Got:      '%s'\n", read_buf);
        return -1;
    }
    
    pr_info("PASS: Data verification\n");
    return 0;
}

static int __init test_loopback_init(void)
{
    int failures = 0;
    int ret;

    pr_info("=== Running NFS loopback integration tests ===\n");
    pr_info("Prerequisites:\n");
    pr_info("  - NFS server running (localhost or remote)\n");
    pr_info("  - Mount point: %s\n", TEST_MOUNT_POINT);
    pr_info("  - kfabric/xprtrdma_kfi modules loaded\n");

    /* Check environment first */
    ret = test_environment_check();
    if (ret) {
        pr_info("=== Integration tests SKIPPED (environment not ready) ===\n");
        return -EAGAIN;
    }

    if (test_file_create())
        failures++;
    if (test_file_write())
        failures++;
    if (test_file_read())
        failures++;
    if (test_file_verify())
        failures++;

    pr_info("=== Loopback tests: %d failures ===\n", failures);

    /* Return error to prevent module staying loaded */
    return failures ? -EINVAL : -EAGAIN;
}

static void __exit test_loopback_exit(void)
{
    pr_info("Loopback tests unloaded\n");
}

module_init(test_loopback_init);
module_exit(test_loopback_exit);
