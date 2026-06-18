#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/device.h>
#include <linux/device/class.h>
#include <linux/fs.h>
#include <linux/poll.h>

MODULE_AUTHOR("Yousef Abbas <yousef.abbas.2@outlook.com>");
MODULE_DESCRIPTION("GPIO event logger driver for raspberry pi zero 2w");
MODULE_LICENSE("GPL");

static int major = 0;
static struct class *gpioevt_class;
static struct device *gpioevt_device;

static int gpioevt_open(struct inode *inode, struct file *file) {
    pr_info("gpioevt: open\n");
    return 0;
}

static int gpioevt_release(struct inode *inode, struct file *file) {
    pr_info("gpioevt: release\n");
    return 0;
}

static ssize_t gpioevt_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) {
    pr_info("gpioevt: read\n");
    return 0;
}

static __poll_t gpioevt_poll(struct file *file, struct poll_table_struct *wait) {
    pr_info("gpioevt: poll\n");
    return 0;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = gpioevt_open,
    .release = gpioevt_release,
    .read = gpioevt_read,
    .poll = gpioevt_poll,
};

static int __init gpioevt_init(void)
{
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

    goto exit;

err_device:
    class_destroy(gpioevt_class);
err_class:
    unregister_chrdev(major, "gpioevt");
exit:
    return ret;
}

static void __exit gpioevt_exit(void)
{
    pr_info("gpioevt: exit\n");

    device_destroy(gpioevt_class, MKDEV(major, 0));
    class_destroy(gpioevt_class);
    unregister_chrdev(major, "gpioevt");
}

module_init(gpioevt_init);
module_exit(gpioevt_exit);
