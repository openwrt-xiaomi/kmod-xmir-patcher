/*
 * mtd_hack - Module for hacking MTD driver
 *
 * Copyright (C) 2025 remittor <remittor@gmail.com>
 *
 */

#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/thread_info.h>

static void * x_kzalloc(size_t size, gfp_t flags)
{
    void * addr;
    addr = __kmalloc(size, flags | __GFP_ZERO);
    return addr;
}

extern struct mutex mtd_table_mutex;
extern struct mtd_info * __mtd_next_device(int index);
extern uint64_t mtdpart_get_offset(const struct mtd_info * mtd);
extern uint64_t mtd_get_device_size(const struct mtd_info * mtd);
extern int mtd_is_locked(struct mtd_info *mtd, loff_t ofs, uint64_t len);


// forward declarations:
static int get_mtd_index(const struct mtd_info * mtd);


static void lock_mtd_table(void)
{
    mutex_lock(&mtd_table_mutex);
}

static void unlock_mtd_table(void)
{
    mutex_unlock(&mtd_table_mutex);
}

// search mtd dev from <index> ... <MAX_INDEX> and return first founded dev
// return NULL on EOL
static struct mtd_info * _get_mtd_next(int index)
{
    return __mtd_next_device(index);
}

// return next mtd dev or return NULL on EOL
static struct mtd_info * get_mtd_next(const struct mtd_info * mtd)
{
    if (mtd == NULL) {
        return _get_mtd_next(0);  // return first dev from mtd_list
    } else {
        int mtd_idx = get_mtd_index(mtd);
        if (mtd_idx >= 0) {
            return _get_mtd_next(mtd_idx + 1);
        }
    }
    return NULL;
}

/*
struct mtd_info {
    u_char type;
    uint32_t flags;
    uint32_t orig_flags; // Flags as before running mtd checks
    uint64_t size;   // Total size of the MTD
    ...
    unsigned int bitflip_threshold;
    const char *name;
    int index;
    ...
    struct module *owner;
    struct device dev;
    int usecount;
    struct mtd_debug_info dbg;
    struct nvmem_device *nvmem;
};    
*/

static size_t get_mtd_info_index_offset(void)
{
    struct mtd_info * mtd;
    const size_t obj_size = sizeof(struct mtd_info);
    const int offset_len = obj_size / 4;
    int offset_score[sizeof(struct mtd_info) / 4 + 1];
    int offnum;
    int max_score = 0;
    int best_offset = 0;
    int offset;
    int mtd_idx;
    int mtd_checked = 0;

    if (g.mtd_index_offset)
        return g.mtd_index_offset;

    g.mtd_dev_numbers = 0;
    g.mtd_max_index = 0;
    memset(offset_score, 0, sizeof(offset_score));

    for (mtd_idx = 20; mtd_idx >= 2; mtd_idx--) {
        mtd = get_mtd_device(NULL, mtd_idx);
        if (PTR_ERR(mtd) == -EBUSY) {
            // device locked
            continue;
        }
        if (IS_ERR_OR_NULL(mtd)) {
            continue;
        }
        mtd_checked++;
        for (offset = 16; offset < obj_size - 32; offset += 4) {
            int * pindex = (int *)((char *)mtd + offset);
            if (*pindex == mtd_idx) {
                offnum = offset / 4;
                offset_score[offnum]++;
            }
        }
        put_mtd_device(mtd);
    }
    if (mtd_checked <= 3) {
        pr_err("ERROR: index offset: mtd_checked = %d", mtd_checked);
        return 0;
    }
    for (offnum = 0; offnum < offset_len; offnum++) {
        if (offset_score[offnum] > max_score) {
            max_score = offset_score[offnum];
            best_offset = offnum;
        }
    }
    if (max_score <= 3 || best_offset == 0) {
        pr_err("ERROR: index offset: max_score = %d , mtd_checked = %d", max_score, mtd_checked);
        return 0;
    }
    g.mtd_index_offset = best_offset * 4;
    pr_info("mtd_info: index offset = 0x%zX", g.mtd_index_offset);
    lock_mtd_table();
    for (mtd = get_mtd_next(NULL); mtd != NULL; mtd = get_mtd_next(mtd)) {
        mtd_idx = get_mtd_index(mtd);
        if (mtd_idx > g.mtd_max_index) {
            g.mtd_max_index = mtd_idx;
        }
        g.mtd_dev_numbers++;
    }
    unlock_mtd_table();
    pr_info("mtd_info: max index   = %d", g.mtd_max_index);
    pr_info("mtd_info: dev numbers = %d", g.mtd_dev_numbers);
    return g.mtd_index_offset;
}

