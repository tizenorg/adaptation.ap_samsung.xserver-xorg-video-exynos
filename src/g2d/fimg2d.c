#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "xf86drm.h"
#include "xf86drmMode.h"
#include "exynos_drm.h"
#include "fimg2d.h"
#include "sec_util.h"

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 1
#endif

#define G2D_MAX_CMD 64
#define G2D_MAX_GEM_CMD 64
#define G2D_MAX_CMD_LIST    64

typedef struct _G2D_CONTEXT {
    unsigned int major;
    unsigned int minor;
    int drm_fd;

    struct drm_exynos_g2d_cmd cmd[G2D_MAX_CMD];
    struct drm_exynos_g2d_cmd cmd_gem[G2D_MAX_GEM_CMD];

    unsigned int cmd_nr;
    unsigned int cmd_gem_nr;

    unsigned int cmdlist_nr;
}G2dContext;

G2dContext* gCtx=NULL;

static void
_g2d_clear(void)
{
    gCtx->cmd_nr = 0;
    gCtx->cmd_gem_nr = 0;
}

int
g2d_init (int fd)
{
    int ret;
    struct drm_exynos_g2d_get_ver ver;

    if(gCtx) return FALSE;

   gCtx = calloc(1, sizeof(*gCtx));
   gCtx->drm_fd = fd;

    ret = ioctl(fd, DRM_IOCTL_EXYNOS_G2D_GET_VER, &ver);
    if (ret < 0) {
        XDBG_ERROR (MG2D, "failed to get version: %s\n", strerror(-ret));
        free(gCtx);
        gCtx = NULL;

        return FALSE;
    }

    gCtx->major = ver.major;
    gCtx->minor = ver.minor;

    XDBG_INFO (MG2D, "[G2D] version(%d.%d) init....OK\n", gCtx->major, gCtx->minor);

    return TRUE;
}

void
g2d_fini (void)
{
    free(gCtx);
    gCtx = NULL;
}

int
g2d_add_cmd (unsigned int cmd, unsigned int value)
{
    switch(cmd) /* Check GEM Command */
    {
    case SRC_BASE_ADDR_REG:
    case SRC_PLANE2_BASE_ADDR_REG:
    case DST_BASE_ADDR_REG:
    case DST_PLANE2_BASE_ADDR_REG:
    case PAT_BASE_ADDR_REG:
    case MASK_BASE_ADDR_REG:
        if(gCtx->cmd_gem_nr >= G2D_MAX_GEM_CMD)
        {
            XDBG_ERROR (MG2D, "Overflow cmd_gem size:%d\n", gCtx->cmd_gem_nr);
            return FALSE;
        }

        gCtx->cmd_gem[gCtx->cmd_gem_nr].offset = cmd;
        gCtx->cmd_gem[gCtx->cmd_gem_nr].data = value;
        gCtx->cmd_gem_nr++;
        break;
    default:
        if(gCtx->cmd_nr >= G2D_MAX_CMD)
        {
            XDBG_ERROR (MG2D, "Overflow cmd size:%d\n", gCtx->cmd_nr);
            return FALSE;
        }

        gCtx->cmd[gCtx->cmd_nr].offset = cmd;
        gCtx->cmd[gCtx->cmd_nr].data = value;
        gCtx->cmd_nr++;
        break;
    }

    return TRUE;
}

void
g2d_reset (unsigned int clear_reg)
{
    gCtx->cmd_nr = 0;
    gCtx->cmd_gem_nr = 0;

    if(clear_reg)
    {
        g2d_add_cmd(SOFT_RESET_REG, 0x01);
    }
}

int
g2d_exec (void)
{
    struct drm_exynos_g2d_exec exec;
    int ret;

    if(gCtx->cmdlist_nr == 0)
        return TRUE;

    exec.async = 0;
    ret = ioctl(gCtx->drm_fd, DRM_IOCTL_EXYNOS_G2D_EXEC, &exec);
    if (ret < 0) {
        XDBG_ERROR (MG2D, "failed to execute(%d): %s\n", ret, strerror(-ret));
        return FALSE;
    }

    gCtx->cmdlist_nr = 0;
    return TRUE;
}

