/*
 * mtd_hack - Module for hacking MTD driver
 *
 * Copyright (C) 2025 remittor <remittor@gmail.com>
 *
 */

#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/mtd/mtd.h>
#include <linux/err.h>
#include <linux/cdev.h>
#include <linux/string.h>
#include <linux/version.h>

#ifndef MODULE
#error "Must be compiled as a module."
#endif


static char * g_dev_name = "xmirp";
module_param_named(name, g_dev_name, charp, S_IRUGO);
MODULE_PARM_DESC(name, "The name of chr device");

#define MDATA_MAGIC        0xC0DE
#define MDATA_MAGIC_OFFSET 30
#define MDATA_PTR_OFFSET   32

#define MAX_CMD_LEN 255
#define MAX_CMD_ARG 8
#define ARG_DELIM '|'
#define END_OF_CMD '\n'
#define MAX_RESP_LEN 2000

typedef struct mod_data {
    char   cmd[MAX_CMD_LEN + 1];
    char * cmd_arg[MAX_CMD_ARG + 1];
    int    cmd_arg_num;

    char   resp[MAX_RESP_LEN + 1];
    int    resp_code;   // def: INT_MIN

    int    dev_major;
    struct class  * mod_class;
    struct device * mod_device;
    struct cdev   * mod_cdev;

    uint64_t dev_open_counter;

    struct mutex * dev_mutex;

    struct file_operations fops;
    char   dummy1[128];
    
    int    mtd_dev_numbers;
    int    mtd_max_index;
    size_t mtd_index_offset;
    size_t mtd_name_offset;

#ifdef XMIR_LL
    size_t page_size;        // 0x1000
    size_t page_shift;       // 12
    phys_addr_t phys_pfn_offset;
    phys_addr_t phys_offset;
    size_t page_offset;      // lower virt addr
    size_t high_memory;      // higher virt addr
#endif
} t_mod_data;

typedef struct _mod_data {
    struct mod_data  data;
} T_mod_data;

#define g \
    ((T_mod_data *)(THIS_MODULE->name + 32))->data

static bool is_mod_data_allocated(void)
{
    uint16_t * magic_ptr = (uint16_t *)(THIS_MODULE->name + MDATA_MAGIC_OFFSET);
    return (*magic_ptr == MDATA_MAGIC) ? true : false;
}

#undef pr_err
#undef pr_warn
#undef pr_info

#define pr_err( fmt, ...) printk(KERN_ERR     "%s: " fmt "\n", THIS_MODULE->name, ##__VA_ARGS__)
#define pr_warn(fmt, ...) printk(KERN_WARNING "%s: " fmt "\n", THIS_MODULE->name, ##__VA_ARGS__)
#define pr_info(fmt, ...) printk(KERN_INFO    "%s: " fmt "\n", THIS_MODULE->name, ##__VA_ARGS__)

#include "xmir_patcher.h"

static int param_set_cmd(const char * val, const struct kernel_param * kp);
static int param_get_cmd(char * buffer, const struct kernel_param * kp);

static const struct kernel_param_ops g_cmd_ops = {
    .set = param_set_cmd,
    .get = param_get_cmd,
};
module_param_cb(cmd, &g_cmd_ops, NULL, 0664);


static int update_resp(int code, const char * resp)
{
    g.resp_code = code;
    if (code == INT_MIN) {
        g.resp[0] = 0;
        return 0;
    }
    g.resp[MAX_RESP_LEN] = 0;
    if (resp) {
        size_t resp_len = strlen(resp);
        if (resp_len > MAX_RESP_LEN) {
            //pr_err("resp: Response too large (len = %zu)", resp_len);
            g.resp_code = -1000200;
            g.resp[0] = 0;
            return -1;
        }
        strncpy(g.resp, resp, MAX_RESP_LEN);
    }
    return 0;
}

static int get_mtd_info(int anum, char * arg[])
{
    const char * mtd_name = NULL;
    int mtd_idx = -1;
    struct mtd_info * mtd;
    
    if (anum != 1 || strlen(arg[0]) == 0) {
        return -3;
    }
    if (strlen(arg[0]) <= 2) {
        unsigned long value;
        if (kstrtoul(arg[0], 10, &value) == 0) {
            mtd_idx = (int)value; 
        }
    }
    if (mtd_idx < 0) {
        mtd_name = arg[0];
    }
    mtd = get_mtd_info_dev(mtd_name, mtd_idx, true, true);
    if (!IS_ERR_OR_NULL(mtd)) {
        const char * name;
        uint64_t addr;
        uint64_t size;
        int flags = mtd->flags;
        int lock;
        mtd_idx = get_mtd_index(mtd);
        name = get_mtd_name(mtd);
        if (!name) {
            pr_err("ERROR: cannot found name for MTD[%d]", mtd_idx);
            unlock_mtd_table();
            return -22;
        }
        addr = mtdpart_get_offset(mtd);
        size = mtd_get_device_size(mtd);
        lock = mtd_is_locked(mtd, 0, size); 
        snprintf(g.resp, MAX_RESP_LEN, "%d|%s|0x%llx|0x%llx|0x%x|%d\n", mtd_idx, name, addr, size, flags, lock);
        unlock_mtd_table();
        return 0;
    }
    return (!mtd) ? -21 : PTR_ERR(mtd);
}

