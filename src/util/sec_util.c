/**************************************************************************

xserver-xorg-video-exynos

Copyright 2010 - 2011 Samsung Electronics co., Ltd. All Rights Reserved.

Contact: Boram Park <boram1288.park@samsung.com>

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sub license, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice (including the
next paragraph) shall be included in all copies or substantial portions
of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
IN NO EVENT SHALL PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR
ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

**************************************************************************/

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <X11/XWDFile.h>

#include "sec.h"
#include "sec_util.h"
#include "sec_output.h"
#include "sec_video_fourcc.h"
#include <exynos_drm.h>
#include <list.h>

#include "fimg2d.h"

#define DUMP_SCALE_RATIO  2

int secUtilDumpBmp (const char * file, const void * data, int width, int height)
{
    int i;

    struct
    {
        unsigned char magic[2];
    } bmpfile_magic = { {'B', 'M'} };

    struct
    {
        unsigned int filesz;
        unsigned short creator1;
        unsigned short creator2;
        unsigned int bmp_offset;
    } bmpfile_header = { 0, 0, 0, 0x36 };

    struct
    {
        unsigned int header_sz;
        unsigned int width;
        unsigned int height;
        unsigned short nplanes;
        unsigned short bitspp;
        unsigned int compress_type;
        unsigned int bmp_bytesz;
        unsigned int hres;
        unsigned int vres;
        unsigned int ncolors;
        unsigned int nimpcolors;
    } bmp_dib_v3_header_t = { 0x28, 0, 0, 1, 24, 0, 0, 0, 0, 0, 0 };
    unsigned int * blocks;

    XDBG_RETURN_VAL_IF_FAIL (data != NULL, UTIL_DUMP_ERR_INTERNAL);
    XDBG_RETURN_VAL_IF_FAIL (width > 0, UTIL_DUMP_ERR_INTERNAL);
    XDBG_RETURN_VAL_IF_FAIL (height > 0, UTIL_DUMP_ERR_INTERNAL);

    XDBG_TRACE (MSEC, "%s : width(%d) height(%d)\n",
                __FUNCTION__, width, height);

    FILE * fp = fopen (file, "w+");
    if (fp == NULL)
    {
        return UTIL_DUMP_ERR_OPENFILE;
    }
    else
    {
        bmpfile_header.filesz = sizeof (bmpfile_magic) + sizeof (bmpfile_header) +
                                sizeof (bmp_dib_v3_header_t) + width * height * 3;
        bmp_dib_v3_header_t.header_sz = sizeof (bmp_dib_v3_header_t);
        bmp_dib_v3_header_t.width = width;
        bmp_dib_v3_header_t.height = -height;
        bmp_dib_v3_header_t.nplanes = 1;
        bmp_dib_v3_header_t.bmp_bytesz = width * height * 3;

        fwrite (&bmpfile_magic, sizeof (bmpfile_magic), 1, fp);
        fwrite (&bmpfile_header, sizeof (bmpfile_header), 1, fp);
        fwrite (&bmp_dib_v3_header_t, sizeof (bmp_dib_v3_header_t), 1, fp);

        blocks = (unsigned int*)data;
        for (i=0; i<height * width; i++)
            fwrite (&blocks[i], 3, 1, fp);

        fclose (fp);
    }

    return UTIL_DUMP_OK;
}

int secUtilDumpShm (int shmid, const void * data, int width, int height)
{
    char * addr;
    struct shmid_ds ds;

    addr = shmat (shmid, 0, 0);
    if (addr == (void*)-1)
    {
        return UTIL_DUMP_ERR_SHMATTACH;
    }

    if ((shmctl (shmid, IPC_STAT, &ds) < 0) || (ds.shm_segsz < width*height*4))
    {
        shmctl (shmid, IPC_RMID, NULL);
        shmdt (addr);

        return UTIL_DUMP_ERR_SEGSIZE;
    }

    memcpy (addr, data, width*height*4);

    shmctl (shmid, IPC_RMID, NULL);
    shmdt (addr);

    return UTIL_DUMP_OK;
}

int secUtilDumpRaw (const char * file, const void * data, int size)
{
//	int i;
    unsigned int * blocks;

    FILE * fp = fopen (file, "w+");
    if (fp == NULL)
    {
        return UTIL_DUMP_ERR_OPENFILE;
    }
    else
    {
        blocks = (unsigned int*)data;
        fwrite (blocks, 1, size, fp);

        fclose (fp);
    }

    return UTIL_DUMP_OK;
}

int
secUtilDumpPixmap (const char * file, PixmapPtr pPixmap)
{
    SECPixmapPriv *privPixmap;
    Bool need_finish = FALSE;
    int ret;

    XDBG_RETURN_VAL_IF_FAIL (pPixmap != NULL, UTIL_DUMP_ERR_INTERNAL);
    XDBG_RETURN_VAL_IF_FAIL (file != NULL, UTIL_DUMP_ERR_INTERNAL);

    privPixmap = exaGetPixmapDriverPrivate (pPixmap);

    if (!privPixmap->bo)
    {
        need_finish = TRUE;
        secExaPrepareAccess (pPixmap, EXA_PREPARE_DEST);
        XDBG_RETURN_VAL_IF_FAIL (privPixmap->bo != NULL, UTIL_DUMP_ERR_INTERNAL);
    }

    ret = secUtilDumpBmp (file, tbm_bo_get_handle (privPixmap->bo, TBM_DEVICE_CPU).ptr,
                          pPixmap->devKind/(pPixmap->drawable.bitsPerPixel >> 3),
                          pPixmap->drawable.height);

    if (need_finish)
        secExaFinishAccess (pPixmap, EXA_PREPARE_DEST);

    return ret;
}

typedef struct _DumpBufInfo
{
    int    index;

    tbm_bo bo;
    int    bo_size;

    char   file[128];
    Bool   dirty;
    Bool   dump_bmp;

    int    width;
    int    height;
    xRectangle rect;
    int    size;

    struct xorg_list link;
} DumpBufInfo;

typedef struct _DumpInfo
{
    ScrnInfoPtr pScrn;

    struct xorg_list *cursor;
    struct xorg_list  bufs;
} DumpInfo;

static Bool
_calculateSize (int width, int height, xRectangle *crop)
{
    if (crop->x < 0)
    {
        crop->width += (crop->x);
        crop->x = 0;
    }
    if (crop->y < 0)
    {
        crop->height += (crop->y);
        crop->y = 0;
    }

    XDBG_GOTO_IF_FAIL (width > 0 && height > 0, fail_cal);
    XDBG_GOTO_IF_FAIL (crop->width > 0 && crop->height > 0, fail_cal);
    XDBG_GOTO_IF_FAIL (crop->x >= 0 && crop->x < width, fail_cal);
    XDBG_GOTO_IF_FAIL (crop->y >= 0 && crop->y < height, fail_cal);

    if (crop->x + crop->width > width)
        crop->width = width - crop->x;

    if (crop->y + crop->height > height)
        crop->height = height - crop->y;

    return TRUE;
fail_cal:
    XDBG_ERROR (MSEC, "(%dx%d : %d,%d %dx%d)\n",
                width, height, crop->x, crop->y, crop->width, crop->height);

    return FALSE;
}

static void
_secUtilConvertBosG2D (tbm_bo src_bo, int sw, int sh, xRectangle *sr, int sstride,
                       tbm_bo dst_bo, int dw, int dh, xRectangle *dr, int dstride,
                       Bool composite, int rotate)
{
    G2dImage *srcImg = NULL, *dstImg = NULL;
    tbm_bo_handle src_bo_handle = {0,};
    tbm_bo_handle dst_bo_handle = {0,};
    G2dColorKeyMode mode;
    G2dOp op;

    mode = G2D_COLOR_FMT_ARGB8888 | G2D_ORDER_AXRGB;
    src_bo_handle = tbm_bo_map (src_bo, TBM_DEVICE_2D, TBM_OPTION_READ);
    XDBG_GOTO_IF_FAIL (src_bo_handle.s32 > 0, access_done);

    dst_bo_handle = tbm_bo_map (dst_bo, TBM_DEVICE_2D, TBM_OPTION_WRITE);
    XDBG_GOTO_IF_FAIL (dst_bo_handle.s32 > 0, access_done);

    srcImg = g2d_image_create_bo (mode, sw, sh, src_bo_handle.s32, sstride);
    XDBG_GOTO_IF_FAIL (srcImg != NULL, access_done);

    dstImg = g2d_image_create_bo (mode, dw, dh, dst_bo_handle.s32, dstride);
    XDBG_GOTO_IF_FAIL (dstImg != NULL, access_done);

    if (!composite)
        op = G2D_OP_SRC;
    else
        op = G2D_OP_OVER;

    if (rotate == 270)
        srcImg->rotate_90 = 1;
    else if (rotate == 180)
    {
        srcImg->xDir = 1;
        srcImg->yDir = 1;
    }
    else if (rotate == 90)
    {
        srcImg->rotate_90 = 1;
        srcImg->xDir = 1;
        srcImg->yDir = 1;
    }

    util_g2d_blend_with_scale (op, srcImg, dstImg,
                               (int)sr->x, (int)sr->y, sr->width, sr->height,
                               (int)dr->x, (int)dr->y, dr->width, dr->height,
                               FALSE);
    g2d_exec ();

access_done:
    if (src_bo_handle.s32)
        tbm_bo_unmap (src_bo);
    if (dst_bo_handle.s32)
        tbm_bo_unmap (dst_bo);
    if (srcImg)
        g2d_image_free (srcImg);
    if (dstImg)
        g2d_image_free (dstImg);
}

