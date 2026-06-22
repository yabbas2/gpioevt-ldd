#include <linux/device.h>
#include <linux/device/class.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/poll.h>
#include <linux/spinlock.h>
#include <linux/time.h>
#include <linux/wait.h>

#include "circular_buffer.h"
#include "gpio.h"
#include "common.h"

#define UNUSED(x) ((void)(x))

#define NOF_GPIOS 40

MODULE_AUTHOR("Yousef Abbas <yousef.abbas.2@outlook.com>");
MODULE_DESCRIPTION("GPIO event logger driver for raspberry pi zero 2w");
MODULE_LICENSE("GPL");

static int major = 0;
static struct class *gpioevt_class;
static struct device *gpioevt_device;
static uint log_size = CIRCULAR_BUFFER_DEFAULT_SIZE;
static uint gpios[NOF_GPIOS];
static uint nof_gpios = 0;
static circ_buf_t log;
static spinlock_t log_lock;
static DECLARE_WAIT_QUEUE_HEAD(read_wq);

module_param(log_size, uint, 0644);
MODULE_PARM_DESC(log_size, "Circular buffer (log) size to store gpio events");
module_param_array(gpios, uint, &nof_gpios, 0644);
MODULE_PARM_DESC(gpios, "List of gpios to monitor (separated by commas)");

static int gpioevt_open(struct inode *inode, struct file *file) {
    pr_info("gpioevt: open\n");
    return 0;
}

static int gpioevt_release(struct inode *inode, struct file *file) {
    pr_info("gpioevt: release\n");
    return 0;
}

static ssize_t gpioevt_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) {
    UNUSED(ppos);
    size_t copy_count;
    size_t nof_avail_entries = 0;
    int ret = 0;

    pr_info("gpioevt: read\n");
    if (count == 0) {
        return 0;
    }
    if ((count % DATA_ENTRY_SIZE) != 0) {
        pr_err("gpioevt: invalid read size\n");
        return -EINVAL;
    }

    spin_lock_irq(&log_lock);
    while (circ_buf_is_empty(&log)) {
        spin_unlock_irq(&log_lock);
        if (file->f_flags & O_NONBLOCK) {
            return -EAGAIN;
        }
        ret = wait_event_interruptible(read_wq, !circ_buf_is_empty(&log));
        if (ret != 0) {
            return ret;
        }
        spin_lock_irq(&log_lock);
    }

    nof_avail_entries = circ_buf_size(&log);
    if (count > nof_avail_entries * DATA_ENTRY_SIZE) {
        count = nof_avail_entries * DATA_ENTRY_SIZE;
    }
    copy_count = count / DATA_ENTRY_SIZE;

    void *tmp = kmalloc(count, GFP_ATOMIC);
    if (!tmp) {
        spin_unlock_irq(&log_lock);
        return -ENOMEM;
    }

    size_t offset = 0;
    while (copy_count > 0) {
        data_entry_t *entry = circ_buf_pop(&log);
        memcpy(tmp + offset, entry->serialized, DATA_ENTRY_SIZE);
        offset += DATA_ENTRY_SIZE;
        copy_count--;
    }
    spin_unlock_irq(&log_lock);

    if (copy_to_user(buf, tmp, count)) {
        pr_err("gpioevt: failed to copy to user\n");
        ret = -EFAULT;
    } else {
        ret = count;
    }
    kfree(tmp);

    return ret;
}

static __poll_t gpioevt_poll(struct file *file, struct poll_table_struct *wait) {
    __poll_t mask = 0;

    poll_wait(file, &read_wq, wait);

    spin_lock_irq(&log_lock);
    if (!circ_buf_is_empty(&log)) {
        mask |= EPOLLIN | EPOLLRDNORM;
    }
    spin_unlock_irq(&log_lock);

    return mask;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = gpioevt_open,
    .release = gpioevt_release,
    .read = gpioevt_read,
    .poll = gpioevt_poll,
};

static void log_event(data_entry_t *entry) {
    spin_lock(&log_lock);
    if (circ_buf_is_full(&log)) {
        pr_warn("gpioevt: log buffer full, dropping oldest entry\n");
    }
    circ_buf_push(&log, entry);
    spin_unlock(&log_lock);

    wake_up_interruptible(&read_wq);
}

static irqreturn_t gpioevt_irq_handler(int irq, void *dev_id) {
    int gpio = (int)(uintptr_t)dev_id;
    data_entry_t entry = {
        .deserialized.timestamp = ktime_get(),
        .deserialized.pin = gpio,
        .deserialized.direction = gpio_get_value(gpio),
    };
    log_event(&entry);
    return IRQ_HANDLED;
}

static int __init gpioevt_init(void) {
    int ret = 0;
    pr_info("gpioevt: init\n");

    // register char device
    major = register_chrdev(major, "gpioevt", &fops);
    if (major < 0) {
        pr_err("gpioevt: failed to register char device\n");
        ret = major;
        goto exit;
    }

    // create class and device
    gpioevt_class = class_create("gpioevt");
    if (IS_ERR(gpioevt_class)) {
        pr_err("gpioevt: failed to create class\n");
        ret = PTR_ERR(gpioevt_class);
        goto err_class;
    }

    gpioevt_device = device_create(gpioevt_class, NULL, MKDEV(major, 0), NULL, "gpioevt");
    if (IS_ERR(gpioevt_device)) {
        pr_err("gpioevt: failed to create device\n");
        ret = PTR_ERR(gpioevt_device);
        goto err_device;
    }

    pr_info("gpioevt: log size =%d, number of gpios =%d\n", log_size, nof_gpios);
    if (!nof_gpios || nof_gpios > NOF_GPIOS) {
        pr_err("gpioevt: invalid number of gpios\n");
        ret = -EINVAL;
        goto err_params;
    }

    // create log buffer (circular buffer)
    ret = circ_buf_init(&log, log_size);
    if (ret != 0) {
        pr_err("gpioevt: failed to create log buffer\n");
        goto err_params;
    }

    // create lock
    spin_lock_init(&log_lock);

    // init gpios and irgs
    ret = gpio_init(&gpioevt_irq_handler, gpios, nof_gpios);
    if (ret != 0) {
        goto err_gpios;
    }

    goto exit;

err_gpios:
    circ_buf_deinit(&log);
err_params:
    device_destroy(gpioevt_class, MKDEV(major, 0));
err_device:
    class_destroy(gpioevt_class);
err_class:
    unregister_chrdev(major, "gpioevt");
exit:
    return ret;
}

static void __exit gpioevt_exit(void) {
    pr_info("gpioevt: exit\n");

    gpio_deinit(gpios, nof_gpios);
    circ_buf_deinit(&log);
    device_destroy(gpioevt_class, MKDEV(major, 0));
    class_destroy(gpioevt_class);
    unregister_chrdev(major, "gpioevt");
}

module_init(gpioevt_init);
module_exit(gpioevt_exit);
