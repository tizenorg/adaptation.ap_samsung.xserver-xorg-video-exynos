#include <xorg-server.h>
#include <stdio.h>
#include <stdio.h>

#include <xorg-server.h>
#include "xf86.h"
#include "fimg2d.h"
#include "sec_util.h"

static int
_util_get_clip(G2dImage* img, int x, int y, int width, int height, G2dPointVal *lt, G2dPointVal *rb)
{
    if(img->select_mode != G2D_SELECT_MODE_NORMAL)
    {
        lt->data.x = 0;
        lt->data.y = 0;
        rb->data.x = 1;
        rb->data.y = 1;
        return 1;
    }

    if(x<0)
    {
        width += x;
        x=0;
    }

    if(y<0)
    {
        height += y;
        y=0;
    }

    if(x+width > img->width)
    {
        width = img->width - x;
    }

    if(y+height > img->height)
    {
        height = img->height - y;
    }

    if(width <= 0 || height <= 0)
    {
        if(img->repeat_mode != G2D_REPEAT_MODE_NONE)
        {
            x=0;
            y=0;
            width = img->width;
            height = img->height;
            return 1;
        }

        return 0;
    }

    lt->data.x = x;
    lt->data.y = y;
    rb->data.x = x+width;
    rb->data.y = y+height;

    return 1;
}

void
util_g2d_fill(G2dImage* img,
            int x, int y, unsigned int w, unsigned int h,
            unsigned int color)
{
    G2dBitBltCmdVal bitblt={0,};
    G2dPointVal lt, rb;

    g2d_add_cmd(DST_SELECT_REG, G2D_SELECT_MODE_FGCOLOR);
    g2d_add_cmd(DST_COLOR_MODE_REG, img->color_mode);
    g2d_add_cmd(DST_BASE_ADDR_REG, img->data.bo[0]);
    g2d_add_cmd(DST_STRIDE_REG, img->stride);

    /*Set Geometry*/
    if(!_util_get_clip(img, x, y, w, h, &lt, &rb))
    {
        XDBG_ERROR (MG2D, "[G2D] %s:%d error: invalid geometry\n",__FUNCTION__, __LINE__);
        g2d_reset(0);
        return;
    }
    g2d_add_cmd(DST_LEFT_TOP_REG, lt.val);
    g2d_add_cmd(DST_RIGHT_BOTTOM_REG, rb.val);

    /*set Src Image*/
    g2d_add_cmd(SF_COLOR_REG, color);

    /*Set G2D Command*/
    bitblt.val = 0;
    bitblt.data.fastSolidColorFillEn = 1;
    g2d_add_cmd(BITBLT_COMMAND_REG, bitblt.val);

    /* Flush and start */
    g2d_flush();
}

