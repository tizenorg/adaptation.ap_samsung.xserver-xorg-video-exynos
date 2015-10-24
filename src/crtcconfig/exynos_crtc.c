/**************************************************************************

xserver-xorg-video-exynos

Copyright 2011-2012 Samsung Electronics co., Ltd. All Rights Reserved.

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

#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>

#include <xace.h>
#include <xacestr.h>
#include <xorgVersion.h>
#include <tbm_bufmgr.h>
#include <xf86Crtc.h>
#include <xf86DDC.h>
#include <xf86cmap.h>
#include <xf86Xinput.h>
#include <exevents.h>
#include <list.h>
#include <X11/Xatom.h>
#include <X11/extensions/dpmsconst.h>
#include "exynos.h"
#include "exynos_util.h"
#include "exynos_crtc.h"
#include "exynos_output.h"
#include "exynos_plane.h"
#include "exynos_layer.h"
#include "exynos_accel.h"
#include "exynos_drm_ipp.h"
#include "fimg2d.h"
#include "xf86RandR12.h"
#include "exynos_hwc.h"
#include "exynos_layer_manager.h"

static void _cursorRegisterBlockHandler(xf86CrtcPtr pCrtc);
static void _cursorUnregisterBlockHandler(xf86CrtcPtr pCrtc);
static void _cursorShow(xf86CrtcPtr pCrtc);
static void _cursorMove(xf86CrtcPtr pCrtc, int x, int y);
static void _cursorDrawCursor(xf86CrtcPtr pCrtc);

static Atom atom_rotate_root_angle;
static Atom atom_relative_device_exist;

static int
_overlayGetXMoveOffset(xf86CrtcPtr pCrtc, int x)
{
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;
    EXYNOSModePtr pExynosMode =
        (EXYNOSModePtr) EXYNOSPTR(pCrtc->scrn)->pExynosMode;
    int offset = 0;

    if (pCrtcPriv->pipe != 0)
        return 0;

    offset = x + EXYNOS_CURSOR_W - pExynosMode->main_lcd_mode.hdisplay;

    return (offset > 0) ? offset : 0;
}

static Bool
_overlayEnsureBuffer(xf86CrtcPtr pCrtc, Bool move_layer)
{
    EXYNOSModePtr pExynosMode =
        (EXYNOSModePtr) EXYNOSPTR(pCrtc->scrn)->pExynosMode;
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL)
        return FALSE;

    if (move_layer) {
        if (!pCrtcPriv->ovl_vbuf_cursor) {
            pCrtcPriv->ovl_vbuf_cursor =
                exynosUtilAllocVideoBuffer(pCrtc->scrn, FOURCC_RGB32,
                                           EXYNOS_CURSOR_W, EXYNOS_CURSOR_H,
                                           FALSE, TRUE, FALSE);
            XDBG_RETURN_VAL_IF_FAIL(pCrtcPriv->ovl_vbuf_cursor != NULL, FALSE);
            XDBG_TRACE(MCRS, "[%p] ovl_vbuf_cursor(%p) %dx%d created. \n",
                       pCrtc, pCrtcPriv->ovl_vbuf_cursor, EXYNOS_CURSOR_W,
                       EXYNOS_CURSOR_H);
        }
    }
    else {
        if (!pCrtcPriv->ovl_vbuf_pixmap) {
            pCrtcPriv->ovl_vbuf_pixmap =
                exynosUtilAllocVideoBuffer(pCrtc->scrn, FOURCC_RGB32,
                                           pExynosMode->main_lcd_mode.hdisplay,
                                           pExynosMode->main_lcd_mode.vdisplay,
                                           FALSE, TRUE, FALSE);
            XDBG_RETURN_VAL_IF_FAIL(pCrtcPriv->ovl_vbuf_pixmap != NULL, FALSE);
            XDBG_TRACE(MCRS, "[%p] ovl_vbuf_pixmap(%p) %dx%d created. \n",
                       pCrtc, pCrtcPriv->ovl_vbuf_pixmap,
                       pExynosMode->main_lcd_mode.hdisplay,
                       pExynosMode->main_lcd_mode.vdisplay);
        }
    }

    return TRUE;
}

static Bool
_overlayEnsureLayer(xf86CrtcPtr pCrtc)
{
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL)
        return FALSE;
    int connector_type;
    EXYNOSLayerOutput output = LAYER_OUTPUT_LCD;

    if (pCrtcPriv->ovl_layer)
        return TRUE;

    connector_type = exynosCrtcGetConnectType(pCrtc);

    if (connector_type == DRM_MODE_CONNECTOR_LVDS ||
        connector_type == DRM_MODE_CONNECTOR_Unknown) {
        output = LAYER_OUTPUT_LCD;
    }
    else if (connector_type == DRM_MODE_CONNECTOR_HDMIA ||
             connector_type == DRM_MODE_CONNECTOR_HDMIB ||
             connector_type == DRM_MODE_CONNECTOR_VIRTUAL) {
        output = LAYER_OUTPUT_EXT;
    }
    else {
        XDBG_NEVER_GET_HERE(MDISP);
        return FALSE;
    }
#ifndef LAYER_MANAGER
    EXYNOSLayer *layer;

    layer = exynosLayerFind(output, LAYER_UPPER);
    XDBG_RETURN_VAL_IF_FAIL(layer == NULL, FALSE);
#endif
    pCrtcPriv->ovl_layer = exynosLayerCreate(pCrtc->scrn, output, LAYER_UPPER);
    XDBG_RETURN_VAL_IF_FAIL(pCrtcPriv->ovl_layer != NULL, FALSE);

    XDBG_TRACE(MCRS, "[%p] ovl_layer(%p) created. \n", pCrtc,
               pCrtcPriv->ovl_layer);

    return TRUE;
}

static Bool
_overlaySelectBuffer(xf86CrtcPtr pCrtc, Bool move_layer)
{
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL)
        return FALSE;
    EXYNOSModePtr pExynosMode =
        (EXYNOSModePtr) EXYNOSPTR(pCrtc->scrn)->pExynosMode;

    if (!_overlayEnsureLayer(pCrtc))
        return FALSE;

    if (!_overlayEnsureBuffer(pCrtc, move_layer))
        return FALSE;

    if (move_layer) {
        if (exynosLayerGetBuffer(pCrtcPriv->ovl_layer) ==
            pCrtcPriv->ovl_vbuf_cursor)
            return TRUE;

        exynosLayerFreezeUpdate(pCrtcPriv->ovl_layer, TRUE);
        _cursorDrawCursor(pCrtc);
        exynosLayerSetBuffer(pCrtcPriv->ovl_layer, pCrtcPriv->ovl_vbuf_cursor);
        exynosLayerFreezeUpdate(pCrtcPriv->ovl_layer, FALSE);

        int offset = _overlayGetXMoveOffset(pCrtc, pCrtcPriv->cursor_win_x);

        _cursorMove(pCrtc, pCrtcPriv->cursor_win_x - offset,
                    pCrtcPriv->cursor_win_y);

        XDBG_TRACE(MCRS, "[%p] Set ovl_vbuf_cursor. \n", pCrtc);
    }
    else {
        xRectangle rect = { 0, };

        if (exynosLayerGetBuffer(pCrtcPriv->ovl_layer) ==
            pCrtcPriv->ovl_vbuf_pixmap)
            return TRUE;

        rect.width = pExynosMode->main_lcd_mode.hdisplay;
        rect.height = pExynosMode->main_lcd_mode.vdisplay;
        exynosLayerFreezeUpdate(pCrtcPriv->ovl_layer, TRUE);
        exynosLayerSetBuffer(pCrtcPriv->ovl_layer, pCrtcPriv->ovl_vbuf_pixmap);
        exynosLayerFreezeUpdate(pCrtcPriv->ovl_layer, FALSE);

        exynosLayerSetRect(pCrtcPriv->ovl_layer, &rect, &rect);

        XDBG_TRACE(MCRS, "[%p] Set ovl_vbuf_pixmap. \n", pCrtc);
    }

    return TRUE;
}

static Bool
_cursorEnsureCursorImage(xf86CrtcPtr pCrtc)
{
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL)
        return FALSE;
    EXYNOSPtr pExynos = EXYNOSPTR(pCrtc->scrn);

    int x, y, cursor_x, cursor_y;
    int win_x, win_y;
    int rotate;
    int tx = 0, ty = 0;
    double c, s;
    pixman_transform_t t;

    x = pCrtcPriv->cursor_pos_x;
    y = pCrtcPriv->cursor_pos_y;

    //Determine cursor image transform
    rotate = exynosUtilRotateAdd(pExynos->rotate, pCrtcPriv->user_rotate);

    //Transform cursor position and screen size
    switch (pExynos->rotate) {
    case RR_Rotate_0:
    default:
        cursor_x = x;
        cursor_y = y;
        break;
    case RR_Rotate_90:
        cursor_x = y;
        cursor_y = pCrtc->scrn->virtualX - 1 - x;
        break;
    case RR_Rotate_180:
        cursor_x = pCrtc->scrn->virtualX - 1 - x;
        cursor_y = pCrtc->scrn->virtualY - 1 - y;
        break;
    case RR_Rotate_270:
        cursor_x = pCrtc->scrn->virtualY - 1 - y;
        cursor_y = x;
        break;
    }

    switch (rotate) {
    case RR_Rotate_0:
    default:
        c = 1.0;
        s = 0.0;
        win_x = cursor_x;
        win_y = cursor_y;
        break;
    case RR_Rotate_90:
        c = 0.0;
        s = 1.0;
        tx = EXYNOS_CURSOR_W;
        ty = 0;

        win_x = cursor_x;
        win_y = cursor_y - EXYNOS_CURSOR_W;
        break;
    case RR_Rotate_180:
        c = -1.0;
        s = 0.0;
        tx = EXYNOS_CURSOR_W;
        ty = EXYNOS_CURSOR_H;

        win_x = cursor_x - EXYNOS_CURSOR_W;
        win_y = cursor_y - EXYNOS_CURSOR_H;
        break;
    case RR_Rotate_270:
        c = 0.0;
        s = -1.0;
        tx = 0;
        ty = EXYNOS_CURSOR_H;

        win_x = cursor_x - EXYNOS_CURSOR_H;
        win_y = cursor_y;
        break;
    }

    pCrtcPriv->cursor_win_x = win_x;
    pCrtcPriv->cursor_win_y = win_y;

    if (pCrtcPriv->cursor_image == NULL) {
        XDBG_RETURN_VAL_IF_FAIL(pCrtcPriv->backup_image != NULL, FALSE);

        XDBG_DEBUG(MCRS, "[%p] (%d + %d) => %d \n", pCrtc,
                   pExynos->rotate, pCrtcPriv->user_rotate, rotate);

        if (rotate == RR_Rotate_0) {
            pCrtcPriv->cursor_image = pCrtcPriv->backup_image;
            pixman_image_ref(pCrtcPriv->cursor_image);
        }
        else {
            //Clear cursor image
            pCrtcPriv->cursor_image =
                pixman_image_create_bits(PIXMAN_a8r8g8b8, EXYNOS_CURSOR_W,
                                         EXYNOS_CURSOR_H, NULL, 0);

            //Copy Cursor image
            pixman_transform_init_rotate(&t, pixman_double_to_fixed(c),
                                         pixman_double_to_fixed(s));
            pixman_transform_translate(&t, NULL, pixman_int_to_fixed(tx),
                                       pixman_int_to_fixed(ty));
            pixman_image_set_transform(pCrtcPriv->backup_image, &t);
            pixman_image_composite(PIXMAN_OP_SRC, pCrtcPriv->backup_image, NULL,
                                   pCrtcPriv->cursor_image, 0, 0, 0, 0, 0, 0,
                                   EXYNOS_CURSOR_W, EXYNOS_CURSOR_H);
            pixman_transform_init_rotate(&t, pixman_double_to_fixed(1.0),
                                         pixman_double_to_fixed(0.0));
            pixman_image_set_transform(pCrtcPriv->backup_image, &t);
        }
    }

    return TRUE;
}

static Bool
_cursorEnsureCanvas(xf86CrtcPtr pCrtc, EXYNOSVideoBuf * vbuf, int width,
                    int height)
{
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL)
        return FALSE;
    tbm_bo_handle bo_handle;

    if (pCrtcPriv->ovl_canvas)
        return TRUE;

    if (!_overlayEnsureBuffer(pCrtc, pCrtcPriv->move_layer))
        return FALSE;

    XDBG_RETURN_VAL_IF_FAIL(vbuf != NULL, FALSE);

    bo_handle = tbm_bo_get_handle(vbuf->bo[0], TBM_DEVICE_CPU);
    XDBG_RETURN_VAL_IF_FAIL(bo_handle.ptr != NULL, FALSE);

    pCrtcPriv->ovl_canvas = pixman_image_create_bits(PIXMAN_a8r8g8b8,
                                                     width, height,
                                                     (uint32_t *) bo_handle.ptr,
                                                     width * 4);

    XDBG_TRACE(MCRS, "[%p] ovl_canvas(%p) %dx%d created.\n", pCrtc,
               pCrtcPriv->ovl_canvas, width, height);

    return TRUE;
}

static Bool
_cursorEnsureSavedImage(xf86CrtcPtr pCrtc)
{
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL)
        return FALSE;
    if (pCrtcPriv->saved_image)
        return TRUE;

    pCrtcPriv->saved_image = pixman_image_create_bits(PIXMAN_a8r8g8b8,
                                                      EXYNOS_CURSOR_W,
                                                      EXYNOS_CURSOR_H, NULL, 0);
    XDBG_TRACE(MCRS, "[%p] saved_image(%p) %dx%d created.\n", pCrtc,
               pCrtcPriv->saved_image, EXYNOS_CURSOR_W, EXYNOS_CURSOR_H);

    return TRUE;
}

static void
_cursorSaveImage(xf86CrtcPtr pCrtc)
{
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL)
        return;
    EXYNOSModePtr pExynosMode =
        (EXYNOSModePtr) EXYNOSPTR(pCrtc->scrn)->pExynosMode;

    XDBG_RETURN_IF_FAIL(pCrtcPriv->move_layer == FALSE);

    _cursorEnsureCanvas(pCrtc, pCrtcPriv->ovl_vbuf_pixmap,
                        pExynosMode->main_lcd_mode.hdisplay,
                        pExynosMode->main_lcd_mode.vdisplay);

    _cursorEnsureSavedImage(pCrtc);

    pixman_image_composite(PIXMAN_OP_SRC,
                           pCrtcPriv->ovl_canvas,
                           NULL,
                           pCrtcPriv->saved_image,
                           pCrtcPriv->cursor_win_x, pCrtcPriv->cursor_win_y,
                           0, 0, 0, 0, EXYNOS_CURSOR_W, EXYNOS_CURSOR_H);

    pCrtcPriv->saved_box.x1 = pCrtcPriv->cursor_win_x;
    pCrtcPriv->saved_box.y1 = pCrtcPriv->cursor_win_y;
    pCrtcPriv->saved_box.x2 = pCrtcPriv->cursor_win_x + EXYNOS_CURSOR_W;
    pCrtcPriv->saved_box.y2 = pCrtcPriv->cursor_win_y + EXYNOS_CURSOR_H;

    XDBG_DEBUG(MCRS, "[%p] (%d,%d %dx%d) saved. \n", pCrtc,
               pCrtcPriv->cursor_win_x, pCrtcPriv->cursor_win_y,
               EXYNOS_CURSOR_W, EXYNOS_CURSOR_H);
}

static void
_cursorRestoreImage(xf86CrtcPtr pCrtc)
{
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL)
        return;

    if (!pCrtcPriv->saved_image || !pCrtcPriv->ovl_canvas)
        return;

    pixman_image_composite(PIXMAN_OP_SRC,
                           pCrtcPriv->saved_image,
                           NULL,
                           pCrtcPriv->ovl_canvas,
                           0, 0, 0, 0,
                           pCrtcPriv->saved_box.x1, pCrtcPriv->saved_box.y1,
                           EXYNOS_CURSOR_W, EXYNOS_CURSOR_H);

    if (pCrtcPriv->ovl_layer && exynosLayerIsVisible(pCrtcPriv->ovl_layer))
        exynosLayerUpdate(pCrtcPriv->ovl_layer);

    XDBG_DEBUG(MCRS, "[%p] (%d,%d %dx%d) restored. \n", pCrtc,
               pCrtcPriv->saved_box.x1, pCrtcPriv->saved_box.y1,
               EXYNOS_CURSOR_W, EXYNOS_CURSOR_H);
}

static void
_cursorDrawCursor(xf86CrtcPtr pCrtc)
{
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL)
        return;
    int x, y;

    XDBG_RETURN_IF_FAIL(pCrtcPriv->ovl_canvas != NULL);
    XDBG_RETURN_IF_FAIL(pCrtcPriv->cursor_image != NULL);

    if (pCrtcPriv->move_layer) {
        /* clear */
        pixman_color_t color = { 0, };
        pixman_rectangle16_t rect = { 0, 0, EXYNOS_CURSOR_W, EXYNOS_CURSOR_H };
        pixman_image_fill_rectangles(PIXMAN_OP_CLEAR, pCrtcPriv->ovl_canvas,
                                     &color, 1, &rect);

        x = _overlayGetXMoveOffset(pCrtc, pCrtcPriv->cursor_win_x);
        y = 0;
    }
    else {
        x = pCrtcPriv->cursor_win_x;
        y = pCrtcPriv->cursor_win_y;
    }

    pixman_image_composite(PIXMAN_OP_OVER,
                           pCrtcPriv->cursor_image,
                           NULL,
                           pCrtcPriv->ovl_canvas,
                           0, 0, 0, 0, x, y, EXYNOS_CURSOR_W, EXYNOS_CURSOR_H);

    XDBG_DEBUG(MCRS, "[%p] (%d,%d %dx%d) drawn. \n", pCrtc,
               x, y, EXYNOS_CURSOR_W, EXYNOS_CURSOR_H);

    exynosUtilCacheFlush(pCrtc->scrn);

    if (pCrtcPriv->ovl_layer && exynosLayerIsVisible(pCrtcPriv->ovl_layer))
        exynosLayerUpdate(pCrtcPriv->ovl_layer);
}