int
g2d_flush (void)
{
    int ret;
    struct drm_exynos_g2d_set_cmdlist cmdlist;

    if (gCtx->cmd_nr == 0 && gCtx->cmd_gem_nr == 0)
        return TRUE;

    if(gCtx->cmdlist_nr >= G2D_MAX_CMD_LIST)
    {
        XDBG_WARNING (MG2D, "Overflow cmdlist:%d\n", gCtx->cmdlist_nr);
        g2d_exec();
    }

    memset(&cmdlist, 0, sizeof(struct drm_exynos_g2d_set_cmdlist));

    cmdlist.cmd = (unsigned long)&gCtx->cmd[0];
    cmdlist.cmd_gem = (unsigned long)&gCtx->cmd_gem[0];
    cmdlist.cmd_nr = gCtx->cmd_nr;
    cmdlist.cmd_gem_nr = gCtx->cmd_gem_nr;
    cmdlist.event_type = G2D_EVENT_NOT;
    cmdlist.user_data = 0;

    gCtx->cmd_nr = 0;
    gCtx->cmd_gem_nr = 0;
    ret = ioctl(gCtx->drm_fd, DRM_IOCTL_EXYNOS_G2D_SET_CMDLIST, &cmdlist);
    if (ret < 0) {

        XDBG_ERROR (MG2D, "numFlush:%d, failed to set cmdlist(%d): %s\n", gCtx->cmdlist_nr, ret, strerror(-ret));
        return FALSE;
    }

    gCtx->cmdlist_nr++;
    return TRUE;
}

G2dImage*
g2d_image_new(void)
{
    G2dImage* img;

    img = calloc(1, sizeof(G2dImage));
    if(img == NULL)
    {
        XDBG_ERROR (MG2D, "Cannot create solid image\n");
        return NULL;
    }

    img->repeat_mode = G2D_REPEAT_MODE_NONE;
    img->scale_mode = G2D_SCALE_MODE_NONE;
    img->xscale = G2D_FIXED_1;
    img->yscale = G2D_FIXED_1;

    return img;
}

G2dImage*
g2d_image_create_solid (unsigned int color)
{
    G2dImage* img;

    img = g2d_image_new();
    if(img == NULL)
    {
        XDBG_ERROR (MG2D, "Cannot create solid image\n");
        return NULL;
    }

    img->select_mode = G2D_SELECT_MODE_FGCOLOR;
    img->color_mode = G2D_COLOR_FMT_ARGB8888|G2D_ORDER_AXRGB;
    img->data.color = color;
    img->width = -1;
    img->height = -1;

    return img;
}

G2dImage*
g2d_image_create_bo (G2dColorMode format, unsigned int width, unsigned int height,
                                            unsigned int bo, unsigned int stride)
{
    G2dImage* img;

    img = g2d_image_new();
    if(img == NULL)
    {
        XDBG_ERROR (MG2D, "Cannot alloc bo\n");
        return NULL;
    }

    img->select_mode = G2D_SELECT_MODE_NORMAL;
    img->color_mode = format;
    img->width = width;
    img->height = height;

    if(bo)
    {
        img->data.bo[0] = bo;
        img->stride = stride;
    }
    else
    {
        unsigned int stride;
        struct drm_exynos_gem_create arg;

        switch(format&G2D_COLOR_FMT_MASK)
        {
        case G2D_COLOR_FMT_XRGB8888:
        case G2D_COLOR_FMT_ARGB8888:
            stride = width*4;
            break;
        case G2D_COLOR_FMT_A1:
            stride = (width+7) / 8;
            break;
        case G2D_COLOR_FMT_A4:
            stride = (width*4+7) /8;
            break;
        case G2D_COLOR_FMT_A8:
        case G2D_COLOR_FMT_L8:
            stride = width;
            break;
        default:
            XDBG_ERROR (MG2D, "Unsurpported format(%d)\n", format);
            free(img);
            return NULL;
        }

        /* Allocation gem buffer */
        arg.flags = EXYNOS_BO_CACHABLE;
        arg.size = stride*height;
        if(drmCommandWriteRead(gCtx->drm_fd, DRM_EXYNOS_GEM_CREATE, &arg, sizeof(arg)))
        {
            XDBG_ERROR (MG2D, "Cannot create bo image(flag:%x, size:%d)\n", arg.flags, (unsigned int)arg.size);
            free(img);
            return NULL;
        }

        /* Map gembuffer */
        {
            struct drm_exynos_gem_mmap arg_map;

            memset(&arg_map, 0, sizeof(arg_map));
            arg_map.handle = arg.handle;
            arg_map.size = arg.size;
            if(drmCommandWriteRead(gCtx->drm_fd, DRM_EXYNOS_GEM_MMAP, &arg_map, sizeof(arg_map)))
            {
                XDBG_ERROR (MG2D, "Cannot map offset bo image\n");
                free(img);
                return NULL;
            }

            img->mapped_ptr[0] = (void*)(unsigned long)arg_map.mapped;
        }

        img->stride = stride;
        img->data.bo[0] = arg.handle;
        img->need_free = 1;
    }

    return img;
}