void
util_g2d_fill_alu(G2dImage* img,
            int x, int y, unsigned int w, unsigned int h,
            unsigned int color, G2dAlu alu)
{
    G2dBitBltCmdVal bitblt={0,};
    G2dROP4Val rop4;
    G2dPointVal lt, rb;
    _X_UNUSED unsigned int bg_color;
    unsigned int dst_mode = G2D_SELECT_MODE_BGCOLOR;
    G2dROP3Type rop3 = 0;

    switch(alu)
    {
    case G2Dclear: /* 0 */
        color = 0x00000000;
        break;
    case G2Dand: /* src AND dst */
        dst_mode = G2D_SELECT_MODE_NORMAL;
        rop3 = G2D_ROP3_SRC & G2D_ROP3_DST;
        break;
    case G2DandReverse: /* src AND NOT dst */
        dst_mode = G2D_SELECT_MODE_NORMAL;
        rop3 = G2D_ROP3_SRC & (~G2D_ROP3_DST);
        break;
    case G2Dcopy: /* src */
        break;
    case G2DandInverted: /* NOT src AND dst */
        dst_mode = G2D_SELECT_MODE_NORMAL;
        rop3 = (~G2D_ROP3_SRC) & G2D_ROP3_DST;
        break;
    case G2Dnoop: /* dst */
        dst_mode = G2D_SELECT_MODE_NORMAL;
        rop3 = G2D_ROP3_DST;
        break;
    case G2Dxor: /* src XOR dst */
        dst_mode = G2D_SELECT_MODE_NORMAL;
        rop3 = G2D_ROP3_SRC ^ G2D_ROP3_DST;
        break;
    case G2Dor: /* src OR dst */
        dst_mode = G2D_SELECT_MODE_NORMAL;
        rop3 = G2D_ROP3_SRC | G2D_ROP3_DST;
        break;
    case G2Dnor: /* NOT src AND NOT dst */
        dst_mode = G2D_SELECT_MODE_NORMAL;
        rop3 = (~G2D_ROP3_SRC) & (~G2D_ROP3_DST);
        break;
    case G2Dequiv: /* NOT src XOR dst */
        dst_mode = G2D_SELECT_MODE_NORMAL;
        rop3 = (~G2D_ROP3_SRC) ^ G2D_ROP3_DST;
        break;
    case G2Dinvert: /* NOT dst */
        dst_mode = G2D_SELECT_MODE_NORMAL;
        rop3 = ~G2D_ROP3_DST;
        break;
    case G2DorReverse: /* src OR NOT dst */
        dst_mode = G2D_SELECT_MODE_NORMAL;
        rop3 = G2D_ROP3_SRC |( ~G2D_ROP3_DST);
        break;
    case G2DcopyInverted: /* NOT src */
        rop3 = ~G2D_ROP3_SRC;
        break;
    case G2DorInverted: /* NOT src OR dst */
        dst_mode = G2D_SELECT_MODE_NORMAL;
        rop3 = (~G2D_ROP3_SRC) | G2D_ROP3_DST;
        break;
    case G2Dnand: /* NOT src OR NOT dst */
        dst_mode = G2D_SELECT_MODE_NORMAL;
        rop3 = (~G2D_ROP3_SRC) | (~G2D_ROP3_DST);
        break;
    case G2Dset: /* 1 */
        color = 0xFFFFFFFF;
        break;
    case G2Dnegative:
        bg_color = 0x00FFFFFF;
        rop3 = G2D_ROP3_SRC ^ G2D_ROP3_DST;
        break;
    default:
        break;
    }

    /*set Dst Image*/
    g2d_add_cmd(DST_SELECT_REG, dst_mode);
    g2d_add_cmd(DST_COLOR_MODE_REG, img->color_mode);
    g2d_add_cmd(DST_BASE_ADDR_REG, img->data.bo[0]);
    g2d_add_cmd(DST_STRIDE_REG, img->stride);

    /*Set Geometry*/
    if(!_util_get_clip(img, x, y, w, h, &lt, &rb))
    {
        XDBG_ERROR (MG2D, "[G2D] %s:%d error: invalid geometry\n",__FUNCTION__, __LINE__);
        g2d_reset(0);
        return;
    }
    g2d_add_cmd(DST_LEFT_TOP_REG, lt.val);
    g2d_add_cmd(DST_RIGHT_BOTTOM_REG, rb.val);

    /*set ROP4 val*/
    if(rop3 != 0)
    {
        /*set Src Image*/
        g2d_add_cmd(SRC_SELECT_REG, G2D_SELECT_MODE_FGCOLOR);
        g2d_add_cmd(SRC_COLOR_MODE_REG, G2D_COLOR_FMT_ARGB8888 | G2D_ORDER_AXRGB);
        g2d_add_cmd(FG_COLOR_REG, color);

        rop4.val = 0;
        rop4.data.unmaskedROP3 = rop3;
        g2d_add_cmd(ROP4_REG, rop4.val);
    }
    else
    {
        g2d_add_cmd(SF_COLOR_REG, color);

        /*Set G2D Command*/
        bitblt.val = 0;
        bitblt.data.fastSolidColorFillEn = 1;
        g2d_add_cmd(BITBLT_COMMAND_REG, bitblt.val);
    }

    /* Flush and start */
    g2d_flush();
}