static void
_secUtilConvertBosPIXMAN (tbm_bo src_bo, int sw, int sh, xRectangle *sr, int sstride,
                          tbm_bo dst_bo, int dw, int dh, xRectangle *dr, int dstride,
                          Bool composite, int rotate)
{
    tbm_bo_handle src_bo_handle = {0,};
    tbm_bo_handle dst_bo_handle = {0,};
    pixman_op_t op;

    src_bo_handle = tbm_bo_map (src_bo, TBM_DEVICE_CPU, TBM_OPTION_READ);
    XDBG_GOTO_IF_FAIL (src_bo_handle.ptr != NULL, access_done);

    dst_bo_handle = tbm_bo_map (dst_bo, TBM_DEVICE_CPU, TBM_OPTION_WRITE);
    XDBG_GOTO_IF_FAIL (dst_bo_handle.ptr != NULL, access_done);

    if (!composite)
        op = PIXMAN_OP_SRC;
    else
        op = PIXMAN_OP_OVER;

    secUtilConvertImage (op, src_bo_handle.ptr, dst_bo_handle.ptr,
                         PIXMAN_a8r8g8b8, PIXMAN_a8r8g8b8,
                         sw, sh, sr,
                         dw, dh, dr,
                         NULL,
                         rotate, FALSE, FALSE);

access_done:
    if (src_bo_handle.ptr)
        tbm_bo_unmap (src_bo);
    if (dst_bo_handle.ptr)
        tbm_bo_unmap (dst_bo);
}

/* support only RGB  */
void
secUtilConvertBos (ScrnInfoPtr pScrn,
                   tbm_bo src_bo, int sw, int sh, xRectangle *sr, int sstride,
                   tbm_bo dst_bo, int dw, int dh, xRectangle *dr, int dstride,
                   Bool composite, int rotate)
{
    SECPtr pSec = SECPTR (pScrn);

    XDBG_RETURN_IF_FAIL (pScrn != NULL);
    XDBG_RETURN_IF_FAIL (src_bo != NULL);
    XDBG_RETURN_IF_FAIL (dst_bo != NULL);
    XDBG_RETURN_IF_FAIL (sr != NULL);
    XDBG_RETURN_IF_FAIL (dr != NULL);

    pSec = SECPTR (pScrn);
    XDBG_RETURN_IF_FAIL (pSec != NULL);

    if (!_calculateSize (sw, sh, sr))
        return;
    if (!_calculateSize (dw, dh, dr))
        return;

    if (rotate < 0)
        rotate += 360;

    XDBG_DEBUG (MVA, "[%dx%d (%d,%d %dx%d) %d] => [%dx%d (%d,%d %dx%d) %d] comp(%d) rot(%d) G2D(%d)\n",
                sw, sh, sr->x, sr->y, sr->width, sr->height, sstride,
                dw, dh, dr->x, dr->y, dr->width, dr->height, dstride,
                composite, rotate, pSec->is_accel_2d);

    if (pSec->is_accel_2d)
        _secUtilConvertBosG2D (src_bo, sw, sh, sr, sstride,
                               dst_bo, dw, dh, dr, dstride,
                               composite, rotate);
    else
    {
        _secUtilConvertBosPIXMAN (src_bo, sw, sh, sr, sstride,
                                  dst_bo, dw, dh, dr, dstride,
                                  composite, rotate);
        tbm_bo_map(src_bo, TBM_DEVICE_CPU, TBM_OPTION_WRITE);
        tbm_bo_unmap(src_bo);
    }
}

void*
secUtilPrepareDump (ScrnInfoPtr pScrn, int bo_size, int buf_cnt)
{
    SECPtr pSec = SECPTR (pScrn);
    DumpInfo *dump;
    int i;

    dump = calloc (1, sizeof (DumpInfo));
    XDBG_RETURN_VAL_IF_FAIL (dump != NULL, NULL);

    bo_size = bo_size / DUMP_SCALE_RATIO;

    dump->pScrn = pScrn;

    xorg_list_init (&dump->bufs);

    for (i = 0; i < buf_cnt; i++)
    {
        tbm_bo bo = tbm_bo_alloc (pSec->tbm_bufmgr, bo_size, TBM_BO_DEFAULT);
        XDBG_GOTO_IF_FAIL (bo != NULL, fail_prepare);

        DumpBufInfo *buf_info = calloc (1, sizeof (DumpBufInfo));
        if (!buf_info)
        {
            tbm_bo_unref (bo);
            XDBG_WARNING_IF_FAIL (buf_info != NULL);
            goto fail_prepare;
        }

        buf_info->index = i;
        buf_info->bo = bo;
        buf_info->bo_size = bo_size;

        tbm_bo_handle handle = tbm_bo_map (bo, TBM_DEVICE_CPU, TBM_OPTION_WRITE);
        memset (handle.ptr, 0x00, buf_info->bo_size);
        tbm_bo_unmap (bo);

        xorg_list_add (&buf_info->link, &dump->bufs);
    }

    dump->cursor = &dump->bufs;

    return (void*)dump;

fail_prepare:
    secUtilFinishDump (dump);
    return NULL;
}

void
secUtilDoDumpRaws (void *d, tbm_bo *bo, int *size, int bo_cnt, const char *file)
{
    DumpInfo *dump = (DumpInfo*)d;
    DumpBufInfo *next = NULL;
    struct xorg_list *next_cursor;
    void *src_ptr, *dst_ptr;
    int i, remain_size, need_size;

    if (!dump || !bo)
        return;

    CARD32 prev = GetTimeInMillis ();

    next_cursor = dump->cursor->next;
    XDBG_RETURN_IF_FAIL (next_cursor != NULL);

    if (next_cursor == &dump->bufs)
    {
        next_cursor = next_cursor->next;
        XDBG_RETURN_IF_FAIL (next_cursor != NULL);
    }

    next = xorg_list_entry (next_cursor, DumpBufInfo, link);
    XDBG_RETURN_IF_FAIL (next != NULL);

    need_size = 0;
    for (i = 0; i < bo_cnt; i++)
        need_size += size[i];
    if (need_size > next->bo_size)
    {
        SECPtr pSec = SECPTR (dump->pScrn);
        tbm_bo new_bo = tbm_bo_alloc (pSec->tbm_bufmgr, need_size, TBM_BO_DEFAULT);
        XDBG_RETURN_IF_FAIL (new_bo != NULL);
        tbm_bo_unref (next->bo);
        next->bo = new_bo;
        next->bo_size = need_size;
    }

    dst_ptr = tbm_bo_map (next->bo, TBM_DEVICE_CPU, TBM_OPTION_WRITE).ptr;
    XDBG_RETURN_IF_FAIL (dst_ptr != NULL);

    remain_size = next->bo_size;
    for (i = 0; i < bo_cnt; i++)
    {
        XDBG_GOTO_IF_FAIL (size[i] <= remain_size, end_dump_raws);

        src_ptr = tbm_bo_map (bo[i], TBM_DEVICE_CPU, TBM_OPTION_READ).ptr;
        XDBG_GOTO_IF_FAIL (src_ptr != NULL, end_dump_raws);

        memcpy (dst_ptr, src_ptr, size[i]);
        dst_ptr += size[i];

        if (i == 0)
            next->size = 0;

        next->size += size[i];
        remain_size -= size[i];

        tbm_bo_unmap (bo[i]);
    }

    snprintf (next->file, sizeof (next->file), "%.3f_%s", GetTimeInMillis()/1000.0, file);
    memset (&next->rect, 0, sizeof (xRectangle));
    next->dirty = TRUE;
    next->dump_bmp = FALSE;

    XDBG_TRACE (MSEC, "DumpRaws: %ld(%d)\n", GetTimeInMillis () - prev, next->index);

    dump->cursor = next_cursor;

end_dump_raws:
    tbm_bo_unmap (next->bo);

    return;
}

void
secUtilDoDumpBmps (void *d, tbm_bo bo, int w, int h, xRectangle *crop, const char *file)
{
    DumpInfo *dump = (DumpInfo*)d;
    DumpBufInfo *next = NULL;
    struct xorg_list *next_cursor;
    int scale_w = w / DUMP_SCALE_RATIO;
    int scale_h = h / DUMP_SCALE_RATIO;
    xRectangle temp = {0,};

    if (!dump || !bo)
        return;

    next_cursor = dump->cursor->next;
    XDBG_RETURN_IF_FAIL (next_cursor != NULL);

    if (next_cursor == &dump->bufs)
    {
        next_cursor = next_cursor->next;
        XDBG_RETURN_IF_FAIL (next_cursor != NULL);
    }

    next = xorg_list_entry (next_cursor, DumpBufInfo, link);
    XDBG_RETURN_IF_FAIL (next != NULL);

    tbm_bo_handle src_handle = tbm_bo_get_handle (bo, TBM_DEVICE_CPU);
    tbm_bo_handle dst_handle = tbm_bo_get_handle (next->bo, TBM_DEVICE_CPU);
    XDBG_RETURN_IF_FAIL (src_handle.ptr != NULL);
    XDBG_RETURN_IF_FAIL (dst_handle.ptr != NULL);
    XDBG_RETURN_IF_FAIL (scale_w*scale_h*4 <= next->bo_size);

    CARD32 prev = GetTimeInMillis ();

    snprintf (next->file, sizeof (next->file), "%.3f_%s", GetTimeInMillis()/1000.0, file);

    next->dirty = TRUE;
    next->dump_bmp = TRUE;
    next->size = scale_w*scale_h*4;

    next->width = scale_w;
    next->height = scale_h;
    next->rect.x = 0;
    next->rect.y = 0;
    next->rect.width = (crop)?(crop->width/DUMP_SCALE_RATIO):next->width;
    next->rect.height = (crop)?(crop->height/DUMP_SCALE_RATIO):next->height;

    temp.width = (crop)?crop->width:w;
    temp.height = (crop)?crop->height:h;

    secUtilConvertBos (dump->pScrn, bo, w, h, &temp, w*4,
                       next->bo, scale_w, scale_h, &next->rect, scale_w*4,
                       FALSE, 0);

    XDBG_TRACE (MSEC, "DumpBmps: %ld(%d)\n", GetTimeInMillis () - prev, next->index);

    dump->cursor = next_cursor;

    return;
}