static void
_cursorReportDamage(DamagePtr pDamage, RegionPtr pRegion, void *closure)
{
    xf86CrtcPtr pCrtc = (xf86CrtcPtr) closure;
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL)
        return;
    EXYNOSPtr pExynos = EXYNOSPTR(pCrtc->scrn);

    if (pCrtcPriv->move_layer)
        return;

    if (!pExynos->enableCursor || !pCrtcPriv->cursor_show)
        return;

    if (RegionContainsRect(pRegion, &pCrtcPriv->saved_box) != rgnOUT) {
        XDBG_TRACE(MCRS, "[%p] \n", pCrtc);
        pCrtcPriv->need_cursor_update = TRUE;
        _cursorRestoreImage(pCrtc);
        _cursorRegisterBlockHandler(pCrtc);
    }
}

static void
_cursorDamageDestroy(DamagePtr pDamage, void *closure)
{
    xf86CrtcPtr pCrtc = (xf86CrtcPtr) closure;
    EXYNOSPtr pExynos = EXYNOSPTR(pCrtc->scrn);

    if (!pExynos->ovl_damage)
        return;

    pExynos->ovl_damage = NULL;
}

static void
_cursorBlockHandler(pointer data, OSTimePtr pTimeout, pointer pRead)
{
    xf86CrtcPtr pCrtc = (xf86CrtcPtr) data;
    EXYNOSPtr pExynos = EXYNOSPTR(pCrtc->scrn);
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL)
        return;
    XDBG_RETURN_IF_FAIL(pCrtcPriv->move_layer == FALSE);

    if (pExynos->ovl_drawable) {
        if (pExynos->ovl_damage == NULL) {
            pExynos->ovl_damage =
                DamageCreate((DamageReportFunc) _cursorReportDamage,
                             (DamageDestroyFunc) _cursorDamageDestroy,
                             DamageReportRawRegion, TRUE, pCrtc->scrn->pScreen,
                             pCrtc);
            XDBG_RETURN_IF_FAIL(pExynos->ovl_damage);
            DamageRegister(pExynos->ovl_drawable, pExynos->ovl_damage);
        }
    }
    else {
        if (pExynos->ovl_damage) {
            DamageDestroy(pExynos->ovl_damage);
            pExynos->ovl_damage = NULL;
        }
    }

    XDBG_DEBUG(MCRS,
               "[%p] enable(%d) cursor_show(%d) need_update(%d) show(%d) \n",
               pCrtc, pExynos->enableCursor, pCrtcPriv->cursor_show,
               pCrtcPriv->need_cursor_update, pCrtcPriv->cursor_show);

    if (pExynos->enableCursor && pCrtcPriv->need_cursor_update) {
        EXYNOSModePtr pExynosMode =
            (EXYNOSModePtr) EXYNOSPTR(pCrtc->scrn)->pExynosMode;

        _cursorEnsureCursorImage(pCrtc);
        _cursorEnsureCanvas(pCrtc, pCrtcPriv->ovl_vbuf_pixmap,
                            pExynosMode->main_lcd_mode.hdisplay,
                            pExynosMode->main_lcd_mode.vdisplay);

        _cursorSaveImage(pCrtc);

        /*Draw Cursor */
        if (pCrtcPriv->cursor_show)
            _cursorDrawCursor(pCrtc);

        _overlaySelectBuffer(pCrtc, pCrtcPriv->move_layer);
        _cursorMove(pCrtc, pCrtcPriv->cursor_win_x, pCrtcPriv->cursor_win_y);

        pCrtcPriv->need_cursor_update = FALSE;
    }

    if (!exynosLayerIsVisible(pCrtcPriv->ovl_layer))
        exynosLayerShow(pCrtcPriv->ovl_layer);

    if (!pExynos->enableCursor || !pCrtcPriv->cursor_show ||
        !pCrtcPriv->need_cursor_update)
        _cursorUnregisterBlockHandler(pCrtc);
}

static Bool
_cursorSetPointerDeviceRotate(DeviceIntPtr dev, int rotate)
{
#define EVDEV_PROP_INVERT_AXES "Evdev Axis Inversion"   /* BOOL, 2 values [x, y], 1 inverts axis */
#define EVDEV_PROP_SWAP_AXES "Evdev Axes Swap"  /* BOOL */

    int swap = 0;
    char inv[2];

    static Atom swap_axes = 0;
    static Atom invert_axes = 0;
    int rc;

    if (!dev)
        return FALSE;

    XDBG_TRACE(MCRS, "device %s (valuator:%p)\n", dev->name, dev->valuator);

    if (!swap_axes)
        swap_axes =
            MakeAtom(EVDEV_PROP_SWAP_AXES, strlen(EVDEV_PROP_SWAP_AXES), TRUE);

    if (!invert_axes)
        invert_axes =
            MakeAtom(EVDEV_PROP_INVERT_AXES, strlen(EVDEV_PROP_INVERT_AXES),
                     TRUE);

    switch (rotate) {
    case RR_Rotate_0:
        swap = 0;
        inv[0] = 0;
        inv[1] = 0;
        break;
    case RR_Rotate_90:
        swap = 1;
        inv[0] = 0;
        inv[1] = 1;
        break;
    case RR_Rotate_180:
        swap = 0;
        inv[0] = 1;
        inv[1] = 1;
        break;
    case RR_Rotate_270:
        swap = 1;
        inv[0] = 1;
        inv[1] = 0;
        break;
    default:
        XDBG_ERROR(MCRS, "Error.. cursor_rotate:%d\n", rotate);
        return FALSE;
    }

    XDBG_TRACE(MCRS, "%s change(swap:%d, inv:%d,%d rotate:%d)\n", dev->name,
               swap, inv[0], inv[1], rotate);
    rc = XIChangeDeviceProperty(dev, swap_axes, XA_INTEGER, 8, PropModeReplace,
                                1, &swap, TRUE);
    if (rc != Success) {
        XDBG_ERROR(MCRS, "Fail change swap(%s , swap:%d)\n", dev->name, swap);
    }

    rc = XIChangeDeviceProperty(dev, invert_axes, XA_INTEGER, 8,
                                PropModeReplace, 2, inv, TRUE);
    if (rc != Success) {
        XDBG_ERROR(MCRS, "Fail change invert(%s , invert:%d,%d)\n", dev->name,
                   inv[0], inv[1]);
    }

    return TRUE;
}

static Bool
_cursorFindRelativeDevice(xf86CrtcPtr pCrtc)
{
    if (pCrtc == NULL) {
        return FALSE;
    }
    InputInfoPtr localDevices;
    DeviceIntPtr dev;
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL)
        return FALSE;

    XDBG_TRACE(MCRS, "[%p]  \n", pCrtc);

    localDevices = xf86FirstLocalDevice();
    while (localDevices) {
        dev = localDevices->dev;
        _cursorSetPointerDeviceRotate(dev, pCrtcPriv->user_rotate);
        localDevices = localDevices->next;
    }

    return TRUE;
}

static void
_cursorRotateHook(CallbackListPtr *pcbl, pointer unused, pointer calldata)
{
    ScrnInfoPtr pScrn = (ScrnInfoPtr) unused;
    xf86CrtcPtr pCrtc = xf86CompatCrtc(pScrn);

    if (pCrtc == NULL)
        return;
    XacePropertyAccessRec *rec = (XacePropertyAccessRec *) calldata;
    PropertyPtr pProp = *rec->ppProp;
    Atom name = pProp->propertyName;

    XDBG_RETURN_IF_FAIL(pCrtc != NULL);

    /* Don't care about the new content check */
    if (rec->pWin != pScrn->pScreen->root)      //Check Rootwindow
        return;

    if (name == atom_rotate_root_angle && (rec->access_mode & DixWriteAccess)) {
        int rotate_degree = *(int *) pProp->data;

        XDBG_TRACE(MCRS, "[%p] Change root angle(%d)\n", pCrtc, rotate_degree);
        exynosCrtcCursorRotate(pCrtc, exynosUtilDegreeToRotate(rotate_degree));
    }

    if (name == atom_relative_device_exist
        && (rec->access_mode & DixWriteAccess)) {
        int exist = *(int *) pProp->data;

        if (exist) {
            _cursorFindRelativeDevice(pCrtc);
            XDBG_TRACE(MCRS, "[%p] Change device exist(%d)\n", pCrtc, exist);
        }
    }

    return;
}

static void
_cursorRegisterBlockHandler(xf86CrtcPtr pCrtc)
{
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL)
        return;

    XDBG_RETURN_IF_FAIL(pCrtcPriv->move_layer == FALSE);

    if (pCrtcPriv->registered_block_handler)
        return;

    XDBG_DEBUG(MCRS, "[%p] \n", pCrtc);

    RegisterBlockAndWakeupHandlers(_cursorBlockHandler,
                                   (WakeupHandlerProcPtr) NoopDDA, pCrtc);

    pCrtcPriv->registered_block_handler = TRUE;
}

static void
_cursorUnregisterBlockHandler(xf86CrtcPtr pCrtc)
{
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL)
        return;

    if (!pCrtcPriv->registered_block_handler)
        return;

    XDBG_DEBUG(MCRS, "[%p] \n", pCrtc);

    RemoveBlockAndWakeupHandlers(_cursorBlockHandler,
                                 (WakeupHandlerProcPtr) NoopDDA, pCrtc);

    pCrtcPriv->registered_block_handler = FALSE;
}