void
util_g2d_copy(G2dImage* src, G2dImage* dst,
            int src_x, int src_y,
            int dst_x, int dst_y,
            unsigned int width, unsigned int height)
{
    G2dROP4Val rop4;
    G2dPointVal lt, rb;

    /*Set dst*/
    g2d_add_cmd(DST_SELECT_REG, G2D_SELECT_MODE_BGCOLOR);
    g2d_add_cmd(DST_COLOR_MODE_REG, dst->color_mode);
    g2d_add_cmd(DST_BASE_ADDR_REG, dst->data.bo[0]);
    if (dst->color_mode & G2D_YCbCr_2PLANE)
    {
        if (dst->data.bo[1] > 0)
            g2d_add_cmd(DST_PLANE2_BASE_ADDR_REG, dst->data.bo[1]);
        else
            XDBG_ERROR (MG2D, "[G2D] %s:%d error: second bo is null.\n",__FUNCTION__, __LINE__);
    }
    g2d_add_cmd(DST_STRIDE_REG, dst->stride);

    /*Set src*/
    g2d_add_cmd(SRC_SELECT_REG, G2D_SELECT_MODE_NORMAL);
    g2d_add_cmd(SRC_COLOR_MODE_REG, src->color_mode);
    g2d_add_cmd(SRC_BASE_ADDR_REG, src->data.bo[0]);
    if (src->color_mode & G2D_YCbCr_2PLANE)
    {
        if (src->data.bo[1] > 0)
            g2d_add_cmd(DST_PLANE2_BASE_ADDR_REG, src->data.bo[1]);
        else
            XDBG_ERROR (MG2D, "[G2D] %s:%d error: second bo is null.\n",__FUNCTION__, __LINE__);
    }
    g2d_add_cmd(SRC_STRIDE_REG, src->stride);
    if(src->repeat_mode)
        g2d_add_cmd(SRC_REPEAT_MODE_REG, src->repeat_mode);

    /*Set cmd*/
    if(!_util_get_clip(src, src_x, src_y, width, height, &lt, &rb))
    {
        XDBG_ERROR (MG2D, "[G2D] %s:%d error: invalid geometry\n",__FUNCTION__, __LINE__);
        g2d_reset(0);
        return;
    }
    g2d_add_cmd(SRC_LEFT_TOP_REG, lt.val);
    g2d_add_cmd(SRC_RIGHT_BOTTOM_REG, rb.val);

    if(!_util_get_clip(dst, dst_x, dst_y, width, height, &lt, &rb))
    {
        XDBG_ERROR (MG2D, "[G2D] %s:%d error: invalid geometry\n",__FUNCTION__, __LINE__);
        g2d_reset(0);
        return;
    }
    g2d_add_cmd(DST_LEFT_TOP_REG, lt.val);
    g2d_add_cmd(DST_RIGHT_BOTTOM_REG, rb.val);

    rop4.val = 0;
    rop4.data.unmaskedROP3 = G2D_ROP3_SRC;
    g2d_add_cmd(ROP4_REG, rop4.val);

    g2d_flush();
}

