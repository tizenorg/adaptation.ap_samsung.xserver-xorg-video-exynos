/**************************************************************************

xserver-xorg-video-exynos

Copyright 2011 Samsung Electronics co., Ltd. All Rights Reserved.

Contact: SooChan Lim <sc1.lim@samsung.com>

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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/ioctl.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>

#include <xorgVersion.h>
#include <tbm_bufmgr.h>
#include <xf86Crtc.h>
#include <xf86DDC.h>
#include <xf86cmap.h>
#include <list.h>
#include <X11/Xatom.h>
#include <X11/extensions/dpmsconst.h>
#include <sec.h>
#include <exynos/exynos_drm.h>

#include "sec_crtc.h"
#include "sec_output.h"
#include "sec_util.h"
#include "sec_video_fourcc.h"
#include "sec_plane.h"
#include "fimg2d.h"

/* HW restriction */
#define MIN_WIDTH   32
#define MIN_HEIGHT   4

enum
{
    PLANE_FB_TYPE_NONE,
    PLANE_FB_TYPE_DEFAULT,
    PLANE_FB_TYPE_BO,
    PLANE_FB_TYPE_MAX
};

typedef struct _SECPlaneAccess
{
    unsigned int fb_id;

    tbm_bo bo;

    int width;
    int height;

    xRectangle src;
    xRectangle dst;
} SECPlaneAccess;

/* This is structure to manage a added buffer. */
typedef struct _SECPlaneFb
{
    intptr_t id;

    int type;
    union
    {
        /* for framebuffer */
        tbm_bo       bo;
        SECVideoBuf *vbuf;
    } buffer;

    int width;
    int height;

    Bool buffer_gone;

    struct xorg_list link;
} SECPlaneFb;

typedef struct _SECPlaneTable
{
    SECPlanePrivPtr pPlanePriv;
    intptr_t plane_id;

    /* buffers which this plane has */
    struct xorg_list  fbs;
    SECPlaneFb  *cur_fb;

    /* visibilitiy information */
    Bool visible;
    int  crtc_id;
    int  zpos;
    xRectangle src;
    xRectangle dst;
    int  conn_type;

    Bool onoff;
    Bool in_use;
    Bool freeze_update;

    /* accessibility */
    SECPlaneAccess *access;
} SECPlaneTable;

/* table of planes which system has entirely */
static SECPlaneTable *plane_table;
static int plane_table_size;

static SECPlaneTable* _secPlaneTableFind (int plane_id);
static SECPlaneFb* _secPlaneTableFindBuffer (SECPlaneTable *table, intptr_t fb_id,
                                             tbm_bo bo, SECVideoBuf *vbuf);
static Bool _secPlaneHideInternal (SECPlaneTable *table);
static void _secPlaneTableFreeBuffer (SECPlaneTable *table, SECPlaneFb *fb);

static void
_secPlaneFreeVbuf (SECVideoBuf *vbuf, void *data)
{
    intptr_t plane_id = (intptr_t)data;
    SECPlaneTable *table;
    SECPlaneFb *fb;

    table = _secPlaneTableFind (plane_id);
    XDBG_RETURN_IF_FAIL (table != NULL);

    fb = _secPlaneTableFindBuffer (table, 0, NULL, vbuf);
    XDBG_RETURN_IF_FAIL (fb != NULL);

    fb->buffer_gone = TRUE;
    _secPlaneTableFreeBuffer (table, fb);
}

static SECPlaneTable*
_secPlaneTableFindPos (int crtc_id, int zpos)
{
    int i;

    XDBG_RETURN_VAL_IF_FAIL (crtc_id > 0, NULL);

    for (i = 0; i < plane_table_size; i++)
        if (plane_table[i].crtc_id == crtc_id && plane_table[i].zpos == zpos)
            return &plane_table[i];

    return NULL;
}

static SECPlaneTable*
_secPlaneTableFind (int plane_id)
{
    int i;

    XDBG_RETURN_VAL_IF_FAIL (plane_id > 0, NULL);

    for (i = 0; i < plane_table_size; i++)
        if (plane_table[i].plane_id == plane_id)
            return &plane_table[i];

    XDBG_TRACE (MPLN, "plane(%d) not found. \n", plane_id);

    return NULL;
}

static SECPlaneTable*
_secPlaneTableFindEmpty (void)
{
    int i;

    for (i = 0; i < plane_table_size; i++)
        if (!plane_table[i].in_use)
            return &plane_table[i];

    return NULL;
}

static SECPlaneFb*
_secPlaneTableFindBuffer (SECPlaneTable *table,
                          intptr_t fb_id,
                          tbm_bo bo,
                          SECVideoBuf *vbuf)
{
    SECPlaneFb *fb = NULL, *fb_next = NULL;

    xorg_list_for_each_entry_safe (fb, fb_next, &table->fbs, link)
    {
        if (fb_id > 0)
        {
            if (fb->id == fb_id)
                return fb;
        }
        else if (bo)
        {
            if (fb->type == PLANE_FB_TYPE_BO && fb->buffer.bo == bo)
                return fb;
        }
        else if (vbuf)
        {
            XDBG_RETURN_VAL_IF_FAIL (VBUF_IS_VALID (vbuf), NULL);

            if (fb->type == PLANE_FB_TYPE_DEFAULT)
                if (fb->buffer.vbuf == vbuf && fb->buffer.vbuf->stamp == vbuf->stamp)
                    return fb;
        }
    }

    return NULL;
}

static void
_secPlaneTableFreeBuffer (SECPlaneTable *table, SECPlaneFb *fb)
{
    if (table->cur_fb == fb)
        return;

    if (fb->type == PLANE_FB_TYPE_BO)
    {
        if (fb->buffer.bo)
            tbm_bo_unref (fb->buffer.bo);
    }
    else
    {
        if (!fb->buffer_gone && fb->buffer.vbuf)
            secUtilRemoveFreeVideoBufferFunc (fb->buffer.vbuf, _secPlaneFreeVbuf,
                                              (void*)table->plane_id);
    }

    xorg_list_del (&fb->link);

    free (fb);
}

static Bool
_secPlaneTableEnsure (ScrnInfoPtr pScrn, int count_planes)
{
    int i;

    XDBG_RETURN_VAL_IF_FAIL (count_planes > 0, FALSE);

    if (plane_table)
    {
        if (plane_table_size != count_planes)
            XDBG_WARNING (MPLN, "%d != %d, need to re-create! \n",
                          plane_table_size, count_planes);
        return TRUE;
    }

    plane_table = calloc (sizeof (SECPlaneTable), count_planes);
    XDBG_RETURN_VAL_IF_FAIL (plane_table != NULL, FALSE);

    plane_table_size = count_planes;

    for (i = 0; i < plane_table_size; i++)
    {
        SECPlaneTable *table = &plane_table[i];
        table->plane_id = -1;
        table->onoff = TRUE;
    }

    return TRUE;
}