G2dImage*
g2d_image_create_bo2 (G2dColorMode format,
                      unsigned int width, unsigned int height,
                      unsigned int bo1, unsigned int bo2, unsigned int stride)
{
    G2dImage* img;

    if (bo1 == 0)
    {
        XDBG_ERROR (MG2D, "[G2D] first bo is NULL. \n");
        return NULL;
    }

    if (format & G2D_YCbCr_2PLANE)
        if (bo2 == 0)
        {
            XDBG_ERROR (MG2D, "[G2D] second bo is NULL. \n");
            return NULL;
        }

    img = g2d_image_new();

    if(img == NULL)
    {
        XDBG_ERROR (MG2D, "Cannot alloc bo\n");
        return NULL;
    }

    img->select_mode = G2D_SELECT_MODE_NORMAL;
    img->color_mode = format;
    img->width = width;
    img->height = height;

    img->data.bo[0] = bo1;
    img->data.bo[1] = bo2;
    img->stride = stride;

    return img;
}

G2dImage*
g2d_image_create_data (G2dColorMode format, unsigned int width, unsigned int height,
                                            void* data, unsigned int stride)
{
    G2dImage* img;

    img = g2d_image_new();
    if(img == NULL)
    {
        XDBG_ERROR (MG2D, "Cannot alloc bo\n");
        return NULL;
    }

    img->select_mode = G2D_SELECT_MODE_NORMAL;
    img->color_mode = format;
    img->width = width;
    img->height = height;

    if(data)
    {
        struct drm_exynos_gem_userptr userptr;

        memset(&userptr, 0, sizeof(struct drm_exynos_gem_userptr));
        userptr.userptr = (uint64_t)((uint32_t)data);
        userptr.size = stride*height;

        img->mapped_ptr[0] = data;
        img->stride = stride;
        if(drmCommandWriteRead(gCtx->drm_fd,
                                        DRM_EXYNOS_GEM_USERPTR,
                                        &userptr, sizeof(userptr)))
        {
            XDBG_ERROR (MG2D, "Cannot create userptr(ptr:%p, size:%d)\n", (void*)((uint32_t)userptr.userptr), (uint32_t)userptr.size);
            free(img);
            return NULL;
        }
        img->data.bo[0] = userptr.handle;
        img->need_free = 1;
    }
    else
    {
        unsigned int stride;
        struct drm_exynos_gem_create arg;

        switch(format&G2D_COLOR_FMT_MASK)
        {
        case G2D_COLOR_FMT_XRGB8888:
        case G2D_COLOR_FMT_ARGB8888:
            stride = width*4;
            break;
        case G2D_COLOR_FMT_A1:
            stride = (width+7) / 8;
            break;
        case G2D_COLOR_FMT_A4:
            stride = (width*4+7) /8;
            break;
        case G2D_COLOR_FMT_A8:
        case G2D_COLOR_FMT_L8:
            stride = width;
            break;
        default:
            XDBG_ERROR (MG2D, "Unsurpported format(%d)\n", format);
            free(img);
            return NULL;
        }

        /* Allocation gem buffer */
        arg.flags = EXYNOS_BO_NONCONTIG|EXYNOS_BO_CACHABLE;
        arg.size = stride*height;
        if(drmCommandWriteRead(gCtx->drm_fd,
                                        DRM_EXYNOS_GEM_CREATE,
                                        &arg, sizeof(arg)))
        {
            XDBG_ERROR (MG2D, "Cannot create bo image(flag:%x, size:%d)\n", arg.flags, (unsigned int)arg.size);
            free(img);
            return NULL;
        }

        /* Map gembuffer */
        {
            struct drm_exynos_gem_mmap arg_map;

            memset(&arg_map, 0, sizeof(arg_map));
            arg_map.handle = arg.handle;
            arg_map.size = arg.size;
            if(drmCommandWriteRead(gCtx->drm_fd,
                                                    DRM_EXYNOS_GEM_MMAP,
                                                    &arg_map, sizeof(arg_map)))
            {
                XDBG_ERROR (MG2D, "Cannot map offset bo image\n");
                free(img);
                return NULL;
            }

            img->mapped_ptr[0] = (void*)(unsigned long)arg_map.mapped;
        }

        img->stride = stride;
        img->data.bo[0] = arg.handle;
        img->need_free = 1;
    }

    return img;
}