void
util_g2d_copy_alu(G2dImage* src, G2dImage* dst,
            int src_x, int src_y,
            int dst_x, int dst_y,
            unsigned int width, unsigned int height,
            G2dAlu alu)
{
    G2dROP4Val rop4;
    G2dPointVal lt, rb;
    unsigned int dst_mode = G2D_SELECT_MODE_BGCOLOR;
    unsigned int src_mode = G2D_SELECT_MODE_NORMAL;
    G2dROP3Type rop3=0;
    unsigned int fg_color = 0, bg_color = 0;

    /*Select alu*/
    switch(alu)
    {
    case G2Dclear: /* 0 */
        src_mode = G2D_SELECT_MODE_FGCOLOR;
        fg_color = 0x00000000;
        break;
    case G2Dand: /* src AND dst */
        dst_mode = G2D_SELECT_MODE_NORMAL;
        rop3 = G2D_ROP3_SRC & G2D_ROP3_DST;
        break;
    case G2DandReverse: /* src AND NOT dst */
        dst_mode = G2D_SELECT_MODE_NORMAL;
        rop3 = G2D_ROP3_SRC & (~G2D_ROP3_DST);
        break;
    case G2Dcopy: /* src */
        rop3 = G2D_ROP3_SRC;
        break;
    case G2DandInverted: /* NOT src AND dst */
        dst_mode = G2D_SELECT_MODE_NORMAL;
        rop3 = (~G2D_ROP3_SRC) & G2D_ROP3_DST;
        break;
    case G2Dnoop: /* dst */
        dst_mode = G2D_SELECT_MODE_NORMAL;
        rop3 = G2D_ROP3_DST;
        break;
    case G2Dxor: /* src XOR dst */
        dst_mode = G2D_SELECT_MODE_NORMAL;
        rop3 = G2D_ROP3_SRC ^ G2D_ROP3_DST;
        break;
    case G2Dor: /* src OR dst */
        dst_mode = G2D_SELECT_MODE_NORMAL;
        rop3 = G2D_ROP3_SRC | G2D_ROP3_DST;
        break;
    case G2Dnor: /* NOT src AND NOT dst */
        dst_mode = G2D_SELECT_MODE_NORMAL;
        rop3 = (~G2D_ROP3_SRC) & (~G2D_ROP3_DST);
        break;
    case G2Dequiv: /* NOT src XOR dst */
        dst_mode = G2D_SELECT_MODE_NORMAL;
        rop3 = (~G2D_ROP3_SRC) ^ G2D_ROP3_DST;
        break;
    case G2Dinvert: /* NOT dst */
        dst_mode = G2D_SELECT_MODE_NORMAL;
        rop3 = ~G2D_ROP3_DST;
        break;
    case G2DorReverse: /* src OR NOT dst */
        dst_mode = G2D_SELECT_MODE_NORMAL;
        rop3 = G2D_ROP3_SRC |( ~G2D_ROP3_DST);
        break;
    case G2DcopyInverted: /* NOT src */
        rop3 = ~G2D_ROP3_SRC;
        break;
    case G2DorInverted: /* NOT src OR dst */
        dst_mode = G2D_SELECT_MODE_NORMAL;
        rop3 = (~G2D_ROP3_SRC) | G2D_ROP3_DST;
        break;
    case G2Dnand: /* NOT src OR NOT dst */
        dst_mode = G2D_SELECT_MODE_NORMAL;
        rop3 = (~G2D_ROP3_SRC) | (~G2D_ROP3_DST);
        break;
    case G2Dset: /* 1 */
        src_mode = G2D_SELECT_MODE_FGCOLOR;
        fg_color = 0xFFFFFFFF;
        rop3 = G2D_ROP3_DST;
        break;
    case G2Dnegative:
        bg_color = 0x00FFFFFF;
        rop3 = G2D_ROP3_SRC ^ G2D_ROP3_DST;
        break;
    default:
        break;
    }

    /*Set dst*/
    if(dst_mode != G2D_SELECT_MODE_NORMAL)
    {
        g2d_add_cmd(DST_SELECT_REG, G2D_SELECT_MODE_BGCOLOR);
        g2d_add_cmd(DST_COLOR_MODE_REG, G2D_COLOR_FMT_ARGB8888 | G2D_ORDER_AXRGB);
        g2d_add_cmd(BG_COLOR_REG, bg_color);
    }
    else
    {
        g2d_add_cmd(DST_SELECT_REG, G2D_SELECT_MODE_NORMAL);
        g2d_add_cmd(DST_COLOR_MODE_REG, dst->color_mode);
    }
    g2d_add_cmd(DST_BASE_ADDR_REG, dst->data.bo[0]);
    g2d_add_cmd(DST_STRIDE_REG, dst->stride);

    /*Set src*/
    if(src_mode != G2D_SELECT_MODE_NORMAL)
    {
        g2d_add_cmd(SRC_SELECT_REG, G2D_SELECT_MODE_FGCOLOR);
        g2d_add_cmd(SRC_COLOR_MODE_REG, G2D_COLOR_FMT_ARGB8888 | G2D_ORDER_AXRGB);
        g2d_add_cmd(FG_COLOR_REG, fg_color);
    }
    else
    {
        g2d_add_cmd(SRC_SELECT_REG, G2D_SELECT_MODE_NORMAL);
        g2d_add_cmd(SRC_COLOR_MODE_REG, src->color_mode);
        g2d_add_cmd(SRC_BASE_ADDR_REG, src->data.bo[0]);
        g2d_add_cmd(SRC_STRIDE_REG, src->stride);
        if(src->repeat_mode)
            g2d_add_cmd(SRC_REPEAT_MODE_REG, src->repeat_mode);
    }

    /*Set cmd*/
    if(!_util_get_clip(src, src_x, src_y, width, height, &lt, &rb))
    {
        XDBG_ERROR (MG2D, "[G2D] %s:%d error: invalid geometry\n",__FUNCTION__, __LINE__);
        g2d_reset(0);
        return;
    }
    g2d_add_cmd(SRC_LEFT_TOP_REG, lt.val);
    g2d_add_cmd(SRC_RIGHT_BOTTOM_REG, rb.val);

    if(!_util_get_clip(dst, dst_x, dst_y, width, height, &lt, &rb))
    {
        XDBG_ERROR (MG2D, "[G2D] %s:%d error: invalid geometry\n",__FUNCTION__, __LINE__);
        g2d_reset(0);
        return;
    }
    g2d_add_cmd(DST_LEFT_TOP_REG, lt.val);
    g2d_add_cmd(DST_RIGHT_BOTTOM_REG, rb.val);

    rop4.val = 0;
    rop4.data.unmaskedROP3 = rop3;
    g2d_add_cmd(ROP4_REG, rop4.val);

    g2d_flush();
}