void
secUtilDoDumpPixmaps (void *d, PixmapPtr pPixmap, const char *file)
{
    SECPixmapPriv *privPixmap;
    xRectangle rect = {0,};
    Bool need_finish = FALSE;

    XDBG_RETURN_IF_FAIL (d != NULL);
    XDBG_RETURN_IF_FAIL (pPixmap != NULL);
    XDBG_RETURN_IF_FAIL (file != NULL);

    privPixmap = exaGetPixmapDriverPrivate (pPixmap);

    if (!privPixmap->bo)
    {
        need_finish = TRUE;
        secExaPrepareAccess (pPixmap, EXA_PREPARE_DEST);
        XDBG_RETURN_IF_FAIL (privPixmap->bo != NULL);
    }

    rect.width = pPixmap->drawable.width;
    rect.height = pPixmap->drawable.height;

    secUtilDoDumpBmps (d, privPixmap->bo,
                       pPixmap->devKind/(pPixmap->drawable.bitsPerPixel >> 3),
                       pPixmap->drawable.height, &rect, file);

    if (need_finish)
        secExaFinishAccess (pPixmap, EXA_PREPARE_DEST);
}

void
secUtilDoDumpVBuf (void *d, SECVideoBuf *vbuf, const char *file)
{
    XDBG_RETURN_IF_FAIL (d != NULL);
    XDBG_RETURN_IF_FAIL (vbuf != NULL);
    XDBG_RETURN_IF_FAIL (file != NULL);
    XDBG_RETURN_IF_FAIL (vbuf->secure == FALSE);

    if (IS_RGB (vbuf->id))
        secUtilDoDumpBmps (d, vbuf->bo[0], vbuf->width, vbuf->height, &vbuf->crop, file);
    else if (vbuf->id == FOURCC_SN12 || vbuf->id == FOURCC_ST12)
        secUtilDoDumpRaws (d, vbuf->bo, vbuf->lengths, 2, file);
    else
        secUtilDoDumpRaws (d, vbuf->bo, &vbuf->size, 1, file);
}

void
secUtilFlushDump (void *d)
{
    static Bool is_dir = FALSE;
    char *dir = DUMP_DIR;
    DumpInfo *dump = (DumpInfo*)d;
    DumpBufInfo *cur = NULL, *next = NULL;

    if (!dump)
        return;

    if (!is_dir)
    {
        DIR *dp;
        mkdir (dir, 0755);
        if (!(dp = opendir (dir)))
        {
            ErrorF ("failed: open'%s'\n", dir);
            return;
        }
        else
            closedir (dp);
        is_dir = TRUE;
    }

    xorg_list_for_each_entry_safe (cur, next, &dump->bufs, link)
    {
        if (cur->dirty)
        {
            if (cur->bo)
            {
                char file[128];

                snprintf (file, sizeof(file), "%s/%s", dir, cur->file);

                if (cur->dump_bmp)
                {
                    unsigned int *p;
                    tbm_bo_handle handle = tbm_bo_map (cur->bo, TBM_DEVICE_CPU, TBM_OPTION_READ);
                    XDBG_GOTO_IF_FAIL (handle.ptr != NULL, reset_dump);

                    /* fill magenta color(#FF00FF) for background */
                    p = (unsigned int*)handle.ptr;
                    if (p)
                    {
                        int i, j;
                        for (j = 0; j < cur->height; j++)
                            for (i = cur->rect.width; i < cur->width ; i++)
                                p[i + j * cur->width] = 0xFFFF00FF;
                    }

                    secUtilDumpBmp (file, handle.ptr, cur->width, cur->height);

                    tbm_bo_unmap (cur->bo);
                }
                else
                {
                    tbm_bo_handle handle = tbm_bo_map (cur->bo, TBM_DEVICE_CPU, TBM_OPTION_READ);
                    XDBG_GOTO_IF_FAIL (handle.ptr != NULL, reset_dump);

                    secUtilDumpRaw (file, handle.ptr, cur->size);

                    tbm_bo_unmap (cur->bo);
                }

                XDBG_ERROR (MSEC, "%s saved\n", file);
            }

reset_dump:
            {
                tbm_bo_handle handle = tbm_bo_map (cur->bo, TBM_DEVICE_CPU, TBM_OPTION_WRITE);
                memset (handle.ptr, 0x00, cur->bo_size);
                tbm_bo_unmap (cur->bo);
            }
            cur->width = 0;
            cur->height = 0;
            cur->size = 0;
            memset (&cur->rect, 0, sizeof (xRectangle));
            cur->file[0] = '\0';
            cur->dirty = FALSE;
            cur->dump_bmp = FALSE;
        }
    }
}

void
secUtilFinishDump (void *d)
{
    DumpInfo *dump = (DumpInfo*)d;
    DumpBufInfo *cur = NULL, *next = NULL;

    if (!dump)
        return;

    xorg_list_for_each_entry_safe (cur, next, &dump->bufs, link)
    {
        if (cur->bo)
            tbm_bo_unref (cur->bo);
        xorg_list_del (&cur->link);
        free (cur);
    }
    free (dump);
}

#ifndef RR_Rotate_All
#define RR_Rotate_All	(RR_Rotate_0|RR_Rotate_90|RR_Rotate_180|RR_Rotate_270)
#endif

int secUtilDegreeToRotate (int degree)
{
    int rotate;

    switch (degree)
    {
    case 0:
        rotate = RR_Rotate_0;
        break;
    case 90:
        rotate = RR_Rotate_90;
        break;
    case 180:
        rotate = RR_Rotate_180;
        break;
    case 270:
        rotate = RR_Rotate_270;
        break;
    default:
        rotate = 0;	/* ERROR */
        break;
    }

    return rotate;
}

int secUtilRotateToDegree (int rotate)
{
    int degree;

    switch (rotate & RR_Rotate_All)
    {
    case RR_Rotate_0:
        degree = 0;
        break;
    case RR_Rotate_90:
        degree = 90;
        break;
    case RR_Rotate_180:
        degree = 180;
        break;
    case RR_Rotate_270:
        degree = 270;
        break;
    default:
        degree = -1;	/* ERROR */
        break;
    }

    return degree;
}

static int
_secUtilRotateToInt (int rot)
{
    switch (rot & RR_Rotate_All)
    {
    case RR_Rotate_0:
        return 0;
    case RR_Rotate_90:
        return 1;
    case RR_Rotate_180:
        return 2;
    case RR_Rotate_270:
        return 3;
    }

    return 0;
}

int secUtilRotateAdd (int rot_a, int rot_b)
{
    int a = _secUtilRotateToInt (rot_a);
    int b = _secUtilRotateToInt (rot_b);

    return (int) ((1 << ((a+b) %4)) &RR_Rotate_All);
}

void
secUtilCacheFlush (ScrnInfoPtr scrn)
{
    struct drm_exynos_gem_cache_op cache_op;
    SECPtr pSec;
    int ret;

    XDBG_RETURN_IF_FAIL (scrn != NULL);

    pSec = SECPTR (scrn);

    CLEAR (cache_op);
    cache_op.flags = EXYNOS_DRM_CACHE_FSH_ALL | EXYNOS_DRM_ALL_CACHES_CORES;
    cache_op.usr_addr = 0;
    cache_op.size = 0;

    ret = drmCommandWriteRead (pSec->drm_fd, DRM_EXYNOS_GEM_CACHE_OP,
                               &cache_op, sizeof(cache_op));
    if (ret)
        xf86DrvMsg (scrn->scrnIndex, X_ERROR,
                    "cache flush failed. (%s)\n", strerror(errno));
}

const PropertyPtr
secUtilGetWindowProperty (WindowPtr pWin, const char* prop_name)
{
    int rc;
    Mask prop_mode = DixReadAccess;
    Atom property;
    PropertyPtr pProp;

    if (!prop_name)
        return NULL;

    property = MakeAtom (prop_name, strlen (prop_name), FALSE);
    if (property == None)
        return NULL;

    rc = dixLookupProperty (&pProp, pWin, property, serverClient, prop_mode);
    if (rc == Success && pProp->data)
    {
        return pProp;
    }

    return NULL;
}

static void *
_copy_one_channel (int width, int height,
                   char *s, int s_size_w, int s_pitches,
                   char *d, int d_size_w, int d_pitches)
{
    uchar *src = (uchar*)s;
    uchar *dst = (uchar*)d;

    if (d_size_w == width && s_size_w == width)
        memcpy (dst, src, s_pitches * height);
    else
    {
        int i;

        for (i = 0; i < height; i++)
        {
            memcpy (dst, src, s_pitches);
            src += s_pitches;
            dst += d_pitches;
        }
    }

    return dst;
}

