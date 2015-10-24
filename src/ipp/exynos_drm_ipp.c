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

#include <sys/ioctl.h>

#include "exynos.h"
#include "exynos_util.h"
#include "exynos_video_types.h"
#include "exynos_video_fourcc.h"
#include "exynos_drm_ipp.h"

#include <drm_fourcc.h>

static unsigned int drmfmt_list[] = {
    /* packed */
    DRM_FORMAT_RGB565,
    DRM_FORMAT_XRGB8888,
    DRM_FORMAT_ARGB8888,
    DRM_FORMAT_YUYV,
    DRM_FORMAT_UYVY,

    /* 2 plane */
    DRM_FORMAT_NV12,
    DRM_FORMAT_NV21,
    DRM_FORMAT_NV12MT,

    /* 3 plane */
    DRM_FORMAT_YVU420,
    DRM_FORMAT_YUV420,
    DRM_FORMAT_YUV444,
};

/* A drmfmt list newly allocated. should be freed. */
unsigned int *
exynosDrmIppGetFormatList(int *num)
{
    unsigned int *drmfmts;

    XDBG_RETURN_VAL_IF_FAIL(num != NULL, NULL);

    drmfmts = malloc(sizeof(drmfmt_list));
    XDBG_RETURN_VAL_IF_FAIL(drmfmts != NULL, NULL);

    memcpy(drmfmts, drmfmt_list, sizeof(drmfmt_list));
    *num = sizeof(drmfmt_list) / sizeof(unsigned int);

    return drmfmts;
}

int
exynosDrmIppSetProperty(ScrnInfoPtr pScrn,
                        struct drm_exynos_ipp_property *property)
{
    int ret = 0;

    XDBG_RETURN_VAL_IF_FAIL(pScrn != NULL, -1);
    XDBG_RETURN_VAL_IF_FAIL(property != NULL, -1);

    if (property->prop_id == (__u32) - 1)
        property->prop_id = 0;

    XDBG_DEBUG(MDRM,
               "src : flip(%x) deg(%d) fmt(%c%c%c%c) sz(%dx%d) pos(%d,%d %dx%d)  \n",
               property->config[0].flip, property->config[0].degree,
               FOURCC_STR(property->config[0].fmt),
               property->config[0].sz.hsize, property->config[0].sz.vsize,
               property->config[0].pos.x, property->config[0].pos.y,
               property->config[0].pos.w, property->config[0].pos.h);
    XDBG_DEBUG(MDRM,
               "dst : flip(%x) deg(%d) fmt(%c%c%c%c) sz(%dx%d) pos(%d,%d %dx%d)  \n",
               property->config[1].flip, property->config[1].degree,
               FOURCC_STR(property->config[1].fmt),
               property->config[1].sz.hsize, property->config[1].sz.vsize,
               property->config[1].pos.x, property->config[1].pos.y,
               property->config[1].pos.w, property->config[1].pos.h);

#ifdef LEGACY_INTERFACE
#ifdef _F_WEARABLE_FEATURE_
    XDBG_DEBUG(MDRM,
               "cmd(%d) type(%d) ipp_id(%d) prop_id(%d) hz(%d) protect(%d) range(%d) blending(%x)\n",
               property->cmd, property->type, property->ipp_id,
               property->prop_id, property->refresh_rate, property->protect,
               property->range, property->blending);
#else
    XDBG_DEBUG(MDRM,
               "cmd(%d) ipp_id(%d) prop_id(%d) hz(%d) protect(%d) range(%d)\n",
               property->cmd, property->ipp_id, property->prop_id,
               property->refresh_rate, property->protect, property->range);
#endif
#else
    XDBG_DEBUG(MDRM, "cmd(%d) ipp_id(%d) prop_id(%d) hz(%d) range(%d)\n",
               property->cmd, property->ipp_id, property->prop_id,
               property->refresh_rate, property->range);
#endif

    ret =
        ioctl(EXYNOSPTR(pScrn)->drm_fd, DRM_IOCTL_EXYNOS_IPP_SET_PROPERTY,
              property);
    if (ret) {
        XDBG_ERRNO(MDRM, "failed. \n");
        return -1;
    }

    XDBG_TRACE(MDRM, "success. prop_id(%d) \n", property->prop_id);

    return property->prop_id;
}

Bool
exynosDrmIppQueueBuf(ScrnInfoPtr pScrn, struct drm_exynos_ipp_queue_buf *buf)
{
    int ret = 0;

    XDBG_RETURN_VAL_IF_FAIL(pScrn != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(buf != NULL, FALSE);

    XDBG_DEBUG(MDRM,
               "prop_id(%d) ops_id(%d) ctrl(%d) id(%d) handles(%x %x %x). \n",
               buf->prop_id, buf->ops_id, buf->buf_type, buf->buf_id,
               buf->handle[0], buf->handle[1], buf->handle[2]);

    ret = ioctl(EXYNOSPTR(pScrn)->drm_fd, DRM_IOCTL_EXYNOS_IPP_QUEUE_BUF, buf);
    if (ret) {
        XDBG_ERRNO(MDRM, "failed. prop_id(%d) op(%d) buf(%d) id(%d)\n",
                   buf->prop_id, buf->ops_id, buf->buf_type, buf->buf_id);
        return FALSE;
    }

    XDBG_DEBUG(MDRM, "success. prop_id(%d) \n", buf->prop_id);

    return TRUE;
}

Bool
exynosDrmIppCmdCtrl(ScrnInfoPtr pScrn, struct drm_exynos_ipp_cmd_ctrl *ctrl)
{
    int ret = 0;

    XDBG_RETURN_VAL_IF_FAIL(pScrn != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(ctrl != NULL, FALSE);

    XDBG_TRACE(MDRM, "prop_id(%d) ctrl(%d). \n", ctrl->prop_id, ctrl->ctrl);

    ret = ioctl(EXYNOSPTR(pScrn)->drm_fd, DRM_IOCTL_EXYNOS_IPP_CMD_CTRL, ctrl);
    if (ret) {
        XDBG_ERRNO(MDRM, "failed. prop_id(%d) ctrl(%d)\n",
                   ctrl->prop_id, ctrl->ctrl);
        return FALSE;
    }

    XDBG_DEBUG(MDRM, "success. prop_id(%d) \n", ctrl->prop_id);

    return TRUE;
}