void
util_g2d_copy_with_scale(G2dImage* src, G2dImage* dst,
            int src_x, int src_y, unsigned int src_w, unsigned int src_h,
            int dst_x, int dst_y, unsigned int dst_w, unsigned int dst_h,
            int negative)
{
    G2dROP4Val rop4;
    G2dPointVal pt;
    int bScale;
    double scalex=1.0, scaley=1.0;

    /*Set dst*/
    g2d_add_cmd(DST_SELECT_REG, G2D_SELECT_MODE_BGCOLOR);
    g2d_add_cmd(DST_COLOR_MODE_REG, dst->color_mode);
    g2d_add_cmd(DST_BASE_ADDR_REG, dst->data.bo[0]);
    g2d_add_cmd(DST_STRIDE_REG, dst->stride);

    /*Set src*/
    g2d_add_cmd(SRC_SELECT_REG, G2D_SELECT_MODE_NORMAL);
    g2d_add_cmd(SRC_COLOR_MODE_REG, src->color_mode);
    g2d_add_cmd(SRC_BASE_ADDR_REG, src->data.bo[0]);
    g2d_add_cmd(SRC_STRIDE_REG, src->stride);

    /*Set cmd*/
    if(src_w == dst_w && src_h == dst_h)
        bScale = 0;
    else
    {
        bScale = 1;
        scalex = (double)src_w/(double)dst_w;
        scaley = (double)src_h/(double)dst_h;
    }

    if(src_x < 0)
    {
        src_w += src_x;
        src_x = 0;
    }

    if(src_y < 0)
    {
        src_h += src_y;
        src_y = 0;
    }
    if(src_x+src_w > src->width) src_w = src->width - src_x;
    if(src_y+src_h > src->height) src_h = src->height - src_y;

    if(dst_x < 0)
    {
        dst_w += dst_x;
        dst_x = 0;
    }

    if(dst_y < 0)
    {
        dst_h += dst_y;
        dst_y = 0;
    }
    if(dst_x+dst_w > dst->width) dst_w = dst->width - dst_x;
    if(dst_y+dst_h > dst->height) dst_h = dst->height - dst_y;

    if(src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0)
    {
        XDBG_ERROR (MG2D, "[G2D] error: invalid geometry\n");
        g2d_reset(0);
        return;
    }

    if(negative)
    {
        g2d_add_cmd(BG_COLOR_REG, 0x00FFFFFF);
        rop4.val = 0;
        rop4.data.unmaskedROP3 = G2D_ROP3_SRC^G2D_ROP3_DST;
        g2d_add_cmd(ROP4_REG, rop4.val);
    }
    else
    {
        rop4.val = 0;
        rop4.data.unmaskedROP3 = G2D_ROP3_SRC;
        g2d_add_cmd(ROP4_REG, rop4.val);
    }

    if(bScale)
    {
        g2d_add_cmd(SRC_SCALE_CTRL_REG, G2D_SCALE_MODE_BILINEAR);
        g2d_add_cmd(SRC_XSCALE_REG, G2D_DOUBLE_TO_FIXED(scalex));
        g2d_add_cmd(SRC_YSCALE_REG, G2D_DOUBLE_TO_FIXED(scaley));
    }

    pt.data.x = src_x;
    pt.data.y = src_y;
    pt.val = (pt.data.y << 16) | pt.data.x ;
    g2d_add_cmd(SRC_LEFT_TOP_REG, pt.val);
    pt.data.x = src_x + src_w;
    pt.data.y = src_y + src_h;
    pt.val = (pt.data.y << 16) | pt.data.x ;
    g2d_add_cmd(SRC_RIGHT_BOTTOM_REG, pt.val);


    pt.data.x = dst_x;
    pt.data.y = dst_y;
    pt.val = (pt.data.y << 16) | pt.data.x ;
    g2d_add_cmd(DST_LEFT_TOP_REG, pt.val);
    pt.data.x = dst_x + dst_w;
    pt.data.y = dst_y + dst_h;
    pt.val = (pt.data.y << 16) | pt.data.x ;
    g2d_add_cmd(DST_RIGHT_BOTTOM_REG, pt.val);

    g2d_flush();
}