void
g2d_image_free (G2dImage* img)
{
    if(img->need_free)
    {
        struct drm_gem_close arg;

        /* Free gem buffer */
        memset(&arg, 0, sizeof(arg));
        arg.handle = img->data.bo[0];
        if(drmIoctl(gCtx->drm_fd, DRM_IOCTL_GEM_CLOSE, &arg))
        {
            XDBG_ERROR (MG2D, "[G2D] %s:%d error: %d\n",__FUNCTION__, __LINE__, errno);
        }
    }

    free(img);
}

int
g2d_set_src(G2dImage* img)
{
    if(img == NULL) return FALSE;

    g2d_add_cmd(SRC_SELECT_REG, img->select_mode);
    g2d_add_cmd(SRC_COLOR_MODE_REG, img->color_mode);

    switch(img->select_mode)
    {
    case G2D_SELECT_MODE_NORMAL:
        g2d_add_cmd(SRC_BASE_ADDR_REG, img->data.bo[0]);
        if (img->color_mode & G2D_YCbCr_2PLANE)
        {
            if (img->data.bo[1] > 0)
                g2d_add_cmd(SRC_PLANE2_BASE_ADDR_REG, img->data.bo[1]);
            else
                XDBG_ERROR (MG2D, "[G2D] %s:%d error: second bo is null.\n",__FUNCTION__, __LINE__);
        }
        g2d_add_cmd(SRC_STRIDE_REG, img->stride);
        break;
    case G2D_SELECT_MODE_FGCOLOR:
        g2d_add_cmd(FG_COLOR_REG, img->data.color);
        break;
    case G2D_SELECT_MODE_BGCOLOR:
        g2d_add_cmd(BG_COLOR_REG, img->data.color);
        break;
    default:
        XDBG_ERROR (MG2D, "Error: set src\n");
        _g2d_clear();
        return FALSE;
    }

    return TRUE;
}

int
g2d_set_mask(G2dImage* img)
{
    G2dMaskModeVal mode;

    if(img == NULL) return FALSE;
    if(img->select_mode != G2D_SELECT_MODE_NORMAL) return FALSE;

    g2d_add_cmd(MASK_BASE_ADDR_REG, img->data.bo[0]);
    g2d_add_cmd(MASK_STRIDE_REG, img->stride);

    mode.val = 0;
    switch(img->color_mode & G2D_COLOR_FMT_MASK)
    {
    case G2D_COLOR_FMT_A1:
        mode.data.maskMode = G2D_MASK_MODE_1BPP;
        break;
    case G2D_COLOR_FMT_A4:
        mode.data.maskMode = G2D_MASK_MODE_4BPP;
        break;
    case G2D_COLOR_FMT_A8:
        mode.data.maskMode = G2D_MASK_MODE_8BPP;
        break;
    case G2D_COLOR_FMT_ARGB8888:
        mode.data.maskMode = G2D_MASK_MODE_32BPP;
        mode.data.maskOrder = (img->color_mode&G2D_ORDER_MASK)>>4;
        break;
    case G2D_COLOR_FMT_RGB565:
        mode.data.maskMode = G2D_MASK_MODE_16BPP_565;
        mode.data.maskOrder = (img->color_mode&G2D_ORDER_MASK)>>4;
        break;
    case G2D_COLOR_FMT_ARGB1555:
        mode.data.maskMode = G2D_MASK_MODE_16BPP_1555;
        mode.data.maskOrder = (img->color_mode&G2D_ORDER_MASK)>>4;
        break;
    case G2D_COLOR_FMT_ARGB4444:
        mode.data.maskMode = G2D_MASK_MODE_16BPP_4444;
        mode.data.maskOrder = (img->color_mode&G2D_ORDER_MASK)>>4;
        break;
    default:
        break;
    }
    g2d_add_cmd(MASK_MODE_REG, mode.val);

    return TRUE;
}