static void
_cursorMove(xf86CrtcPtr pCrtc, int x, int y)
{
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL)
        return;

    if (!pCrtcPriv->move_layer)
        return;

    if (pCrtcPriv->ovl_layer) {
        xRectangle src = { 0, };
        xRectangle dst = { 0, };

        src.width = EXYNOS_CURSOR_W;
        src.height = EXYNOS_CURSOR_H;

        dst.x = x;
        dst.y = y;
        dst.width = EXYNOS_CURSOR_W;
        dst.height = EXYNOS_CURSOR_H;

        XDBG_DEBUG(MCRS, "[%p] to (%d,%d)\n", pCrtc, x, y);

        exynosLayerSetRect(pCrtcPriv->ovl_layer, &src, &dst);
    }
}

static void
_cursorInit(xf86CrtcPtr pCrtc)
{
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL)
        return;

    XDBG_TRACE(MCRS, "[%p] \n", pCrtc);

    //Damage Create
    if (!pCrtcPriv->move_layer)
        _cursorRegisterBlockHandler(pCrtc);
}

static int
_cursorDestroy(xf86CrtcPtr pCrtc)
{
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL)
        return FALSE;

    XDBG_TRACE(MCRS, "[%p]  \n", pCrtc);

    if (pCrtcPriv->saved_image) {
        pixman_image_unref(pCrtcPriv->saved_image);
        pCrtcPriv->saved_image = NULL;
    }

    if (pCrtcPriv->cursor_image) {
        pixman_image_unref(pCrtcPriv->cursor_image);
        pCrtcPriv->cursor_image = NULL;
    }

    if (pCrtcPriv->ovl_canvas) {
        XDBG_TRACE(MCRS, "[%p] ovl_canvas(%p) destroy.\n", pCrtc,
                   pCrtcPriv->ovl_canvas);
        pixman_image_unref(pCrtcPriv->ovl_canvas);
        pCrtcPriv->ovl_canvas = NULL;
        pCrtcPriv->need_draw_cursor = TRUE;
    }

    if (pCrtcPriv->ovl_layer) {
        XDBG_TRACE(MCRS, "[%p] ovl_layer(%p) destroy.\n", pCrtc,
                   pCrtcPriv->ovl_layer);
        exynosLayerUnref(pCrtcPriv->ovl_layer);
        pCrtcPriv->ovl_layer = NULL;
    }

    return TRUE;
}

static void
_cursorShow(xf86CrtcPtr pCrtc)
{
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL)
        return;
    EXYNOSPtr pExynos = EXYNOSPTR(pCrtc->scrn);

    if (!pExynos->enableCursor)
        return;

    if (pCrtcPriv->ovl_layer && !exynosLayerTurnStatus(pCrtcPriv->ovl_layer))
        exynosLayerTurn(pCrtcPriv->ovl_layer, TRUE, FALSE);

    XDBG_TRACE(MCRS, "[%p] user_rotate(%d)\n", pCrtc, pCrtcPriv->user_rotate);

    if (pCrtcPriv->move_layer) {
        _overlayEnsureBuffer(pCrtc, pCrtcPriv->move_layer);
        _overlayEnsureLayer(pCrtc);

        _cursorEnsureCursorImage(pCrtc);
        _cursorEnsureCanvas(pCrtc, pCrtcPriv->ovl_vbuf_cursor,
                            EXYNOS_CURSOR_W, EXYNOS_CURSOR_H);
        _cursorDrawCursor(pCrtc);

        _overlaySelectBuffer(pCrtc, pCrtcPriv->move_layer);

        int offset = _overlayGetXMoveOffset(pCrtc, pCrtcPriv->cursor_win_x);

        _cursorMove(pCrtc, pCrtcPriv->cursor_win_x - offset,
                    pCrtcPriv->cursor_win_y);

        if (!exynosLayerIsVisible(pCrtcPriv->ovl_layer))
            exynosLayerShow(pCrtcPriv->ovl_layer);
    }
    else {
        pCrtcPriv->need_cursor_update = TRUE;
        _cursorRestoreImage(pCrtc);
        _cursorRegisterBlockHandler(pCrtc);
    }
}

static void
_cursorHide(xf86CrtcPtr pCrtc)
{
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL)
        return;

    XDBG_TRACE(MCRS, "[%p] \n", pCrtc);

    if (pCrtcPriv->move_layer) {
        if (pCrtcPriv->ovl_layer && exynosLayerIsVisible(pCrtcPriv->ovl_layer))
            exynosLayerHide(pCrtcPriv->ovl_layer);
    }
    else {
        _cursorRestoreImage(pCrtc);

        if (pCrtcPriv->need_off && !pCrtcPriv->cursor_show) {
            if (pCrtcPriv->ovl_layer &&
                exynosLayerIsVisible(pCrtcPriv->ovl_layer))
                exynosLayerHide(pCrtcPriv->ovl_layer);
            return;
        }
    }

    if (pCrtcPriv->ovl_layer && exynosLayerTurnStatus(pCrtcPriv->ovl_layer)) {
        Bool turnoff = FALSE;

        if (pCrtcPriv->ref_overlay && pCrtcPriv->need_off)
            turnoff = TRUE;
        if (!pCrtcPriv->ref_overlay)
            turnoff = TRUE;

        if (turnoff)
            _cursorDestroy(pCrtc);
    }

    pCrtcPriv->cursor_old_offset = 0;
    pCrtcPriv->need_cursor_update = TRUE;
}

static Bool
_cursorEnable(xf86CrtcPtr pCrtc, Bool enable)
{
    ScrnInfoPtr pScrn = pCrtc->scrn;
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL)
        return FALSE;

    if (!pCrtcPriv->cursor_show)
        return FALSE;

    XDBG_TRACE(MCRS, "[%p] enable(%d) \n", pCrtc, enable);

    if (enable) {
        _cursorShow(pCrtc);

        if (pCrtc == xf86CompatCrtc(pScrn)) {
            PropertyPtr rotate_prop;

            /* Set Current Root Rotation */
            rotate_prop = exynosUtilGetWindowProperty(pScrn->pScreen->root,
                                                      "_E_ILLUME_ROTATE_ROOT_ANGLE");
            if (rotate_prop) {
                int rotate =
                    exynosUtilDegreeToRotate(*(int *) rotate_prop->data);

                pCrtcPriv->user_rotate = rotate;

                //Send swap property to relative input device
                _cursorFindRelativeDevice(pCrtc);
            }
        }

        /* Hook for window rotate */
        atom_rotate_root_angle =
            MakeAtom("_E_ILLUME_ROTATE_ROOT_ANGLE",
                     strlen("_E_ILLUME_ROTATE_ROOT_ANGLE"), FALSE);
        atom_relative_device_exist =
            MakeAtom("X Mouse Exist", strlen("X Mouse Exist"), TRUE);

        if (atom_rotate_root_angle != None) {
            if (!XaceRegisterCallback
                (XACE_PROPERTY_ACCESS, _cursorRotateHook, pScrn))
                XDBG_ERROR(MCRS,
                           "[%p] Fail XaceRegisterCallback:XACE_PROPERTY_ACCESS\n",
                           pCrtc);

            XDBG_TRACE(MCRS,
                       "[%p] Hook property : _E_ILLUME_ROTATE_ROOT_ANGLE\n",
                       pCrtc);
        }
        else
            XDBG_TRACE(MCRS, "[%p] Cannot find _E_ILLUME_ROTATE_ROOT_ANGLE\n",
                       pCrtc);
    }
    else {
        XaceDeleteCallback(XACE_PROPERTY_ACCESS, _cursorRotateHook, pScrn);

        _cursorHide(pCrtc);
    }

    pCrtcPriv->cursor_old_offset = 0;

    return TRUE;
}

static Bool
_cursorRotate(xf86CrtcPtr pCrtc, int rotate)
{
    EXYNOSPtr pExynos = EXYNOSPTR(pCrtc->scrn);
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL)
        return FALSE;

    if (pCrtcPriv->user_rotate == rotate)
        return TRUE;

    if (!pCrtcPriv->cursor_show)
        return TRUE;

    XDBG_TRACE(MCRS, "[%p] rotate(%d) \n", pCrtc, rotate);

    pCrtcPriv->user_rotate = rotate;

    if (pExynos->enableCursor && pCrtcPriv->cursor_show) {
        //Send swap property to relative input device
        _cursorFindRelativeDevice(pCrtc);

        if (pCrtcPriv->cursor_image) {
            pixman_image_unref(pCrtcPriv->cursor_image);
            pCrtcPriv->cursor_image = NULL;
        }

        if (pCrtcPriv->move_layer) {
            _overlayEnsureBuffer(pCrtc, pCrtcPriv->move_layer);
            _overlayEnsureLayer(pCrtc);

            _cursorEnsureCursorImage(pCrtc);
            _cursorEnsureCanvas(pCrtc, pCrtcPriv->ovl_vbuf_cursor,
                                EXYNOS_CURSOR_W, EXYNOS_CURSOR_H);
            _cursorDrawCursor(pCrtc);

            int offset = _overlayGetXMoveOffset(pCrtc, pCrtcPriv->cursor_win_x);

            _cursorMove(pCrtc, pCrtcPriv->cursor_win_x - offset,
                        pCrtcPriv->cursor_win_y);
        }
        else {
            pCrtcPriv->need_cursor_update = TRUE;
            _cursorRestoreImage(pCrtc);
            _cursorRegisterBlockHandler(pCrtc);
        }
    }

    pCrtcPriv->cursor_old_offset = 0;

    return TRUE;
}

static Bool
_cursorChangeStatus(xf86CrtcPtr pCrtc)
{
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL)
        return FALSE;
    EXYNOSPtr pExynos = EXYNOSPTR(pCrtc->scrn);
    int new_value;

    if (pCrtcPriv->ref_overlay && !pCrtcPriv->need_off)
        new_value = FALSE;
    else
        new_value = TRUE;

    XDBG_TRACE(MCRS, "[%p] ref(%d) off(%d) value(%d=>%d) cursor(%d,%d) \n",
               pCrtc, pCrtcPriv->ref_overlay, pCrtcPriv->need_off,
               pCrtcPriv->move_layer, new_value, pCrtcPriv->cursor_show,
               pExynos->enableCursor);

    /* layer off if needed */
    if (!pExynos->enableCursor && pCrtcPriv->ovl_layer &&
        exynosLayerTurnStatus(pCrtcPriv->ovl_layer)) {
        Bool turnoff = FALSE;

        if (pCrtcPriv->ref_overlay && pCrtcPriv->need_off)
            turnoff = TRUE;
        if (!pCrtcPriv->ref_overlay)
            turnoff = TRUE;

        if (turnoff) {
            _cursorDestroy(pCrtc);
            return TRUE;
        }
    }

    /* layer on if needed */
    if (pCrtcPriv->ovl_layer && !exynosLayerTurnStatus(pCrtcPriv->ovl_layer))
        if (pExynos->enableCursor ||
            (pCrtcPriv->ref_overlay && !pCrtcPriv->need_off))
            exynosLayerTurn(pCrtcPriv->ovl_layer, TRUE, FALSE);

    if (pCrtcPriv->move_layer == new_value)
        return TRUE;

    pCrtcPriv->move_layer = new_value;

    if (pCrtcPriv->ovl_canvas) {
        XDBG_TRACE(MCRS, "[%p] ovl_canvas(%p) destroy.\n", pCrtc,
                   pCrtcPriv->ovl_canvas);
        pixman_image_unref(pCrtcPriv->ovl_canvas);
        pCrtcPriv->ovl_canvas = NULL;
        pCrtcPriv->need_draw_cursor = TRUE;
    }

    if (pCrtcPriv->cursor_show)
        _cursorShow(pCrtc);

    if (new_value && pCrtcPriv->ovl_vbuf_pixmap) {
        EXYNOSModePtr pExynosMode =
            (EXYNOSModePtr) EXYNOSPTR(pCrtc->scrn)->pExynosMode;
        pixman_image_t *old = pCrtcPriv->ovl_canvas;

        pCrtcPriv->ovl_canvas = NULL;

        _cursorEnsureCanvas(pCrtc, pCrtcPriv->ovl_vbuf_pixmap,
                            pExynosMode->main_lcd_mode.hdisplay,
                            pExynosMode->main_lcd_mode.vdisplay);

        _cursorRestoreImage(pCrtc);

        if (pCrtcPriv->ovl_canvas)
            pixman_image_unref(pCrtcPriv->ovl_canvas);

        pCrtcPriv->ovl_canvas = old;
    }

    if (!pCrtcPriv->ovl_layer)
        _overlaySelectBuffer(pCrtc, pCrtcPriv->move_layer);

    if (pCrtcPriv->ovl_layer)
        if (!exynosLayerIsVisible(pCrtcPriv->ovl_layer))
            exynosLayerShow(pCrtcPriv->ovl_layer);

    return TRUE;
}

static void
_flipPixmapInit(xf86CrtcPtr pCrtc)
{
    ScrnInfoPtr pScrn = pCrtc->scrn;
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL)
        return;
    int flip_backbufs = pExynos->flip_bufs - 1;
    int i;

    pCrtcPriv->flip_backpixs.lub = -1;
    pCrtcPriv->flip_backpixs.num = flip_backbufs;

    pCrtcPriv->flip_backpixs.pix_free = calloc(flip_backbufs, sizeof(void *));
    XDBG_RETURN_IF_FAIL(pCrtcPriv->flip_backpixs.pix_free != NULL);
    for (i = 0; i < flip_backbufs; i++)
        pCrtcPriv->flip_backpixs.pix_free[i] = TRUE;
    pCrtcPriv->flip_backpixs.flip_pixmaps =
        calloc(flip_backbufs, sizeof(void *));
    pCrtcPriv->flip_backpixs.flip_draws = calloc(flip_backbufs, sizeof(void *));
}