void
util_g2d_blend(G2dOp op, G2dImage* src, G2dImage* dst,
            int src_x, int src_y,
            int dst_x, int dst_y,
            unsigned int width, unsigned int height)
{
    G2dBitBltCmdVal bitblt;
    G2dBlendFunctionVal blend;
    G2dPointVal pt;

    bitblt.val = 0;
    blend.val = 0;

    /*Set dst*/
    if(op == G2D_OP_SRC || op == G2D_OP_CLEAR)
        g2d_add_cmd(DST_SELECT_REG, G2D_SELECT_MODE_BGCOLOR);
    else
        g2d_add_cmd(DST_SELECT_REG, G2D_SELECT_MODE_NORMAL);
    g2d_add_cmd(DST_COLOR_MODE_REG, dst->color_mode);
    g2d_add_cmd(DST_BASE_ADDR_REG, dst->data.bo[0]);
    g2d_add_cmd(DST_STRIDE_REG, dst->stride);

    /*Set src*/
    g2d_set_src(src);

    /*Set cmd*/
    if(src_x < 0) src_x = 0;
    if(src_y < 0) src_y = 0;
    if(src_x+width > src->width) width = src->width - src_x;
    if(src_y+height > src->height) height = src->height - src_y;

    if(dst_x < 0) dst_x = 0;
    if(dst_y < 0) dst_y = 0;
    if(dst_x+width > dst->width) width = dst->width - dst_x;
    if(dst_y+height > dst->height) height = dst->height - dst_y;

    if(width <= 0 || height <= 0)
    {
        XDBG_ERROR (MG2D, "[G2D] error: invalid geometry\n");
        g2d_reset(0);
        return;
    }

    bitblt.data.alphaBlendMode = G2D_ALPHA_BLEND_MODE_ENABLE;
    blend.val = g2d_get_blend_op(op);
    g2d_add_cmd(BITBLT_COMMAND_REG, bitblt.val);
    g2d_add_cmd(BLEND_FUNCTION_REG, blend.val);

    pt.data.x = src_x;
    pt.data.y = src_y;
    g2d_add_cmd(SRC_LEFT_TOP_REG, pt.val);
    pt.data.x = src_x + width;
    pt.data.y = src_y + height;
    g2d_add_cmd(SRC_RIGHT_BOTTOM_REG, pt.val);


    pt.data.x = dst_x;
    pt.data.y = dst_y;
    g2d_add_cmd(DST_LEFT_TOP_REG, pt.val);
    pt.data.x = dst_x + width;
    pt.data.y = dst_y + height;
    g2d_add_cmd(DST_RIGHT_BOTTOM_REG, pt.val);

    g2d_flush();
}

