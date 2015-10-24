#ifndef XV_API_H
#define XV_API_H
#include <xcb/xcb.h>
#include <xcb/xv.h>
#include <xcb/xfixes.h>

#include <xcb/xcb_util.h>
#include "xv_test.h"

typedef struct
{
    uint32_t width;
    uint32_t height;
} size_s;

typedef struct
{
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
} rectangle_s;

typedef struct
{
    uint32_t fourcc_id;
    uint16_t num_planes;
    size_t full_size;
    size_t sizes[3];
    uint32_t offsets[3];
    uint32_t pitches[3];
    uint32_t width;
    uint32_t height;
} image_attr_s;

typedef struct
{
    rectangle_s *src_crop_p;
    rectangle_s *dst_crop_p;
    image_attr_s *image_p;
    xcb_shm_seg_t segment;
} frame_attr_s;

typedef struct
{
    xcb_xv_port_t xv_port_id;
    error_s error;
} get_xv_port_s;

typedef struct
{
    error_s error;
    image_attr_s image;
} get_image_attr_s;

typedef struct
{
    xcb_window_t window_id;
    error_s error;
} get_window_s;

typedef struct
{
    xcb_gcontext_t gc_id;
    error_s error;
} get_gc_s;

get_gc_s create_gc (xcb_connection_t *xcb_conn_p, xcb_drawable_t drawable_id, xcb_screen_t * screen_p);
get_window_s create_window (xcb_connection_t *xcb_conn_p, xcb_screen_t * screen_p);


get_image_attr_s get_image_attr(xcb_connection_t * xcb_conn_p, xcb_xv_port_t xv_port,
                            uint32_t fourcc_id, uint16_t width, uint16_t height);
get_xv_port_s    open_adaptor (xcb_connection_t *xcb_conn_p, xcb_window_t window_id);
xcb_void_cookie_t put_image (xcb_connection_t * xcb_conn_p, xcb_xv_port_t xv_port,
           xcb_window_t window_id, xcb_gcontext_t gc_id, frame_attr_s *frame_p);

xcb_void_cookie_t make_transparent (xcb_connection_t *xcb_conn_p, xcb_screen_t * screen_p,
                                    xcb_drawable_t drawable_id, xcb_gcontext_t gc_id);
#endif // XV_API_H