static void
_flipPixmapDeinit(xf86CrtcPtr pCrtc)
{
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL)
        return;
    ScreenPtr pScreen = pCrtc->scrn->pScreen;
    int i;

    for (i = 0; i < pCrtcPriv->flip_backpixs.num; i++) {
        pCrtcPriv->flip_backpixs.pix_free[i] = TRUE;
        if (pCrtcPriv->flip_backpixs.flip_pixmaps[i]) {
#if USE_XDBG
            if (pCrtcPriv->flip_backpixs.flip_draws[i])
                xDbgLogPListDrawRemoveRefPixmap(pCrtcPriv->flip_backpixs.
                                                flip_draws[i],
                                                pCrtcPriv->flip_backpixs.
                                                flip_pixmaps[i]);
#endif

            (*pScreen->DestroyPixmap) (pCrtcPriv->flip_backpixs.
                                       flip_pixmaps[i]);
            pCrtcPriv->flip_backpixs.flip_pixmaps[i] = NULL;
            pCrtcPriv->flip_backpixs.flip_draws[i] = NULL;
        }
    }
    pCrtcPriv->flip_backpixs.lub = -1;
}

static xf86CrtcPtr
_exynosCrtcGetFromPipe(ScrnInfoPtr pScrn, int pipe)
{
    xf86CrtcConfigPtr pXf86CrtcConfig;

    pXf86CrtcConfig = XF86_CRTC_CONFIG_PTR(pScrn);
    xf86CrtcPtr pCrtc = NULL;
    EXYNOSCrtcPrivPtr pCrtcPriv = NULL;
    int i;

    for (i = 0; i < pXf86CrtcConfig->num_output; i++) {
        pCrtc = pXf86CrtcConfig->crtc[i];
        pCrtcPriv = pCrtc->driver_private;
        if (pCrtcPriv == NULL)
            continue;
        if (pCrtcPriv->pipe == pipe) {
            return pCrtc;
        }
    }

    return NULL;
}

static void
EXYNOSCrtcDpms(xf86CrtcPtr pCrtc, int pMode)
{

}

static Bool
EXYNOSCrtcSetModeMajor(xf86CrtcPtr pCrtc, DisplayModePtr pMode,
                       Rotation rotation, int x, int y)
{
    ScrnInfoPtr pScrn = pCrtc->scrn;
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    EXYNOSFbPtr pFb = pExynos->pFb;
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL) {
        return TRUE;
    }
    EXYNOSModePtr pExynosMode = pCrtcPriv->pExynosMode;
    tbm_bo bo = NULL, old_bo = NULL;
    tbm_bo bo_accessibility[2] = { 0, }
    , old_bo_accessibility[2] = {
    0,};
    int saved_x, saved_y;
    Rotation saved_rotation;
    DisplayModeRec saved_mode;
    Bool ret = FALSE;

    XDBG_DEBUG(MDISP,
               "SetModeMajor pMode:%d cur(%dx%d+%d+%d),rot:%d new(%dx%d+%d+%d),refresh(%f)rot:%d\n",
               exynosCrtcID(pCrtcPriv),
               pCrtc->mode.HDisplay, pCrtc->mode.VDisplay, pCrtc->x, pCrtc->y,
               pCrtc->rotation,
               pMode->HDisplay, pMode->VDisplay, x, y, pMode->VRefresh,
               rotation);

    memcpy(&saved_mode, &pCrtc->mode, sizeof(DisplayModeRec));
    saved_x = pCrtc->x;
    saved_y = pCrtc->y;
    saved_rotation = pCrtc->rotation;

    memcpy(&pCrtc->mode, pMode, sizeof(DisplayModeRec));
    pCrtc->x = x;
    pCrtc->y = y;
    pCrtc->rotation = rotation;

    if (pExynos->fake_root)
        exynosDisplaySwapModeToKmode(pCrtc->scrn, &pCrtcPriv->kmode, pMode);
    else
        exynosDisplayModeToKmode(pCrtc->scrn, &pCrtcPriv->kmode, pMode);

    /* accessibility */
    if (pCrtcPriv->bAccessibility || pCrtcPriv->screen_rotate_degree > 0) {
        XDBG_GOTO_IF_FAIL(pCrtcPriv->accessibility_front_bo != NULL, fail);
        XDBG_GOTO_IF_FAIL(pCrtcPriv->accessibility_back_bo != NULL, fail);

        old_bo_accessibility[0] = pCrtcPriv->accessibility_front_bo;
        old_bo_accessibility[1] = pCrtcPriv->accessibility_back_bo;

        bo_accessibility[0] =
            exynosRenderBoCreate(pScrn, pMode->HDisplay, pMode->VDisplay);
        bo_accessibility[1] =
            exynosRenderBoCreate(pScrn, pMode->HDisplay, pMode->VDisplay);

        pCrtcPriv->accessibility_front_bo = bo_accessibility[0];
        pCrtcPriv->accessibility_back_bo = bo_accessibility[1];
    }

    /* find bo which covers the requested mode of crtc */
    old_bo = pCrtcPriv->front_bo;
    bo = exynosFbGetBo(pFb, x, y, pMode->HDisplay, pMode->VDisplay, FALSE);
    XDBG_GOTO_IF_FAIL(bo != NULL, fail);
    pCrtcPriv->front_bo = bo;

    ret = exynosCrtcApply(pCrtc);
    XDBG_GOTO_IF_FAIL(ret == TRUE, fail);
    int i;
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pCrtc->scrn);

    for (i = 0; i < xf86_config->num_output; i++) {
        xf86OutputPtr pOutput = xf86_config->output[i];
        EXYNOSOutputPrivPtr pOutputPriv;

        if (pOutput->crtc != pCrtc)
            continue;

        pOutputPriv = pOutput->driver_private;

        /* TODO :: soolim :: check this out */
        exynosOutputDpmsSet(pOutput, DPMSModeOn);
        pOutputPriv->dpms_mode = DPMSModeOn;

        /* update mode_encoder */
#ifdef NO_CRTC_MODE
        if (pOutputPriv->is_dummy == FALSE)
#endif
        {
            drmModeFreeEncoder(pOutputPriv->mode_encoder);
            pOutputPriv->mode_encoder =
                drmModeGetEncoder(pExynosMode->fd,
                                  pOutputPriv->mode_output->encoders[0]);
        }
#if 1
        /* set display connector and display set mode */
        if (pOutputPriv->mode_output->connector_type == DRM_MODE_CONNECTOR_HDMIA
            || pOutputPriv->mode_output->connector_type ==
            DRM_MODE_CONNECTOR_HDMIB) {

            exynosDisplaySetDispConnMode(pScrn, DISPLAY_CONN_MODE_HDMI);
            /* TODO : find the display mode */
            exynosDisplaySetDispSetMode(pScrn, DISPLAY_SET_MODE_EXT);

            /* should be shown again when crtc on. */
//                exynosLayerShowAll (pScrn, LAYER_OUTPUT_EXT);
        }
        else if (pOutputPriv->mode_output->connector_type ==
                 DRM_MODE_CONNECTOR_VIRTUAL) {
            exynosDisplaySetDispConnMode(pScrn, DISPLAY_CONN_MODE_VIRTUAL);
            /* TODO : find the display mode */
            exynosDisplaySetDispSetMode(pScrn, DISPLAY_SET_MODE_EXT);

            /* should be shown again when crtc on. */
//              exynosLayerShowAll (pScrn, LAYER_OUTPUT_EXT);
        }
        else if (pOutputPriv->mode_output->connector_type ==
                 DRM_MODE_CONNECTOR_LVDS) {
            /* should be shown again when crtc on. */
//            exynosDisplaySetDispConnMode(pScrn, DISPLAY_CONN_MODE_LVDS);
            exynosLayerShowAll(pScrn, LAYER_OUTPUT_LCD);
        }

        else
            XDBG_NEVER_GET_HERE(MDISP);
#endif
    }

#ifdef NO_CRTC_MODE
    pExynos->isCrtcOn = exynosCrtcCheckInUseAll(pScrn);
#endif
    /* set the default external mode */
    exynosDisplayModeToKmode(pCrtc->scrn, &pExynosMode->ext_connector_mode,
                             pMode);

    /* accessibility */
    if (pCrtcPriv->bAccessibility || pCrtcPriv->screen_rotate_degree > 0) {
        if (ret) {
            if (old_bo_accessibility[0])
                exynosRenderBoUnref(old_bo_accessibility[0]);
            if (old_bo_accessibility[1])
                exynosRenderBoUnref(old_bo_accessibility[1]);
        }
    }
    exynosOutputDrmUpdate(pScrn);
    return ret;
 fail:
    XDBG_ERROR(MDISP, "Fail crtc apply(crtc_id:%d, rotate:%d, %dx%d+%d+%d\n",
               exynosCrtcID(pCrtcPriv), rotation, x, y, pCrtc->mode.HDisplay,
               pCrtc->mode.VDisplay);

    pCrtcPriv->front_bo = old_bo;

    /* accessibility */
    if (pCrtcPriv->bAccessibility || pCrtcPriv->screen_rotate_degree > 0) {
        if (bo_accessibility[0])
            exynosRenderBoUnref(bo_accessibility[0]);
        if (bo_accessibility[1])
            exynosRenderBoUnref(bo_accessibility[1]);

        pCrtcPriv->accessibility_front_bo = old_bo_accessibility[0];
        pCrtcPriv->accessibility_back_bo = old_bo_accessibility[1];
    }

    if (pExynos->fake_root)
        exynosDisplaySwapModeToKmode(pCrtc->scrn, &pCrtcPriv->kmode,
                                     &saved_mode);
    else
        exynosDisplayModeToKmode(pCrtc->scrn, &pCrtcPriv->kmode, &saved_mode);

    memcpy(&pCrtc->mode, &saved_mode, sizeof(DisplayModeRec));
    pCrtc->x = saved_x;
    pCrtc->y = saved_y;
    pCrtc->rotation = saved_rotation;
    exynosOutputDrmUpdate(pScrn);
    return ret;
}

static void
EXYNOSCrtcSetCursorColors(xf86CrtcPtr pCrtc, int bg, int fg)
{
    XDBG_TRACE(MCRS, "[%p]  \n", pCrtc);
}

static void
EXYNOSCrtcSetCursorPosition(xf86CrtcPtr pCrtc, int x, int y)
{
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL) {
        return;
    }
    EXYNOSPtr pExynos = EXYNOSPTR(pCrtc->scrn);

    pCrtcPriv->cursor_pos_x = x;
    pCrtcPriv->cursor_pos_y = y;

    XDBG_DEBUG(MCRS, "[%p] (%d,%d) \n", pCrtc, x, y);

    if (!pExynos->enableCursor)
        return;

    if (!pCrtcPriv->cursor_show)
        return;

    if (pCrtcPriv->move_layer) {
        _cursorEnsureCanvas(pCrtc, pCrtcPriv->ovl_vbuf_cursor,
                            EXYNOS_CURSOR_W, EXYNOS_CURSOR_H);
        _cursorEnsureCursorImage(pCrtc);

        int offset = _overlayGetXMoveOffset(pCrtc, pCrtcPriv->cursor_win_x);

        if (pCrtcPriv->cursor_old_offset != offset) {
            _cursorDrawCursor(pCrtc);
            pCrtcPriv->cursor_old_offset = offset;
        }

        _cursorMove(pCrtc, pCrtcPriv->cursor_win_x - offset,
                    pCrtcPriv->cursor_win_y);
    }
    else {
        /* Draw cursor in block handler */
        pCrtcPriv->need_cursor_update = TRUE;
        _cursorRestoreImage(pCrtc);
        _cursorRegisterBlockHandler(pCrtc);
    }
}

static void
EXYNOSCrtcShowCursor(xf86CrtcPtr pCrtc)
{
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL) {
        return;
    }

    XDBG_TRACE(MCRS, "[%p] cursor_show(%d)\n", pCrtc, pCrtcPriv->cursor_show);

    if (pCrtcPriv->cursor_show)
        return;

    pCrtcPriv->cursor_show = TRUE;

    _cursorShow(pCrtc);
}

static void
EXYNOSCrtcHideCursor(xf86CrtcPtr pCrtc)
{
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL) {
        return;
    }

    XDBG_TRACE(MCRS, "[%p] cursor_show(%d)\n", pCrtc, pCrtcPriv->cursor_show);

    if (!pCrtcPriv->cursor_show)
        return;

    pCrtcPriv->cursor_show = FALSE;

    _cursorHide(pCrtc);
}

static void
EXYNOSCrtcLoadCursorArgb(xf86CrtcPtr pCrtc, CARD32 *image)
{
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL || image == NULL) {
        return;
    }
    XDBG_TRACE(MCRS, "[%p] image(%p) \n", pCrtc, image);

    if (pCrtcPriv->backup_image)
        pixman_image_unref(pCrtcPriv->backup_image);

    pCrtcPriv->backup_image =
        pixman_image_create_bits(PIXMAN_a8r8g8b8, EXYNOS_CURSOR_W,
                                 EXYNOS_CURSOR_H, NULL, 0);

    XDBG_RETURN_IF_FAIL(pCrtcPriv->backup_image != NULL);

    memcpy(pixman_image_get_data(pCrtcPriv->backup_image), image,
           EXYNOS_CURSOR_W * EXYNOS_CURSOR_H * 4);

    if (pCrtcPriv->cursor_image) {
        pixman_image_unref(pCrtcPriv->cursor_image);
        pCrtcPriv->cursor_image = NULL;
    }

    pCrtcPriv->need_cursor_update = TRUE;
}