static int set_mtd_rw(int anum, char * arg[])
{
    const char * mtd_name = NULL;
    int mtd_idx = -1;
    struct mtd_info * mtd;
    
    if (anum != 1 || strlen(arg[0]) == 0) {
        return -3;
    }
    if (strlen(arg[0]) <= 2) {
        unsigned long value;
        if (kstrtoul(arg[0], 10, &value) == 0) {
            mtd_idx = (int)value; 
        }
    }
    if (mtd_idx < 0) {
        mtd_name = arg[0];
    }
    mtd = get_mtd_info_dev(mtd_name, mtd_idx, true, true);
    if (!IS_ERR_OR_NULL(mtd)) {
        mtd_idx = get_mtd_index(mtd);
        unlock_mtd_table();
        mtd = get_mtd_device(NULL, mtd_idx);
        if (IS_ERR_OR_NULL(mtd)) {
            pr_err("ERROR: Cannot get mtd[%d] device (err = %ld)", mtd_idx, PTR_ERR(mtd));
            return -32;
        }
        mtd->flags |= MTD_WRITEABLE;
        snprintf(g.resp, MAX_RESP_LEN, "%d|0x%x\n", mtd_idx, mtd->flags);
        put_mtd_device(mtd);
        pr_info("CMD: set RW flag for MTD[%d] device", mtd_idx);
        return 0;
    }
    return (!mtd) ? -31 : PTR_ERR(mtd);
}

static int dev_process_command(const char * cmd)
{
    size_t cmdlen = strlen(cmd);
    int i;
    int anum;
    int rc = -2;

    update_resp(INT_MIN, NULL);
    g.cmd_arg_num = 0;    
    g.resp[0] = 0;
    if (cmd != g.cmd) {
        strncpy(g.cmd, cmd, MAX_CMD_LEN);
        g.cmd[cmdlen] = 0;
    }
    if (cmdlen < 2) {
        g.cmd[0] = 0;
        return -1;
    }
    if (g.cmd[cmdlen - 1] == END_OF_CMD) {
        g.cmd[cmdlen - 1] = 0;
        cmdlen--;
    }
    if (g.cmd[0] == 0 || g.cmd[0] == ARG_DELIM) {
        g.cmd[0] = 0;
        return -1;
    }
    g.cmd_arg[0] = g.cmd;
    anum = 1;
    for (i = 0; i < cmdlen; i++) {
        if (g.cmd[i] == ARG_DELIM) {
            g.cmd_arg[anum] = g.cmd + i + 1;
            g.cmd[i] = 0;
            anum++;
        }
    }
    g.cmd_arg_num = anum;
    //for (i = 0; i < anum; i++) {
    //    pr_info("  arg[%d] = '%s'", i, g.cmd_arg[i]);
    //}
    if (strcmp(g.cmd, "get_mtd_info") == 0) {
        rc = get_mtd_info(anum - 1, g.cmd_arg + 1);
    } else
    if (strcmp(g.cmd, "set_mtd_rw") == 0) {
        rc = set_mtd_rw(anum - 1, g.cmd_arg + 1);
    } else {
        pr_err("ERROR: Command '%s' not supported!", g.cmd);
        rc = -2;
    }
    if (g.resp[0]) {
        pr_info("CMD: RESULT: %d|%s", rc, g.resp);
    }
    return rc;
}

// =========================================================================================

static int param_set_cmd(const char * val, const struct kernel_param * kp)
{
    size_t len = (val) ? strlen(val) : 0;
    int rc;
    int err_code;

    if (!is_mod_data_allocated()) {
        return -ETXTBSY;
    }
    update_resp(INT_MIN, NULL);
    g.cmd[0] = 0;
    g.cmd_arg_num = 0;

    if (len < 2 || len > MAX_CMD_LEN) {
        pr_err("RECV: Invalid arg len = %zu, value = '%s'", len, val);
        return -EINVAL;
    }
    memcpy(g.cmd, val, len + 1);
    if (g.cmd[len - 1] == END_OF_CMD) {
        g.cmd[len - 1] = 0;
        len--;
    }
    pr_info("RECV: CMD = \"%s\" ", g.cmd);
    rc = dev_process_command(g.cmd);
    err_code = update_resp(rc, NULL);
    if (err_code) {
        pr_err("RECV: Response too large (len = %zu)", strlen(g.resp));
    }
    pr_info("RECV: resp code = %d (len = %zu)", g.resp_code, strlen(g.resp));
    return 0;
}

