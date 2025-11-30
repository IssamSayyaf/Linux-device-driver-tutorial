# Character Device Drivers

## Overview

Character devices are the most common type of Linux device driver. They transfer data character by character (byte by byte) and appear as files in `/dev/`. This chapter covers creating, registering, and implementing character device drivers.

---

## Table of Contents

1. [Character Device Fundamentals](#character-device-fundamentals)
2. [Device Numbers](#device-numbers)
3. [File Operations](#file-operations)
4. [Device Registration](#device-registration)
5. [Complete Example](#complete-example)
6. [IOCTL Interface](#ioctl-interface)
7. [Poll and Select](#poll-and-select)
8. [Advanced Topics](#advanced-topics)

---

## Character Device Fundamentals

### What is a Character Device?
```
+------------------------------------------------------------------+
|                    CHARACTER DEVICE FLOW                          |
+------------------------------------------------------------------+
|                                                                   |
|    Userspace                     Kernel Space                     |
|    ---------                     ------------                     |
|                                                                   |
|    Application                   Character Driver                 |
|    +----------+                  +--------------+                 |
|    |  open()  | ──────────────▶ |  .open()     |                 |
|    |  read()  | ──────────────▶ |  .read()     |                 |
|    |  write() | ──────────────▶ |  .write()    |                 |
|    |  ioctl() | ──────────────▶ |  .ioctl()    |                 |
|    |  close() | ──────────────▶ |  .release()  |                 |
|    +----------+                  +--------------+                 |
|         │                              │                          |
|         ▼                              ▼                          |
|    /dev/mydev                    Hardware/Resource                |
|                                                                   |
+------------------------------------------------------------------+
```

### Character vs Block Devices
| Feature | Character Device | Block Device |
|---------|-----------------|--------------|
| Access | Byte by byte | Block by block |
| Buffering | None (usually) | Yes |
| Seeking | Optional | Yes |
| Examples | Serial, GPIO, sensors | Disks, USB storage |
| Major/Minor | Yes | Yes |
| File ops | Full control | Uses block layer |

---

## Device Numbers

### Major and Minor Numbers
```c
#include <linux/types.h>
#include <linux/kdev_t.h>

/*
 * Device numbers are 32-bit values:
 * - Major number (12 bits): Identifies the driver
 * - Minor number (20 bits): Identifies specific device
 *
 * dev_t is the type for device numbers
 */

dev_t devno;

/* Create device number from major/minor */
devno = MKDEV(major, minor);

/* Extract major number */
int major = MAJOR(devno);

/* Extract minor number */
int minor = MINOR(devno);
```

### Allocating Device Numbers
```c
#include <linux/fs.h>

/* Static allocation (deprecated) */
int register_chrdev_region(dev_t first, unsigned count, const char *name);

/* Dynamic allocation (recommended) */
int alloc_chrdev_region(dev_t *dev, unsigned baseminor,
                        unsigned count, const char *name);

/* Free device numbers */
void unregister_chrdev_region(dev_t first, unsigned count);
```

### Example: Device Number Allocation
```c
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>

MODULE_LICENSE("GPL");

static dev_t dev_num;
static unsigned int num_devices = 1;

static int __init devnum_init(void)
{
    int ret;

    /* Request dynamic device number */
    ret = alloc_chrdev_region(&dev_num, 0, num_devices, "mydevice");
    if (ret < 0) {
        pr_err("Failed to allocate device number\n");
        return ret;
    }

    pr_info("Device registered: major=%d, minor=%d\n",
            MAJOR(dev_num), MINOR(dev_num));

    return 0;
}

static void __exit devnum_exit(void)
{
    unregister_chrdev_region(dev_num, num_devices);
    pr_info("Device unregistered\n");
}

module_init(devnum_init);
module_exit(devnum_exit);
```

---

## File Operations

### struct file_operations
```c
#include <linux/fs.h>

/*
 * File operations structure - defines driver's interface
 * Each function pointer corresponds to a system call
 */
struct file_operations {
    struct module *owner;           /* Prevents module unload while in use */
    loff_t (*llseek)(struct file *, loff_t, int);
    ssize_t (*read)(struct file *, char __user *, size_t, loff_t *);
    ssize_t (*write)(struct file *, const char __user *, size_t, loff_t *);
    int (*open)(struct inode *, struct file *);
    int (*release)(struct inode *, struct file *);
    long (*unlocked_ioctl)(struct file *, unsigned int, unsigned long);
    int (*mmap)(struct file *, struct vm_area_struct *);
    unsigned int (*poll)(struct file *, struct poll_table_struct *);
    int (*fasync)(int, struct file *, int);
    /* ... more operations ... */
};
```

### Implementing File Operations

#### open
```c
/*
 * Called when device file is opened
 * - Initialize device if needed
 * - Allocate per-file resources
 * - Store private data
 */
static int my_open(struct inode *inode, struct file *file)
{
    struct my_device *dev;

    /* Get device structure from inode */
    dev = container_of(inode->i_cdev, struct my_device, cdev);

    /* Store in private_data for other operations */
    file->private_data = dev;

    /* Check access mode */
    if ((file->f_flags & O_ACCMODE) == O_WRONLY) {
        /* Write-only access */
    }

    pr_info("Device opened\n");
    return 0;
}
```

#### release
```c
/*
 * Called when the last reference to the file is closed
 * - Free per-file resources
 * - Shutdown device if needed
 */
static int my_release(struct inode *inode, struct file *file)
{
    pr_info("Device closed\n");
    return 0;
}
```

#### read
```c
#include <linux/uaccess.h>

/*
 * Read data from device to user space
 * - file: File structure
 * - buf: User space buffer (must use copy_to_user)
 * - count: Requested bytes
 * - offset: Current file position
 * Return: Bytes read, 0 for EOF, negative for error
 */
static ssize_t my_read(struct file *file, char __user *buf,
                       size_t count, loff_t *offset)
{
    struct my_device *dev = file->private_data;
    ssize_t bytes_read = 0;
    size_t available;

    /* Check bounds */
    if (*offset >= dev->size)
        return 0;  /* EOF */

    available = dev->size - *offset;
    if (count > available)
        count = available;

    /* Copy data to user space */
    if (copy_to_user(buf, dev->buffer + *offset, count))
        return -EFAULT;

    *offset += count;
    bytes_read = count;

    pr_info("Read %zu bytes from offset %lld\n", count, *offset - count);
    return bytes_read;
}
```

#### write
```c
/*
 * Write data from user space to device
 * - Must use copy_from_user for user buffers
 * Return: Bytes written, negative for error
 */
static ssize_t my_write(struct file *file, const char __user *buf,
                        size_t count, loff_t *offset)
{
    struct my_device *dev = file->private_data;
    ssize_t bytes_written = 0;
    size_t space_available;

    /* Check bounds */
    if (*offset >= dev->size)
        return -ENOSPC;

    space_available = dev->size - *offset;
    if (count > space_available)
        count = space_available;

    /* Copy data from user space */
    if (copy_from_user(dev->buffer + *offset, buf, count))
        return -EFAULT;

    *offset += count;
    bytes_written = count;

    pr_info("Wrote %zu bytes at offset %lld\n", count, *offset - count);
    return bytes_written;
}
```

#### llseek
```c
/*
 * Change file position
 * - whence: SEEK_SET, SEEK_CUR, SEEK_END
 * Return: New position, negative for error
 */
static loff_t my_llseek(struct file *file, loff_t offset, int whence)
{
    struct my_device *dev = file->private_data;
    loff_t newpos;

    switch (whence) {
    case SEEK_SET:
        newpos = offset;
        break;
    case SEEK_CUR:
        newpos = file->f_pos + offset;
        break;
    case SEEK_END:
        newpos = dev->size + offset;
        break;
    default:
        return -EINVAL;
    }

    if (newpos < 0 || newpos > dev->size)
        return -EINVAL;

    file->f_pos = newpos;
    return newpos;
}
```

---

## Device Registration

### The cdev Structure
```c
#include <linux/cdev.h>

/*
 * cdev represents a character device in the kernel
 * Links device numbers to file operations
 */
struct cdev {
    struct kobject kobj;
    struct module *owner;
    const struct file_operations *ops;
    struct list_head list;
    dev_t dev;
    unsigned int count;
};

/* Initialize cdev structure */
void cdev_init(struct cdev *cdev, const struct file_operations *fops);

/* Add cdev to system */
int cdev_add(struct cdev *cdev, dev_t dev, unsigned count);

/* Remove cdev from system */
void cdev_del(struct cdev *cdev);
```

### Creating Device Files Automatically
```c
#include <linux/device.h>

/*
 * class and device structures create entries in sysfs
 * and trigger udev to create device files in /dev
 */

/* Create device class */
struct class *class_create(struct module *owner, const char *name);

/* Destroy device class */
void class_destroy(struct class *cls);

/* Create device (creates /dev entry via udev) */
struct device *device_create(struct class *cls, struct device *parent,
                             dev_t devt, void *drvdata, const char *fmt, ...);

/* Destroy device */
void device_destroy(struct class *cls, dev_t devt);
```

---

## Complete Example

### Full Character Device Driver

**File: `simple_char.c`**
```c
/*
 * simple_char.c - A simple character device driver
 *
 * This driver creates a memory buffer that can be read/written
 * from userspace, demonstrating core chardev concepts.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/mutex.h>

#define DEVICE_NAME "simple_char"
#define CLASS_NAME  "simple"
#define BUFFER_SIZE 4096

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Simple character device driver");
MODULE_VERSION("1.0");

/* Device structure */
struct simple_char_dev {
    char *buffer;
    size_t size;
    size_t data_len;
    struct cdev cdev;
    struct mutex lock;
};

/* Global variables */
static dev_t dev_num;
static struct class *dev_class;
static struct simple_char_dev *simple_dev;

/* File operations */
static int simple_open(struct inode *inode, struct file *file)
{
    struct simple_char_dev *dev;

    dev = container_of(inode->i_cdev, struct simple_char_dev, cdev);
    file->private_data = dev;

    pr_info("%s: Device opened\n", DEVICE_NAME);
    return 0;
}

static int simple_release(struct inode *inode, struct file *file)
{
    pr_info("%s: Device closed\n", DEVICE_NAME);
    return 0;
}

static ssize_t simple_read(struct file *file, char __user *buf,
                           size_t count, loff_t *offset)
{
    struct simple_char_dev *dev = file->private_data;
    ssize_t ret;
    size_t available;

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    /* Check if we're past end of data */
    if (*offset >= dev->data_len) {
        ret = 0;  /* EOF */
        goto out;
    }

    /* Limit read to available data */
    available = dev->data_len - *offset;
    if (count > available)
        count = available;

    if (copy_to_user(buf, dev->buffer + *offset, count)) {
        ret = -EFAULT;
        goto out;
    }

    *offset += count;
    ret = count;

    pr_info("%s: Read %zu bytes\n", DEVICE_NAME, count);

out:
    mutex_unlock(&dev->lock);
    return ret;
}

static ssize_t simple_write(struct file *file, const char __user *buf,
                            size_t count, loff_t *offset)
{
    struct simple_char_dev *dev = file->private_data;
    ssize_t ret;

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    /* Check buffer space */
    if (*offset >= dev->size) {
        ret = -ENOSPC;
        goto out;
    }

    if (count > dev->size - *offset)
        count = dev->size - *offset;

    if (copy_from_user(dev->buffer + *offset, buf, count)) {
        ret = -EFAULT;
        goto out;
    }

    *offset += count;

    /* Update data length */
    if (*offset > dev->data_len)
        dev->data_len = *offset;

    ret = count;

    pr_info("%s: Wrote %zu bytes\n", DEVICE_NAME, count);

out:
    mutex_unlock(&dev->lock);
    return ret;
}

static loff_t simple_llseek(struct file *file, loff_t offset, int whence)
{
    struct simple_char_dev *dev = file->private_data;
    loff_t newpos;

    switch (whence) {
    case SEEK_SET:
        newpos = offset;
        break;
    case SEEK_CUR:
        newpos = file->f_pos + offset;
        break;
    case SEEK_END:
        newpos = dev->data_len + offset;
        break;
    default:
        return -EINVAL;
    }

    if (newpos < 0 || newpos > dev->size)
        return -EINVAL;

    file->f_pos = newpos;
    return newpos;
}

/* File operations structure */
static const struct file_operations simple_fops = {
    .owner   = THIS_MODULE,
    .open    = simple_open,
    .release = simple_release,
    .read    = simple_read,
    .write   = simple_write,
    .llseek  = simple_llseek,
};

/* Module initialization */
static int __init simple_char_init(void)
{
    int ret;

    pr_info("%s: Initializing driver\n", DEVICE_NAME);

    /* Allocate device structure */
    simple_dev = kzalloc(sizeof(*simple_dev), GFP_KERNEL);
    if (!simple_dev) {
        pr_err("%s: Failed to allocate device structure\n", DEVICE_NAME);
        return -ENOMEM;
    }

    /* Allocate buffer */
    simple_dev->buffer = kzalloc(BUFFER_SIZE, GFP_KERNEL);
    if (!simple_dev->buffer) {
        pr_err("%s: Failed to allocate buffer\n", DEVICE_NAME);
        ret = -ENOMEM;
        goto err_free_dev;
    }
    simple_dev->size = BUFFER_SIZE;
    simple_dev->data_len = 0;

    /* Initialize mutex */
    mutex_init(&simple_dev->lock);

    /* Allocate device number */
    ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        pr_err("%s: Failed to allocate device number\n", DEVICE_NAME);
        goto err_free_buffer;
    }
    pr_info("%s: Registered with major=%d, minor=%d\n",
            DEVICE_NAME, MAJOR(dev_num), MINOR(dev_num));

    /* Initialize and add cdev */
    cdev_init(&simple_dev->cdev, &simple_fops);
    simple_dev->cdev.owner = THIS_MODULE;

    ret = cdev_add(&simple_dev->cdev, dev_num, 1);
    if (ret < 0) {
        pr_err("%s: Failed to add cdev\n", DEVICE_NAME);
        goto err_unregister;
    }

    /* Create device class */
    dev_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(dev_class)) {
        pr_err("%s: Failed to create class\n", DEVICE_NAME);
        ret = PTR_ERR(dev_class);
        goto err_del_cdev;
    }

    /* Create device file */
    if (IS_ERR(device_create(dev_class, NULL, dev_num, NULL, DEVICE_NAME))) {
        pr_err("%s: Failed to create device\n", DEVICE_NAME);
        ret = -EINVAL;
        goto err_destroy_class;
    }

    pr_info("%s: Driver initialized successfully\n", DEVICE_NAME);
    return 0;

err_destroy_class:
    class_destroy(dev_class);
err_del_cdev:
    cdev_del(&simple_dev->cdev);
err_unregister:
    unregister_chrdev_region(dev_num, 1);
err_free_buffer:
    kfree(simple_dev->buffer);
err_free_dev:
    kfree(simple_dev);
    return ret;
}

/* Module cleanup */
static void __exit simple_char_exit(void)
{
    device_destroy(dev_class, dev_num);
    class_destroy(dev_class);
    cdev_del(&simple_dev->cdev);
    unregister_chrdev_region(dev_num, 1);
    kfree(simple_dev->buffer);
    kfree(simple_dev);

    pr_info("%s: Driver removed\n", DEVICE_NAME);
}

module_init(simple_char_init);
module_exit(simple_char_exit);
```

### Testing the Driver
```bash
# Build and load
make
sudo insmod simple_char.ko

# Check device was created
ls -la /dev/simple_char

# Write to device
echo "Hello, Kernel!" > /dev/simple_char

# Read from device
cat /dev/simple_char

# Use dd for precise operations
dd if=/dev/simple_char bs=5 count=1
echo "Test data" | dd of=/dev/simple_char bs=1

# Unload
sudo rmmod simple_char
```

---

## IOCTL Interface

### Defining IOCTL Commands
```c
#include <linux/ioctl.h>

/*
 * IOCTL command encoding:
 * _IO(type, nr)           - No data transfer
 * _IOR(type, nr, size)    - Read from driver
 * _IOW(type, nr, size)    - Write to driver
 * _IOWR(type, nr, size)   - Read and write
 *
 * type: Magic number (unique per driver)
 * nr: Command number
 * size: Size of data structure
 */

#define SIMPLE_IOC_MAGIC  'S'

#define SIMPLE_IOC_RESET     _IO(SIMPLE_IOC_MAGIC, 0)
#define SIMPLE_IOC_GET_SIZE  _IOR(SIMPLE_IOC_MAGIC, 1, size_t)
#define SIMPLE_IOC_SET_SIZE  _IOW(SIMPLE_IOC_MAGIC, 2, size_t)
#define SIMPLE_IOC_GET_INFO  _IOR(SIMPLE_IOC_MAGIC, 3, struct device_info)

struct device_info {
    size_t buffer_size;
    size_t data_len;
    unsigned int access_count;
};
```

### Implementing IOCTL Handler
```c
static long simple_ioctl(struct file *file, unsigned int cmd,
                         unsigned long arg)
{
    struct simple_char_dev *dev = file->private_data;
    struct device_info info;
    size_t new_size;
    int ret = 0;

    /* Validate command */
    if (_IOC_TYPE(cmd) != SIMPLE_IOC_MAGIC)
        return -ENOTTY;

    switch (cmd) {
    case SIMPLE_IOC_RESET:
        mutex_lock(&dev->lock);
        memset(dev->buffer, 0, dev->size);
        dev->data_len = 0;
        mutex_unlock(&dev->lock);
        pr_info("Buffer reset\n");
        break;

    case SIMPLE_IOC_GET_SIZE:
        if (copy_to_user((size_t __user *)arg, &dev->size, sizeof(size_t)))
            return -EFAULT;
        break;

    case SIMPLE_IOC_SET_SIZE:
        if (copy_from_user(&new_size, (size_t __user *)arg, sizeof(size_t)))
            return -EFAULT;

        /* Validate and resize (simplified) */
        if (new_size > MAX_BUFFER_SIZE)
            return -EINVAL;

        /* Would need to reallocate buffer here */
        pr_info("Set size to %zu (not implemented)\n", new_size);
        break;

    case SIMPLE_IOC_GET_INFO:
        info.buffer_size = dev->size;
        info.data_len = dev->data_len;
        info.access_count = dev->access_count;

        if (copy_to_user((struct device_info __user *)arg,
                         &info, sizeof(info)))
            return -EFAULT;
        break;

    default:
        return -ENOTTY;
    }

    return ret;
}

/* Add to file_operations */
static const struct file_operations simple_fops = {
    /* ... */
    .unlocked_ioctl = simple_ioctl,
#ifdef CONFIG_COMPAT
    .compat_ioctl = simple_ioctl,  /* For 32-bit userspace on 64-bit kernel */
#endif
};
```

### Userspace IOCTL Usage
```c
/* userspace_app.c */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "simple_ioctl.h"  /* Header with IOCTL definitions */

int main(void)
{
    int fd;
    size_t size;
    struct device_info info;

    fd = open("/dev/simple_char", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    /* Reset buffer */
    if (ioctl(fd, SIMPLE_IOC_RESET) < 0)
        perror("ioctl RESET");

    /* Get buffer size */
    if (ioctl(fd, SIMPLE_IOC_GET_SIZE, &size) == 0)
        printf("Buffer size: %zu\n", size);

    /* Get device info */
    if (ioctl(fd, SIMPLE_IOC_GET_INFO, &info) == 0) {
        printf("Info: size=%zu, data=%zu, access=%u\n",
               info.buffer_size, info.data_len, info.access_count);
    }

    close(fd);
    return 0;
}
```

---

## Poll and Select

### Implementing Poll
```c
#include <linux/poll.h>
#include <linux/wait.h>

struct simple_char_dev {
    /* ... */
    wait_queue_head_t read_queue;
    wait_queue_head_t write_queue;
};

/* Initialize in probe/init */
init_waitqueue_head(&dev->read_queue);
init_waitqueue_head(&dev->write_queue);

/* Poll implementation */
static unsigned int simple_poll(struct file *file,
                                struct poll_table_struct *wait)
{
    struct simple_char_dev *dev = file->private_data;
    unsigned int mask = 0;

    mutex_lock(&dev->lock);

    /* Add wait queues to poll table */
    poll_wait(file, &dev->read_queue, wait);
    poll_wait(file, &dev->write_queue, wait);

    /* Check if data is available for reading */
    if (dev->data_len > 0)
        mask |= POLLIN | POLLRDNORM;

    /* Check if space is available for writing */
    if (dev->data_len < dev->size)
        mask |= POLLOUT | POLLWRNORM;

    mutex_unlock(&dev->lock);

    return mask;
}

/* Wake up waiting processes */
/* After writing data: */
wake_up_interruptible(&dev->read_queue);

/* After reading data: */
wake_up_interruptible(&dev->write_queue);
```

### Userspace Poll Usage
```c
#include <poll.h>

int main(void)
{
    struct pollfd fds[1];
    int ret;

    fds[0].fd = open("/dev/simple_char", O_RDWR);
    fds[0].events = POLLIN | POLLOUT;

    while (1) {
        ret = poll(fds, 1, 1000);  /* 1 second timeout */

        if (ret < 0) {
            perror("poll");
            break;
        }

        if (ret == 0) {
            printf("Timeout\n");
            continue;
        }

        if (fds[0].revents & POLLIN)
            printf("Data available for reading\n");

        if (fds[0].revents & POLLOUT)
            printf("Space available for writing\n");
    }

    close(fds[0].fd);
    return 0;
}
```

---

## Advanced Topics

### Blocking I/O
```c
static ssize_t blocking_read(struct file *file, char __user *buf,
                             size_t count, loff_t *offset)
{
    struct simple_char_dev *dev = file->private_data;
    ssize_t ret;

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    /* Wait for data */
    while (dev->data_len == 0) {
        mutex_unlock(&dev->lock);

        /* Non-blocking mode */
        if (file->f_flags & O_NONBLOCK)
            return -EAGAIN;

        /* Sleep until data available */
        if (wait_event_interruptible(dev->read_queue,
                                     dev->data_len > 0))
            return -ERESTARTSYS;

        if (mutex_lock_interruptible(&dev->lock))
            return -ERESTARTSYS;
    }

    /* Read data (same as before) */
    /* ... */

    mutex_unlock(&dev->lock);
    return ret;
}
```

### Memory Mapping (mmap)
```c
static int simple_mmap(struct file *file, struct vm_area_struct *vma)
{
    struct simple_char_dev *dev = file->private_data;
    unsigned long size = vma->vm_end - vma->vm_start;

    if (size > dev->size)
        return -EINVAL;

    /* Map kernel buffer to userspace */
    if (remap_pfn_range(vma, vma->vm_start,
                        virt_to_phys(dev->buffer) >> PAGE_SHIFT,
                        size, vma->vm_page_prot))
        return -EAGAIN;

    return 0;
}
```

### Fasync (Asynchronous Notification)
```c
#include <linux/fs.h>

struct simple_char_dev {
    /* ... */
    struct fasync_struct *async_queue;
};

static int simple_fasync(int fd, struct file *file, int mode)
{
    struct simple_char_dev *dev = file->private_data;
    return fasync_helper(fd, file, mode, &dev->async_queue);
}

/* Signal userspace when data is ready */
if (dev->async_queue)
    kill_fasync(&dev->async_queue, SIGIO, POLL_IN);

/* In release */
simple_fasync(-1, file, 0);
```

---

## Summary

### Registration Sequence
```
1. alloc_chrdev_region() - Get device numbers
2. cdev_init() - Initialize cdev structure
3. cdev_add() - Add cdev to kernel
4. class_create() - Create device class
5. device_create() - Create device file
```

### Cleanup Sequence (reverse order)
```
1. device_destroy() - Remove device file
2. class_destroy() - Remove device class
3. cdev_del() - Remove cdev from kernel
4. unregister_chrdev_region() - Release device numbers
```

### Best Practices
1. Always use `copy_to_user()` / `copy_from_user()` for userspace buffers
2. Protect shared data with mutexes or spinlocks
3. Check return values of all functions
4. Clean up resources in reverse order of allocation
5. Use `container_of()` to get device structure from cdev
6. Implement proper error handling with goto cleanup

---

**Previous:** [Kernel Module Basics](01-kernel-module-basics.md) | **Next:** [Memory Management](03-memory-management.md)
