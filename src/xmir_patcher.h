/*
 * mtd_hack - Module for hacking MTD driver
 *
 * Copyright (C) 2025 remittor <remittor@gmail.com>
 *
 */

#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/thread_info.h>

extern struct mutex mtd_table_mutex;
extern struct mtd_info * __mtd_next_device(int index);
extern uint64_t mtdpart_get_offset(const struct mtd_info * mtd);
extern uint64_t mtd_get_device_size(const struct mtd_info * mtd);
extern int mtd_is_locked(struct mtd_info *mtd, loff_t ofs, uint64_t len);

/*
struct mtd_info {
    u_char type;
    uint32_t flags;
    uint32_t orig.flags; // Flags as before running mtd checks
    uint64_t size;   // Total size of the MTD
    ...
    unsigned int bitflip_threshold;
    const char *name;
    int index;
    ...
    struct module *owner;
    struct device dev;
    int usecount;
    struct mtd_debug.info dbg;
    struct nvmem_device *nvmem;
};    
*/
       
static size_t get_mtd_info_index_offset(void)
{
    struct mtd_info * mtd;
    const size_t obj_size = sizeof(struct mtd_info);
    const int offset_len = obj_size / 4;
    int offset_score[offset_len + 1];
    int max_mtd_idx = 0;
    int offnum;
    int max_score = 0;
    int best_offset = 0;
    int offset;
    int mtd_idx;

    if (g.mtd_index_offset)
        return g.mtd_index_offset;

    g.mtd_max_index = 0;
    memset(g.mtd_dev, 0, sizeof(g.mtd_dev));
    memset(offset_score, 0, sizeof(offset_score));

    for (mtd_idx = 0; mtd_idx <= MAX_MTD_IDX; mtd_idx++) {
        mtd = get_mtd_device(NULL, mtd_idx);
        if (IS_ERR_OR_NULL(mtd)) {
            if (PTR_ERR(mtd) == -EBUSY) {
                max_mtd_idx = mtd_idx;
                g.mtd_dev[mtd_idx] = 1;
            }
            continue;
        }
        max_mtd_idx = mtd_idx;
        g.mtd_dev[mtd_idx] = 1;
        for (offset = 16; offset < obj_size; offset += 4) {
            int * pindex = (int *)((char *)mtd + offset);
            if (*pindex == mtd_idx) {
                offset_score[offset / 4]++;
            }    
        }
        put_mtd_device(mtd);
    }
    if (max_mtd_idx <= 3) {
        pr_err("ERROR: max_mtd_idx = %d", max_mtd_idx);
        return 0;
    }
    
    for (offnum = 0; offnum < offset_len; offnum++) {
        if (offset_score[offnum] > max_score) {
            max_score = offset_score[offnum];
            best_offset = offnum;
        }
    }
    if (max_score < 3 || best_offset == 0) {
        pr_err("ERROR: index offset max_score = %d", max_score);
        return 0;
    }
    g.mtd_max_index = max_mtd_idx;
    pr_info("mtd_info: max index = %d", max_mtd_idx);
    g.mtd_index_offset = best_offset * 4;
    pr_info("mtd_info: index offset = 0x%zX", g.mtd_index_offset);
    return g.mtd_index_offset;
}

static int init_mtd_base_info(bool show_error)
{
    if (g.mtd_max_index == 0 || g.mtd_index_offset == 0) {
        get_mtd_info_index_offset();
        if (g.mtd_max_index == 0 || g.mtd_index_offset == 0) {
            if (show_error) {
                pr_err("ERROR: cannot get MTD table base info");
            }
            return -11;
        }
    }
    return 0;
}

static int get_mtd_index(struct mtd_info * mtd)
{
    size_t index_offset = get_mtd_info_index_offset();
    if (index_offset) {
        int * ptr = (int *)((char *)mtd + index_offset);
        return *ptr;
    }
    return -1;
}

static void lock_mtd_table(void)
{
    mutex_lock(&mtd_table_mutex);
}

static void unlock_mtd_table(void)
{
    mutex_unlock(&mtd_table_mutex);
}

static struct mtd_info * get_mtd_info_dev(int mtd_idx, bool lock, bool show_error)
{
    struct mtd_info * mtd;
    
    if (init_mtd_base_info(true) != 0) {
        return ERR_PTR(-15);
    }
    if (mtd_idx > g.mtd_max_index || g.mtd_dev[mtd_idx] == 0) {
        if (show_error)
            pr_err("ERROR: MTD[%d] device not present", mtd_idx);
        return ERR_PTR(-16);
    }
    if (lock) {
        lock_mtd_table();
    }
    mtd = __mtd_next_device(mtd_idx);
    if (mtd != NULL && !IS_ERR(mtd) && get_mtd_index(mtd) == mtd_idx) {
        return mtd;
    }
    if (lock) {
        unlock_mtd_table();
    }
    if (show_error) {
        pr_err("ERROR: cannot get MTD[%d] device (ERR = %ld)", mtd_idx, PTR_ERR(mtd));
    }
    return ERR_PTR(-17);
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

static const char * get_mtd_name(struct mtd_info * mtd)
{
    size_t name_offset = get_mtd_info_name_offset();
    if (name_offset) {
        const char ** ptr = (const char **)((char *)mtd + name_offset);
        return *ptr;
    }
    return NULL;
}