static int param_get_cmd(char * buffer, const struct kernel_param * kp)
{
    int len;    

    if (!is_mod_data_allocated()) {
        return -ETXTBSY;
    }
    if (g.resp_code == INT_MIN) {
        pr_info("SEND: sent empty response to user (resp_code is invalid)");
        buffer[0] = 0;
        return 0;
    }
    len = snprintf(buffer, 4000, "%d|%s", g.resp_code, g.resp);
    pr_info("SEND: sent %d characters to the user: \"%s\"", len, g.resp);
    return len;
}

static ssize_t dev_write(struct file * fileptr, const char * buffer, size_t len, loff_t * offset)
{
    unsigned long retval;
    int rc;
    int err_code;
    
    pr_info("recv: buffer = %px, len = %zu, offset = %d", buffer, len, offset ? (int)*offset : -1);
    update_resp(INT_MIN, NULL);
    g.cmd[0] = 0;
    g.cmd_arg_num = 0;

    if (offset && *offset != 0) {
        pr_err("recv: Invalid arg offset = %d (len = %zu)", (int)*offset, len);
        return -EINVAL;
    }
    if (len < 2 || len > MAX_CMD_LEN) {
        pr_err("recv: Invalid arg len = %zu", len);
        return -EINVAL;
    }
    retval = x_copy_from_user(g.cmd, buffer, len);
    if (retval != 0) {
        pr_err("recv: copy_from_user ret_val = %lu", retval);
        g.cmd[0] = 0;
        return -EFAULT;
    }
    if (g.cmd[len - 1] != END_OF_CMD) {
        pr_err("recv: Command terminator not found. (len = %zu)", len);
        g.cmd[0] = 0;
        return -EINVAL;
    }
    //pr_info("recv: CMD LEN = %zu", len);
    g.cmd[len - 1] = 0;
    g.cmd[len] = 0;
    pr_info("recv: CMD = \"%s\" ", g.cmd);
    rc = dev_process_command(g.cmd);
    err_code = update_resp(rc, NULL);
    if (err_code) {
        pr_err("recv: Response too large (len = %zu)", strlen(g.resp));
    }
    pr_info("recv: resp code = %d (len = %zu)", g.resp_code, strlen(g.resp));
    if (offset) {
        *offset = len;
    }
    return len;
}

static ssize_t dev_read(struct file * fileptr, char * buffer, size_t len, loff_t * offset)
{
    unsigned long retval = 0;
    size_t resp_len = strlen(g.resp);
    char prefix[20] = {0};
    int prefix_len;
    int total_len;

    if (fileptr->private_data != NULL) {
        fileptr->private_data = NULL;  // data not readed
        pr_info("send: EOF");
        return 0;
    }
    if (g.resp_code == INT_MIN) {
        fileptr->private_data = NULL;
        pr_info("send: sent empty response to user (resp_code = %d)", g.resp_code);
        return 0;
    }
    prefix_len = snprintf(prefix, sizeof(prefix), "%d|", g.resp_code);
    total_len = prefix_len + resp_len;
    if (*offset >= total_len) {
        fileptr->private_data = NULL;
        pr_err("send: eof");
        return 0;
    }
    if (*offset != 0) {
        pr_err("send: Invalid arg offset = %d", (int)*offset);
        return -EINVAL;
    }
    if (prefix_len + resp_len >= len) {
        pr_err("send: Invalid resp len = %d (len = %zu)", total_len, len);
        return -EFBIG;
    }
    retval = x_copy_to_user(buffer, prefix, prefix_len);
    if (retval != 0) {
        pr_err("send: Failed to send %d characters to the user", prefix_len);
        return -EFAULT;
    }
    *offset = prefix_len;
    if (resp_len) {
        retval = x_copy_to_user(buffer + *offset, g.resp, resp_len);
        if (retval != 0) {
            pr_err("send: failed to send %d characters to the user", resp_len);
            return -EFAULT;
        }
        *offset += resp_len;
    }
    pr_info("send: sent %d characters to the user: \"%s\"", (int)*offset, g.resp);
    fileptr->private_data = (void *)(size_t)*offset;  // resp readed
    return *offset;
}

static int dev_open(struct inode * inodep, struct file * fileptr)
{
    if (!mutex_trylock(g.dev_mutex)) {
        pr_err("open: device '%s' in use by another process", g_dev_name);
        return -EBUSY;
    }
    //if (!try_module_get(THIS_MODULE)) {
    //    return -EFAULT;
    //}
    fileptr->private_data = NULL;
    g.dev_open_counter++;
    pr_info("open: device '%s' has been opened %llu times", g_dev_name, g.dev_open_counter);
    return 0;
}

