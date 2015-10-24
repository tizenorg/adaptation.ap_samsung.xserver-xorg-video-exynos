#include "xcb_api.h"
get_image_attr_s
get_image_attr(xcb_connection_t * xcb_conn_p, xcb_xv_port_t xv_port,
               uint32_t fourcc_id, uint16_t width, uint16_t height)
{
    get_image_attr_s ret = {};
    xcb_generic_error_t  *error_p = NULL;
    xcb_xv_query_image_attributes_reply_t * xv_attr_reply_p = NULL;

    if (!xcb_conn_p || xv_port == XCB_XV_BAD_PORT)
    {
        ret.error.error_str = "Wrong arguments";
        ret.error.error_code = 0;
        goto close;
    }

    xcb_xv_query_image_attributes_cookie_t xv_attr_ck =
    xcb_xv_query_image_attributes (xcb_conn_p  /**< */,
                                   xv_port  /**< */,
                                   fourcc_id  /**< */,
                                   width  /**< */,
                                   height  /**< */);
    xv_attr_reply_p =
    xcb_xv_query_image_attributes_reply (xcb_conn_p  /**< */,
                                         xv_attr_ck  /**< */,
                                         &error_p  /**< */);
    if (error_p)
    {
        ret.error.error_str = "Can't query image attr";
        ret.error.error_code = error_p->error_code;
        goto close;
    }

    if (xv_attr_reply_p->data_size == 0)
    {
        ret.error.error_str = "Wrong input data format";
        ret.error.error_code = 0;
        goto close;
    }

    const uint32_t *offsets=
            xcb_xv_query_image_attributes_offsets (xv_attr_reply_p);
    const uint32_t *pitches=
            xcb_xv_query_image_attributes_pitches (xv_attr_reply_p);
    ret.image.num_planes = xv_attr_reply_p->num_planes;
    ret.image.full_size =  xv_attr_reply_p->data_size;
    ret.image.fourcc_id = fourcc_id;
    ret.image.width = xv_attr_reply_p->width;
    ret.image.height = xv_attr_reply_p->height;
    switch (ret.image.num_planes)
    {
    case 3:
        ret.image.sizes[0] = offsets[1];
        ret.image.sizes[1] = offsets[2] - offsets[1];
        ret.image.sizes[2] = ret.image.full_size - offsets[2];
        break;
    case 2:
        ret.image.sizes[1] = ret.image.full_size - offsets[1];
        ret.image.sizes[0] = offsets[1];
        break;
    case 1:
        ret.image.sizes[0] = ret.image.full_size;
        break;
    default:
        ret.error.error_str = "Wrong input data format";
        goto close;
        break;
    }
    int i;
    for (i = 0; i < ret.image.num_planes; ++i)
    {
        ret.image.offsets[i] = offsets[i];
        ret.image.pitches[i] = pitches[i];
    }

close:

    if (error_p)
        free(error_p);
    if (xv_attr_reply_p)
        free(xv_attr_reply_p);

    return ret;
}

xcb_void_cookie_t
put_image (xcb_connection_t * xcb_conn_p, xcb_xv_port_t xv_port,
           xcb_window_t window_id, xcb_gcontext_t gc_id, frame_attr_s *frame_p)
{
    xcb_void_cookie_t ret = {};
    ret = xcb_xv_shm_put_image_checked (
                xcb_conn_p, xv_port, window_id, gc_id,
                frame_p->segment, frame_p->image_p->fourcc_id, 0,
                frame_p->src_crop_p->x, frame_p->src_crop_p->y,
                frame_p->src_crop_p->width, frame_p->src_crop_p->height,
                frame_p->dst_crop_p->x, frame_p->dst_crop_p->y,
                frame_p->dst_crop_p->width, frame_p->dst_crop_p->height,
                frame_p->image_p->width, frame_p->image_p->height, 0);
    return ret;
}