static void *
EXYNOSCrtcShadowAllocate(xf86CrtcPtr pCrtc, int width, int height)
{
#if 0
    ScrnInfoPtr scrn = pCrtc->scrn;
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;
    EXYNOSModePtr pExynosMode = pCrtcPriv->pExynosMode;
    unsigned long rotate_pitch;
    uint32_t tiling;
    int ret;

    pCrtcPriv->rotate_bo = intel_allocate_framebuffer(scrn,
                                                      width, height,
                                                      pExynosMode->cpp,
                                                      &rotate_pitch, &tiling);

    if (!pCrtcPriv->rotate_bo) {
        xf86DrvMsg(pCrtc->scrn->scrnIndex, X_ERROR,
                   "Couldn't allocate shadow memory for rotated CRTC\n");
        return NULL;
    }

    ret = drmModeAddFB(pExynosMode->fd, width, height, pCrtc->scrn->depth,
                       pCrtc->scrn->bitsPerPixel, rotate_pitch,
                       pCrtcPriv->rotate_bo->handle, &pCrtcPriv->rotate_fb_id);
    if (ret < 0) {
        ErrorF("failed to add rotate fb\n");
        drm_intel_bo_unreference(pCrtcPriv->rotate_bo);
        return NULL;
    }

    pCrtcPriv->rotate_pitch = rotate_pitch;
    return pCrtcPriv->rotate_bo;
#else
    return NULL;
#endif
}

static PixmapPtr
EXYNOSCrtcShadowCreate(xf86CrtcPtr pCrtc, void *data, int width, int height)
{
#if 0
    ScrnInfoPtr pScrn = pCrtc->scrn;
    EXYNOSPtr pSEC = EXYNOSPtr(pScrn);
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;
    PixmapPtr rotate_pixmap;

    if (!data) {
        data = EXYNOSCrtcShadowAllocate(pCrtc, width, height);
        if (!data) {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "Couldn't allocate shadow pixmap for rotated CRTC\n");
            return NULL;
        }
    }
    if (pCrtcPriv->rotate_bo == NULL) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Couldn't allocate shadow pixmap for rotated CRTC\n");
        return NULL;
    }

    rotate_pixmap = GetScratchPixmapHeader(pScrn->pScreen,
                                           width, height,
                                           pScrn->depth,
                                           pScrn->bitsPerPixel,
                                           pCrtcPriv->rotate_pitch, NULL);

    if (rotate_pixmap == NULL) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Couldn't allocate shadow pixmap for rotated CRTC\n");
        return NULL;
    }

//    intel_set_pixmap_bo(rotate_pixmap, pCrtcPriv->rotate_bo);

    pSEC->shadow_present = TRUE;

    return rotate_pixmap;
#else
    return NULL;
#endif
}

static void
EXYNOSCrtcShadowDestroy(xf86CrtcPtr pCrtc, PixmapPtr rotate_pixmap, void *data)
{
#if 0
    ScrnInfoPtr pScrn = pCrtc->scrn;
    EXYNOSPtr pSEC = EXYNOSPtr(pScrn);
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;
    EXYNOSModePtr pExynosMode = pCrtcPriv->mode;

    if (rotate_pixmap) {
        intel_set_pixmap_bo(rotate_pixmap, NULL);
        FreeScratchPixmapHeader(rotate_pixmap);
    }

    if (data) {
        /* Be sure to sync acceleration before the memory gets
         * unbound. */
        drmModeRmFB(pExynosMode->fd, pCrtcPriv->rotate_fb_id);
        pCrtcPriv->rotate_fb_id = 0;

        tbm_bo_unreference(pCrtcPriv->rotate_bo);
        pCrtcPriv->rotate_bo = NULL;
    }

    pSEC->shadow_present = pSEC->use_shadow;
#else
    return;
#endif
}

static void
EXYNOSCrtcGammaSet(xf86CrtcPtr pCrtc,
                   CARD16 *red, CARD16 *green, CARD16 *blue, int size)
{
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL) {
        return;
    }
    EXYNOSModePtr pExynosMode = pCrtcPriv->pExynosMode;

    drmModeCrtcSetGamma(pExynosMode->fd, exynosCrtcID(pCrtcPriv),
                        size, red, green, blue);
}

static void
EXYNOSCrtcDestroy(xf86CrtcPtr pCrtc)
{
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL) {
        return;
    }
    DRI2FrameEventPtr event_ref = NULL, event_next = NULL;

    xorg_list_for_each_entry_safe(event_ref, event_next,
                                  &pCrtcPriv->pending_flips,
                                  crtc_pending_link) {
        free(event_ref);
    }

    _flipPixmapDeinit(pCrtc);

    _cursorDestroy(pCrtc);
    _cursorUnregisterBlockHandler(pCrtc);

#if 1
    if (pCrtcPriv->pFpsDebug) {
        xDbgLogFpsDebugDestroy(pCrtcPriv->pFpsDebug);
        pCrtcPriv->pFpsDebug = NULL;
    }
#endif

    if (pCrtcPriv->accessibility_front_bo) {
        exynosRenderBoUnref(pCrtcPriv->accessibility_front_bo);
        pCrtcPriv->accessibility_front_bo = NULL;
    }

    if (pCrtcPriv->accessibility_back_bo) {
        exynosRenderBoUnref(pCrtcPriv->accessibility_back_bo);
        pCrtcPriv->accessibility_back_bo = NULL;
    }

    if (pCrtcPriv->backup_image) {
        pixman_image_unref(pCrtcPriv->backup_image);
        pCrtcPriv->backup_image = NULL;
    }

    if (pCrtcPriv->ovl_vbuf_cursor) {
        exynosUtilVideoBufferUnref(pCrtcPriv->ovl_vbuf_cursor);
        pCrtcPriv->ovl_vbuf_cursor = NULL;
    }

    if (pCrtcPriv->ovl_vbuf_pixmap) {
        exynosUtilVideoBufferUnref(pCrtcPriv->ovl_vbuf_pixmap);
        pCrtcPriv->ovl_vbuf_pixmap = NULL;
    }

    if (pCrtcPriv->ovl_layer) {
        exynosLayerUnref(pCrtcPriv->ovl_layer);
        pCrtcPriv->ovl_layer = NULL;
    }

    if (pCrtcPriv->mode_crtc)
        drmModeFreeCrtc(pCrtcPriv->mode_crtc);

    if (pCrtcPriv->front_bo) {
        pCrtcPriv->front_bo = NULL;
    }

    if (pCrtcPriv->back_bo) {
        exynosRenderBoUnref(pCrtcPriv->back_bo);
        pCrtcPriv->back_bo = NULL;
    }

    if (pCrtcPriv->flip_backpixs.pix_free != NULL) {
        free(pCrtcPriv->flip_backpixs.pix_free);
        pCrtcPriv->flip_backpixs.pix_free = NULL;
    }

    if (pCrtcPriv->flip_backpixs.flip_pixmaps != NULL) {
        free(pCrtcPriv->flip_backpixs.flip_pixmaps);
        pCrtcPriv->flip_backpixs.flip_pixmaps = NULL;
    }

    if (pCrtcPriv->flip_backpixs.flip_draws != NULL) {
        free(pCrtcPriv->flip_backpixs.flip_draws);
        pCrtcPriv->flip_backpixs.flip_draws = NULL;
    }

    xorg_list_del(&pCrtcPriv->link);
    free(pCrtcPriv);

    pCrtc->driver_private = NULL;
}

static const xf86CrtcFuncsRec exynos_crtc_funcs = {
    .dpms = EXYNOSCrtcDpms,
    .set_mode_major = EXYNOSCrtcSetModeMajor,
    .set_cursor_colors = EXYNOSCrtcSetCursorColors,
    .set_cursor_position = EXYNOSCrtcSetCursorPosition,
    .show_cursor = EXYNOSCrtcShowCursor,
    .hide_cursor = EXYNOSCrtcHideCursor,
    .load_cursor_argb = EXYNOSCrtcLoadCursorArgb,
    .shadow_create = EXYNOSCrtcShadowCreate,
    .shadow_allocate = EXYNOSCrtcShadowAllocate,
    .shadow_destroy = EXYNOSCrtcShadowDestroy,
    .gamma_set = EXYNOSCrtcGammaSet,
    .destroy = EXYNOSCrtcDestroy,
};

void
exynosCrtcInit(ScrnInfoPtr pScrn, EXYNOSModePtr pExynosMode, int num)
{
    xf86CrtcPtr pCrtc;
    EXYNOSCrtcPrivPtr pCrtcPriv;
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);

//    exynosLogSetLevel("CRTC", 0);

    pCrtcPriv = calloc(sizeof(EXYNOSCrtcPrivRec), 1);
    if (pCrtcPriv == NULL)
        return;

    pCrtc = xf86CrtcCreate(pScrn, &exynos_crtc_funcs);
    if (pCrtc == NULL) {
        free(pCrtcPriv);
        return;
    }

    pCrtcPriv->idx = num;
    pCrtcPriv->mode_crtc = drmModeGetCrtc(pExynosMode->fd,
                                          pExynosMode->mode_res->crtcs[num]);
    pCrtcPriv->move_layer = TRUE;
    pCrtcPriv->user_rotate = RR_Rotate_0;

    pCrtcPriv->pExynosMode = pExynosMode;
    pCrtc->driver_private = pCrtcPriv;

    pCrtcPriv->pipe = num;
    pCrtcPriv->onoff = TRUE;

    xorg_list_init(&pCrtcPriv->pending_flips);

    pCrtcPriv->pCrtc = pCrtc;

#ifdef USE_XDBG
    pCrtcPriv->pFpsDebug = xDbgLogFpsDebugCreate();
    if (pCrtcPriv->pFpsDebug == NULL) {
        free(pCrtcPriv);
        return;
    }
#endif

    if (pExynos->enableCursor)
        _cursorInit(pCrtc);

    _flipPixmapInit(pCrtc);

    xorg_list_add(&(pCrtcPriv->link), &(pExynosMode->crtcs));
#ifdef NO_CRTC_MODE
    pExynosMode->num_real_crtc++;
#endif
}

/* check the crtc is on */
Bool
exynosCrtcOn(xf86CrtcPtr pCrtc)
{
    ScrnInfoPtr pScrn = pCrtc->scrn;
    xf86CrtcConfigPtr pCrtcConfig = XF86_CRTC_CONFIG_PTR(pScrn);
    int i;

    if (!pCrtc->enabled)
        return FALSE;

    /* Kernel manage CRTC status based out output config */
    for (i = 0; i < pCrtcConfig->num_output; i++) {
        xf86OutputPtr pOutput = pCrtcConfig->output[i];

        if (pOutput->crtc == pCrtc &&
            exynosOutputDpmsStatus(pOutput) == DPMSModeOn)
            return TRUE;
    }

    return TRUE;
}

Bool
exynosCrtcApply(xf86CrtcPtr pCrtc)
{
    ScrnInfoPtr pScrn = pCrtc->scrn;
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL)
        return FALSE;
    EXYNOSModePtr pExynosMode = pCrtcPriv->pExynosMode;
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pCrtc->scrn);
    uint32_t *output_ids;
    int output_count = 0;
    int fb_id, x, y;
    int i;
    Bool ret = FALSE;
    EXYNOSFbBoDataPtr bo_data;
    tbm_bo bo;

    output_ids = calloc(sizeof(uint32_t), xf86_config->num_output);
    if (!output_ids)
        return FALSE;

    for (i = 0; i < xf86_config->num_output; i++) {
        xf86OutputPtr pOutput = xf86_config->output[i];
        EXYNOSOutputPrivPtr pOutputPriv;

        if (pOutput->crtc != pCrtc)
            continue;

        pOutputPriv = pOutput->driver_private;
        if (pOutputPriv == NULL) {
            continue;
        }

        /* modify the physical size of monitor */
#if 0
        if (!strcmp(pOutput->name, "LVDS1")) {
            exynosDisplaySetDispConnMode(pScrn, DISPLAY_CONN_MODE_LVDS);
        }
#endif
        {
            pOutput->mm_width = pOutputPriv->mode_output->mmWidth;
            pOutput->mm_height = pOutputPriv->mode_output->mmHeight;
            if (pOutput->conf_monitor) {
                pOutput->conf_monitor->mon_width =
                    pOutputPriv->mode_output->mmWidth;
                pOutput->conf_monitor->mon_height =
                    pOutputPriv->mode_output->mmHeight;
            }
        }

        output_ids[output_count] = pOutputPriv->mode_output->connector_id;
        output_count++;
    }
#if 0
    if (!xf86CrtcRotate(pCrtc))
        goto done;
#endif
    pCrtc->funcs->gamma_set(pCrtc, pCrtc->gamma_red, pCrtc->gamma_green,
                            pCrtc->gamma_blue, pCrtc->gamma_size);

    /* accessilitity */
    if (pCrtcPriv->bAccessibility || pCrtcPriv->screen_rotate_degree > 0) {
        tbm_bo temp;

        bo = pCrtcPriv->accessibility_front_bo;
        temp = pCrtcPriv->accessibility_front_bo;
        pCrtcPriv->accessibility_front_bo = pCrtcPriv->accessibility_back_bo;
        pCrtcPriv->accessibility_back_bo = temp;
    }
    else {
        bo = pCrtcPriv->front_bo;
    }

    tbm_bo_get_user_data(bo, TBM_BO_DATA_FB, (void * *) &bo_data);
    x = pCrtc->x - bo_data->pos.x1;
    y = pCrtc->y - bo_data->pos.y1;
    fb_id = bo_data->fb_id;

    if (pCrtcPriv->rotate_fb_id) {
        fb_id = pCrtcPriv->rotate_fb_id;
        x = 0;
        y = 0;
    }

    XDBG_INFO(MDISP,
              "fb_id,%d name,%s width,%d height,%d, vrefresh,%d, accessibility,%d\n",
              fb_id, pCrtcPriv->kmode.name, pCrtcPriv->kmode.hdisplay,
              pCrtcPriv->kmode.vdisplay, pCrtcPriv->kmode.vrefresh,
              pCrtcPriv->bAccessibility);

    /* turn off the crtc if the same crtc is set already by another display mode
     * before the set crtcs
     */