static void
_secPlaneExecAccessibility (tbm_bo src_bo, int sw, int sh, xRectangle *sr,
                            tbm_bo dst_bo, int dw, int dh, xRectangle *dr,
                            Bool bNegative)
{
    G2dImage *srcImg = NULL, *dstImg = NULL;
    tbm_bo_handle src_bo_handle = {0,};
    tbm_bo_handle dst_bo_handle = {0,};
    G2dColorKeyMode mode;

    mode = G2D_COLOR_FMT_ARGB8888 | G2D_ORDER_AXRGB;
    src_bo_handle = tbm_bo_map (src_bo, TBM_DEVICE_2D, TBM_OPTION_READ);
    XDBG_GOTO_IF_FAIL (src_bo_handle.s32 > 0, access_done);

    dst_bo_handle = tbm_bo_map (dst_bo, TBM_DEVICE_2D, TBM_OPTION_WRITE);
    XDBG_GOTO_IF_FAIL (dst_bo_handle.s32 > 0, access_done);

    srcImg = g2d_image_create_bo (mode, sw, sh, src_bo_handle.s32, sw * 4);
    XDBG_GOTO_IF_FAIL (srcImg != NULL, access_done);

    dstImg = g2d_image_create_bo (mode, dw, dh, dst_bo_handle.s32, dw * 4);
    XDBG_GOTO_IF_FAIL (dstImg != NULL, access_done);

    util_g2d_copy_with_scale (srcImg, dstImg,
                              (int)sr->x, (int)sr->y, sr->width, sr->height,
                              (int)dr->x, (int)dr->y, dr->width, dr->height,
                              (int)bNegative);
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
static Bool
_alignLVDSPlane (xRectanglePtr src, xRectanglePtr dst,
                 xRectanglePtr aligned_src, xRectanglePtr aligned_dst,
                 int buf_w, int buf_h, int disp_w, int disp_h)
{
    Bool ret = TRUE;
    aligned_dst->x = dst->x;
    aligned_dst->y = dst->y;
    aligned_dst->width = dst->width & (~0x1);
    aligned_dst->height = dst->height;
    aligned_src->x = src->x < 0 ? 0 : src->x;
    aligned_src->y = src->y < 0 ? 0 : src->y;


    if ((aligned_src->x + src->width > buf_w) || (aligned_src->y + src->height > buf_h))
    {
        XDBG_WARNING (MPLN, "hide: crop coord x %d y %d w %d h %d is higher than buf_w(%d) buf_h(%d)\n",
                      aligned_src->x, src->width,
                      aligned_src->y, src->height,
                      buf_w, buf_h);
        return FALSE;
    }

    aligned_src->width = src->width & (~0x1);
    aligned_src->height = src->height;

    if ((aligned_dst->width < MIN_WIDTH) || (aligned_dst->height < MIN_HEIGHT))
    {
        XDBG_WARNING (MPLN, "hide: %d, %d: buf_w(%d) buf_h(%d)\n",
                      aligned_dst->width,
                      aligned_dst->height ,
                      buf_w, buf_h);
        XDBG_WARNING (MPLN, "src(x%d,y%d w%d-h%d) dst(x%d,y%d w%d-h%d)\n",
                      src->x, src->y, src->width, src->height,
                      dst->x, dst->y, dst->width, dst->height);
        XDBG_WARNING (MPLN,"start(x%d,y%d) end(w%d,h%d)\n",
                      aligned_dst->x, aligned_dst->y,
                      aligned_dst->width, aligned_dst->height);
        ret = FALSE;
    }

    if ((aligned_src->width < MIN_WIDTH) || (aligned_src->height < MIN_HEIGHT))
    {
        XDBG_WARNING (MPLN, "hide: %d, %d: buf_w(%d) buf_h(%d)\n",
                      aligned_src->width,
                      aligned_src->height,
                      buf_w, buf_h);
        XDBG_WARNING (MPLN, "src(x%d,y%d w%d-h%d) dst(x%d,y%d w%d-h%d)\n",
                      src->x, src->y, src->width, src->height,
                      dst->x, dst->y, dst->width, dst->height);
        XDBG_WARNING (MPLN,"start(x%d,y%d) end(w%d,h%d)\n",
                      aligned_src->x, aligned_src->y,
                      aligned_src->width, aligned_src->height);
        ret = FALSE;
    }

    return ret;
}

static Bool
_alignHDMIPlane (xRectanglePtr src, xRectanglePtr dst,
                 xRectanglePtr aligned_src, xRectanglePtr aligned_dst,
                 int buf_w, int buf_h, int disp_w, int disp_h)
{
    Bool ret = TRUE;
    aligned_dst->x = (dst->x < 0) ? 0 : dst->x;
    aligned_dst->y = (dst->y < 0) ? 0 : dst->y;
    aligned_dst->width = (((aligned_dst->x + dst->width) > disp_w) ? disp_w : (dst->width)) &(~0x1);
    aligned_dst->height = ((aligned_dst->y + dst->height) > disp_h) ? disp_h : (dst->height);
    aligned_src->x = (src->x < 0) ? 0 : src->x;
    aligned_src->y = (src->y < 0) ? 0 : src->y;
    aligned_src->width = (((aligned_src->x + src->width) > disp_w) ? disp_w : (src->width)) &(~0x1);
    aligned_src->height = ((aligned_src->y + src->height) > disp_h) ? disp_h : (src->height);

    if ((aligned_dst->width < MIN_WIDTH) || (aligned_dst->height < MIN_HEIGHT))
    {
        XDBG_WARNING (MPLN, "hide: %d, %d: buf_w(%d) buf_h(%d)\n",
                      aligned_dst->width,
                      aligned_dst->height,
                      buf_w, buf_h);
        XDBG_WARNING (MPLN, "src(x%d,y%d w%d-h%d) dst(x%d,y%d w%d-h%d)\n",
                      src->x, src->y, src->width, src->height,
                      dst->x, dst->y, dst->width, dst->height);
        XDBG_WARNING (MPLN,"start(x%d,y%d) end(w%d,h%d)\n",
                      aligned_dst->x, aligned_dst->y,
                      aligned_dst->width, aligned_dst->height);
        ret = FALSE;
    }

    if ((aligned_src->width < MIN_WIDTH) || (aligned_src->height < MIN_HEIGHT))
    {
        XDBG_WARNING (MPLN, "hide: %d, %d: buf_w(%d) buf_h(%d)\n",
                      aligned_src->width,
                      aligned_src->height,
                      buf_w, buf_h);
        XDBG_WARNING (MPLN, "src(x%d,y%d w%d-h%d) dst(x%d,y%d w%d-h%d)\n",
                      src->x, src->y, src->width, src->height,
                      dst->x, dst->y, dst->width, dst->height);
        XDBG_WARNING (MPLN,"start(x%d,y%d) end(w%d,h%d)\n",
                      aligned_src->x, aligned_src->y,
                      aligned_src->width, aligned_src->height);
        ret = FALSE;
    }
    return ret;
}

static Bool
_check_hw_restriction (ScrnInfoPtr pScrn, int crtc_id, int buf_w, int buf_h,
                       xRectanglePtr src, xRectanglePtr dst,
                       xRectanglePtr aligned_src, xRectanglePtr aligned_dst)
{
    /* Kernel manage CRTC status based out output config */
    Bool ret = FALSE;
    xf86CrtcPtr pCrtc = secCrtcGetByID(pScrn, crtc_id);
    if (pCrtc == NULL)
    {
        XDBG_ERROR(MPLN, "Can't found crtc_id(%d)\n", crtc_id);
        return ret;
    }
    if (!pCrtc->enabled && !pCrtc->active)
    {
        XDBG_ERROR(MPLN, "Current crtc_id(%d) not active\n", crtc_id);
        return ret;
    }

    int max_w = pCrtc->mode.CrtcHDisplay;
    int max_h = pCrtc->mode.CrtcVDisplay;
    if (max_w == 0 || max_h == 0)
    {
        max_w = pScrn->virtualX;
        max_h = pScrn->virtualY;
    }
    switch (secCrtcGetConnectType (pCrtc))
    {
        case DRM_MODE_CONNECTOR_LVDS:
            ret = _alignLVDSPlane (src, dst, aligned_src, aligned_dst,
                                   buf_w, buf_h, max_w, max_h);
            break;
        case DRM_MODE_CONNECTOR_HDMIA:
        case DRM_MODE_CONNECTOR_HDMIB:
            ret = _alignHDMIPlane (src, dst, aligned_src, aligned_dst,
                                   buf_w, buf_h, max_w, max_h);
            break;
        default:
            XDBG_WARNING (MPLN, "Not supported plane for current output %d\n",
                          secCrtcGetConnectType (pCrtc));
            break;
    }

    if (buf_w < MIN_WIDTH || buf_w % 2)
    {
        XDBG_WARNING (MPLN, "hide: buf_w(%d) not 2's multiple or less than %d\n",
                    buf_w, MIN_WIDTH);
        ret = FALSE;
    }

    if (buf_h < MIN_HEIGHT)
    {
        XDBG_WARNING (MPLN, "hide: buf_h(%d) less than %d\n",
                    buf_h, MIN_HEIGHT);
        ret = FALSE;
    }
#if 0
    if (src_x > dst_x || ((dst_x - src_x) + buf_w) > max_w)
        virtual_screen = TRUE;
    else
        virtual_screen = FALSE;

    if (!virtual_screen)
    {
        /* Pagewidth of window (= 8 byte align / bytes-per-pixel ) */
        if ((end_dst - start_dst) % 2)
            end_dst--;
    }
    else
    {
        /* You should align the sum of PAGEWIDTH_F and OFFSIZE_F double-word (8 byte) boundary. */
        if (end_dst % 2)
            end_dst--;
    }

    *new_dst_x = start_dst;
    *new_dst_w = end_dst - start_dst;
    *new_src_w = end_src - start_src;
//    diff = start_dst - dst_x;
    *new_src_x = start_src;

    XDBG_RETURN_VAL_IF_FAIL (*new_src_w > 0, FALSE);
    XDBG_RETURN_VAL_IF_FAIL (*new_dst_w > 0, FALSE);
    if (src_x != *new_src_x || src_w != *new_src_w ||
        dst_x != *new_dst_x || dst_w != *new_dst_w)
        XDBG_TRACE (MPLN, " => buf_w(%d) src(%d,%d) dst(%d,%d), virt(%d) start(%d) end(%d)\n",
                    buf_w, *new_src_x, *new_src_w, *new_dst_x, *new_dst_w, virtual_screen, start_dst, end_dst);
#endif
    XDBG_TRACE (MPLN, "src(x%d,y%d w%d-h%d) dst(x%d,y%d w%d-h%d) ratio_x %f ratio_y %f\n",
                src->x, src->y, src->width, src->height,
                dst->x, dst->y, dst->width, dst->height,
                (double) src->width/dst->width, (double) src->height/dst->height);
    if (memcmp(aligned_src, src, sizeof(xRectangle))
        || memcmp(aligned_dst, dst, sizeof(xRectangle)))
    {
        XDBG_TRACE(MPLN, "===> src(x%d,y%d w%d-h%d) dst(x%d,y%d w%d-h%d) ratio_x %f ratio_y %f\n",
                   aligned_src->x, aligned_src->y,
                   aligned_src->width, aligned_src->height,
                   aligned_dst->x, aligned_dst->y,
                   aligned_dst->width, aligned_dst->height,
                   (double) aligned_src->width / aligned_dst->width,
                   (double) aligned_src->height / aligned_dst->height);
    }    
    return ret;
}

static Bool
_secPlaneShowInternal (SECPlaneTable *table,
                       SECPlaneFb *old_fb, SECPlaneFb *new_fb,
                       xRectangle *new_src, xRectangle *new_dst, int new_zpos,
                       Bool need_set_plane)
{
    SECPtr pSec;
    SECModePtr pSecMode;
    XDBG_RETURN_VAL_IF_FAIL(old_fb != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(new_fb != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(table != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(new_src != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(new_dst != NULL, FALSE);
    xRectangle old_src = table->src;
    xRectangle old_dst = table->dst;
    int old_zpos = table->zpos;
    Bool change_zpos = FALSE;
    tbm_bo_handle bo_handle;

    pSec = SECPTR (table->pPlanePriv->pScrn);
    pSecMode = table->pPlanePriv->pSecMode;

//    if (pSec->isLcdOff)
//    {
//        XDBG_TRACE (MPLN, "lcd off, can't show : plane(%d) crtc(%d) pos(%d). \n",
//                    table->plane_id, table->crtc_id, new_zpos);
//        return FALSE;
//    }

    if (!table->onoff)
    {
        XDBG_TRACE (MPLN, "plane off, can't show : plane(%d) crtc(%d) pos(%d). \n",
                    table->plane_id, table->crtc_id, new_zpos);
        return FALSE;
    }

    /* should set zpos before doing drmModeSetPlane */
    if (new_zpos != old_zpos)
    {
        if (!secUtilSetDrmProperty (pSecMode, table->plane_id, DRM_MODE_OBJECT_PLANE,
                                    "zpos", new_zpos))
            return FALSE;

        table->zpos = new_zpos;
        change_zpos = TRUE;

        XDBG_TRACE (MPLN, "plane(%d) => crtc(%d) zpos(%d)\n",
                    table->plane_id, table->crtc_id, table->zpos);
    }

    if (!table->visible || need_set_plane ||
        change_zpos ||
        (!old_fb || (old_fb != new_fb)) ||
        (memcmp (&old_src, new_src, sizeof (xRectangle))) ||
        (memcmp (&old_dst, new_dst, sizeof (xRectangle))))
    {
        xf86CrtcConfigPtr pCrtcConfig = XF86_CRTC_CONFIG_PTR (table->pPlanePriv->pScrn);
        SECCrtcPrivPtr pCrtcPriv = NULL;
        int c;

        for (c = 0; c < pCrtcConfig->num_crtc; c++)
        {
            xf86CrtcPtr pCrtc = pCrtcConfig->crtc[c];
            SECCrtcPrivPtr pTemp =  pCrtc->driver_private;
            if (pTemp->mode_crtc && pTemp->mode_crtc->crtc_id == table->crtc_id)
            {
                pCrtcPriv = pTemp;
                break;
            }
        }

        XDBG_RETURN_VAL_IF_FAIL (pCrtcPriv != NULL, FALSE);

        XDBG_TRACE (MPLN, "plane(%d) => crtc(%d) fb(%d) (%d,%d %dx%d) => (%d,%d %dx%d) [%d,%d,%c%c%c%c]\n",
                    table->plane_id, table->crtc_id, new_fb->id,
                    new_src->x, new_src->y, new_src->width, new_src->height,
                    new_dst->x, new_dst->y, new_dst->width, new_dst->height,
                    pCrtcPriv->bAccessibility, new_fb->type,
                    FOURCC_STR (new_fb->buffer.vbuf->id));

        if (!pCrtcPriv->bAccessibility ||
            (new_fb->type == PLANE_FB_TYPE_DEFAULT && new_fb->buffer.vbuf->id != FOURCC_RGB32))
        {
#if 0
            int aligned_src_x = new_src->x;
            int aligned_src_w = new_src->width;
            int aligned_dst_x = new_dst->x;
            int aligned_dst_w = new_dst->width;
#endif
            xRectangle aligned_src;
            xRectangle aligned_dst;
            if (!_check_hw_restriction (table->pPlanePriv->pScrn, table->crtc_id,
                                        table->cur_fb->width, table->cur_fb->height,
                                        new_src, new_dst,
                                        &aligned_src, &aligned_dst))
            {
                XDBG_TRACE (MPLN, "out of range: plane(%d) crtc(%d) pos(%d) crtc(x%d,y%d w%d-h%d)\n",
                            table->plane_id, table->crtc_id, new_zpos,
                            new_dst->x, new_dst->y, new_dst->width, new_dst->height);
                XDBG_TRACE (MPLN, "src(x%d,y%d w%d-h%d)\n",
                            new_src->x, new_src->y, new_src->width, new_src->height);

                _secPlaneHideInternal (table);

                return TRUE;
            }

            /* Source values are 16.16 fixed point */

            uint32_t fixed_x = ((unsigned int)aligned_src.x) << 16;
            uint32_t fixed_y = ((unsigned int)aligned_src.y) << 16;
            uint32_t fixed_w = ((unsigned int)aligned_src.width) << 16;
            uint32_t fixed_h = ((unsigned int)aligned_src.height) << 16;


            if (drmModeSetPlane (pSecMode->fd, table->plane_id, table->crtc_id,
                                 new_fb->id, 0,
                                 aligned_dst.x, aligned_dst.y,
                                 aligned_dst.width, aligned_dst.height,
                                 fixed_x, fixed_y,
                                 fixed_w, fixed_h))
            {
                XDBG_ERRNO (MPLN, "drmModeSetPlane failed. plane(%d) crtc(%d) pos(%d) on: fb(%d,%c%c%c%c,%dx%d,[%d,%d %dx%d]=>[%d,%d %dx%d])\n",
                            table->plane_id, table->crtc_id, table->zpos,
                            new_fb->id, FOURCC_STR (new_fb->buffer.vbuf->id),
                            new_fb->buffer.vbuf->width, new_fb->buffer.vbuf->height,
                            aligned_src.x, aligned_src.y, aligned_src.width, aligned_src.height,
                            aligned_dst.x, aligned_dst.y, aligned_dst.width, aligned_dst.height);

                return FALSE;
            }

            if (!table->visible)
            {
                XDBG_SECURE (MPLN, "plane(%d) crtc(%d) pos(%d) on: fb(%d,%c%c%c%c,%dx%d,[%d,%d %dx%d]=>[%d,%d %dx%d])\n",
                             table->plane_id, table->crtc_id, table->zpos,
                             new_fb->id, FOURCC_STR (new_fb->buffer.vbuf->id),
                             new_fb->buffer.vbuf->width, new_fb->buffer.vbuf->height,
                             aligned_src.x, aligned_src.y, aligned_src.width, aligned_src.height,
                             aligned_dst.x, aligned_dst.y, aligned_dst.width, aligned_dst.height);
                table->visible = TRUE;
            }
        }
        else
        {
            SECPlaneAccess *access;
            xRectangle fb_src = {0,};
            tbm_bo src_bo;
            int old_w = 0, old_h = 0;

            if (!table->access)
            {
                table->access = calloc (1, sizeof (SECPlaneAccess));
                XDBG_RETURN_VAL_IF_FAIL (table->access != NULL, FALSE);
            }
            else
            {
                old_w = table->access->width;
                old_h = table->access->height;
            }

            access = table->access;

            if (pCrtcPriv->bScale)
            {
                float h_ratio = 0.0, v_ratio = 0.0;
                xRectangle crop;

                h_ratio = (float)pCrtcPriv->kmode.hdisplay / pCrtcPriv->sw;
                v_ratio = (float)pCrtcPriv->kmode.vdisplay / pCrtcPriv->sh;

                fb_src.x = new_src->x;
                fb_src.y = new_src->y;
                fb_src.width = new_src->width;
                fb_src.height = new_src->height;

                CLEAR (crop);
                crop.x = pCrtcPriv->sx;
                crop.y = pCrtcPriv->sy;
                crop.width = pCrtcPriv->sw;
                crop.height = pCrtcPriv->sh;

                crop.x -= new_dst->x;
                crop.y -= new_dst->y;
                crop.x += new_src->x;
                crop.y += new_src->y;
                secUtilRectIntersect (&fb_src, &fb_src, &crop);

                access->dst = *new_dst;

                access->dst.x = new_dst->x;
                access->dst.y = new_dst->y;
                access->dst.width = new_dst->width;
                access->dst.height = new_dst->height;

                CLEAR (crop);
                crop.x = pCrtcPriv->sx;
                crop.y = pCrtcPriv->sy;
                crop.width = pCrtcPriv->sw;
                crop.height = pCrtcPriv->sh;
                secUtilRectIntersect (&access->dst, &access->dst, &crop);

                access->dst.x -= pCrtcPriv->sx;
                access->dst.y -= pCrtcPriv->sy;

                access->dst.x *= h_ratio;
                access->dst.y *= v_ratio;
                access->dst.width *= h_ratio;
                access->dst.height *= v_ratio;

                access->width = pCrtcPriv->kmode.hdisplay;
                access->height = pCrtcPriv->kmode.vdisplay;

                access->src.x = 0;
                access->src.y = 0;
                access->src.width = access->dst.width;
                access->src.height = access->dst.height;
            }
            else
            {
                fb_src.x = new_src->x;
                fb_src.y = new_src->y;
                fb_src.width = new_src->width;
                fb_src.height = new_src->height;

                /* hw restriction: 8 bytes */
                new_dst->width &= ~1;

                access->dst.x = new_dst->x;
                access->dst.y = new_dst->y;
                access->dst.width = new_dst->width;
                access->dst.height = new_dst->height;

                access->width = access->dst.width;
                access->height = access->dst.height;

                access->src.x = 0;
                access->src.y = 0;
                access->src.width = access->dst.width;
                access->src.height = access->dst.height;
            }

            XDBG_DEBUG (MPLN, "access : accessibility_status(%d) scale(%d) bo(%p) fb(%d) (%d,%d %dx%d) (%dx%d) (%d,%d %dx%d) (%d,%d %dx%d).\n",
                        pCrtcPriv->accessibility_status, pCrtcPriv->bScale,
                        access->bo, access->fb_id,
                        fb_src.x, fb_src.y, fb_src.width, fb_src.height,
                        access->width, access->height,
                        access->src.x, access->src.y, access->src.width, access->src.height,
                        access->dst.x, access->dst.y, access->dst.width, access->dst.height);

            if (!fb_src.width || !fb_src.height ||
                !access->width || !access->height ||
                !access->dst.width || !access->dst.height)
            {
                _secPlaneHideInternal (table);

                return TRUE;
            }

            if (access->bo)
            {
                if (old_w != access->width || old_h != access->height)
                {
                    if (table->access->fb_id)
                    {
                        drmModeRmFB (pSecMode->fd, table->access->fb_id);
                        table->access->fb_id = 0;
                    }

                    tbm_bo_unref (table->access->bo);
                    table->access->bo = NULL;
                }
            }

            if (!access->bo)
            {
                access->bo = tbm_bo_alloc (pSec->tbm_bufmgr,
                                               access->width * access->height * 4,
                                               TBM_BO_NONCACHABLE);
                XDBG_RETURN_VAL_IF_FAIL (access->bo != NULL, FALSE);

                bo_handle = tbm_bo_get_handle (access->bo, TBM_DEVICE_DEFAULT);
                if (drmModeAddFB (pSecMode->fd, access->width, access->height,
                                  table->pPlanePriv->pScrn->depth,
                                  table->pPlanePriv->pScrn->bitsPerPixel,
                                  access->width * 4,
                                  bo_handle.u32,
                                  &access->fb_id))
                {
                    XDBG_ERRNO (MPLN, "drmModeAddFB failed. plane(%d)\n", table->plane_id);
                    return FALSE;
                }

                XDBG_RETURN_VAL_IF_FAIL (access->fb_id > 0, FALSE);
            }

            if (new_fb->type == PLANE_FB_TYPE_DEFAULT)
                src_bo = new_fb->buffer.vbuf->bo[0];
            else
                src_bo = new_fb->buffer.bo;
            XDBG_RETURN_VAL_IF_FAIL (src_bo != NULL, FALSE);

            _secPlaneExecAccessibility (src_bo, new_fb->width, new_fb->height, &fb_src,
                                        access->bo, access->width, access->height, &access->src,
                                        pCrtcPriv->accessibility_status);
            xRectangle aligned_src;
            xRectangle aligned_dst;
            if (!_check_hw_restriction (table->pPlanePriv->pScrn, table->crtc_id,
                                        access->width, access->height,
                                        &access->src, &access->dst,
                                        &aligned_src, &aligned_dst))
            {
                XDBG_TRACE (MPLN, "access : out of range: plane(%d) crtc(%d) pos(%d) crtc(x%d,y%d w%d-h%d)\n",
                            table->plane_id, table->crtc_id, new_zpos,
                            access->dst.x, access->dst.y, access->dst.width, access->dst.height);
                XDBG_TRACE (MPLN, "access : src(x%d,y%d w%d-h%d)\n",
                            access->src.x, access->src.y, access->src.width, access->src.height);

                _secPlaneHideInternal (table);

                return TRUE;
            }

            /* Source values are 16.16 fixed point */
            uint32_t fixed_x = ((unsigned int)aligned_src.x) << 16;
            uint32_t fixed_y = ((unsigned int)aligned_src.y) << 16;
            uint32_t fixed_w = ((unsigned int)aligned_src.width) << 16;
            uint32_t fixed_h = ((unsigned int)aligned_src.height) << 16;

            XDBG_DEBUG(MPLN, "Access plane: fixed:(x%d,y%d w%d-h%d), dst:(x%d,y%d w%d-h%d)\n",
                       aligned_src.x, aligned_src.y, aligned_src.width, aligned_src.height,
                       aligned_dst.x, aligned_dst.y, aligned_dst.width, aligned_dst.height);

            if (drmModeSetPlane (pSecMode->fd, table->plane_id, table->crtc_id,
                                 access->fb_id, 0,
                                 aligned_dst.x, aligned_dst.y,
                                 aligned_dst.width, aligned_dst.height,
                                 fixed_x, fixed_y,
                                 fixed_w, fixed_h))
            {
                XDBG_ERRNO (MPLN, "drmModeSetPlane failed. \n");

                return FALSE;
            }
            if (!table->visible)
            {
                XDBG_SECURE (MPLN, "plane(%d) crtc(%d) pos(%d) on: access_fb(%d,%dx%d,[%d,%d %dx%d]=>[%d,%d %dx%d])\n",
                             table->plane_id, table->crtc_id, table->zpos,
                             access->fb_id, access->width, access->height,
                             aligned_src.x, aligned_src.y, aligned_src.width, aligned_src.height,
                             aligned_dst.x, aligned_dst.y, aligned_dst.width, aligned_dst.height);
                table->visible = TRUE;
            }
        }

        memcpy (&table->src, new_src, sizeof (xRectangle));
        memcpy (&table->dst, new_dst, sizeof (xRectangle));
    }

    return TRUE;
}

static Bool
_secPlaneHideInternal (SECPlaneTable *table)
{
    SECPtr pSec;
    SECModePtr pSecMode;

    XDBG_RETURN_VAL_IF_FAIL (table != NULL, FALSE);

    if (!table->visible)
        return TRUE;

    XDBG_RETURN_VAL_IF_FAIL (table->crtc_id > 0, FALSE);

    pSec = SECPTR (table->pPlanePriv->pScrn);
    pSecMode = table->pPlanePriv->pSecMode;

    if (!pSec->isLcdOff && table->onoff)
    {
        if (drmModeSetPlane (pSecMode->fd,
                             table->plane_id,
                             table->crtc_id,
                             0, 0,
                             0, 0, 0, 0, 0, 0, 0, 0))
        {
            XDBG_ERRNO (MPLN, "drmModeSetPlane failed. plane(%d) crtc(%d) zpos(%d) fb(%d)\n",
                        table->plane_id, table->crtc_id, table->zpos, table->cur_fb->id);

            return FALSE;
        }
    }

    if (table->visible)
    {
        XDBG_SECURE (MPLN, "plane(%d) crtc(%d) zpos(%d) off. lcd(%s) onoff(%d)\n",
                     table->plane_id, table->crtc_id, table->zpos,
                     (pSec->isLcdOff)?"off":"on", table->onoff);
        table->visible = FALSE;
    }

    XDBG_TRACE (MPLN, "plane(%d) fb(%d) removed from crtc(%d) zpos(%d). LCD(%s) ONOFF(%s).\n",
                table->plane_id, table->cur_fb->id, table->crtc_id, table->zpos,
                (pSec->isLcdOff)?"OFF":"ON", (table->onoff)?"ON":"OFF");

    return TRUE;

}


void
secPlaneInit (ScrnInfoPtr pScrn, SECModePtr pSecMode, int num)
{
    SECPlanePrivPtr pPlanePriv;

    XDBG_RETURN_IF_FAIL (pScrn != NULL);
    XDBG_RETURN_IF_FAIL (pSecMode != NULL);
    XDBG_RETURN_IF_FAIL (pSecMode->plane_res != NULL);
    XDBG_RETURN_IF_FAIL (pSecMode->plane_res->count_planes > 0);

    if (!_secPlaneTableEnsure (pScrn, pSecMode->plane_res->count_planes))
        return;

    pPlanePriv = calloc (sizeof (SECPlanePrivRec), 1);
    XDBG_RETURN_IF_FAIL (pPlanePriv != NULL);

    pPlanePriv->mode_plane = drmModeGetPlane (pSecMode->fd,
                             pSecMode->plane_res->planes[num]);
    if (!pPlanePriv->mode_plane)
    {
        XDBG_ERRNO (MPLN, "drmModeGetPlane failed. plane(%d)\n",
                    pSecMode->plane_res->planes[num]);

        free (pPlanePriv);
        return;
    }

    pPlanePriv->pScrn = pScrn;
    pPlanePriv->pSecMode = pSecMode;
    pPlanePriv->plane_id = pPlanePriv->mode_plane->plane_id;

    plane_table[num].plane_id = pPlanePriv->plane_id;
    plane_table[num].pPlanePriv = pPlanePriv;
    xorg_list_init (&plane_table[num].fbs);

    xorg_list_add(&pPlanePriv->link, &pSecMode->planes);
}

void
secPlaneDeinit (ScrnInfoPtr pScrn, SECPlanePrivPtr pPlanePriv)
{
    int i;

    XDBG_RETURN_IF_FAIL (pScrn != NULL);
    XDBG_RETURN_IF_FAIL (pPlanePriv != NULL);

    secPlaneFreeId (pPlanePriv->plane_id);
    drmModeFreePlane (pPlanePriv->mode_plane);
    xorg_list_del (&pPlanePriv->link);

    for (i = 0; i < plane_table_size; i++)
        if (plane_table[i].plane_id == pPlanePriv->plane_id)
        {
            plane_table[i].plane_id = -1;
            break;
        }

    free (pPlanePriv);

    if (plane_table)
    {
        for (i = 0; i < plane_table_size; i++)
            if (plane_table[i].plane_id != -1)
                return;

        free (plane_table);
        plane_table = NULL;
        plane_table_size = 0;
        XDBG_TRACE (MPLN, "plane_table destroyed. %d\n", plane_table_size);
    }
}

void
secPlaneShowAll (int crtc_id)
{
    int i;

    XDBG_TRACE (MPLN, "crtc(%d) \n", crtc_id);

    for (i = 0; i < plane_table_size; i++)
    {
        SECPlaneTable *table = &plane_table[i];

        if (!table || !table->in_use || !table->visible || !table->onoff)
            continue;

        if (table->crtc_id != crtc_id)
            continue;
        if (!table->cur_fb)
            continue;
        if (!_secPlaneShowInternal (table, table->cur_fb, table->cur_fb,
                                    &table->src, &table->dst, table->zpos, TRUE))

        {
            XDBG_WARNING (MPLN, "_secPlaneShowInternal failed. \n");
        }

        XDBG_TRACE (MPLN, "plane(%d) crtc(%d) zpos(%d) on.\n",
                    table->plane_id, table->crtc_id, table->zpos);
    }
}

intptr_t
secPlaneGetID (void)
{
    SECPlaneTable *table = _secPlaneTableFindEmpty ();

    if (!table)
    {
        XDBG_ERROR (MPLN, "No avaliable plane ID. %d\n", -1);
        return -1;
    }

    table->in_use = TRUE;
    table->onoff = TRUE;

    XDBG_TRACE (MPLN, "plane(%d). \n", table->plane_id);

    return table->plane_id;
}

void
secPlaneFreeId (intptr_t plane_id)
{
    SECPlaneTable *table = _secPlaneTableFind (plane_id);
    SECPlaneFb *fb = NULL, *fb_next = NULL;

    XDBG_RETURN_IF_FAIL (table != NULL);

    secPlaneHide (table->plane_id);

    table->visible = FALSE;
    table->crtc_id = 0;

    table->zpos = 0;
    memset (&table->src, 0x00, sizeof (xRectangle));
    memset (&table->dst, 0x00, sizeof (xRectangle));

    xorg_list_for_each_entry_safe (fb, fb_next, &table->fbs, link)
    {
        _secPlaneTableFreeBuffer (table, fb);
    }

    if (table->access)
    {
        if (table->access->fb_id)
        {
            SECModePtr pSecMode = table->pPlanePriv->pSecMode;
            if (pSecMode)
                drmModeRmFB (pSecMode->fd, table->access->fb_id);
        }

        if (table->access->bo)
            tbm_bo_unref (table->access->bo);

        free (table->access);
        table->access = NULL;
    }

    table->in_use = FALSE;
    table->onoff = TRUE;

    XDBG_TRACE (MPLN, "plane(%d).\n", table->plane_id);
}

void
secPlaneFlushFBId (intptr_t plane_id)
{
    SECPlaneTable *table = _secPlaneTableFind (plane_id);
    SECPlaneFb *fb = NULL, *fb_next = NULL;

    XDBG_RETURN_IF_FAIL (table != NULL);
    table->cur_fb = NULL;
    xorg_list_for_each_entry_safe (fb, fb_next, &table->fbs, link)
    {
        _secPlaneTableFreeBuffer (table, fb);
    }

    if (table->access)
    {
        if (table->access->fb_id)
        {
            SECModePtr pSecMode = table->pPlanePriv->pSecMode;
            if (pSecMode)
                drmModeRmFB (pSecMode->fd, table->access->fb_id);
        }

        if (table->access->bo)
            tbm_bo_unref (table->access->bo);
    }

    XDBG_TRACE (MPLN, "flush plane(%d).\n", table->plane_id);
}


Bool
secPlaneTrun (intptr_t plane_id, Bool onoff, Bool user)
{
    SECPlaneTable *table = _secPlaneTableFind (plane_id);
    SECPtr pSec;

    XDBG_RETURN_VAL_IF_FAIL (table != NULL, FALSE);

    pSec = SECPTR (table->pPlanePriv->pScrn);

    if (pSec->isLcdOff)
        return TRUE;

    onoff = (onoff > 0) ? TRUE : FALSE;

    if (table->onoff == onoff)
        return TRUE;

    if (onoff)
    {
        table->onoff = onoff;

        if (!table->visible)
        {
            if (!_secPlaneShowInternal (table, table->cur_fb, table->cur_fb,
                                        &table->src, &table->dst, table->zpos, TRUE))

            {
                XDBG_WARNING (MPLN, "_secPlaneShowInternal failed. \n");
            }

            XDBG_DEBUG (MPLN, "%s >> plane(%d,%d,%d) '%s'. \n", (user)?"user":"Xorg",
                        plane_id, table->crtc_id, table->zpos, (onoff)?"ON":"OFF");
        }
    }
    else
    {
        if (table->visible)
        {
            if (!_secPlaneHideInternal (table))

            {
                XDBG_WARNING (MPLN, "_secPlaneHideInternal failed. \n");
            }

            XDBG_DEBUG (MPLN, "%s >> plane(%d,%d,%d) '%s'. \n", (user)?"user":"Xorg",
                        plane_id, table->crtc_id, table->zpos, (onoff)?"ON":"OFF");
        }

        table->onoff = onoff;
    }

    return TRUE;
}

Bool
secPlaneTrunStatus (intptr_t plane_id)
{
    SECPlaneTable *table = _secPlaneTableFind (plane_id);

    XDBG_RETURN_VAL_IF_FAIL (table != NULL, FALSE);

    return table->onoff;
}

void
secPlaneFreezeUpdate (intptr_t plane_id, Bool enable)
{
    SECPlaneTable *table = _secPlaneTableFind (plane_id);

    XDBG_RETURN_IF_FAIL (table != NULL);

    table->freeze_update = enable;
}

//Bool
//secPlaneRemoveBuffer (intptr_t plane_id, int fb_id)
//{
//    SECPlaneTable *table;
//    SECPlaneFb *fb;
//
//    XDBG_RETURN_VAL_IF_FAIL (fb_id > 0, FALSE);
//
//    table = _secPlaneTableFind (plane_id);
//    XDBG_RETURN_VAL_IF_FAIL (table != NULL, FALSE);
//
//    fb = _secPlaneTableFindBuffer (table, fb_id, NULL, NULL);
//    XDBG_RETURN_VAL_IF_FAIL (fb != NULL, FALSE);
//
//    _secPlaneTableFreeBuffer (table, fb);
//
//    XDBG_TRACE (MPLN, "plane(%d) fb(%d). \n", plane_id, fb_id);
//
//    return TRUE;
//}

int
secPlaneAddBo (intptr_t plane_id, tbm_bo bo)
{
    SECPlaneTable *table;
    SECPlaneFb *fb;
    intptr_t fb_id = 0;
    SECFbBoDataPtr bo_data = NULL;
    int width, height;

    XDBG_RETURN_VAL_IF_FAIL (bo != NULL, 0);

    table = _secPlaneTableFind (plane_id);
    XDBG_RETURN_VAL_IF_FAIL (table != NULL, 0);

    fb = _secPlaneTableFindBuffer (table, 0, bo, NULL);
    XDBG_RETURN_VAL_IF_FAIL (fb == NULL, 0);

    tbm_bo_get_user_data (bo, TBM_BO_DATA_FB, (void**)&bo_data);
    XDBG_RETURN_VAL_IF_FAIL (bo_data != NULL, 0);

    fb_id = bo_data->fb_id;
    width = bo_data->pos.x2 - bo_data->pos.x1;
    height = bo_data->pos.y2 - bo_data->pos.y1;

    XDBG_RETURN_VAL_IF_FAIL (fb_id > 0, 0);
    XDBG_RETURN_VAL_IF_FAIL (width > 0, 0);
    XDBG_RETURN_VAL_IF_FAIL (height > 0, 0);

    fb = calloc (1, sizeof (SECPlaneFb));
    XDBG_RETURN_VAL_IF_FAIL (fb != NULL, 0);

    xorg_list_add(&fb->link, &table->fbs);

    fb->type = PLANE_FB_TYPE_BO;
    fb->id = fb_id;
    fb->width = width;
    fb->height = height;

    fb->buffer.bo = tbm_bo_ref (bo);

    XDBG_TRACE (MPLN, "plane(%d) bo(%d,%dx%d)\n", plane_id,
                fb_id, fb->width, fb->height);

    return fb->id;
}

int
secPlaneAddBuffer (intptr_t plane_id, SECVideoBuf *vbuf)
{
    SECPlaneTable *table;
    SECPlaneFb *fb;

    XDBG_RETURN_VAL_IF_FAIL (VBUF_IS_VALID (vbuf), 0);
    XDBG_RETURN_VAL_IF_FAIL (vbuf->fb_id > 0, 0);
    XDBG_RETURN_VAL_IF_FAIL (vbuf->width > 0, 0);
    XDBG_RETURN_VAL_IF_FAIL (vbuf->height > 0, 0);

    table = _secPlaneTableFind (plane_id);
    XDBG_RETURN_VAL_IF_FAIL (table != NULL, 0);

    fb = _secPlaneTableFindBuffer (table, 0, NULL, vbuf);
    XDBG_RETURN_VAL_IF_FAIL (fb == NULL, 0);

    fb = calloc (1, sizeof (SECPlaneFb));
    XDBG_RETURN_VAL_IF_FAIL (fb != NULL, 0);

    xorg_list_add(&fb->link, &table->fbs);

    fb->type = PLANE_FB_TYPE_DEFAULT;
    fb->id = vbuf->fb_id;
    fb->width = vbuf->width;
    fb->height = vbuf->height;

    fb->buffer.vbuf = vbuf;

    secUtilAddFreeVideoBufferFunc (vbuf, _secPlaneFreeVbuf, (void*)plane_id);

    XDBG_TRACE (MPLN, "plane(%d) vbuf(%"PRIuPTR",%d,%dx%d)\n", plane_id,
                vbuf->stamp, vbuf->fb_id, vbuf->width, vbuf->height);

    return fb->id;
}

intptr_t
secPlaneGetBuffer (int plane_id, tbm_bo bo, SECVideoBuf *vbuf)
{
    SECPlaneTable *table;
    SECPlaneFb *fb;

    table = _secPlaneTableFind (plane_id);
    XDBG_RETURN_VAL_IF_FAIL (table != NULL, 0);

    fb = _secPlaneTableFindBuffer (table, 0, bo, vbuf);
    if (!fb)
        return 0;

    return fb->id;
}

void
secPlaneGetBufferSize (int plane_id, int fb_id, int *width, int *height)
{
    SECPlaneTable *table;
    SECPlaneFb *fb;

    table = _secPlaneTableFind (plane_id);
    XDBG_RETURN_IF_FAIL (table != NULL);

    fb = _secPlaneTableFindBuffer (table, fb_id, NULL, NULL);
    XDBG_RETURN_IF_FAIL (fb != NULL);

    if (width)
        *width = fb->width;

    if (height)
        *height = fb->height;
}

Bool
secPlaneAttach (int plane_id, int fb_id, SECVideoBuf *vbuf)
{
    SECPlaneTable *table = _secPlaneTableFind (plane_id);
    SECPlaneFb *fb;

    XDBG_RETURN_VAL_IF_FAIL (table != NULL, FALSE);

    fb = _secPlaneTableFindBuffer (table, fb_id, NULL, vbuf);
    XDBG_RETURN_VAL_IF_FAIL (fb != NULL, FALSE);

    table->cur_fb = fb;

    XDBG_DEBUG (MPLN, "plane(%d) fb(%d)\n", plane_id, fb_id);

    return TRUE;
}

Bool
secPlaneIsVisible (int plane_id)
{
    SECPlaneTable *table = _secPlaneTableFind (plane_id);

    XDBG_RETURN_VAL_IF_FAIL (table != NULL, FALSE);

    return table->visible;
}

Bool
secPlaneShow (int plane_id, int crtc_id,
              int src_x, int src_y, int src_w, int src_h,
              int dst_x, int dst_y, int dst_w, int dst_h,
              int zpos, Bool need_update)
{
    SECPlaneTable *table = _secPlaneTableFind (plane_id);
    SECPlaneTable *temp;
    xRectangle src = {src_x, src_y, src_w, src_h};
    xRectangle dst = {dst_x, dst_y, dst_w, dst_h};

    XDBG_RETURN_VAL_IF_FAIL (table != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL (table->cur_fb != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL (crtc_id > 0, FALSE);
    XDBG_RETURN_VAL_IF_FAIL (zpos >= 0, FALSE);

    temp = _secPlaneTableFindPos (crtc_id, zpos);

    if (!need_update && temp && temp->plane_id != plane_id && temp->visible)
    {
        XDBG_ERROR (MPLN, "can't change zpos. plane(%d) is at zpos(%d) crtc(%d) \n",
                    temp->plane_id, temp->zpos, crtc_id);
        return FALSE;
    }

    if (!table->visible)
        table->crtc_id = crtc_id;
    else if (table->crtc_id != crtc_id)
    {
        XDBG_ERROR (MPLN, "can't change crtc. plane(%d) is on crtc(%d) \n",
                    table->plane_id, table->zpos);
        return FALSE;
    }

    if (!_secPlaneShowInternal (table, table->cur_fb, table->cur_fb,
                                &src, &dst, zpos, need_update))
    {
        return FALSE;
    }

    return TRUE;
}

Bool
secPlaneHide (int plane_id)
{
    SECPlaneTable *table = _secPlaneTableFind (plane_id);

    XDBG_RETURN_VAL_IF_FAIL (table != NULL, FALSE);

    if (!table->visible)
        return TRUE;

    XDBG_TRACE (MPLN, "plane(%d) crtc(%d)\n", table->plane_id, table->crtc_id);

    _secPlaneHideInternal (table);

    return TRUE;
}

Bool
secPlaneMove (int plane_id, int x, int y)
{
    SECPlaneTable *table = _secPlaneTableFind (plane_id);
    xRectangle dst;

    XDBG_RETURN_VAL_IF_FAIL (table != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL (table->cur_fb != NULL, FALSE);

    dst.x = x;
    dst.y = y;
    dst.width = table->dst.width;
    dst.height = table->dst.height;

    if (table->visible && !table->freeze_update)
        if (!_secPlaneShowInternal (table, table->cur_fb, table->cur_fb,
                                    &table->src, &dst, table->zpos, FALSE))
        {
            return FALSE;
        }

    XDBG_TRACE (MPLN, "plane(%d) moved to (%d,%d)\n", table->plane_id, x, y);

    return TRUE;
}

char*
secPlaneDump (char *reply, int *len)
{
    Bool in_use = FALSE;
    int i;

    for (i = 0; i < plane_table_size; i++)
        if (plane_table[i].in_use)
        {
            in_use = TRUE;
            break;
        }

    if (!in_use)
        return reply;

    XDBG_REPLY ("=================================================\n");
    XDBG_REPLY ("plane\tcrtc\tpos\tvisible\tonoff\tfb(w,h)\tsrc\t\tdst\n");

    for (i = 0; i < plane_table_size; i++)
    {
        if (plane_table[i].in_use)
            XDBG_REPLY ("%"PRIdPTR"\t%d\t%d\t%d\t%d\t%"PRIdPTR"(%dx%d)\t%d,%d %dx%d\t%d,%d %dx%d\n",
                        plane_table[i].plane_id,
                        plane_table[i].crtc_id, plane_table[i].zpos,
                        plane_table[i].visible,
                        plane_table[i].onoff,
                        (plane_table[i].cur_fb)?plane_table[i].cur_fb->id:0,
                        (plane_table[i].cur_fb)?plane_table[i].cur_fb->width:0,
                        (plane_table[i].cur_fb)?plane_table[i].cur_fb->height:0,
                        plane_table[i].src.x, plane_table[i].src.y,
                        plane_table[i].src.width, plane_table[i].src.height,
                        plane_table[i].dst.x, plane_table[i].dst.y,
                        plane_table[i].dst.width, plane_table[i].dst.height);
    }

    XDBG_REPLY ("=================================================\n");

    return reply;
}

int
secPlaneGetCount(void)
{
    return plane_table_size;
}