static int dev_close(struct inode * inodep, struct file * fileptr)
{
    fileptr->private_data = NULL;
    //module_put(THIS_MODULE);
    mutex_unlock(g.dev_mutex);
    pr_info("close: device '%s' successfully closed", g_dev_name);
    return 0;
}

static int __init mod_init(void)
{
    int err = -EFAULT;
    int rc;
    char * mdata = NULL;
    struct mod_data ** gptr;
    uint16_t * magic_ptr;

    pr_info("init: ============> dev name: '%s'", g_dev_name);

    if (strncmp(THIS_MODULE->name, "xmir", 4) != 0) {
        pr_err("init: Unsupported module name: '%s'", THIS_MODULE->name);
        return -EFAULT;
    }
    if (strlen(THIS_MODULE->name) >= MDATA_MAGIC_OFFSET - 1) {
        pr_err("init: unsupported module name: '%s'", THIS_MODULE->name);
        return -EFAULT;
    }
    mdata = x_kzalloc(sizeof(struct mod_data), GFP_KERNEL);
    if (!mdata) {
        pr_err("init: cannot allocate %zu bytes for data", sizeof(struct mod_data));
        return -ENOMEM;
    }
    gptr = (struct mod_data **)(THIS_MODULE->name + MDATA_PTR_OFFSET);
    *gptr = (struct mod_data *)mdata;
    magic_ptr = (uint16_t *)(THIS_MODULE->name + MDATA_MAGIC_OFFSET);
    *magic_ptr = MDATA_MAGIC;

    g.mod_cdev = x_kzalloc(sizeof(struct cdev) + 128, GFP_KERNEL);
    g.dev_mutex = x_kzalloc(sizeof(struct mutex) + 64, GFP_KERNEL);
    if (!g.mod_cdev || !g.dev_mutex) {
        pr_err("init: cannot allocate kernel objects");
        err = -ENOMEM;
        goto err1;
    }
    g.fops.owner = THIS_MODULE;
    g.fops.open = dev_open;
    g.fops.read = dev_read;
    g.fops.write = dev_write;
    g.fops.release = dev_close;

    g.resp_code = INT_MIN;
    
    mutex_init(g.dev_mutex);

#ifdef XMIR_LL    
    err = init_kernel_cfg();
    if (err)
        goto err1;
#endif

    g.dev_major = register_chrdev(0, g_dev_name, &g.fops);
    if (g.dev_major < 0) {
        pr_err("init: failed to register a major number (err = %d)", g.dev_major);
        err = g.dev_major;
        goto err1;
    }
    pr_info("init: module registered with major number %d", g.dev_major);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
    g.mod_class = class_create(g_dev_name);
#else
    g.mod_class = class_create(THIS_MODULE, g_dev_name);    
#endif
    if (IS_ERR_OR_NULL(g.mod_class)) {
        pr_err("init: failed to register device class (err = %ld)", PTR_ERR(g.mod_class));
        err = -EAGAIN;
        goto err2;
    }
    pr_info("init: device class registered correctly");

    g.mod_device = device_create(g.mod_class, NULL, MKDEV(g.dev_major, 0), NULL, g_dev_name);
    if (IS_ERR_OR_NULL(g.mod_device)) {
        pr_err("init: failed to create the device '%s' (err = %ld)", g_dev_name, PTR_ERR(g.mod_device));
        err = PTR_ERR(g.mod_device); 
        goto err3;
    }
    pr_info("init: device '%s' created correctly", g_dev_name);
    
    cdev_init(g.mod_cdev, &g.fops);
    //g.mod_cdev->owner = THIS_MODULE;
    rc = cdev_add(g.mod_cdev, MKDEV(g.dev_major, 0), 1);
    if (rc < 0) {
        pr_err("init: failed to create the chr device (err = %d)", rc);
        err = -EMLINK; 
        goto err4;
    }
    pr_info("init: char device created correctly");
    return 0;
    
//err5:
//    cdev_del(g.mod_cdev);
err4:
    device_destroy(g.mod_class, MKDEV(g.dev_major, 0));
err3:
    class_destroy(g.mod_class);
err2:
    unregister_chrdev(g.dev_major, g_dev_name);
err1:
    kfree(g.mod_cdev);
    kfree(g.dev_mutex);
    kfree(mdata);
    return err;
}

module_init(mod_init);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("remittor <remittor@gmail.com>");
MODULE_DESCRIPTION("Module for hacking xq kernel");
MODULE_VERSION("1.0");
