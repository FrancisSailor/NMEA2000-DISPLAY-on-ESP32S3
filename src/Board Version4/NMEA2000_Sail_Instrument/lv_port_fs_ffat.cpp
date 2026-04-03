#include <Arduino.h>
#include <FFat.h>
#include <lvgl.h>
#include "lv_port_fs_ffat.h"

#ifndef LV_FS_MAX_PATH_LENGTH
#define LV_FS_MAX_PATH_LENGTH 256
#endif

typedef struct {
    File file;
} ffat_file_t;

static void * ffat_open_cb(lv_fs_drv_t *drv, const char *path, lv_fs_mode_t mode)
{
    LV_UNUSED(drv);
    bool wr = (mode & LV_FS_MODE_WR) != 0;

    char full[LV_FS_MAX_PATH_LENGTH];
    // try with /ffat prefix
    if (path[0] == '/')
        snprintf(full, sizeof(full), "/ffat%s", path);
    else
        snprintf(full, sizeof(full), "/ffat/%s", path);

    File f = FFat.open(full, wr ? FILE_WRITE : FILE_READ);
    if (!f) {
        // try raw (FFat mounted at root)
        f = FFat.open(path, wr ? FILE_WRITE : FILE_READ);
    }

    if (!f) {
        LV_LOG_ERROR("FS(F): open fail '%s'", path);
        return NULL;
    }

    ffat_file_t *ff = (ffat_file_t*)lv_mem_alloc(sizeof(ffat_file_t));
    if (!ff) {
        f.close();
        return NULL;
    }
    ff->file = f;
    return ff;
}

static lv_fs_res_t ffat_close_cb(lv_fs_drv_t *drv, void *file_p)
{
    LV_UNUSED(drv);
    if (!file_p) return LV_FS_RES_INV_PARAM;
    ffat_file_t *ff = (ffat_file_t*)file_p;
    if (ff->file) ff->file.close();
    lv_mem_free(ff);
    return LV_FS_RES_OK;
}

static lv_fs_res_t ffat_read_cb(lv_fs_drv_t *drv, void *file_p,
                                void *buf, uint32_t btr, uint32_t *br)
{
    LV_UNUSED(drv);
    if (!file_p || !buf || !br) return LV_FS_RES_INV_PARAM;
    ffat_file_t *ff = (ffat_file_t*)file_p;
    size_t r = ff->file.read((uint8_t*)buf, btr);
    *br = (uint32_t)r;
    return LV_FS_RES_OK;
}

static lv_fs_res_t ffat_write_cb(lv_fs_drv_t *drv, void *file_p,
                                 const void *buf, uint32_t btw, uint32_t *bw)
{
    LV_UNUSED(drv); LV_UNUSED(file_p);
    LV_UNUSED(buf); LV_UNUSED(btw); LV_UNUSED(bw);
    return LV_FS_RES_NOT_IMP;
}

static lv_fs_res_t ffat_seek_cb(lv_fs_drv_t *drv, void *file_p,
                                uint32_t pos, lv_fs_whence_t whence)
{
    LV_UNUSED(drv);
    if (!file_p) return LV_FS_RES_INV_PARAM;
    ffat_file_t *ff = (ffat_file_t*)file_p;

    SeekMode m;
    switch (whence) {
        case LV_FS_SEEK_SET: m = SeekSet; break;
        case LV_FS_SEEK_CUR: m = SeekCur; break;
        case LV_FS_SEEK_END: m = SeekEnd; break;
        default: return LV_FS_RES_INV_PARAM;
    }
    if (!ff->file.seek(pos, m)) return LV_FS_RES_FS_ERR;
    return LV_FS_RES_OK;
}

static lv_fs_res_t ffat_tell_cb(lv_fs_drv_t *drv, void *file_p,
                                uint32_t *pos_p)
{
    LV_UNUSED(drv);
    if (!file_p || !pos_p) return LV_FS_RES_INV_PARAM;
    ffat_file_t *ff = (ffat_file_t*)file_p;
    *pos_p = (uint32_t)ff->file.position();
    return LV_FS_RES_OK;
}

void lv_port_fs_ffat_init(void)
{
    static lv_fs_drv_t drv;
    lv_fs_drv_init(&drv);
    drv.letter = 'F';
    drv.open_cb = ffat_open_cb;
    drv.close_cb = ffat_close_cb;
    drv.read_cb = ffat_read_cb;
    drv.write_cb = ffat_write_cb;
    drv.seek_cb = ffat_seek_cb;
    drv.tell_cb = ffat_tell_cb;
    drv.cache_size = 0;
    lv_fs_drv_register(&drv);
    LV_LOG_INFO("FS(F): registered");
}