//    exynosDisplaySetDispSetMode(pScrn, DISPLAY_SET_MODE_OFF);

//    if (!pCrtcPriv->onoff)
//        exynosCrtcTurn (pCrtc, TRUE, FALSE, FALSE);

    /* for cache control */
    tbm_bo_map(bo, TBM_DEVICE_2D, TBM_OPTION_READ);
    tbm_bo_unmap(bo);
    ret = drmModeSetCrtc(pExynosMode->fd, exynosCrtcID(pCrtcPriv),
                         fb_id, x, y, output_ids, output_count,
                         &pCrtcPriv->kmode);
    if (ret) {
        XDBG_INFO(MDISP, "failed to set mode: %s\n", strerror(-ret));
        ret = FALSE;
    }
    else {
        ret = TRUE;

        /* Force DPMS to On for all outputs, which the kernel will have done
         * with the mode set. Also, restore the backlight level
         */
    }

#if 1
    if (pScrn->pScreen)
        xf86_reload_cursors(pScrn->pScreen);
#endif
#if 0
 done:
#endif
    free(output_ids);
    return ret;
}

Bool
exynosCrtcOverlayNeedOff(xf86CrtcPtr pCrtc, Bool need_off)
{
    EXYNOSCrtcPrivPtr pCrtcPriv;

    XDBG_RETURN_VAL_IF_FAIL(pCrtc != NULL, FALSE);

    pCrtcPriv = pCrtc->driver_private;
    XDBG_RETURN_VAL_IF_FAIL(pCrtcPriv != NULL, FALSE);

    pCrtcPriv->need_off = need_off;

    XDBG_TRACE(MCRS, "[%p] need_off(%d) \n", pCrtc, need_off);

    _cursorChangeStatus(pCrtc);

    return TRUE;
}

Bool
exynosCrtcOverlayRef(xf86CrtcPtr pCrtc, Bool refer)
{
    EXYNOSCrtcPrivPtr pCrtcPriv;

    XDBG_RETURN_VAL_IF_FAIL(pCrtc != NULL, FALSE);

    pCrtcPriv = pCrtc->driver_private;
    XDBG_RETURN_VAL_IF_FAIL(pCrtcPriv != NULL, FALSE);

    pCrtcPriv->ref_overlay = refer;

    XDBG_TRACE(MCRS, "[%p] refer(%d) \n", pCrtc, refer);

    _cursorChangeStatus(pCrtc);

    return TRUE;
}

Bool
exynosCrtcCursorEnable(ScrnInfoPtr pScrn, Bool enable)
{
    EXYNOSModePtr pExynosMode = (EXYNOSModePtr) EXYNOSPTR(pScrn)->pExynosMode;
    EXYNOSCrtcPrivPtr pCur = NULL, pNext = NULL;

    xorg_list_for_each_entry_safe(pCur, pNext, &pExynosMode->crtcs, link) {
        xf86CrtcPtr pCrtc = pCur->pCrtc;
        int connector_type = exynosCrtcGetConnectType(pCrtc);

        if (connector_type != DRM_MODE_CONNECTOR_Unknown)
            _cursorEnable(pCrtc, enable);
    }

    return TRUE;
}

Bool
exynosCrtcCursorRotate(xf86CrtcPtr pCrtc, int rotate)
{
    return _cursorRotate(pCrtc, rotate);
}

xf86CrtcPtr
exynosCrtcGetAtGeometry(ScrnInfoPtr pScrn, int x, int y, int width, int height)
{
    BoxRec box;

    XDBG_RETURN_VAL_IF_FAIL(pScrn != NULL, NULL);

    box.x1 = x;
    box.y1 = y;
    box.x2 = box.x1 + width;
    box.y2 = box.y1 + height;

    return exynosModeCoveringCrtc(pScrn, &box, NULL, NULL);
}

int
exynosCrtcGetConnectType(xf86CrtcPtr pCrtc)
{
    xf86CrtcConfigPtr pCrtcConfig;
    int i;

    XDBG_RETURN_VAL_IF_FAIL(pCrtc != NULL, DRM_MODE_CONNECTOR_Unknown);

    pCrtcConfig = XF86_CRTC_CONFIG_PTR(pCrtc->scrn);
    XDBG_RETURN_VAL_IF_FAIL(pCrtcConfig != NULL, DRM_MODE_CONNECTOR_Unknown);

    for (i = 0; i < pCrtcConfig->num_output; i++) {
        xf86OutputPtr pOutput = pCrtcConfig->output[i];
        EXYNOSOutputPrivPtr pOutputPriv = pOutput->driver_private;

        if (pOutput->crtc == pCrtc) {
            if (pOutputPriv != NULL)
                return pOutputPriv->mode_output->connector_type;
            else
                return DRM_MODE_CONNECTOR_Unknown;
        }
    }

    return DRM_MODE_CONNECTOR_Unknown;
}

Bool
exynosCrtcIsFlipping(xf86CrtcPtr pCrtc)
{
    EXYNOSCrtcPrivPtr pCrtcPriv;

    XDBG_RETURN_VAL_IF_FAIL(pCrtc != NULL, FALSE);
    pCrtcPriv = pCrtc->driver_private;
    if (pCrtcPriv == NULL)
        return FALSE;

    /* if isFlipping is true, return true */
    if (pCrtcPriv->is_flipping)
        return TRUE;

    /* if there is pending_flips in the list, return true */
    if (!xorg_list_is_empty(&pCrtcPriv->pending_flips))
        return TRUE;

    return FALSE;
}

DRI2FrameEventPtr
exynosCrtcGetPendingFlip(xf86CrtcPtr pCrtc, DRI2FrameEventPtr pEvent)
{
    EXYNOSCrtcPrivPtr pCrtcPriv;
    DRI2FrameEventPtr item = NULL, tmp = NULL;

    XDBG_RETURN_VAL_IF_FAIL(pCrtc != NULL, NULL);
    pCrtcPriv = pCrtc->driver_private;
    if (pCrtcPriv == NULL)
        return NULL;

    if (xorg_list_is_empty(&pCrtcPriv->pending_flips))
        return NULL;

    xorg_list_for_each_entry_safe(item, tmp, &pCrtcPriv->pending_flips,
                                  crtc_pending_link) {
        if (item == pEvent)
            return item;
    }

    return NULL;
}

DRI2FrameEventPtr
exynosCrtcGetFirstPendingFlip(xf86CrtcPtr pCrtc)
{
    DRI2FrameEventPtr pEvent = NULL;
    EXYNOSCrtcPrivPtr pCrtcPriv;
    DRI2FrameEventPtr item = NULL, tmp = NULL;

    XDBG_RETURN_VAL_IF_FAIL(pCrtc != NULL, NULL);
    pCrtcPriv = pCrtc->driver_private;
    if (pCrtcPriv == NULL)
        return NULL;

    if (xorg_list_is_empty(&pCrtcPriv->pending_flips))
        return NULL;

    xorg_list_for_each_entry_safe(item, tmp, &pCrtcPriv->pending_flips,
                                  crtc_pending_link) {
        /* get the last item in the circular list ( last item is at last_item.next==head) */
        if (item->crtc_pending_link.next == &pCrtcPriv->pending_flips) {
            pEvent = item;
            break;
        }
    }

    return pEvent;
}

void
exynosCrtcAddPendingFlip(xf86CrtcPtr pCrtc, DRI2FrameEventPtr pEvent)
{
    EXYNOSCrtcPrivPtr pCrtcPriv;

    XDBG_RETURN_IF_FAIL(pCrtc != NULL);

    pCrtcPriv = pCrtc->driver_private;
    if (pCrtcPriv == NULL)
        return;

    xorg_list_add(&(pEvent->crtc_pending_link), &(pCrtcPriv->pending_flips));
}

void
exynosCrtcRemovePendingFlip(xf86CrtcPtr pCrtc, DRI2FrameEventPtr pEvent)
{
    EXYNOSCrtcPrivPtr pCrtcPriv;
    DRI2FrameEventPtr item = NULL, tmp = NULL;

    XDBG_RETURN_IF_FAIL(pCrtc != NULL);

    pCrtcPriv = pCrtc->driver_private;
    if (pCrtcPriv == NULL)
        return;

    if (xorg_list_is_empty(&pCrtcPriv->pending_flips))
        return;

    xorg_list_for_each_entry_safe(item, tmp, &pCrtcPriv->pending_flips,
                                  crtc_pending_link) {
        if (item == pEvent) {
            xorg_list_del(&item->crtc_pending_link);
        }
    }
}

static Bool
_exynosCrtcExecAccessibilityScaleNegative(xf86CrtcPtr pCrtc, tbm_bo src_bo,
                                          tbm_bo dst_bo)
{
    XDBG_RETURN_VAL_IF_FAIL(pCrtc != NULL, FALSE);

    EXYNOSPtr pExynos = EXYNOSPTR(pCrtc->scrn);

    if (pExynos->isLcdOff) {
        XDBG_INFO(MDISP, "Accessibility execute : LCD IS OFF\n");
        return TRUE;
    }

    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL)
        return FALSE;

    XDBG_RETURN_VAL_IF_FAIL(src_bo != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(dst_bo != NULL, FALSE);

    EXYNOSFbBoDataPtr src_bo_data;
    EXYNOSFbBoDataPtr dst_bo_data;
    G2dColorKeyMode mode;
    G2dImage *srcImg = NULL, *dstImg = NULL;
    unsigned int src_bo_w, src_bo_h, src_bo_stride;
    unsigned int dst_bo_w, dst_bo_h, dst_bo_stride;
    int src_x, src_y;
    unsigned int src_w, src_h;
    int negative = 0;
    tbm_bo_handle src_bo_handle;
    tbm_bo_handle dst_bo_handle;

    tbm_bo_get_user_data(src_bo, TBM_BO_DATA_FB, (void * *) &src_bo_data);
    XDBG_RETURN_VAL_IF_FAIL(src_bo_data != NULL, FALSE);

    tbm_bo_get_user_data(dst_bo, TBM_BO_DATA_FB, (void * *) &dst_bo_data);
    XDBG_RETURN_VAL_IF_FAIL(dst_bo_data != NULL, FALSE);

    src_bo_w = src_bo_data->pos.x2 - src_bo_data->pos.x1;
    src_bo_h = src_bo_data->pos.y2 - src_bo_data->pos.y1;
    src_bo_stride = src_bo_w * 4;

    dst_bo_w = dst_bo_data->pos.x2 - dst_bo_data->pos.x1;
    dst_bo_h = dst_bo_data->pos.y2 - dst_bo_data->pos.y1;
    dst_bo_stride = dst_bo_w * 4;

    mode = G2D_COLOR_FMT_ARGB8888 | G2D_ORDER_AXRGB;
    src_bo_handle = tbm_bo_map(src_bo, TBM_DEVICE_2D, TBM_OPTION_READ);
    dst_bo_handle = tbm_bo_map(dst_bo, TBM_DEVICE_2D, TBM_OPTION_WRITE);

    srcImg =
        g2d_image_create_bo(mode, src_bo_w, src_bo_h, src_bo_handle.u32,
                            src_bo_stride);
    dstImg =
        g2d_image_create_bo(mode, dst_bo_w, dst_bo_h, dst_bo_handle.u32,
                            dst_bo_stride);
    if (!srcImg || !dstImg) {
        XDBG_ERROR(MDISP, "Accessibility : Fail to create g2d_image\n");
        tbm_bo_unmap(src_bo);
        tbm_bo_unmap(dst_bo);

        if (srcImg)
            g2d_image_free(srcImg);

        if (dstImg)
            g2d_image_free(dstImg);

        return FALSE;
    }

    if (pCrtcPriv->accessibility_status == ACCESSIBILITY_MODE_NEGATIVE) {
        negative = 1;
    }

    if (pCrtcPriv->bScale) {
        src_x = pCrtcPriv->sx;
        src_y = pCrtcPriv->sy;
        src_w = pCrtcPriv->sw;
        src_h = pCrtcPriv->sh;
    }
    else {
        src_x = 0;
        src_y = 0;
        src_w = src_bo_w;
        src_h = src_bo_h;
    }

    util_g2d_copy_with_scale(srcImg, dstImg,
                             src_x, src_y, src_w, src_h,
                             0, 0, dst_bo_w, dst_bo_h, negative);
    g2d_exec();

    tbm_bo_unmap(src_bo);
    tbm_bo_unmap(dst_bo);

    g2d_image_free(srcImg);
    g2d_image_free(dstImg);

    return TRUE;
}