void
util_g2d_blend_with_scale(G2dOp op, G2dImage* src, G2dImage* dst,
            int src_x, int src_y, unsigned int src_w, unsigned int src_h,
            int dst_x, int dst_y, unsigned int dst_w, unsigned int dst_h,
            int negative)
{
    G2dROP4Val rop4;
    G2dPointVal pt;
    int bScale;
    double scalex=1.0, scaley=1.0;
    G2dRotateVal rotate;
    G2dSrcMaskDirVal dir;
    int rotate_w, rotate_h;
    G2dBitBltCmdVal bitblt;
    G2dBlendFunctionVal blend;

    bitblt.val = 0;
    blend.val = 0;
    rotate.val = 0;
    dir.val = 0;

    if(src_x < 0)
    {
        src_w += src_x;
        src_x = 0;
    }

    if(src_y < 0)
    {
        src_h += src_y;
        src_y = 0;
    }
    if(src_x+src_w > src->width)
        src_w = src->width - src_x;
    if(src_y+src_h > src->height)
        src_h = src->height - src_y;

    if(dst_x < 0)
    {
        dst_w += dst_x;
        dst_x = 0;
    }
    if(dst_y < 0)
    {
        dst_h += dst_y;
        dst_y = 0;
    }
    if(dst_x+dst_w > dst->width)
        dst_w = dst->width - dst_x;
    if(dst_y+dst_h > dst->height)
        dst_h = dst->height - dst_y;

    if(src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0)
    {
        XDBG_ERROR (MG2D, "[G2D] error: invalid geometry\n");
        g2d_reset(0);
        return;
    }

    /*Set dst*/
    if(op == G2D_OP_SRC || op == G2D_OP_CLEAR)
        g2d_add_cmd(DST_SELECT_REG, G2D_SELECT_MODE_BGCOLOR);
    else
        g2d_add_cmd(DST_SELECT_REG, G2D_SELECT_MODE_NORMAL);
    g2d_add_cmd(DST_COLOR_MODE_REG, dst->color_mode);
    g2d_add_cmd(DST_BASE_ADDR_REG, dst->data.bo[0]);
    if (dst->color_mode & G2D_YCbCr_2PLANE)
    {
        if (dst->data.bo[1] > 0)
            g2d_add_cmd(DST_PLANE2_BASE_ADDR_REG, dst->data.bo[1]);
        else
            fprintf(stderr, "[G2D] %s:%d error: second bo is null.\n",__FUNCTION__, __LINE__);
    }
    g2d_add_cmd(DST_STRIDE_REG, dst->stride);

    /*Set src*/
    g2d_add_cmd(SRC_SELECT_REG, G2D_SELECT_MODE_NORMAL);
    g2d_add_cmd(SRC_COLOR_MODE_REG, src->color_mode);
    g2d_add_cmd(SRC_BASE_ADDR_REG, src->data.bo[0]);
    if (src->color_mode & G2D_YCbCr_2PLANE)
    {
        if (src->data.bo[1] > 0)
            g2d_add_cmd(SRC_PLANE2_BASE_ADDR_REG, src->data.bo[1]);
        else
            fprintf(stderr, "[G2D] %s:%d error: second bo is null.\n",__FUNCTION__, __LINE__);
    }
    g2d_add_cmd(SRC_STRIDE_REG, src->stride);

    /*Set cmd*/
    rotate_w = (src->rotate_90)?dst_h:dst_w;
    rotate_h = (src->rotate_90)?dst_w:dst_h;

    if(src_w == rotate_w && src_h == rotate_h)
        bScale = 0;
    else
    {
        bScale = 1;
        scalex = (double)src_w/(double)rotate_w;
        scaley = (double)src_h/(double)rotate_h;
    }

    if(negative)
    {
        g2d_add_cmd(BG_COLOR_REG, 0x00FFFFFF);
        rop4.val = 0;
        rop4.data.unmaskedROP3 = G2D_ROP3_SRC^G2D_ROP3_DST;
        g2d_add_cmd(ROP4_REG, rop4.val);
    }
    else
    {
        rop4.val = 0;
        rop4.data.unmaskedROP3 = G2D_ROP3_SRC;
        g2d_add_cmd(ROP4_REG, rop4.val);
    }

    if(bScale)
    {
        g2d_add_cmd(SRC_SCALE_CTRL_REG, G2D_SCALE_MODE_BILINEAR);
        g2d_add_cmd(SRC_XSCALE_REG, G2D_DOUBLE_TO_FIXED(scalex));
        g2d_add_cmd(SRC_YSCALE_REG, G2D_DOUBLE_TO_FIXED(scaley));
    }

    if(src->rotate_90 || src->xDir || src->yDir)
    {
        rotate.data.srcRotate = src->rotate_90;
        dir.data.dirSrcX = src->xDir;
        dir.data.dirSrcY = src->yDir;
    }

    pt.data.x = src_x;
    pt.data.y = src_y;
    pt.val = (pt.data.y << 16) | pt.data.x ;
    g2d_add_cmd(SRC_LEFT_TOP_REG, pt.val);
    pt.data.x = src_x + src_w;
    pt.data.y = src_y + src_h;
    pt.val = (pt.data.y << 16) | pt.data.x ;
    g2d_add_cmd(SRC_RIGHT_BOTTOM_REG, pt.val);

    pt.data.x = dst_x;
    pt.data.y = dst_y;
    pt.val = (pt.data.y << 16) | pt.data.x ;
    g2d_add_cmd(DST_LEFT_TOP_REG, pt.val);
    pt.data.x = dst_x + dst_w;
    pt.data.y = dst_y + dst_h;
    pt.val = (pt.data.y << 16) | pt.data.x ;
    g2d_add_cmd(DST_RIGHT_BOTTOM_REG, pt.val);

    if(op != G2D_OP_SRC || op != G2D_OP_CLEAR)
    {
        bitblt.data.alphaBlendMode = G2D_ALPHA_BLEND_MODE_ENABLE;
        blend.val = g2d_get_blend_op(op);
        g2d_add_cmd(BITBLT_COMMAND_REG, bitblt.val);
        g2d_add_cmd(BLEND_FUNCTION_REG, blend.val);
    }

    g2d_add_cmd(ROTATE_REG, rotate.val);
    g2d_add_cmd(SRC_MASK_DIRECT_REG, dir.val);

    g2d_flush();
}

