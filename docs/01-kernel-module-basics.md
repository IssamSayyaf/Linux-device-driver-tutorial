# Linux Kernel Module Basics

## Overview

This chapter covers the fundamentals of Linux kernel modules - the building blocks of all Linux device drivers. You'll learn how modules work, how to create them, and essential kernel programming concepts.

---

## Table of Contents

1. [What is a Kernel Module?](#what-is-a-kernel-module)
2. [Module Structure](#module-structure)
3. [Hello World Module](#hello-world-module)
4. [Module Parameters](#module-parameters)
5. [Kernel Symbol Export](#kernel-symbol-export)
6. [Module Dependencies](#module-dependencies)
7. [Kernel Logging](#kernel-logging)
8. [Error Handling](#error-handling)
9. [Exercises](#exercises)

---

## What is a Kernel Module?

### Definition
A kernel module is a piece of code that can be loaded and unloaded into the kernel on demand. Modules extend kernel functionality without requiring a reboot.

### Benefits
- **Dynamic loading:** Add/remove functionality at runtime
- **Reduced kernel size:** Only load what you need
- **Easy development:** Test without rebooting
- **Hot-plugging support:** Load drivers when hardware is connected

### Types of Kernel Modules
```
+----------------------------------------+
|         KERNEL MODULE TYPES            |
+----------------------------------------+
|                                        |
|  Device Drivers:                       |
|  - Character devices (/dev/tty)        |
|  - Block devices (/dev/sda)            |
|  - Network devices (eth0)              |
|                                        |
|  Filesystem Drivers:                   |
|  - ext4, btrfs, ntfs                   |
|                                        |
|  Protocol Drivers:                     |
|  - TCP/IP stack extensions             |
|  - Netfilter modules                   |
|                                        |
|  Hardware Abstraction:                 |
|  - Bus drivers (I2C, SPI, USB)         |
|  - Platform drivers                    |
|                                        |
+----------------------------------------+
```

---

## Module Structure

### Basic Module Anatomy
```c
/* Required headers */
#include <linux/init.h>      /* module_init, module_exit */
#include <linux/module.h>    /* MODULE_* macros */
#include <linux/kernel.h>    /* pr_info, pr_err, etc. */

/* Module metadata */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Module description");
MODULE_VERSION("1.0");

/* Initialization function - called when module is loaded */
static int __init my_module_init(void)
{
    pr_info("Module loaded\n");
    return 0;  /* 0 = success, negative = error */
}

/* Cleanup function - called when module is unloaded */
static void __exit my_module_exit(void)
{
    pr_info("Module unloaded\n");
}

/* Register init and exit functions */
module_init(my_module_init);
module_exit(my_module_exit);
```

### Key Concepts

#### `__init` and `__exit` Macros
```c
/* __init: Mark function as initialization code
 * - Memory is freed after init completes
 * - Only for functions called once at load time
 */
static int __init my_init(void) { ... }

/* __exit: Mark function as cleanup code
 * - Discarded if module is built into kernel
 * - Only for module unload cleanup
 */
static void __exit my_exit(void) { ... }

/* __initdata: Mark data used only during init */
static int __initdata init_value = 42;
```

#### MODULE_LICENSE
```c
/* Common license strings:
 * "GPL"           - GNU Public License v2 or later
 * "GPL v2"        - GNU Public License v2
 * "GPL and additional rights"
 * "Dual BSD/GPL"  - Choice of BSD or GPL
 * "Dual MIT/GPL"  - Choice of MIT or GPL
 * "Proprietary"   - Non-free license (taints kernel)
 */
MODULE_LICENSE("GPL");

/* Non-GPL modules:
 * - Cannot use EXPORT_SYMBOL_GPL symbols
 * - Kernel is marked as "tainted"
 * - May limit kernel developer support
 */
```

---

## Hello World Module

### Complete Example

**File: `hello_world.c`**
```c
/*
 * hello_world.c - The simplest kernel module
 *
 * This module demonstrates basic kernel module structure
 * and kernel logging facilities.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name <your.email@example.com>");
MODULE_DESCRIPTION("A simple Hello World kernel module");
MODULE_VERSION("1.0");

/*
 * hello_init - Module initialization function
 *
 * Called when the module is loaded into the kernel.
 * Return 0 on success, negative error code on failure.
 */
static int __init hello_init(void)
{
    pr_info("Hello, World! Module loaded.\n");
    pr_info("Kernel version: %s\n", UTS_RELEASE);
    return 0;
}

/*
 * hello_exit - Module cleanup function
 *
 * Called when the module is removed from the kernel.
 */
static void __exit hello_exit(void)
{
    pr_info("Goodbye, World! Module unloaded.\n");
}

module_init(hello_init);
module_exit(hello_exit);
```

### Makefile for Out-of-Tree Module

**File: `Makefile`**
```makefile
# Makefile for out-of-tree kernel module

# Module name (without .ko extension)
obj-m += hello_world.o

# For multi-file modules:
# obj-m += my_module.o
# my_module-objs := file1.o file2.o file3.o

# Kernel source directory
KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build

# Current directory
PWD := $(shell pwd)

# Default target
all: modules

# Build modules
modules:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) modules

# Clean build artifacts
clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) clean

# Install module
install:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) modules_install

# Show module info
info:
	modinfo hello_world.ko

# Load module
load:
	sudo insmod hello_world.ko

# Unload module
unload:
	sudo rmmod hello_world

# Reload module
reload: unload load

.PHONY: all modules clean install info load unload reload
```

### Build and Test
```bash
# Build the module
make

# Check module information
modinfo hello_world.ko

# Load the module
sudo insmod hello_world.ko

# Check kernel log
dmesg | tail -5

# List loaded modules
lsmod | grep hello

# Unload the module
sudo rmmod hello_world

# Check kernel log again
dmesg | tail -5
```

---

## Module Parameters

### Defining Parameters
```c
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

MODULE_LICENSE("GPL");

/* Integer parameter with default value */
static int count = 1;
module_param(count, int, 0644);
MODULE_PARM_DESC(count, "Number of iterations (default: 1)");

/* String parameter */
static char *name = "default";
module_param(name, charp, 0644);
MODULE_PARM_DESC(name, "User name (default: 'default')");

/* Boolean parameter */
static bool debug = false;
module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "Enable debug mode (default: false)");

/* Array parameter */
static int values[4] = {0, 0, 0, 0};
static int num_values = 0;
module_param_array(values, int, &num_values, 0644);
MODULE_PARM_DESC(values, "Array of values (max 4)");

static int __init param_init(void)
{
    int i;

    pr_info("Parameters received:\n");
    pr_info("  count = %d\n", count);
    pr_info("  name = %s\n", name);
    pr_info("  debug = %s\n", debug ? "enabled" : "disabled");

    pr_info("  values (%d elements):", num_values);
    for (i = 0; i < num_values; i++)
        pr_cont(" %d", values[i]);
    pr_cont("\n");

    return 0;
}

static void __exit param_exit(void)
{
    pr_info("Module with parameters unloaded\n");
}

module_init(param_init);
module_exit(param_exit);
```

### Permission Flags
```c
/*
 * Permission bits for module parameters:
 *
 * 0     - Not visible in sysfs
 * 0444  - Read-only for all
 * 0644  - Read/write for owner, read for others
 * 0600  - Read/write for owner only
 *
 * S_IRUGO = 0444 (read for all)
 * S_IWUSR = 0200 (write for owner)
 * S_IRUGO | S_IWUSR = 0644
 */

/* Parameter visible in /sys/module/modname/parameters/ */
module_param(debug, bool, S_IRUGO | S_IWUSR);

/* Modify parameter at runtime */
// echo 1 > /sys/module/modname/parameters/debug
```

### Loading with Parameters
```bash
# Load with parameters
sudo insmod hello.ko count=5 name="John" debug=1

# Or use modprobe (for installed modules)
sudo modprobe hello count=5 name="John"

# View current parameter values
cat /sys/module/hello/parameters/count
cat /sys/module/hello/parameters/name

# Modify parameter at runtime (if permissions allow)
echo 10 > /sys/module/hello/parameters/count
```

---

## Kernel Symbol Export

### Exporting Symbols
```c
/* file: math_ops.c */
#include <linux/init.h>
#include <linux/module.h>

MODULE_LICENSE("GPL");

/* Function to export */
int add_numbers(int a, int b)
{
    return a + b;
}

/* Export for all modules */
EXPORT_SYMBOL(add_numbers);

/* Export only for GPL modules */
int multiply_numbers(int a, int b)
{
    return a * b;
}
EXPORT_SYMBOL_GPL(multiply_numbers);

static int __init math_init(void)
{
    pr_info("Math operations module loaded\n");
    return 0;
}

static void __exit math_exit(void)
{
    pr_info("Math operations module unloaded\n");
}

module_init(math_init);
module_exit(math_exit);
```

### Using Exported Symbols
```c
/* file: math_user.c */
#include <linux/init.h>
#include <linux/module.h>

MODULE_LICENSE("GPL");

/* Declare external functions */
extern int add_numbers(int a, int b);
extern int multiply_numbers(int a, int b);

static int __init user_init(void)
{
    int sum = add_numbers(5, 3);
    int product = multiply_numbers(5, 3);

    pr_info("5 + 3 = %d\n", sum);
    pr_info("5 * 3 = %d\n", product);
    return 0;
}

static void __exit user_exit(void)
{
    pr_info("Math user module unloaded\n");
}

module_init(user_init);
module_exit(user_exit);
```

### Makefile for Multiple Modules
```makefile
obj-m += math_ops.o
obj-m += math_user.o

KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) clean
```

### Load Order Matters
```bash
# Load provider first
sudo insmod math_ops.ko

# Then load consumer
sudo insmod math_user.ko

# Check symbols
cat /proc/kallsyms | grep add_numbers

# Unload in reverse order
sudo rmmod math_user
sudo rmmod math_ops
```

---

## Module Dependencies

### Automatic Dependency Resolution
```bash
# Create module dependency file
sudo depmod -a

# View dependencies
modinfo math_user.ko | grep depends

# Use modprobe for automatic dependency handling
sudo modprobe math_user  # Automatically loads math_ops
```

### modules.dep
```bash
# View dependency database
cat /lib/modules/$(uname -r)/modules.dep | head -20

# Regenerate after installing modules
sudo depmod -a
```

---

## Kernel Logging

### Print Functions
```c
#include <linux/kernel.h>

/*
 * Kernel log levels (lowest number = highest priority):
 *
 * KERN_EMERG   "0" - System is unusable
 * KERN_ALERT   "1" - Action must be taken immediately
 * KERN_CRIT    "2" - Critical conditions
 * KERN_ERR     "3" - Error conditions
 * KERN_WARNING "4" - Warning conditions
 * KERN_NOTICE  "5" - Normal but significant
 * KERN_INFO    "6" - Informational
 * KERN_DEBUG   "7" - Debug messages
 */

/* Old style (deprecated) */
printk(KERN_INFO "Message with level\n");

/* Modern style - preferred */
pr_emerg("Emergency message\n");
pr_alert("Alert message\n");
pr_crit("Critical message\n");
pr_err("Error message\n");
pr_warn("Warning message\n");
pr_notice("Notice message\n");
pr_info("Info message\n");
pr_debug("Debug message\n");  /* Requires DEBUG defined or dynamic debug */

/* Continue previous message (no newline/prefix) */
pr_cont("continued...\n");

/* Device-specific logging */
#include <linux/device.h>
dev_err(&dev, "Device error: %d\n", error);
dev_warn(&dev, "Device warning\n");
dev_info(&dev, "Device info\n");
dev_dbg(&dev, "Device debug\n");
```

### Formatting
```c
/* Standard printf specifiers work */
pr_info("Integer: %d, Unsigned: %u\n", -42, 42);
pr_info("Hex: 0x%x, Pointer: %p\n", 255, ptr);
pr_info("String: %s\n", "hello");

/* Kernel-specific format specifiers */
pr_info("Physical address: %pa\n", &phys_addr);
pr_info("DMA address: %pad\n", &dma_addr);
pr_info("Resource: %pR\n", &resource);
pr_info("Symbol: %pS\n", func_ptr);  /* Function name */
pr_info("MAC address: %pM\n", mac);
pr_info("IPv4 address: %pI4\n", &ipv4);
pr_info("IPv6 address: %pI6\n", &ipv6);
pr_info("UUID: %pU\n", uuid);

/* Print buffer as hex */
print_hex_dump(KERN_INFO, "data: ", DUMP_PREFIX_OFFSET,
               16, 1, buffer, len, true);
```

### Viewing Kernel Log
```bash
# View all kernel messages
dmesg

# Follow kernel log in real-time
dmesg -w

# Clear kernel ring buffer
sudo dmesg -c

# View with timestamp
dmesg -T

# Filter by log level
dmesg --level=err,warn

# View through systemd journal
journalctl -k
journalctl -k -f  # Follow
```

### Rate Limiting
```c
/* Prevent log flooding */
#include <linux/ratelimit.h>

/* Print at most 5 messages per second */
pr_info_ratelimited("Frequent event\n");

/* Custom rate limit */
static DEFINE_RATELIMIT_STATE(my_ratelimit, HZ, 5);

if (__ratelimit(&my_ratelimit))
    pr_info("Rate limited message\n");

/* Print once only */
pr_info_once("This prints only once\n");
pr_warn_once("One-time warning\n");
```

### Dynamic Debug
```bash
# Enable at boot
# Add to kernel command line: dyndbg="+p"

# Enable for specific module
echo 'module hello_world +p' > /sys/kernel/debug/dynamic_debug/control

# Enable for specific function
echo 'func my_function +p' > /sys/kernel/debug/dynamic_debug/control

# Enable for specific file
echo 'file drivers/my_driver.c +p' > /sys/kernel/debug/dynamic_debug/control

# View all dynamic debug entries
cat /sys/kernel/debug/dynamic_debug/control
```

---

## Error Handling

### Error Codes
```c
#include <linux/errno.h>

/*
 * Common error codes (negative values):
 *
 * -ENOMEM    - Out of memory
 * -EINVAL    - Invalid argument
 * -EBUSY     - Device busy
 * -ENODEV    - No such device
 * -EEXIST    - File exists
 * -ENOENT    - No such file
 * -EIO       - I/O error
 * -EPERM     - Operation not permitted
 * -EACCES    - Permission denied
 * -EFAULT    - Bad address
 * -ETIMEDOUT - Operation timed out
 * -EAGAIN    - Try again
 * -ENOTSUPP  - Not supported
 */

static int my_init(void)
{
    void *ptr;

    ptr = kmalloc(1024, GFP_KERNEL);
    if (!ptr)
        return -ENOMEM;

    /* ... */

    return 0;
}
```

### Proper Cleanup Pattern
```c
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>

MODULE_LICENSE("GPL");

static char *buffer1 = NULL;
static char *buffer2 = NULL;
static char *buffer3 = NULL;

static int __init cleanup_demo_init(void)
{
    /* Allocate resources */
    buffer1 = kmalloc(1024, GFP_KERNEL);
    if (!buffer1) {
        pr_err("Failed to allocate buffer1\n");
        return -ENOMEM;
    }

    buffer2 = kmalloc(2048, GFP_KERNEL);
    if (!buffer2) {
        pr_err("Failed to allocate buffer2\n");
        goto err_free_buffer1;
    }

    buffer3 = kmalloc(4096, GFP_KERNEL);
    if (!buffer3) {
        pr_err("Failed to allocate buffer3\n");
        goto err_free_buffer2;
    }

    pr_info("All buffers allocated successfully\n");
    return 0;

/* Cleanup in reverse order of allocation */
err_free_buffer2:
    kfree(buffer2);
    buffer2 = NULL;
err_free_buffer1:
    kfree(buffer1);
    buffer1 = NULL;
    return -ENOMEM;
}

static void __exit cleanup_demo_exit(void)
{
    kfree(buffer3);
    kfree(buffer2);
    kfree(buffer1);
    pr_info("All buffers freed\n");
}

module_init(cleanup_demo_init);
module_exit(cleanup_demo_exit);
```

### ERR_PTR Pattern
```c
#include <linux/err.h>

/*
 * ERR_PTR pattern for functions returning pointers:
 * - Return error codes encoded as pointers
 * - More elegant than separate error parameter
 */

void *my_alloc_function(size_t size)
{
    void *ptr;

    if (size == 0)
        return ERR_PTR(-EINVAL);

    ptr = kmalloc(size, GFP_KERNEL);
    if (!ptr)
        return ERR_PTR(-ENOMEM);

    return ptr;
}

static int __init err_ptr_demo_init(void)
{
    void *ptr;

    ptr = my_alloc_function(1024);
    if (IS_ERR(ptr)) {
        pr_err("Allocation failed: %ld\n", PTR_ERR(ptr));
        return PTR_ERR(ptr);
    }

    /* Use ptr... */

    kfree(ptr);
    return 0;
}
```

### BUG and WARN
```c
/* Assertion that kernel cannot continue */
BUG_ON(ptr == NULL);  /* Causes kernel panic */

/* Warning that shouldn't happen but kernel continues */
WARN_ON(count < 0);
WARN_ON_ONCE(count < 0);  /* Only warn once */

/* Warning with message */
WARN(count < 0, "Invalid count: %d\n", count);
```

---

## Exercises

### Exercise 1: Basic Module
Create a module that:
1. Prints your name on load
2. Prints a goodbye message on unload
3. Exports a function that returns your student ID

### Exercise 2: Parameterized Module
Create a module with:
1. A string parameter for a greeting message
2. An integer parameter for repetition count
3. A boolean parameter to enable/disable uppercase output

### Exercise 3: Multi-File Module
Create a module composed of:
1. `main.c` - Init/exit and main logic
2. `utils.c` - Helper functions
3. `math.c` - Mathematical operations
Use proper Makefile to build all files together.

### Exercise 4: Module Communication
Create two modules:
1. Provider module: Exports `register_callback()` and `unregister_callback()`
2. Consumer module: Registers a callback that gets called by provider

---

## Common Mistakes to Avoid

1. **Forgetting MODULE_LICENSE** - Kernel will taint and some symbols unavailable
2. **Using floating point** - Kernel space doesn't support FPU by default
3. **Sleeping in atomic context** - Don't call sleeping functions with spinlocks held
4. **Memory leaks** - Always free allocated memory
5. **Returning wrong error codes** - Return negative errno values
6. **Not checking return values** - Always check allocation and registration functions
7. **Accessing user space directly** - Use `copy_from_user()` / `copy_to_user()`

---

## Summary

| Concept | Purpose |
|---------|---------|
| `module_init()` | Register initialization function |
| `module_exit()` | Register cleanup function |
| `__init` | Mark function for init-time only |
| `__exit` | Mark function for exit-time only |
| `MODULE_*` | Module metadata macros |
| `module_param()` | Define module parameter |
| `EXPORT_SYMBOL()` | Export symbol for other modules |
| `pr_*()` | Kernel logging functions |
| Goto cleanup | Error handling pattern |

---

**Previous:** [Environment Setup](00-environment-setup.md) | **Next:** [Character Devices](02-character-devices.md)