static Bool
_exynosCrtcExecRotate(xf86CrtcPtr pCrtc, tbm_bo src_bo, tbm_bo dst_bo)
{
    EXYNOSPtr pExynos;
    EXYNOSCrtcPrivPtr pCrtcPriv;
    struct drm_exynos_ipp_queue_buf buf;

    XDBG_RETURN_VAL_IF_FAIL(pCrtc != NULL, FALSE);

    pCrtcPriv = pCrtc->driver_private;
    if (pCrtcPriv == NULL)
        return FALSE;
    XDBG_RETURN_VAL_IF_FAIL(pCrtcPriv->screen_rotate_prop_id > 0, FALSE);

    pExynos = EXYNOSPTR(pCrtc->scrn);
    if (pExynos->isLcdOff) {
        XDBG_INFO(MDISP, "screen rotate execute : LCD IS OFF\n");
        return TRUE;
    }

    CLEAR(buf);
    buf.ops_id = EXYNOS_DRM_OPS_SRC;
    buf.buf_type = IPP_BUF_ENQUEUE;
    buf.prop_id = pCrtcPriv->screen_rotate_prop_id;
    buf.handle[0] = (__u32) tbm_bo_get_handle(src_bo, TBM_DEVICE_DEFAULT).u32;

    if (!exynosDrmIppQueueBuf(pCrtc->scrn, &buf))
        return FALSE;

    CLEAR(buf);
    buf.ops_id = EXYNOS_DRM_OPS_DST;
    buf.buf_type = IPP_BUF_ENQUEUE;
    buf.prop_id = pCrtcPriv->screen_rotate_prop_id;
    buf.handle[0] = (__u32) tbm_bo_get_handle(dst_bo, TBM_DEVICE_DEFAULT).u32;

    if (!exynosDrmIppQueueBuf(pCrtc->scrn, &buf))
        return FALSE;

    if (pCrtcPriv->screen_rotate_ipp_status == IPP_CTRL_STOP) {
        struct drm_exynos_ipp_cmd_ctrl ctrl = { 0, };
        ctrl.prop_id = pCrtcPriv->screen_rotate_prop_id;
        ctrl.ctrl = IPP_CTRL_PLAY;
        exynosDrmIppCmdCtrl(pCrtc->scrn, &ctrl);
        pCrtcPriv->screen_rotate_ipp_status = IPP_CTRL_PLAY;

        XDBG_INFO(MDISP, "screen rotate ipp(id:%d) play\n",
                  pCrtcPriv->screen_rotate_prop_id);
    }
    else if (pCrtcPriv->screen_rotate_ipp_status == IPP_CTRL_PAUSE) {
        struct drm_exynos_ipp_cmd_ctrl ctrl = { 0, };
        ctrl.prop_id = pCrtcPriv->screen_rotate_prop_id;
        ctrl.ctrl = IPP_CTRL_RESUME;
        exynosDrmIppCmdCtrl(pCrtc->scrn, &ctrl);
        pCrtcPriv->screen_rotate_ipp_status = IPP_CTRL_RESUME;

        XDBG_INFO(MDISP, "screen rotate ipp(id:%d) resume\n",
                  pCrtcPriv->screen_rotate_prop_id);
    }

    return TRUE;
}

Bool
exynosCrtcExecAccessibility(xf86CrtcPtr pCrtc, tbm_bo src_bo, tbm_bo dst_bo)
{
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL)
        return FALSE;
    Bool ret = FALSE;
    CARD32 elapsed = 0;
    EXYNOSPtr pExynos = EXYNOSPTR(pCrtc->scrn);

    tbm_bo_map(src_bo, TBM_DEVICE_2D, TBM_OPTION_READ);
    tbm_bo_map(dst_bo, TBM_DEVICE_2D, TBM_OPTION_READ);

    if (pExynos->xvperf_mode & XBERC_XVPERF_MODE_ACCESS)
        elapsed = GetTimeInMillis();

    if (pCrtcPriv->screen_rotate_degree > 0)
        ret = _exynosCrtcExecRotate(pCrtc, src_bo, dst_bo);
    else if (pCrtcPriv->bAccessibility)
        ret = _exynosCrtcExecAccessibilityScaleNegative(pCrtc, src_bo, dst_bo);
    else
        XDBG_NEVER_GET_HERE(MDISP);

    if (pExynos->xvperf_mode & XBERC_XVPERF_MODE_ACCESS)
        ErrorF("Access exec: %3" PRIXID " ms \n", GetTimeInMillis() - elapsed);

    tbm_bo_unmap(src_bo);
    tbm_bo_unmap(dst_bo);

    return ret;
}

Bool
exynosCrtcEnableAccessibility(xf86CrtcPtr pCrtc)
{
    XDBG_RETURN_VAL_IF_FAIL(pCrtc != NULL, FALSE);

    ScrnInfoPtr pScrn = pCrtc->scrn;
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL)
        return FALSE;

    /* accessibility and screen rotate can't be enable at the same time */
    XDBG_RETURN_VAL_IF_FAIL(pCrtcPriv->screen_rotate_degree == 0, FALSE);

    int bAccessibility = (pCrtcPriv->accessibility_status | pCrtcPriv->bScale);
    int width = pCrtc->mode.HDisplay;
    int height = pCrtc->mode.VDisplay;

    EXYNOSLayer *pLayer = NULL;
    xf86CrtcConfigPtr pCrtcConfig = XF86_CRTC_CONFIG_PTR(pCrtc->scrn);
    xf86OutputPtr pOutput = NULL;
    int i;

    for (i = 0; i < pCrtcConfig->num_output; i++) {
        xf86OutputPtr pTemp = pCrtcConfig->output[i];

        if (pTemp->crtc == pCrtc) {
            pOutput = pTemp;
            break;
        }
    }
    XDBG_RETURN_VAL_IF_FAIL(pOutput != NULL, FALSE);

    EXYNOSOutputPrivPtr pOutputPriv = pOutput->driver_private;
    EXYNOSLayerOutput output = LAYER_OUTPUT_LCD;

    if (pOutputPriv->mode_output->connector_type == DRM_MODE_CONNECTOR_LVDS ||
        pOutputPriv->mode_output->connector_type == DRM_MODE_CONNECTOR_Unknown)
    {
        output = LAYER_OUTPUT_LCD;
    }
    else if (pOutputPriv->mode_output->connector_type ==
             DRM_MODE_CONNECTOR_HDMIA ||
             pOutputPriv->mode_output->connector_type ==
             DRM_MODE_CONNECTOR_HDMIB ||
             pOutputPriv->mode_output->connector_type ==
             DRM_MODE_CONNECTOR_VIRTUAL) {
        output = LAYER_OUTPUT_EXT;
    }
    else
        XDBG_NEVER_GET_HERE(MACCE);

    if (bAccessibility) {
        if (!pCrtcPriv->accessibility_front_bo) {
            pCrtcPriv->accessibility_front_bo =
                exynosRenderBoCreate(pScrn, width, height);
            XDBG_RETURN_VAL_IF_FAIL(pCrtcPriv->accessibility_front_bo != NULL,
                                    FALSE);
        }

        pCrtcPriv->bAccessibility = TRUE;

        /* do accessibility */
        if (!exynosCrtcExecAccessibility
            (pCrtc, pCrtcPriv->front_bo, pCrtcPriv->accessibility_front_bo)) {
            XDBG_ERROR(MDISP,
                       "Accessibility : Fail to execute accessibility\n");
            exynosRenderBoUnref(pCrtcPriv->accessibility_front_bo);
            pCrtcPriv->accessibility_front_bo = NULL;
            pCrtcPriv->bAccessibility = FALSE;
            return FALSE;
        }

        XDBG_INFO(MDISP,
                  "accessibility_status(%d), scale(%d):[sx,sy,sw,sh]=[%d,%d,%d,%d]\n",
                  pCrtcPriv->accessibility_status, pCrtcPriv->bScale,
                  pCrtcPriv->sx, pCrtcPriv->sy, pCrtcPriv->sw, pCrtcPriv->sh);

        /* layer update */
        pLayer = exynosLayerFind(output, LAYER_UPPER);
        if (pLayer && exynosLayerIsVisible(pLayer))
            exynosLayerUpdate(pLayer);

        /* set crtc when accessibility buffer destroy, or drmvlank is error */
        if (!exynosCrtcApply(pCrtc)) {

            XDBG_ERROR(MDISP, "Accessibility : Fail to set crtc\n");
            exynosRenderBoUnref(pCrtcPriv->accessibility_front_bo);
            pCrtcPriv->accessibility_front_bo = NULL;
            pCrtcPriv->bAccessibility = FALSE;
            return FALSE;
        }

    }
    else {
        pCrtcPriv->bAccessibility = FALSE;

        XDBG_INFO(MDISP,
                  "accessibility_status(%d), scale(%d):[sx,sy,sw,sh]=[%d,%d,%d,%d]\n",
                  pCrtcPriv->accessibility_status, pCrtcPriv->bScale,
                  pCrtcPriv->sx, pCrtcPriv->sy, pCrtcPriv->sw, pCrtcPriv->sh);

        if (!exynosCrtcApply(pCrtc)) {
            XDBG_ERROR(MDISP, "Accessibility : Fail to set crtc\n");
            pCrtcPriv->bAccessibility = TRUE;
            return FALSE;
        }

        EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
        EXYNOSHwcPtr pHwc = pExynos->pHwc;

        if (pHwc && EXYNOSPTR(pCrtc->scrn)->hwc_active) {
            /* hwc active,release accessibility layers */
            EXYNOSLayerPos p_lpos[PLANE_MAX];
            EXYNOSLayerOutput output = LAYER_OUTPUT_LCD;

            EXYNOSLayerMngClientID lyr_clientid = exynosHwcGetLyrClientId(pHwc);
            int max_lpos =
                exynosLayerMngGetListOfOwnedPos(lyr_clientid, output, p_lpos);

            for (i = 0; i < max_lpos; ++i) {
                exynosLayerMngRelease(lyr_clientid, output, p_lpos[i]);
            }
            exynosHwcUpdate(pCrtc->scrn);
        }
        /* layer update */
        pLayer = exynosLayerFind(output, LAYER_UPPER);
        if (pLayer && exynosLayerIsVisible(pLayer))
            exynosLayerUpdate(pLayer);
    }

    return TRUE;
}

Bool
exynosCrtcEnableScreenRotate(xf86CrtcPtr pCrtc, Bool enable)
{
#ifdef _F_WEARABLE_FEATURE_
    XDBG_RETURN_VAL_IF_FAIL(pCrtc != NULL, FALSE);

    ScrnInfoPtr pScrn = pCrtc->scrn;
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL)
        return FALSE;
    int width = pCrtc->mode.HDisplay;
    int height = pCrtc->mode.VDisplay;
    int degree = pCrtcPriv->screen_rotate_degree;

    /* accessibility and screen rotate can't be enable at the same time */
    XDBG_RETURN_VAL_IF_FAIL(pCrtcPriv->bAccessibility == FALSE, FALSE);

    if (enable) {
        struct drm_exynos_ipp_property property;
        int prop_id;

        if (!pCrtcPriv->accessibility_front_bo) {
            pCrtcPriv->accessibility_front_bo =
                exynosRenderBoCreate(pScrn, width, height);
            XDBG_RETURN_VAL_IF_FAIL(pCrtcPriv->accessibility_front_bo != NULL,
                                    FALSE);

            pCrtcPriv->accessibility_back_bo =
                exynosRenderBoCreate(pScrn, width, height);
            XDBG_RETURN_VAL_IF_FAIL(pCrtcPriv->accessibility_back_bo != NULL,
                                    FALSE);
        }

        prop_id = pCrtcPriv->screen_rotate_prop_id;
        if (prop_id != 0) {
            struct drm_exynos_ipp_cmd_ctrl ctrl = { 0, };
            ctrl.prop_id = pCrtcPriv->screen_rotate_prop_id;
            ctrl.ctrl = IPP_CTRL_PAUSE;
            exynosDrmIppCmdCtrl(pScrn, &ctrl);
            pCrtcPriv->screen_rotate_ipp_status = IPP_CTRL_PAUSE;
            XDBG_INFO(MDISP, "screen rotate ipp(id:%d) pause\n",
                      pCrtcPriv->screen_rotate_prop_id);
        }
        else
            pCrtcPriv->screen_rotate_ipp_status = IPP_CTRL_STOP;

        CLEAR(property);
        property.config[0].ops_id = EXYNOS_DRM_OPS_SRC;
        property.config[0].fmt = DRM_FORMAT_ARGB8888;
        property.config[0].sz.hsize = (__u32) width;
        property.config[0].sz.vsize = (__u32) height;
        property.config[0].pos.x = 0;
        property.config[0].pos.y = 0;
        property.config[0].pos.w = (__u32) width;
        property.config[0].pos.h = (__u32) height;
        property.config[1].ops_id = EXYNOS_DRM_OPS_DST;
        if (degree % 360 == 90)
            property.config[1].degree = EXYNOS_DRM_DEGREE_90;
        else if (degree % 360 == 180)
            property.config[1].degree = EXYNOS_DRM_DEGREE_180;
        else if (degree % 360 == 270)
            property.config[1].degree = EXYNOS_DRM_DEGREE_270;
        else
            property.config[1].degree = EXYNOS_DRM_DEGREE_0;
        property.config[1].fmt = DRM_FORMAT_ARGB8888;
        property.config[1].sz.hsize = width;
        property.config[1].sz.vsize = height;
        property.config[1].pos.x = (__u32) 0;
        property.config[1].pos.y = (__u32) 0;
        property.config[1].pos.w = (__u32) width;
        property.config[1].pos.h = (__u32) height;

        property.cmd = IPP_CMD_M2M;
        property.type = IPP_SYNC_WORK;
        property.prop_id = prop_id;

        prop_id = exynosDrmIppSetProperty(pScrn, &property);
        XDBG_RETURN_VAL_IF_FAIL(prop_id != 0, FALSE);
        pCrtcPriv->screen_rotate_prop_id = prop_id;

        XDBG_INFO(MDISP, "screen rotate ipp(id:%d) start\n", prop_id);
    }
    else {
        if (pCrtcPriv->screen_rotate_prop_id > 0) {
            struct drm_exynos_ipp_cmd_ctrl ctrl = { 0, };
            ctrl.prop_id = pCrtcPriv->screen_rotate_prop_id;
            ctrl.ctrl = IPP_CTRL_STOP;
            exynosDrmIppCmdCtrl(pScrn, &ctrl);
            pCrtcPriv->screen_rotate_prop_id = 0;
            pCrtcPriv->screen_rotate_ipp_status = IPP_CTRL_STOP;
            XDBG_INFO(MDISP, "screen rotate ipp(id:%d) stop\n",
                      pCrtcPriv->screen_rotate_prop_id);
        }
    }