void
util_g2d_composite(G2dOp op, G2dImage* src, G2dImage* mask, G2dImage* dst,
            int src_x, int src_y,
            int mask_x, int mask_y,
            int dst_x, int dst_y,
            unsigned int width, unsigned int height)
{
    G2dBitBltCmdVal bitblt;
    G2dBlendFunctionVal blend;
    G2dRotateVal rotate;
    G2dSrcMaskDirVal dir;
    G2dPointVal lt, rb;

    bitblt.val = 0;
    blend.val = 0;
    rotate.val = 0;
    dir.val = 0;

    /*Set dst*/
    if(op == G2D_OP_SRC || op == G2D_OP_CLEAR)
        g2d_add_cmd(DST_SELECT_REG, G2D_SELECT_MODE_BGCOLOR);
    else
        g2d_add_cmd(DST_SELECT_REG, G2D_SELECT_MODE_NORMAL);
    g2d_add_cmd(DST_COLOR_MODE_REG, dst->color_mode);
    g2d_add_cmd(DST_BASE_ADDR_REG, dst->data.bo[0]);

    if (dst->color_mode & G2D_YCbCr_2PLANE)
    {
        if (dst->data.bo[1] > 0)
            g2d_add_cmd(DST_PLANE2_BASE_ADDR_REG, dst->data.bo[1]);
        else
            XDBG_ERROR (MG2D, "[G2D] %s:%d error: second bo is null.\n",__FUNCTION__, __LINE__);
    }

    g2d_add_cmd(DST_STRIDE_REG, dst->stride);

    /*Set src*/
    g2d_set_src(src);
    if(src->repeat_mode)
    {
        g2d_add_cmd(SRC_REPEAT_MODE_REG, src->repeat_mode);
    }

    if(src->scale_mode)
    {
        g2d_add_cmd(SRC_XSCALE_REG, src->xscale);
        g2d_add_cmd(SRC_YSCALE_REG, src->yscale);
        g2d_add_cmd(SRC_SCALE_CTRL_REG, src->scale_mode);
    }

    if(src->rotate_90 || src->xDir || src->yDir)
    {
        rotate.data.srcRotate = src->rotate_90;
        dir.data.dirSrcX = src->xDir;
        dir.data.dirSrcY = src->yDir;
    }

    /*Set Mask*/
    if(mask)
    {
        bitblt.data.maskingEn = 1;
        g2d_set_mask(mask);

        if(mask->repeat_mode)
        {
            g2d_add_cmd(MASK_REPEAT_MODE_REG, mask->repeat_mode);
        }

        if(mask->scale_mode)
        {
            g2d_add_cmd(MASK_XSCALE_REG, mask->xscale);
            g2d_add_cmd(MASK_YSCALE_REG, mask->yscale);
            g2d_add_cmd(MASK_SCALE_CTRL_REG, mask->scale_mode);
        }

        if(mask->rotate_90 || mask->xDir || mask->yDir)
        {
            rotate.data.maskRotate = mask->rotate_90;
            dir.data.dirMaskX = mask->xDir;
            dir.data.dirMaskY = mask->yDir;
        }
    }

    /*Set Geometry*/
    if(!_util_get_clip(src, src_x, src_y, width, height, &lt, &rb))
    {
        XDBG_ERROR (MG2D, "[G2D] %s:%d error: invalid geometry\n",__FUNCTION__, __LINE__);
        g2d_reset(0);
        return;
    }
    g2d_add_cmd(SRC_LEFT_TOP_REG, lt.val);
    g2d_add_cmd(SRC_RIGHT_BOTTOM_REG, rb.val);

    if(mask)
    {
        if(!_util_get_clip(mask, mask_x, mask_y, width, height, &lt, &rb))
        {
            XDBG_ERROR (MG2D, "[G2D] %s:%d error: invalid geometry\n",__FUNCTION__, __LINE__);
            g2d_reset(0);
            return;
        }
        g2d_add_cmd(MASK_LEFT_TOP_REG, lt.val);
        g2d_add_cmd(MASK_RIGHT_BOTTOM_REG, rb.val);
    }

    if(!_util_get_clip(dst, dst_x, dst_y, width, height, &lt, &rb))
    {
        XDBG_ERROR (MG2D, "[G2D] %s:%d error: invalid geometry\n",__FUNCTION__, __LINE__);
        g2d_reset(0);
        return;
    }
    g2d_add_cmd(DST_LEFT_TOP_REG, lt.val);
    g2d_add_cmd(DST_RIGHT_BOTTOM_REG, rb.val);

    bitblt.data.alphaBlendMode = G2D_ALPHA_BLEND_MODE_ENABLE;
    blend.val = g2d_get_blend_op(op);
    g2d_add_cmd(BITBLT_COMMAND_REG, bitblt.val);
    g2d_add_cmd(BLEND_FUNCTION_REG, blend.val);
    g2d_add_cmd(ROTATE_REG, rotate.val);
    g2d_add_cmd(SRC_MASK_DIRECT_REG, dir.val);

    g2d_flush();
}