void*
secUtilCopyImage (int width, int height,
                  char *s, int s_size_w, int s_size_h,
                  int *s_pitches, int *s_offsets, int *s_lengths,
                  char *d, int d_size_w, int d_size_h,
                  int *d_pitches, int *d_offsets, int *d_lengths,
                  int channel, int h_sampling, int v_sampling)
{
    int i;

    for (i = 0; i < channel; i++)
    {
        int c_width = width;
        int c_height = height;

        if (i > 0)
        {
            c_width = c_width / h_sampling;
            c_height = c_height / v_sampling;
        }

        _copy_one_channel (c_width, c_height,
                           s, s_size_w, s_pitches[i],
                           d, d_size_w, d_pitches[i]);

        s = s + s_lengths[i];
        d = d + d_lengths[i];
    }

    return d;
}

/*
 * RR_Rotate_90:  Target turns to 90. UI turns to 270.
 * RR_Rotate_270: Target turns to 270. UI turns to 90.
 *
 *     [Target]            ----------
 *                         |        |
 *     Top (RR_Rotate_90)  |        |  Top (RR_Rotate_270)
 *                         |        |
 *                         ----------
 *     [UI,FIMC]           ----------
 *                         |        |
 *      Top (degree: 270)  |        |  Top (degree: 90)
 *                         |        |
 *                         ----------
 */
void
secUtilRotateArea (int *width, int *height, xRectangle *rect, int degree)
{
//    int old_w, old_h;

    XDBG_RETURN_IF_FAIL (width != NULL);
    XDBG_RETURN_IF_FAIL (height != NULL);
    XDBG_RETURN_IF_FAIL (rect != NULL);

    if (degree == 0)
        return;

    secUtilRotateRect (*width, *height, rect, degree);

//    old_w = *width;
//    old_h = *height;

    if (degree % 180)
        SWAP (*width, *height);

//    ErrorF ("%d: (%dx%d) => (%dx%d)\n", degree, old_w, old_h, *width, *height);
}

void
secUtilRotateRect2 (int width, int height, xRectangle *rect, int degree, const char *func)
{
    xRectangle new_rect = {0,};

    XDBG_RETURN_IF_FAIL (rect != NULL);

    if (degree == 0)
        return;

    degree = (degree + 360) % 360;

    switch (degree)
    {
    case 90:
        new_rect.x = height - (rect->y + rect->height);
        new_rect.y = rect->x;
        new_rect.width  = rect->height;
        new_rect.height = rect->width;
        break;
    case 180:
        new_rect.x = width  - (rect->x + rect->width);
        new_rect.y = height - (rect->y + rect->height);
        new_rect.width  = rect->width;
        new_rect.height = rect->height;
        break;
    case 270:
        new_rect.x = rect->y;
        new_rect.y = width - (rect->x + rect->width);
        new_rect.width  = rect->height;
        new_rect.height = rect->width;
        break;
    }

//    ErrorF ("%d: %dx%d (%d,%d %dx%d) => (%d,%d %dx%d)   %s\n",
//            degree, width, height,
//            rect->x, rect->y, rect->width, rect->height,
//            new_rect.x, new_rect.y, new_rect.width, new_rect.height, func);

    *rect = new_rect;
}

void
secUtilRotateRegion (int width, int height, RegionPtr region, int degree)
{
    RegionRec new_region;
    int    nbox;
    BoxPtr pBox;

    if (!region)
        return;

    nbox = RegionNumRects (region);
    pBox = RegionRects (region);

    if (nbox == 0)
        return;

    RegionInit (&new_region, NULL, 0);

    while (nbox--)
    {
        if (pBox)
        {
            xRectangle temp;
            RegionPtr temp_region;
            temp.x = pBox->x1;
            temp.y = pBox->y1;
            temp.width  = pBox->x2 - pBox->x1;
            temp.height = pBox->y2 - pBox->y1;
            secUtilRotateRect (width, height, &temp, degree);
            temp_region = RegionFromRects (1, &temp, 0);
            RegionUnion (&new_region, &new_region, temp_region);
            RegionDestroy (temp_region);
        }

        pBox++;
    }

    RegionCopy (region, &new_region);
    RegionUninit (&new_region);
}

void
secUtilAlignRect (int src_w, int src_h, int dst_w, int dst_h, xRectangle *fit, Bool hw)
{
    int fit_width;
    int fit_height;
    float rw, rh, max;

    if (!fit)
        return;

    XDBG_RETURN_IF_FAIL (src_w > 0 && src_h > 0);
    XDBG_RETURN_IF_FAIL (dst_w > 0 && dst_h > 0);

    rw = (float)src_w / dst_w;
    rh = (float)src_h / dst_h;
    max = MAX (rw, rh);

    fit_width = src_w / max;
    fit_height = src_h / max;

    if (hw)
        fit_width &= (~0x3);

    fit->x = (dst_w - fit_width) / 2;
    fit->y = (dst_h - fit_height) / 2;
    fit->width = fit_width;
    fit->height = fit_height;
}

void
secUtilScaleRect (int src_w, int src_h, int dst_w, int dst_h, xRectangle *scale)
{
    float ratio;
    xRectangle fit;

    XDBG_RETURN_IF_FAIL (scale != NULL);
    XDBG_RETURN_IF_FAIL (src_w > 0 && src_h > 0);
    XDBG_RETURN_IF_FAIL (dst_w > 0 && dst_h > 0);

    if ((src_w == dst_w) && (src_h == dst_h))
        return;

    secUtilAlignRect (src_w, src_h, dst_w, dst_h, &fit, FALSE);

    ratio = (float)fit.width / src_w;

    scale->x = scale->x * ratio + fit.x;
    scale->y = scale->y * ratio + fit.y;
    scale->width = scale->width * ratio;
    scale->height = scale->height * ratio;
}

/*  true iff two Boxes overlap */
#define EXTENTCHECK(r1, r2)	   \
    (!( ((r1)->x2 <= (r2)->x1)  || \
        ((r1)->x1 >= (r2)->x2)  || \
        ((r1)->y2 <= (r2)->y1)  || \
        ((r1)->y1 >= (r2)->y2) ) )

/* true iff (x,y) is in Box */
#define INBOX(r, x, y)	\
    ( ((r)->x2 >  x) && \
      ((r)->x1 <= x) && \
      ((r)->y2 >  y) && \
      ((r)->y1 <= y) )

/* true iff Box r1 contains Box r2 */
#define SUBSUMES(r1, r2)	\
    ( ((r1)->x1 <= (r2)->x1) && \
      ((r1)->x2 >= (r2)->x2) && \
      ((r1)->y1 <= (r2)->y1) && \
      ((r1)->y2 >= (r2)->y2) )

int
secUtilBoxInBox (BoxPtr base, BoxPtr box)
{
    XDBG_RETURN_VAL_IF_FAIL(base != NULL, -1);
    XDBG_RETURN_VAL_IF_FAIL(box != NULL, -1);

    if(base->x1 == box->x1 && base->y1 == box->y1 && base->x2 == box->x2 && base->y2 == box->y2)
    {
        return rgnSAME;
    }
    else if(SUBSUMES(base, box))
    {
        return rgnIN;
    }
    else if(EXTENTCHECK(base, box))
    {
        return rgnPART;
    }
    else
        return rgnOUT;

    return -1;
}

int
secUtilBoxArea(BoxPtr pBox)
{
    return (int) (pBox->x2 - pBox->x1) *(int) (pBox->y2 -pBox->y1);
}

int
secUtilBoxIntersect(BoxPtr pDstBox, BoxPtr pBox1, BoxPtr pBox2)
{
    pDstBox->x1 = pBox1->x1 > pBox2->x1 ? pBox1->x1 : pBox2->x1;
    pDstBox->x2 = pBox1->x2 < pBox2->x2 ? pBox1->x2 : pBox2->x2;
    pDstBox->y1 = pBox1->y1 > pBox2->y1 ? pBox1->y1 : pBox2->y1;
    pDstBox->y2 = pBox1->y2 < pBox2->y2 ? pBox1->y2 : pBox2->y2;

    if (pDstBox->x1 >= pDstBox->x2 || pDstBox->y1 >= pDstBox->y2)
    {
        pDstBox->x1 = 0;
        pDstBox->x2 = 0;
        pDstBox->y1 = 0;
        pDstBox->y2 = 0;
        return rgnOUT;
    }

    if (pDstBox->x1 == pBox2->x1 &&
        pDstBox->y1 == pBox2->y1 &&
        pDstBox->x2 == pBox2->x2 &&
        pDstBox->y2 == pBox2->y2)
        return rgnIN;

    return rgnPART;
}

void
secUtilBoxMove(BoxPtr pBox, int dx, int dy)
{
    if(dx == 0 && dy == 0) return;

    pBox->x1 += dx;
    pBox->x2 += dx;
    pBox->y1 += dy;
    pBox->y2 += dy;
}