#endif
    return TRUE;
}

Bool
exynosCrtcScreenRotate(xf86CrtcPtr pCrtc, int degree)
{
#ifdef _F_WEARABLE_FEATURE_
    XDBG_RETURN_VAL_IF_FAIL(pCrtc != NULL, FALSE);

    ScrnInfoPtr pScrn = pCrtc->scrn;
    EXYNOSPtr pExynos = EXYNOSPTR(pCrtc->scrn);
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL)
        return FALSE;
    CARD32 elapsed[3] = { 0, };

    if (pCrtcPriv->screen_rotate_degree == degree)
        return TRUE;

    /* accessibility and screen rotate can't be enable at the same time */
    XDBG_RETURN_VAL_IF_FAIL(pCrtcPriv->bAccessibility == FALSE, FALSE);

    pCrtcPriv->screen_rotate_degree = degree;

    if (pExynos->isLcdOff) {
        XDBG_INFO(MDISP, "screen rotate(degree:%d)\n", degree);
        exynosVideoScreenRotate(pScrn, degree);
        return TRUE;
    }

    elapsed[0] = GetTimeInMillis();

    if (degree > 0) {
        exynosCrtcEnableScreenRotate(pCrtc, TRUE);

        /* do accessibility */
        if (!exynosCrtcExecAccessibility
            (pCrtc, pCrtcPriv->front_bo, pCrtcPriv->accessibility_back_bo)) {
            exynosRenderBoUnref(pCrtcPriv->accessibility_front_bo);
            pCrtcPriv->accessibility_front_bo = NULL;
            exynosRenderBoUnref(pCrtcPriv->accessibility_back_bo);
            pCrtcPriv->accessibility_back_bo = NULL;
            return FALSE;
        }
    }
    else
        exynosCrtcEnableScreenRotate(pCrtc, FALSE);

    elapsed[1] = GetTimeInMillis();

    exynosCrtcApply(pCrtc);

    elapsed[2] = GetTimeInMillis();

    exynosVideoScreenRotate(pScrn, degree);

    XDBG_INFO(MDISP, "screen rotate done(degree:%d, dur:%ld~%ld~%ld ms)\n",
              degree, elapsed[1] - elapsed[0], elapsed[2] - elapsed[1],
              GetTimeInMillis() - elapsed[2]);
#endif
    return TRUE;
}

Bool
exynosCrtcTurn(xf86CrtcPtr pCrtc, Bool onoff, Bool always, Bool user)
{
    EXYNOSModePtr pExynosMode = EXYNOSPTR(pCrtc->scrn)->pExynosMode;
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL)
        return FALSE;
    int crtc_id = exynosCrtcID(pCrtcPriv);
    int mode;

    mode = (onoff > 0) ? 0 : 1;

    if (pCrtcPriv->onoff == onoff) {
        pCrtcPriv->onoff_always = always;
        XDBG_ERROR(MDISP, "Crtc(%d) UI layer is '%s'%s\n",
                   crtc_id, (onoff) ? "ON" : "OFF",
                   (always) ? "(always)." : ".");
        return TRUE;
    }

    if (pCrtcPriv->onoff_always)
        if (!always) {
            XDBG_ERROR(MDISP, "Crtc(%d) UI layer can't be '%s'.\n", crtc_id,
                       (onoff) ? "ON" : "OFF");
            return FALSE;
        }

    /* 0 : normal, 1 : blank, 2 : defer */
    if (pCrtcPriv->is_dummy == FALSE) {
        if (!exynosUtilSetDrmProperty(pExynosMode, crtc_id,
                                      DRM_MODE_OBJECT_CRTC, "mode", mode)) {
            XDBG_ERROR(MDISP, "SetDrmProperty failed. crtc(%d) onoff(%d) \n",
                       crtc_id, onoff);
            return FALSE;
        }
    }

    pCrtcPriv->onoff = onoff;
    pCrtcPriv->onoff_always = always;

    XDBG_INFO(MDISP, "%s >> crtc(%d) UI layer '%s'%s\n",
              (user) ? "user" : "Xorg", crtc_id, (onoff) ? "ON" : "OFF",
              (always) ? "(always)." : ".");

    return TRUE;
}

Bool
exynosCrtcCheckOn(xf86CrtcPtr pCrtc)
{
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL)
        return FALSE;

    return pCrtcPriv->onoff;
}

/* return true if there is no flip pixmap available */
Bool
exynosCrtcFullFreeFlipPixmap(ScrnInfoPtr pScrn, int crtc_pipe)
{
    xf86CrtcPtr pCrtc = NULL;
    EXYNOSCrtcPrivPtr pCrtcPriv = NULL;
    int i;

    pCrtc = _exynosCrtcGetFromPipe(pScrn, crtc_pipe);
    XDBG_RETURN_VAL_IF_FAIL(pCrtc != NULL, FALSE);

    pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL)
        return FALSE;

    /* there is a free flip pixmap, return false */
    for (i = 0; i < pCrtcPriv->flip_backpixs.num; i++) {
        if (pCrtcPriv->flip_backpixs.pix_free[i]) {
            return FALSE;
        }
    }

    XDBG_WARNING(MFLIP, "no free flip pixmap\n");

    return TRUE;
}

#define GET_NEXT_IDX(idx, max) (((idx+1) % (max)))
PixmapPtr
exynosCrtcGetFreeFlipPixmap(ScrnInfoPtr pScrn, int crtc_pipe, DrawablePtr pDraw,
                            unsigned int usage_hint)
{
    xf86CrtcPtr pCrtc = NULL;
    EXYNOSCrtcPrivPtr pCrtcPriv = NULL;
    PixmapPtr pPixmap = NULL;
    ScreenPtr pScreen = pScrn->pScreen;
    int i;
    int check_release = 0;

    pCrtc = _exynosCrtcGetFromPipe(pScrn, crtc_pipe);
    XDBG_RETURN_VAL_IF_FAIL(pCrtc != NULL, FALSE);

    pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL)
        return NULL;

    /* check if there is free flip pixmaps */
    if (exynosCrtcFullFreeFlipPixmap(pScrn, crtc_pipe)) {
        /* case : flip pixmap is never release
           if flip_count is 0 where there is no uncompleted pageflipping,
           release the flip_pixmap which occupied by a drawable. */
        if (pCrtcPriv->flip_count == 0) {
            exynosCrtcRelAllFlipPixmap(pScrn, crtc_pipe);
            check_release = 1;
            XDBG_WARNING(MFLIP,
                         "@@ release the drawable pre-occuiped the flip_pixmap\n");
        }

        /* return null, if there is no flip_backpixmap which can release */
        if (!check_release)
            return NULL;
    }

    /* return flip pixmap */
    for (i =
         GET_NEXT_IDX(pCrtcPriv->flip_backpixs.lub,
                      pCrtcPriv->flip_backpixs.num);
         i < pCrtcPriv->flip_backpixs.num;
         i = GET_NEXT_IDX(i, pCrtcPriv->flip_backpixs.num)) {
        if (pCrtcPriv->flip_backpixs.pix_free[i]) {
            if (pCrtcPriv->flip_backpixs.flip_pixmaps[i]) {
                pPixmap = pCrtcPriv->flip_backpixs.flip_pixmaps[i];
                XDBG_DEBUG(MFLIP,
                           "the index(%d, %d) of the flip pixmap in pipe(%d) is set\n",
                           i, tbm_bo_export(exynosExaPixmapGetBo(pPixmap)),
                           crtc_pipe);
            }
            else {
                pPixmap = (*pScreen->CreatePixmap) (pScreen,
                                                    pDraw->width,
                                                    pDraw->height,
                                                    pDraw->depth, usage_hint);
                XDBG_RETURN_VAL_IF_FAIL(pPixmap != NULL, NULL);
                pCrtcPriv->flip_backpixs.flip_pixmaps[i] = pPixmap;

                XDBG_DEBUG(MFLIP,
                           "the index(%d, %d) of the flip pixmap in pipe(%d) is created\n",
                           i, tbm_bo_export(exynosExaPixmapGetBo(pPixmap)),
                           crtc_pipe);
            }

#if USE_XDBG
            if (pCrtcPriv->flip_backpixs.flip_draws[i] &&
                (pCrtcPriv->flip_backpixs.flip_draws[i] != pDraw)) {
                xDbgLogPListDrawRemoveRefPixmap(pCrtcPriv->flip_backpixs.
                                                flip_draws[i],
                                                pCrtcPriv->flip_backpixs.
                                                flip_pixmaps[i]);
            }
#endif

            pCrtcPriv->flip_backpixs.pix_free[i] = FALSE;
            pCrtcPriv->flip_backpixs.flip_draws[i] = pDraw;
            pCrtcPriv->flip_backpixs.lub = i;

#if USE_XDBG
            xDbgLogPListDrawAddRefPixmap(pDraw, pPixmap);
#endif
            break;
        }
    }

    return pPixmap;
}

void
exynosCrtcRelFlipPixmap(ScrnInfoPtr pScrn, int crtc_pipe, PixmapPtr pPixmap)
{
    xf86CrtcPtr pCrtc = NULL;
    EXYNOSCrtcPrivPtr pCrtcPriv = NULL;
    int i;

    pCrtc = _exynosCrtcGetFromPipe(pScrn, crtc_pipe);
    XDBG_RETURN_IF_FAIL(pCrtc != NULL);

    pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL)
        return;

    /* release flip pixmap */
    for (i = 0; i < pCrtcPriv->flip_backpixs.num; i++) {
        if (pPixmap == pCrtcPriv->flip_backpixs.flip_pixmaps[i]) {
            pCrtcPriv->flip_backpixs.pix_free[i] = TRUE;
            /*pCrtcPriv->flip_backpixs.flip_draws[i] = NULL; */

            XDBG_DEBUG(MFLIP,
                       "the index(%d, %d) of the flip pixmap in pipe(%d) is unset\n",
                       i, tbm_bo_export(exynosExaPixmapGetBo(pPixmap)),
                       crtc_pipe);
            break;
        }
    }
}

void
exynosCrtcRelAllFlipPixmap(ScrnInfoPtr pScrn, int crtc_pipe)
{
    xf86CrtcPtr pCrtc = NULL;
    EXYNOSCrtcPrivPtr pCrtcPriv = NULL;
    int i;

    pCrtc = _exynosCrtcGetFromPipe(pScrn, crtc_pipe);
    XDBG_RETURN_IF_FAIL(pCrtc != NULL);

    pCrtcPriv = pCrtc->driver_private;
    if (pCrtcPriv == NULL)
        return;

    /* release flip pixmap */
    for (i = 0; i < pCrtcPriv->flip_backpixs.num; i++) {
        pCrtcPriv->flip_backpixs.pix_free[i] = TRUE;
        /*pCrtcPriv->flip_backpixs.flip_draws[i] = NULL; */

        XDBG_DEBUG(MFLIP,
                   "the index(%d) of the flip draw in pipe(%d) is unset\n",
                   i, crtc_pipe);
    }
}

void
exynosCrtcRemoveFlipPixmap(xf86CrtcPtr pCrtc)
{
    _flipPixmapDeinit(pCrtc);
}

void
exynosCrtcCountFps(xf86CrtcPtr pCrtc)
{
    EXYNOSCrtcPrivPtr pCrtcPriv = NULL;
    int conn_type;

    if (!pCrtc->enabled)
        return;

    pCrtcPriv = pCrtc->driver_private;
    if (pCrtcPriv == NULL)
        return;
    conn_type = exynosCrtcGetConnectType(pCrtc);

    xDbgLogFpsDebugCount(pCrtcPriv->pFpsDebug, conn_type);
}

#ifdef NO_CRTC_MODE
Bool
exynosCrtcCheckInUseAll(ScrnInfoPtr pScrn)
{
    xf86CrtcConfigPtr config = XF86_CRTC_CONFIG_PTR(pScrn);
    int i;
    Bool ret = FALSE;

    if (config == NULL)
        return ret;
    for (i = 0; i < config->num_crtc; i++) {
        xf86CrtcPtr pCrtc = config->crtc[i];

        if (xf86CrtcInUse(pCrtc)) {
            ret = TRUE;
            pCrtc->enabled = TRUE;
        }
        else
            pCrtc->enabled = FALSE;
    }
    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
               "%s found active CRTC\n", ret ? "" : "NOT");
    return ret;
}
#endif                          //NO_CRTC_MODE

xf86CrtcPtr
exynosCrtcGetByID(ScrnInfoPtr pScrn, int crtc_id)
{
    xf86CrtcConfigPtr pCrtcConfig = XF86_CRTC_CONFIG_PTR(pScrn);
    int i;

    for (i = 0; i < pCrtcConfig->num_crtc; i++) {
        EXYNOSCrtcPrivPtr pCrtcPriv = pCrtcConfig->crtc[i]->driver_private;

        if (pCrtcPriv != NULL) {
            if (pCrtcPriv->mode_crtc != NULL) {
                if (pCrtcPriv->mode_crtc->crtc_id == crtc_id)
                    return pCrtcConfig->crtc[i];
            }
        }
    }
    return NULL;
}