static int init_mtd_base_info(bool show_error)
{
    if (g.mtd_max_index == 0) {
        get_mtd_info_index_offset();
        if (g.mtd_max_index == 0) {
            if (show_error) {
                pr_err("ERROR: cannot get MTD table base info");
            }
            return -11;
        }
    }
    return 0;
}

static int get_mtd_index(const struct mtd_info * mtd)
{
    size_t index_offset = get_mtd_info_index_offset();
    if (mtd && index_offset) {
        int * ptr = (int *)((char *)mtd + index_offset);
        return *ptr;
    }
    return -1;
}
    
static size_t get_mtd_info_name_offset(void)
{
    if (g.mtd_name_offset)
        return g.mtd_name_offset;

    if (!g.mtd_index_offset) {
        int mtd_index_offset = get_mtd_info_index_offset();
        if (!mtd_index_offset)
            return 0;
    }
    g.mtd_name_offset = g.mtd_index_offset - sizeof(size_t);
    pr_info("mtd_info: name offset = 0x%zx", g.mtd_name_offset);
    return g.mtd_name_offset;
}

static const char * get_mtd_name(const struct mtd_info * mtd)
{
    size_t name_offset = get_mtd_info_name_offset();
    if (mtd && name_offset) {
        const char ** ptr = (const char **)((char *)mtd + name_offset);
        return *ptr;
    }
    return NULL;
}

static struct mtd_info * get_mtd_by_name(const char * name)
{
    struct mtd_info * mtd;
    size_t name_len = (name) ? strlen(name) : 0;
    if (name_len) {
        for (mtd = get_mtd_next(NULL); mtd != NULL; mtd = get_mtd_next(mtd)) {
            const char * mtd_name = get_mtd_name(mtd);
            if (mtd_name && strcmp(mtd_name, name) == 0) {
                return mtd;
            }
        }
    }
    return NULL;
}

static struct mtd_info * get_mtd_info_dev(const char * name, int index, bool lock, bool show_error)
{
    struct mtd_info * mtd = NULL;
    long rc = -19;
    
    if (init_mtd_base_info(true) != 0) {
        return ERR_PTR(-13);
    }
    if (name) {
        size_t name_offset = get_mtd_info_name_offset();
        if (name_offset == 0) {
            if (show_error) {
                pr_err("ERROR: cannot find name offset for mtd_info");
            }
            return ERR_PTR(-15);
        }
    }
    lock_mtd_table();
    if (name) {
        mtd = get_mtd_by_name(name);
        if (!mtd) {
            if (show_error) {
                pr_err("ERROR: cannot find MTD dev by name = '%s'", name);
            }
            rc = -16;
            goto err;
        }
        index = get_mtd_index(mtd);
    }
    if (index >= 0) {
        mtd = _get_mtd_next(index);
        if (!IS_ERR_OR_NULL(mtd)) {
            if (get_mtd_index(mtd) == index) {
                if (!lock) {
                    unlock_mtd_table();
                }
                return mtd;
            }
        }
    }
    rc = -19;
err:
    unlock_mtd_table();
    if (show_error) {
        if (name) {
            pr_err("ERROR: cannot get MTD[%s] device (ERR = %ld)", name, rc);
        } else {
            pr_err("ERROR: cannot get MTD[%d] device (ERR = %ld)", index, rc);
        }
    }
    return ERR_PTR(rc);
}
