/*
 * kfi_progress.c - Manual progress engine for CXI provider
 *
 * CHALLENGE 2 MITIGATION: CXI doesn't support FI_PROGRESS_AUTO,
 * so we need a kernel thread to drive completions.
 */

#include <linux/kthread.h>
#include <linux/sched.h>
#include "../include/kfi_internal.h"

struct kfi_progress_thread {
    struct task_struct *thread;
    struct kfi_device *device;
    atomic_t should_stop;
    wait_queue_head_t wait_queue;
};

static struct kfi_progress_thread *progress_threads[MAX_DEVICES];
static int num_progress_threads = 0;

/**
 * kfi_progress_worker - Progress thread main loop
 */
static int kfi_progress_worker(void *data)
{
    struct kfi_progress_thread *pt = data;
    struct kfi_cq_data_entry entries[16];
    ssize_t ret;

    pr_info("kfi_progress: Started for device %s\n", pt->device->name);

    while (!atomic_read(&pt->should_stop)) {
        /* Try to read completions from all CQs
         * In real implementation, track all CQs associated with device */
        
        /* Poll with short timeout to avoid busy-wait */
        ret = kfi_cq_read(pt->device->default_cq, entries, 16);
        
        if (ret > 0) {
            /* Completions available - trigger handlers */
            pr_debug("kfi_progress: Got %zd completions\n", ret);
            /* These would be processed by the CQ comp_handler */
        } else if (ret == -KFI_EAGAIN) {
            /* No completions - sleep briefly */
            usleep_range(10, 100);
        } else if (ret < 0) {
            pr_err("kfi_progress: cq_read error: %zd\n", ret);
            usleep_range(1000, 5000);
        }

        /* Check if we should yield */
        if (need_resched())
            cond_resched();
    }

    pr_info("kfi_progress: Stopped for device %s\n", pt->device->name);
    return 0;
}

/**
 * kfi_progress_start - Start progress thread for a device
 */
int kfi_progress_start(struct kfi_device *device)
{
    struct kfi_progress_thread *pt;

    if (num_progress_threads >= MAX_DEVICES)
        return -ENOMEM;

    pt = kzalloc(sizeof(*pt), GFP_KERNEL);
    if (!pt)
        return -ENOMEM;

    pt->device = device;
    atomic_set(&pt->should_stop, 0);
    init_waitqueue_head(&pt->wait_queue);

    pt->thread = kthread_create(kfi_progress_worker, pt,
                                 "kfi_prog_%s", device->name);
    if (IS_ERR(pt->thread)) {
        int ret = PTR_ERR(pt->thread);
        kfree(pt);
        return ret;
    }

    /* Pin to a specific CPU to reduce cache bouncing */
    kthread_bind(pt->thread, num_progress_threads % num_online_cpus());
    wake_up_process(pt->thread);

    progress_threads[num_progress_threads++] = pt;
    
    pr_info("kfi_progress: Started thread for %s\n", device->name);
    return 0;
}

/**
 * kfi_progress_stop - Stop progress thread
 */
void kfi_progress_stop(struct kfi_device *device)
{
    int i;

    for (i = 0; i < num_progress_threads; i++) {
        if (progress_threads[i]->device == device) {
            atomic_set(&progress_threads[i]->should_stop, 1);
            kthread_stop(progress_threads[i]->thread);
            kfree(progress_threads[i]);
            progress_threads[i] = NULL;
            pr_info("kfi_progress: Stopped thread for %s\n", device->name);
            return;
        }
    }
}

void kfi_progress_cleanup_all(void)
{
    int i;
    
    for (i = 0; i < num_progress_threads; i++) {
        if (progress_threads[i]) {
            atomic_set(&progress_threads[i]->should_stop, 1);
            kthread_stop(progress_threads[i]->thread);
            kfree(progress_threads[i]);
        }
    }
    num_progress_threads = 0;
}