int
g2d_set_dst(G2dImage* img)
{
    if(img == NULL) return FALSE;

    g2d_add_cmd(DST_SELECT_REG, G2D_SELECT_MODE_FGCOLOR);
    g2d_add_cmd(DST_COLOR_MODE_REG, img->color_mode);

    switch(img->select_mode)
    {
    case G2D_SELECT_MODE_NORMAL:
        g2d_add_cmd(DST_BASE_ADDR_REG, img->data.bo[0]);
        g2d_add_cmd(DST_STRIDE_REG, img->stride);
        break;
    case G2D_SELECT_MODE_FGCOLOR:
        g2d_add_cmd(FG_COLOR_REG, img->data.color);
        break;
    case G2D_SELECT_MODE_BGCOLOR:
        g2d_add_cmd(BG_COLOR_REG, img->data.color);
        break;
    default:
        XDBG_ERROR (MG2D, "Error: set src\n");
        _g2d_clear();
        return FALSE;
    }

    return TRUE;
}

unsigned int
g2d_get_blend_op(G2dOp op)
{
    G2dBlendFunctionVal val;
#define     set_bf(sc, si, scsa, scda, dc, di, dcsa, dcda)    \
                        val.data.srcCoeff = sc;        \
                        val.data.invSrcColorCoeff = si;    \
                        val.data.srcCoeffSrcA = scsa;        \
                        val.data.srcCoeffDstA = scda;        \
                        val.data.dstCoeff = dc;        \
                        val.data.invDstColorCoeff = di;    \
                        val.data.dstCoeffSrcA = dcsa;        \
                        val.data.dstCoeffDstA = dcda

    val.val = 0;
    switch (op)
    {
    case G2D_OP_CLEAR:
    case G2D_OP_DISJOINT_CLEAR:
    case G2D_OP_CONJOINT_CLEAR:
        set_bf (G2D_COEFF_MODE_ZERO, 0,0,0,
                    G2D_COEFF_MODE_ZERO, 0,0,0);
        break;
    case G2D_OP_SRC:
    case G2D_OP_DISJOINT_SRC:
    case G2D_OP_CONJOINT_SRC:
        set_bf (G2D_COEFF_MODE_ONE, 0,0,0,
                    G2D_COEFF_MODE_ZERO, 0,0,0);
        break;
    case G2D_OP_DST:
    case G2D_OP_DISJOINT_DST:
    case G2D_OP_CONJOINT_DST:
        set_bf (G2D_COEFF_MODE_ZERO, 0,0,0,
                    G2D_COEFF_MODE_ONE, 0,0,0);
        break;
    case G2D_OP_OVER:
        set_bf (G2D_COEFF_MODE_ONE, 0,0,0,
                    G2D_COEFF_MODE_SRC_ALPHA, 1,0,0);
        break;
    case G2D_OP_OVER_REVERSE:
    case G2D_OP_IN:
    case G2D_OP_IN_REVERSE:
    case G2D_OP_OUT:
    case G2D_OP_OUT_REVERSE:
    case G2D_OP_ATOP:
    case G2D_OP_ATOP_REVERSE:
    case G2D_OP_XOR:
    case G2D_OP_ADD:
    case G2D_OP_NONE:
    default:
        XDBG_ERROR (MG2D, "[FIMG2D] Not support op:%d\n", op);
        set_bf (G2D_COEFF_MODE_ONE, 0,0,0,
                    G2D_COEFF_MODE_ZERO, 0,0,0);
        break;
    }
#undef set_bf

    return val.val;
}

void
g2d_dump(void)
{
    int i;
    XDBG_DEBUG (MG2D, "==================\n");
    XDBG_DEBUG (MG2D, "         G2D REG DUMP         \n");
    XDBG_DEBUG (MG2D, "==================\n");

    for(i=0; i<gCtx->cmd_gem_nr; i++)
    {
        XDBG_DEBUG (MG2D, "[GEM] 0x%08x   0x%08x\n",
                gCtx->cmd_gem[i].offset, gCtx->cmd_gem[i].data);
    }

    for(i=0; i<gCtx->cmd_nr; i++)
    {
        XDBG_DEBUG (MG2D, "[CMD] 0x%08x   0x%08x\n",
                gCtx->cmd[i].offset, gCtx->cmd[i].data);
    }
}