get_xv_port_s
open_adaptor (xcb_connection_t *xcb_conn_p, xcb_window_t window_id)
{
    xcb_generic_error_t  *error_p = NULL;
    get_xv_port_s ret = {};
    xcb_xv_adaptor_info_iterator_t xv_adaptor_iter;
    xcb_xv_grab_port_reply_t *xv_grab_port_p = NULL;
    xcb_xv_query_adaptors_cookie_t xv_adaptor_ck =
            xcb_xv_query_adaptors (xcb_conn_p, window_id);
    xcb_xv_query_adaptors_reply_t *xv_adaptors_list_p =
            xcb_xv_query_adaptors_reply (xcb_conn_p, xv_adaptor_ck, &error_p);
    if (error_p)
    {
        ret.error.error_str = "Can't query xv adaptor";
        ret.error.error_code = error_p->error_code;
        goto close;
    }

    for (xv_adaptor_iter = xcb_xv_query_adaptors_info_iterator (xv_adaptors_list_p);
         xv_adaptor_iter.rem > 0;
         xcb_xv_adaptor_info_next (&xv_adaptor_iter))
    {
        if (!(xv_adaptor_iter.data->type & XCB_XV_TYPE_IMAGE_MASK))
            continue;
        int i;
        for (i = 0; i < xv_adaptor_iter.data->num_ports; i++)
        {
            xcb_xv_port_t xv_port = xv_adaptor_iter.data->base_id + i;
            xcb_xv_grab_port_cookie_t xv_grab_port_c =
                    xcb_xv_grab_port (xcb_conn_p, xv_port, XCB_CURRENT_TIME);
            xv_grab_port_p =
                    xcb_xv_grab_port_reply (xcb_conn_p, xv_grab_port_c, &error_p);
            if (error_p)
            {
                ret.error.error_str = "Can't query xv adaptor";
                ret.error.error_code = error_p->error_code;
                goto close;
            }

            if (xv_grab_port_p && xv_grab_port_p->result == 0)
            {
                ret.xv_port_id = xv_port;
                break;
            }
        }
        if (ret.xv_port_id != 0)
        {
            break;
        }
    }
    
close:
    if (xv_adaptors_list_p)
        free(xv_adaptors_list_p);
    if (xv_grab_port_p)
        free(xv_grab_port_p);
    if (error_p)
        free(error_p);

    return ret;
}

get_gc_s
create_gc (xcb_connection_t *xcb_conn_p, xcb_drawable_t drawable_id, xcb_screen_t * screen_p)
{
    get_gc_s ret = {};
    xcb_void_cookie_t gc_ck;
    xcb_generic_error_t  *error_p = NULL;
    uint32_t values[2];
    ret.gc_id = xcb_generate_id (xcb_conn_p);
    uint32_t mask = XCB_GC_GRAPHICS_EXPOSURES;
    values[0] = 0;
//    values[0] = screen_p->white_pixel;
    gc_ck = xcb_create_gc_checked (xcb_conn_p, ret.gc_id, drawable_id, mask, values);

    error_p = xcb_request_check (xcb_conn_p, gc_ck);
    if (error_p)
    {
      ret.error.error_str = "Can't create graphic context";
      ret.error.error_code = error_p->error_code;
      goto close;
    }
close:
    if (error_p)
        free(error_p);
    return ret;
}

get_window_s
create_window (xcb_connection_t *xcb_conn_p, xcb_screen_t * screen_p)
{
    get_window_s ret = {};
    ret.window_id = xcb_generate_id (xcb_conn_p);
    uint32_t              mask;
    uint32_t              values[3];
    xcb_generic_error_t *error_p = NULL;
    mask = XCB_CW_EVENT_MASK;
//    values[0] = screen_p->black_pixel;
    values[0] = XCB_EVENT_MASK_EXPOSURE;
    xcb_void_cookie_t window_ck =
            xcb_create_window_checked (xcb_conn_p,
                                       screen_p->root_depth,
                                       ret.window_id, screen_p->root,
                                       0, 0, screen_p->width_in_pixels,
                                       screen_p->height_in_pixels,
                                       0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                                       screen_p->root_visual,
                                       mask, values);

    error_p = xcb_request_check (xcb_conn_p, window_ck);
    if (error_p)
    {
        ret.error.error_str = "Can't create window";
        ret.error.error_code = error_p->error_code;
        goto close;
    }

close:
    if (error_p)
        free(error_p);
    return ret;
}

static xcb_format_t *
find_format_by_depth (const xcb_setup_t *setup, uint8_t depth)
{
  xcb_format_t *fmt = xcb_setup_pixmap_formats(setup);
  xcb_format_t *fmtend = fmt + xcb_setup_pixmap_formats_length(setup);
  for(; fmt != fmtend; ++fmt)
      if(fmt->depth == depth)
      return fmt;
  return 0;
}

#if 1
xcb_void_cookie_t
make_transparent (xcb_connection_t *xcb_conn_p, xcb_screen_t * screen_p,
                  xcb_drawable_t drawable_id, xcb_gcontext_t gc_id)
{
    xcb_format_t *fmt = find_format_by_depth(xcb_get_setup (xcb_conn_p), 32);

    uint32_t base = 1920 * fmt->bits_per_pixel;
    uint32_t pad = fmt->scanline_pad >> 3;
    uint32_t b = base + pad - 1;
        /* faster if pad is a power of two */
        if (((pad - 1) & pad) == 0)
          b = b & -pad;
        else
          b = b - b % pad;
    uint32_t size = 1080 * b;
    uint8_t *data = (uint8_t *)calloc (1, size);
    return
            xcb_put_image_checked (xcb_conn_p, XCB_IMAGE_FORMAT_Z_PIXMAP, drawable_id, gc_id,
                  screen_p->width_in_pixels, screen_p->height_in_pixels, 0, 0,
                  0, 32, size, data);
}
#endif