Bool
secUtilRectIntersect (xRectanglePtr pDest, xRectanglePtr pRect1, xRectanglePtr pRect2)
{
    int dest_x, dest_y;
    int dest_x2, dest_y2;

    if (!pDest)
        return FALSE;

    dest_x = MAX (pRect1->x, pRect2->x);
    dest_y = MAX (pRect1->y, pRect2->y);
    dest_x2 = MIN (pRect1->x + pRect1->width, pRect2->x + pRect2->width);
    dest_y2 = MIN (pRect1->y + pRect1->height, pRect2->y + pRect2->height);

    if (dest_x2 > dest_x && dest_y2 > dest_y)
    {
        pDest->x = dest_x;
        pDest->y = dest_y;
        pDest->width = dest_x2 - dest_x;
        pDest->height = dest_y2 - dest_y;
    }
    else
    {
        pDest->width = 0;
        pDest->height = 0;
    }

    return TRUE;
}


void
secUtilSaveImage (pixman_image_t *image, char *path)
{
    void *data;
    int width, height;

    XDBG_RETURN_IF_FAIL (image != NULL);
    XDBG_RETURN_IF_FAIL (path != NULL);

    width = pixman_image_get_width (image);
    height = pixman_image_get_height (image);

    data = pixman_image_get_data (image);
    XDBG_RETURN_IF_FAIL (data != NULL);

    secUtilDumpBmp (path, data, width, height);
}

Bool
secUtilConvertImage (pixman_op_t op, uchar *srcbuf, uchar *dstbuf,
                     pixman_format_code_t src_format, pixman_format_code_t dst_format,
                     int sw, int sh, xRectangle *sr,
                     int dw, int dh, xRectangle *dr,
                     RegionPtr dst_clip_region,
                     int rotate, int hflip, int vflip)
{
    pixman_image_t    *src_img;
    pixman_image_t    *dst_img;
    int                src_stride, dst_stride;
    int                src_bpp;
    int                dst_bpp;
    double             scale_x, scale_y;
    int                rotate_step;
    int                ret = FALSE;
    pixman_transform_t t;
    struct pixman_f_transform ft;

    src_bpp = PIXMAN_FORMAT_BPP (src_format) / 8;
    XDBG_RETURN_VAL_IF_FAIL (src_bpp > 0, FALSE);

    dst_bpp = PIXMAN_FORMAT_BPP (dst_format) / 8;
    XDBG_RETURN_VAL_IF_FAIL (dst_bpp > 0, FALSE);

    src_stride = sw * src_bpp;
    dst_stride = dw * dst_bpp;

    src_img = pixman_image_create_bits (src_format, sw, sh,
                                        (uint32_t*)srcbuf, src_stride);
    dst_img = pixman_image_create_bits (dst_format, dw, dh,
                                        (uint32_t*)dstbuf, dst_stride);

    XDBG_GOTO_IF_FAIL (src_img != NULL, CANT_CONVERT);
    XDBG_GOTO_IF_FAIL (dst_img != NULL, CANT_CONVERT);

    pixman_f_transform_init_identity (&ft);

    if (hflip)
    {
        pixman_f_transform_scale (&ft, NULL, -1, 1);
        pixman_f_transform_translate (&ft, NULL, dr->width, 0);
    }

    if (vflip)
    {
        pixman_f_transform_scale (&ft, NULL, 1, -1);
        pixman_f_transform_translate (&ft, NULL, 0, dr->height);
    }

    rotate_step = (rotate + 360) / 90 % 4;

    if (rotate_step > 0)
    {
        int c, s, tx = 0, ty = 0;
        switch (rotate_step)
        {
        case 1:
            /* 90 degrees */
            c = 0;
            s = -1;
            tx = -dr->width;
            break;
        case 2:
            /* 180 degrees */
            c = -1;
            s = 0;
            tx = -dr->width;
            ty = -dr->height;
            break;
        case 3:
            /* 270 degrees */
            c = 0;
            s = 1;
            ty = -dr->height;
            break;
        default:
            /* 0 degrees */
            c = 0;
            s = 0;
            break;
        }

        pixman_f_transform_translate (&ft, NULL, tx, ty);
        pixman_f_transform_rotate (&ft, NULL, c, s);
    }

    if (rotate_step % 2 == 0)
    {
        scale_x = (double)sr->width / dr->width;
        scale_y = (double)sr->height / dr->height;
    }
    else
    {
        scale_x = (double)sr->width / dr->height;
        scale_y = (double)sr->height / dr->width;
    }

    pixman_f_transform_scale (&ft, NULL, scale_x, scale_y);
    pixman_f_transform_translate (&ft, NULL, sr->x, sr->y);

    pixman_transform_from_pixman_f_transform (&t, &ft);
    pixman_image_set_transform (src_img, &t);

    pixman_image_composite (op, src_img, NULL, dst_img, 0, 0, 0, 0,
                            dr->x, dr->y, dr->width, dr->height);

    ret = TRUE;

CANT_CONVERT:
    if (src_img)
        pixman_image_unref (src_img);
    if (dst_img)
        pixman_image_unref (dst_img);

    return ret;
}

void
secUtilFreeHandle (ScrnInfoPtr scrn, unsigned int handle)
{
    struct drm_gem_close close;
    SECPtr pSec;

    XDBG_RETURN_IF_FAIL (scrn != NULL);

    pSec = SECPTR (scrn);

    CLEAR (close);
    close.handle = handle;
    if (drmIoctl (pSec->drm_fd, DRM_IOCTL_GEM_CLOSE, &close))
    {
        XDBG_ERRNO (MSEC, "DRM_IOCTL_GEM_CLOSE failed.\n");
    }
}

Bool
secUtilConvertPhyaddress (ScrnInfoPtr scrn, unsigned int phy_addr, int size,
                          unsigned int *handle)
{
    struct drm_exynos_gem_phy_imp phy_imp = {0,};
    SECPtr pSec;

    XDBG_RETURN_VAL_IF_FAIL (scrn != NULL, FALSE);

    if (!phy_addr || size <= 0 || !handle)
        return FALSE;

    pSec = SECPTR (scrn);
    phy_imp.phy_addr = (unsigned long)phy_addr;
    phy_imp.size = (unsigned long)size;

    if (pSec->drm_fd)
        if (ioctl(pSec->drm_fd, DRM_IOCTL_EXYNOS_GEM_PHY_IMP, &phy_imp) < 0)
        {
            XDBG_ERRNO (MSEC, "DRM_IOCTL_EXYNOS_GEM_PHY_IMP failed. %p(%d)\n",
                        (void*)phy_addr, size);
            return FALSE;
        }

    *handle = phy_imp.gem_handle;

    return TRUE;
}

Bool
secUtilConvertHandle (ScrnInfoPtr scrn, unsigned int handle,
                      unsigned int *phy_addr, int *size)
{
    struct drm_exynos_gem_get_phy get_phy;
    SECPtr pSec;

    XDBG_RETURN_VAL_IF_FAIL (scrn != NULL, FALSE);

    if (handle == 0 || (!phy_addr && !size))
        return FALSE;

    pSec = SECPTR (scrn);
    memset (&get_phy, 0, sizeof (struct drm_exynos_gem_get_phy));
    get_phy.gem_handle = handle;

    if (pSec->drm_fd)
        if (ioctl(pSec->drm_fd, DRM_IOCTL_EXYNOS_GEM_GET_PHY, &get_phy) < 0)
        {
            XDBG_DEBUG (MLYR, "DRM_IOCTL_EXYNOS_GEM_GET_PHY failed. (%d)(%s,%d)\n",
                        handle, strerror(errno), errno);
            return FALSE;
        }

    if (phy_addr)
        *phy_addr = (unsigned int)get_phy.phy_addr;

    if (size)
        *size = (int)((unsigned int)get_phy.size);

    return TRUE;
}

typedef struct _ListData
{
    void *key;
    void *data;

    struct xorg_list link;
} ListData;

static ListData*
_secUtilListGet (void *list, void *key)
{
    ListData *data = NULL, *next = NULL;

    if (!list)
        return NULL;

    xorg_list_for_each_entry_safe (data, next, (struct xorg_list*)list, link)
    {
        if (data->key == key)
            return data;
    }
    return NULL;
}

void*
secUtilListAdd (void *list, void *key, void *user_data)
{
    ListData *data;
    int list_flag;

    XDBG_RETURN_VAL_IF_FAIL (key != NULL, NULL);

    if (!list)
    {
        list = calloc (sizeof (struct xorg_list), 1);
        XDBG_RETURN_VAL_IF_FAIL (list != NULL, NULL);
        xorg_list_init ((struct xorg_list*)list);
        list_flag = 1;
    }

    if (_secUtilListGet (list, key))
        return list;

    data = malloc (sizeof (ListData));
    XDBG_GOTO_IF_FAIL (data != NULL, fail);
    data->key = key;
    data->data = user_data;

    xorg_list_add (&data->link, (struct xorg_list*)list);

    return list;

fail:
    if (list_flag && list)
        free (list);

    return NULL;
}

void*
secUtilListRemove (void *list, void *key)
{
    ListData *data;

    XDBG_RETURN_VAL_IF_FAIL (key != NULL, NULL);

    data = _secUtilListGet (list, key);
    if (data)
    {
        xorg_list_del (&data->link);
        free (data);

        if (xorg_list_is_empty ((struct xorg_list*)list))
        {
            free (list);
            return NULL;
        }
    }

    return list;
}

void*
secUtilListGetData (void *list, void *key)
{
    ListData *data;

    XDBG_RETURN_VAL_IF_FAIL (key != NULL, NULL);

    data = _secUtilListGet (list, key);
    if (data)
        return data->data;

    return NULL;
}

Bool
secUtilListIsEmpty (void *list)
{
    if (!list)
        return FALSE;

    return xorg_list_is_empty ((struct xorg_list*)list);
}

void
secUtilListDestroyData (void *list, DestroyDataFunc func, void *func_data)
{
    ListData *cur = NULL, *next = NULL;
    struct xorg_list *l;

    if (!list || !func)
        return;

    l = (struct xorg_list*)list;
    xorg_list_for_each_entry_safe (cur, next, l, link)
    {
        func (func_data, cur->data);
    }
}

void
secUtilListDestroy (void *list)
{
    ListData *data = NULL, *next = NULL;
    struct xorg_list *l;

    if (!list)
        return;

    l = (struct xorg_list*)list;
    xorg_list_for_each_entry_safe (data, next, l, link)
    {
        xorg_list_del (&data->link);
        free (data);
    }

    free (list);
}

Bool
secUtilSetDrmProperty (SECModePtr pSecMode, unsigned int obj_id, unsigned int obj_type,
                       const char *prop_name, unsigned int value)
{
    drmModeObjectPropertiesPtr props;
    unsigned int i;

    XDBG_RETURN_VAL_IF_FAIL (pSecMode != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL (obj_id > 0, FALSE);
    XDBG_RETURN_VAL_IF_FAIL (obj_type > 0, FALSE);
    XDBG_RETURN_VAL_IF_FAIL (prop_name != NULL, FALSE);

    props = drmModeObjectGetProperties (pSecMode->fd, obj_id, obj_type);
    if (!props)
    {
        XDBG_ERRNO (MPLN, "fail : drmModeObjectGetProperties.\n");
        return FALSE;
    }

    for (i = 0; i < props->count_props; i++)
    {
        drmModePropertyPtr prop = drmModeGetProperty (pSecMode->fd, props->props[i]);
        int ret;

        if (!prop)
        {
            XDBG_ERRNO (MPLN, "fail : drmModeGetProperty.\n");
            drmModeFreeObjectProperties (props);
            return FALSE;
        }

        if (!strcmp (prop->name, prop_name))
        {
            ret = drmModeObjectSetProperty (pSecMode->fd, obj_id, obj_type, prop->prop_id, value);
            if (ret < 0)
            {
                XDBG_ERRNO (MPLN, "fail : drmModeObjectSetProperty.\n");
                drmModeFreeProperty(prop);
                drmModeFreeObjectProperties (props);
                return FALSE;
            }

            drmModeFreeProperty(prop);
            drmModeFreeObjectProperties (props);

            return TRUE;
        }

        drmModeFreeProperty(prop);
    }

    XDBG_ERROR (MPLN, "fail : drm set property.\n");

    drmModeFreeObjectProperties (props);

    return FALSE;
}

Bool
secUtilEnsureExternalCrtc (ScrnInfoPtr scrn)
{
    SECModePtr pSecMode;
    SECOutputPrivPtr pOutputPriv = NULL;

    XDBG_RETURN_VAL_IF_FAIL (scrn != NULL, FALSE);

    pSecMode = (SECModePtr) SECPTR (scrn)->pSecMode;

    if (pSecMode->conn_mode == DISPLAY_CONN_MODE_HDMI)
    {
        pOutputPriv = secOutputGetPrivateForConnType (scrn, DRM_MODE_CONNECTOR_HDMIA);
        if (!pOutputPriv)
            pOutputPriv = secOutputGetPrivateForConnType (scrn, DRM_MODE_CONNECTOR_HDMIB);
    }
    else
        pOutputPriv = secOutputGetPrivateForConnType (scrn, DRM_MODE_CONNECTOR_VIRTUAL);

    XDBG_RETURN_VAL_IF_FAIL (pOutputPriv != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL (pOutputPriv->mode_encoder != NULL, FALSE);

    if (pOutputPriv->mode_encoder->crtc_id > 0)
        return TRUE;

    secDisplayDeinitDispMode (scrn);

    return secDisplayInitDispMode (scrn, pSecMode->conn_mode);
}

typedef struct _VBufFreeFuncInfo
{
    FreeVideoBufFunc  func;
    void             *data;
    struct xorg_list  link;
} VBufFreeFuncInfo;

static SECFormatTable format_table[] =
{
    { FOURCC_RGB565, DRM_FORMAT_RGB565,    TYPE_RGB    },
    { FOURCC_SR16,   DRM_FORMAT_RGB565,    TYPE_RGB    },
    { FOURCC_RGB32,  DRM_FORMAT_XRGB8888,  TYPE_RGB    },
    { FOURCC_SR32,   DRM_FORMAT_XRGB8888,  TYPE_RGB    },
    { FOURCC_YV12,   DRM_FORMAT_YVU420,    TYPE_YUV420 },
    { FOURCC_I420,   DRM_FORMAT_YUV420,    TYPE_YUV420 },
    { FOURCC_S420,   DRM_FORMAT_YUV420,    TYPE_YUV420 },
    { FOURCC_ST12,   DRM_FORMAT_NV12MT,    TYPE_YUV420 },
    { FOURCC_SN12,   DRM_FORMAT_NV12,      TYPE_YUV420 },
    { FOURCC_NV12,   DRM_FORMAT_NV12,      TYPE_YUV420 },
    { FOURCC_SN21,   DRM_FORMAT_NV21,      TYPE_YUV420 },
    { FOURCC_NV21,   DRM_FORMAT_NV21,      TYPE_YUV420 },
    { FOURCC_YUY2,   DRM_FORMAT_YUYV,      TYPE_YUV422 },
    { FOURCC_SUYV,   DRM_FORMAT_YUYV,      TYPE_YUV422 },
    { FOURCC_UYVY,   DRM_FORMAT_UYVY,      TYPE_YUV422 },
    { FOURCC_SYVY,   DRM_FORMAT_UYVY,      TYPE_YUV422 },
    { FOURCC_ITLV,   DRM_FORMAT_UYVY,      TYPE_YUV422 },
};

static struct xorg_list vbuf_lists;

#define VBUF_RETURN_IF_FAIL(cond) \
    {if (!(cond)) { XDBG_ERROR (MVBUF, "[%s] : '%s' failed. (%s)\n", __FUNCTION__, #cond, func); return; }}
#define VBUF_RETURN_VAL_IF_FAIL(cond, val) \
    {if (!(cond)) { XDBG_ERROR (MVBUF, "[%s] : '%s' failed. (%s)\n", __FUNCTION__, #cond, func); return val; }}

static void
_secUtilInitVbuf (void)
{
    static Bool init = FALSE;
    if (!init)
    {
        xorg_list_init (&vbuf_lists);
        init = TRUE;
    }
}

static void
_secUtilYUV420BlackFrame (unsigned char *buf, int buf_size, int width, int height)
{
    int i;
    int y_len = 0;
    int yuv_len = 0;

    y_len = width * height;
    yuv_len = (width * height * 3) >> 1;

    if (buf_size < yuv_len)
        return;

    if (width % 4)
    {
        for (i = 0 ; i < y_len ; i++)
            buf[i] = 0x10;

        for ( ; i < yuv_len ; i++)
            buf[i] = 0x80;
    }
    else
    {
        /* faster way */
        int *ibuf = NULL;
        short *sbuf = NULL;
        ibuf = (int *)buf;

        for (i = 0 ; i < y_len / 4 ; i++)
            ibuf[i] = 0x10101010; /* set YYYY */

        sbuf = (short*)(&buf[y_len]);

        for (i = 0 ; i < (yuv_len - y_len) / 2 ; i++)
            sbuf[i] = 0x8080; /* set UV */
    }

    return;
}

static void
_secUtilYUV422BlackFrame (int id, unsigned char *buf, int buf_size, int width, int height)
{
    /* YUYV */
    int i;
    int yuv_len = 0;
    int *ibuf = NULL;

    ibuf = (int *)buf;

    yuv_len = (width * height * 2);

    if (buf_size < yuv_len)
        return;

    for (i = 0 ; i < yuv_len / 4 ; i++)
        if (id == FOURCC_UYVY || id == FOURCC_SYVY || id == FOURCC_ITLV)
            ibuf[i] = 0x80108010; /* YUYV -> 0xVYUY */
        else
            ibuf[i] = 0x10801080; /* YUYV -> 0xVYUY */

    return;
}

static tbm_bo
_secUtilAllocNormalBuffer (ScrnInfoPtr scrn, int size, int flags)
{
    SECPtr pSec = SECPTR (scrn);

    return tbm_bo_alloc (pSec->tbm_bufmgr, size, flags);
}

static tbm_bo
_secUtilAllocSecureBuffer (ScrnInfoPtr scrn, int size, int flags)
{
    SECPtr pSec = SECPTR (scrn);
    struct tzmem_get_region tzmem_get = {0,};
    struct drm_prime_handle arg_handle = {0,};
    struct drm_gem_flink arg_flink = {0,};
    struct drm_gem_close arg_close = {0,};
    tbm_bo bo = NULL;
    int tzmem_fd;

    tzmem_fd = -1;
    tzmem_get.fd = -1;

    tzmem_fd = open ("/dev/tzmem", O_EXCL);
    XDBG_GOTO_IF_FAIL (tzmem_fd >= 0, done_secure_buffer);

    tzmem_get.key = "fimc";
    tzmem_get.size = size;
    if (ioctl (tzmem_fd, TZMEM_IOC_GET_TZMEM, &tzmem_get))
    {
        XDBG_ERRNO (MVBUF, "failed : create tzmem (%d)\n", size);
        goto done_secure_buffer;
    }
    XDBG_GOTO_IF_FAIL (tzmem_get.fd >= 0, done_secure_buffer);

    arg_handle.fd = (__s32)tzmem_get.fd;
    if (drmIoctl (pSec->drm_fd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &arg_handle))
    {
        XDBG_ERRNO (MVBUF, "failed : convert to gem (%d)\n", tzmem_get.fd);
        goto done_secure_buffer;
    }
    XDBG_GOTO_IF_FAIL (arg_handle.handle > 0, done_secure_buffer);

    arg_flink.handle = arg_handle.handle;
    if (drmIoctl (pSec->drm_fd, DRM_IOCTL_GEM_FLINK, &arg_flink))
    {
        XDBG_ERRNO (MVBUF, "failed : flink gem (%ld)\n", arg_handle.handle);
        goto done_secure_buffer;
    }
    XDBG_GOTO_IF_FAIL (arg_flink.name > 0, done_secure_buffer);

    bo = tbm_bo_import (pSec->tbm_bufmgr, arg_flink.name);
    XDBG_GOTO_IF_FAIL (bo != NULL, done_secure_buffer);

done_secure_buffer:
    if (arg_handle.handle > 0)
    {
        arg_close.handle = arg_handle.handle;
        if (drmIoctl (pSec->drm_fd, DRM_IOCTL_GEM_CLOSE, &arg_close))
            XDBG_ERRNO (MVBUF, "failed : close gem (%ld)\n", arg_handle.handle);
    }

    if (tzmem_get.fd >= 0)
        close (tzmem_get.fd);

    if (tzmem_fd >= 0)
        close (tzmem_fd);

    return bo;
}

/*
 * # planar #
 * format: YV12    Y/V/U 420
 * format: I420    Y/U/V 420 #YU12, S420
 * format: NV12    Y/UV  420
 * format: NV12M   Y/UV  420 #SN12
 * format: NV12MT  Y/UV  420 #ST12
 * format: NV21    Y/VU  420
 * format: Y444    YUV   444
 * # packed #
 * format: YUY2  YUYV  422 #YUYV, SUYV, SUY2
 * format: YVYU  YVYU  422
 * format: UYVY  UYVY  422 #SYVY
 */
G2dColorMode
secUtilGetG2dFormat (unsigned int id)
{
    G2dColorMode g2dfmt = 0;

    switch (id)
    {
    case FOURCC_NV12:
    case FOURCC_SN12:
        g2dfmt = G2D_COLOR_FMT_YCbCr420 | G2D_YCbCr_2PLANE | G2D_YCbCr_ORDER_CrCb;
        break;
    case FOURCC_NV21:
    case FOURCC_SN21:
        g2dfmt = G2D_COLOR_FMT_YCbCr420 | G2D_YCbCr_2PLANE | G2D_YCbCr_ORDER_CbCr;
        break;
    case FOURCC_SUYV:
    case FOURCC_YUY2:
        g2dfmt = G2D_COLOR_FMT_YCbCr422 | G2D_YCbCr_ORDER_Y1CbY0Cr;
        break;
    case FOURCC_SYVY:
    case FOURCC_UYVY:
        g2dfmt = G2D_COLOR_FMT_YCbCr422 | G2D_YCbCr_ORDER_CbY1CrY0;
        break;
    case FOURCC_SR16:
    case FOURCC_RGB565:
        g2dfmt = G2D_COLOR_FMT_RGB565 | G2D_ORDER_AXRGB;
        break;
    case FOURCC_SR32:
    case FOURCC_RGB32:
        g2dfmt = G2D_COLOR_FMT_ARGB8888 | G2D_ORDER_AXRGB;
        break;
    case FOURCC_YV12:
    case FOURCC_I420:
    case FOURCC_S420:
    case FOURCC_ITLV:
    case FOURCC_ST12:
    default:
        XDBG_NEVER_GET_HERE (MVA);
        return 0;
    }

    return g2dfmt;
}

unsigned int
secUtilGetDrmFormat (unsigned int id)
{
    int i, size;

    size = sizeof (format_table) / sizeof (SECFormatTable);

    for (i = 0; i < size; i++)
        if (format_table[i].id == id)
            return format_table[i].drmfmt;

    return 0;
}

SECFormatType
secUtilGetColorType (unsigned int id)
{
    int i, size;

    size = sizeof (format_table) / sizeof (SECFormatTable);

    for (i = 0; i < size; i++)
        if (format_table[i].id == id)
            return format_table[i].type;

    return TYPE_NONE;
}

static SECVideoBuf*
_findVideoBuffer (CARD32 stamp)
{
    SECVideoBuf *cur = NULL, *next = NULL;

    _secUtilInitVbuf ();

    if (!vbuf_lists.next)
        return NULL;

    xorg_list_for_each_entry_safe (cur, next, &vbuf_lists, valid_link)
    {
        if (cur->stamp == stamp)
            return cur;
    }

    return NULL;
}

SECVideoBuf*
_secUtilAllocVideoBuffer (ScrnInfoPtr scrn, int id, int width, int height,
                          Bool scanout, Bool reset, Bool secure, const char *func)
{
    SECPtr pSec = SECPTR (scrn);
    SECVideoBuf *vbuf = NULL;
    int flags = 0;
    int i;
    tbm_bo_handle bo_handle;
    CARD32 stamp;

    XDBG_RETURN_VAL_IF_FAIL (scrn != NULL, NULL);
    XDBG_RETURN_VAL_IF_FAIL (id > 0, NULL);
    XDBG_RETURN_VAL_IF_FAIL (width > 0, NULL);
    XDBG_RETURN_VAL_IF_FAIL (height > 0, NULL);

    vbuf = calloc (1, sizeof (SECVideoBuf));
    XDBG_GOTO_IF_FAIL (vbuf != NULL, alloc_fail);

    vbuf->ref_cnt = 1;

    vbuf->pScrn = scrn;
    vbuf->id = id;
    vbuf->width = width;
    vbuf->height = height;
    vbuf->crop.width = width;
    vbuf->crop.height = height;

    vbuf->size = secVideoQueryImageAttrs (scrn, id, &width, &height,
                                          vbuf->pitches, vbuf->offsets, vbuf->lengths);
    XDBG_GOTO_IF_FAIL (vbuf->size > 0, alloc_fail);

    for (i = 0; i < PLANAR_CNT; i++)
    {
        int alloc_size = 0;

        if (id == FOURCC_SN12 || id == FOURCC_SN21 || id == FOURCC_ST12)
            alloc_size = vbuf->lengths[i];
        else if (i == 0)
            alloc_size = vbuf->size;

        if (alloc_size <= 0)
            continue;

        /* if i > 1, do check. */
        if (id == FOURCC_SN12 || id == FOURCC_SN21 || id == FOURCC_ST12)
        {
            XDBG_GOTO_IF_FAIL (i <= 1, alloc_fail);
        }
        else
            XDBG_GOTO_IF_FAIL (i == 0, alloc_fail);

        if (scanout)
            flags = TBM_BO_SCANOUT|TBM_BO_WC;
        else if (!pSec->cachable)
            flags = TBM_BO_WC;
        else
            flags = TBM_BO_DEFAULT;

        if (!secure)
            vbuf->bo[i] = _secUtilAllocNormalBuffer (scrn, alloc_size, flags);
        else
            vbuf->bo[i] = _secUtilAllocSecureBuffer (scrn, alloc_size, flags);
        XDBG_GOTO_IF_FAIL (vbuf->bo[i] != NULL, alloc_fail);

        vbuf->keys[i] = tbm_bo_export (vbuf->bo[i]);
        XDBG_GOTO_IF_FAIL (vbuf->keys[i] > 0, alloc_fail);

        bo_handle = tbm_bo_get_handle (vbuf->bo[i], TBM_DEVICE_DEFAULT);
        vbuf->handles[i] = bo_handle.u32;
        XDBG_GOTO_IF_FAIL (vbuf->handles[i] > 0, alloc_fail);

        if (scanout)
            secUtilConvertHandle (scrn, vbuf->handles[i], &vbuf->phy_addrs[i], NULL);

        XDBG_DEBUG (MVBUF, "handle(%d) => phy_addrs(%d) \n", vbuf->handles[i], vbuf->phy_addrs[i]);
    }

    if (reset)
        secUtilClearVideoBuffer (vbuf);

    vbuf->secure = secure;
    vbuf->dirty = TRUE;

    xorg_list_init (&vbuf->convert_info);
    xorg_list_init (&vbuf->free_funcs);

    _secUtilInitVbuf ();
    xorg_list_add (&vbuf->valid_link, &vbuf_lists);

    stamp = GetTimeInMillis ();
    while (_findVideoBuffer (stamp))
        stamp++;
    vbuf->stamp = stamp;

    vbuf->func = strdup (func);
    vbuf->flags = flags;
    vbuf->scanout = scanout;

    XDBG_DEBUG (MVBUF, "%ld alloc(flags:%x, scanout:%d): %s\n", vbuf->stamp, flags, scanout, func);

    return vbuf;

alloc_fail:
    if (vbuf)
    {
        for (i = 0; i < PLANAR_CNT && vbuf->bo[i]; i++)
            tbm_bo_unref (vbuf->bo[i]);

        free (vbuf);
    }

    return NULL;
}

SECVideoBuf*
_secUtilCreateVideoBuffer (ScrnInfoPtr scrn, int id, int width, int height, Bool secure, const char *func)
{
    SECVideoBuf *vbuf = NULL;
    CARD32 stamp;

    XDBG_RETURN_VAL_IF_FAIL (scrn != NULL, NULL);
    XDBG_RETURN_VAL_IF_FAIL (id > 0, NULL);
    XDBG_RETURN_VAL_IF_FAIL (width > 0, NULL);
    XDBG_RETURN_VAL_IF_FAIL (height > 0, NULL);

    vbuf = calloc (1, sizeof (SECVideoBuf));
    XDBG_GOTO_IF_FAIL (vbuf != NULL, alloc_fail);

    vbuf->ref_cnt = 1;

    vbuf->pScrn = scrn;
    vbuf->id = id;
    vbuf->width = width;
    vbuf->height = height;
    vbuf->crop.width = width;
    vbuf->crop.height = height;

    vbuf->size = secVideoQueryImageAttrs (scrn, id, &width, &height,
                                          vbuf->pitches, vbuf->offsets, vbuf->lengths);
    XDBG_GOTO_IF_FAIL (vbuf->size > 0, alloc_fail);

    vbuf->secure = secure;

    xorg_list_init (&vbuf->convert_info);
    xorg_list_init (&vbuf->free_funcs);

    _secUtilInitVbuf ();
    xorg_list_add (&vbuf->valid_link, &vbuf_lists);

    stamp = GetTimeInMillis ();
    while (_findVideoBuffer (stamp))
        stamp++;
    vbuf->stamp = stamp;

    vbuf->func = strdup (func);
    vbuf->flags = -1;

    XDBG_DEBUG (MVBUF, "%ld create: %s\n", vbuf->stamp, func);

    return vbuf;

alloc_fail:
    if (vbuf)
        secUtilFreeVideoBuffer (vbuf);

    return NULL;
}

SECVideoBuf*
secUtilVideoBufferRef (SECVideoBuf *vbuf)
{
    if (!vbuf)
        return NULL;

    XDBG_RETURN_VAL_IF_FAIL (VBUF_IS_VALID (vbuf), NULL);

    vbuf->ref_cnt++;

    return vbuf;
}

void
_secUtilVideoBufferUnref (SECVideoBuf *vbuf, const char *func)
{
    if (!vbuf)
        return;

    VBUF_RETURN_IF_FAIL (_secUtilIsVbufValid (vbuf, func));

    vbuf->ref_cnt--;
    if (vbuf->ref_cnt == 0)
        _secUtilFreeVideoBuffer (vbuf, func);
}

void
_secUtilFreeVideoBuffer (SECVideoBuf *vbuf, const char *func)
{
    VBufFreeFuncInfo *cur = NULL, *next = NULL;
    int i;

    if (!vbuf)
        return;

    VBUF_RETURN_IF_FAIL (_secUtilIsVbufValid (vbuf, func));
    VBUF_RETURN_IF_FAIL (!VBUF_IS_CONVERTING (vbuf));
    VBUF_RETURN_IF_FAIL (vbuf->showing == FALSE);

    xorg_list_for_each_entry_safe (cur, next, &vbuf->free_funcs, link)
    {
        /* call before tmb_bo_unref and drmModeRmFB. */
        if (cur->func)
            cur->func (vbuf, cur->data);
        xorg_list_del (&cur->link);
        free (cur);
    }

    for (i = 0; i < PLANAR_CNT; i++)
    {
        if (vbuf->bo[i])
            tbm_bo_unref (vbuf->bo[i]);
    }

    if (vbuf->fb_id > 0)
    {
        XDBG_DEBUG (MVBUF, "vbuf(%ld) fb_id(%d) removed. \n", vbuf->stamp, vbuf->fb_id);
        drmModeRmFB (SECPTR(vbuf->pScrn)->drm_fd, vbuf->fb_id);
    }

    xorg_list_del (&vbuf->valid_link);

    XDBG_DEBUG (MVBUF, "%ld freed: %s\n", vbuf->stamp, func);

    vbuf->stamp = 0;

    if (vbuf->func)
        free (vbuf->func);

    free (vbuf);
}

static void
_secUtilClearNormalVideoBuffer (SECVideoBuf *vbuf)
{
    int i;
    tbm_bo_handle bo_handle;

    if (!vbuf)
        return;

    for (i = 0; i < PLANAR_CNT; i++)
    {
        int size = 0;

        if (vbuf->id == FOURCC_SN12 || vbuf->id == FOURCC_SN21 || vbuf->id == FOURCC_ST12)
            size = vbuf->lengths[i];
        else if (i == 0)
            size = vbuf->size;

        if (size <= 0 || !vbuf->bo[i])
            continue;

        bo_handle = tbm_bo_map (vbuf->bo[i], TBM_DEVICE_CPU, TBM_OPTION_WRITE);
        XDBG_RETURN_IF_FAIL (bo_handle.ptr != NULL);

        if (vbuf->id == FOURCC_SN12 || vbuf->id == FOURCC_SN21 || vbuf->id == FOURCC_ST12)
        {
            if (i == 0)
                memset (bo_handle.ptr, 0x10, size);
            else if (i == 1)
                memset (bo_handle.ptr, 0x80, size);
        }
        else
        {
            int type = secUtilGetColorType (vbuf->id);

            if (type == TYPE_YUV420)
                _secUtilYUV420BlackFrame (bo_handle.ptr, size, vbuf->width, vbuf->height);
            else if (type == TYPE_YUV422)
                _secUtilYUV422BlackFrame (vbuf->id, bo_handle.ptr, size, vbuf->width, vbuf->height);
            else if (type == TYPE_RGB)
                memset (bo_handle.ptr, 0, size);
            else
                XDBG_NEVER_GET_HERE (MSEC);
        }

        tbm_bo_unmap (vbuf->bo[i]);
    }

    secUtilCacheFlush (vbuf->pScrn);
}

static void
_secUtilClearSecureVideoBuffer (SECVideoBuf *vbuf)
{
}

void
secUtilClearVideoBuffer (SECVideoBuf *vbuf)
{
    if (!vbuf)
        return;

    if (!vbuf->secure)
        _secUtilClearNormalVideoBuffer (vbuf);
    else
        _secUtilClearSecureVideoBuffer (vbuf);

    vbuf->dirty = FALSE;
    vbuf->need_reset = FALSE;
}

Bool
_secUtilIsVbufValid (SECVideoBuf *vbuf, const char *func)
{
    SECVideoBuf *cur = NULL, *next = NULL;

    _secUtilInitVbuf ();

    VBUF_RETURN_VAL_IF_FAIL (vbuf != NULL, FALSE);
    VBUF_RETURN_VAL_IF_FAIL (vbuf->stamp != 0, FALSE);

    xorg_list_for_each_entry_safe (cur, next, &vbuf_lists, valid_link)
    {
        if (cur->stamp == vbuf->stamp)
            return TRUE;
    }

    return FALSE;
}

static VBufFreeFuncInfo*
_secUtilFindFreeVideoBufferFunc (SECVideoBuf *vbuf, FreeVideoBufFunc func, void *data)
{
    VBufFreeFuncInfo *cur = NULL, *next = NULL;

    xorg_list_for_each_entry_safe (cur, next, &vbuf->free_funcs, link)
    {
        if (cur->func == func && cur->data == data)
            return cur;
    }

    return NULL;
}

void
secUtilAddFreeVideoBufferFunc (SECVideoBuf *vbuf, FreeVideoBufFunc func, void *data)
{
    VBufFreeFuncInfo *info;

    XDBG_RETURN_IF_FAIL (VBUF_IS_VALID (vbuf));
    XDBG_RETURN_IF_FAIL (func != NULL);

    info = _secUtilFindFreeVideoBufferFunc (vbuf, func, data);
    if (info)
        return;

    info = calloc (1, sizeof (VBufFreeFuncInfo));
    XDBG_RETURN_IF_FAIL (info != NULL);

    info->func = func;
    info->data = data;

    xorg_list_add (&info->link, &vbuf->free_funcs);
}

void
secUtilRemoveFreeVideoBufferFunc (SECVideoBuf *vbuf, FreeVideoBufFunc func, void *data)
{
    VBufFreeFuncInfo *info;

    XDBG_RETURN_IF_FAIL (VBUF_IS_VALID (vbuf));
    XDBG_RETURN_IF_FAIL (func != NULL);

    info = _secUtilFindFreeVideoBufferFunc (vbuf, func, data);
    if (!info)
        return;

    xorg_list_del (&info->link);

    free (info);
}

char*
secUtilDumpVideoBuffer (char *reply, int *len)
{
    SECVideoBuf *cur = NULL, *next = NULL;

    _secUtilInitVbuf ();

    if (xorg_list_is_empty (&vbuf_lists))
        return reply;

    XDBG_REPLY ("\nVideo buffers:\n");
    XDBG_REPLY ("id\tsize\t\t\tformat\tflags\trefcnt\tsecure\tstamp\tfunc\n");

    xorg_list_for_each_entry_safe (cur, next, &vbuf_lists, valid_link)
    {
        XDBG_REPLY ("%d\t(%dx%d,%d)\t%c%c%c%c\t%d\t%d\t%d\t%ld\t%s\n",
                    cur->fb_id, cur->width, cur->height, cur->size,
                    FOURCC_STR (cur->id),
                    cur->flags, cur->ref_cnt, cur->secure, cur->stamp, cur->func);
    }

    return reply;
}

