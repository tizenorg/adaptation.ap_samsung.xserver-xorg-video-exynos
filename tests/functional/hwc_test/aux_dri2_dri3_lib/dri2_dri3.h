
#ifndef _DRI2_DRI3_H
#define _DRI2_DRI3_H

#include <xcb/xcb.h>
#include <tbm_bufmgr.h>

#define BPP (32)         // bits per pixel
#define DEPTH (24)

#define DRI2_DRI3_TRUE  0x1
#define DRI2_DRI3_FALSE 0x0

// modes for use in init_dri2_dri3() function
#define DRI2_MODE         0x0
#define DRI3_MODE         0x1
#define DRI3_PRESENT_MODE 0x2
#define PRESENT_MODE      0x4

// describes buffer for direct access
typedef struct bo_t
{
    tbm_bo bo;      // these bos represents such gem object as pxmap in graphics_ctx_t structure
    uint32_t width;
    uint32_t height;
    uint32_t stride;    
} bo_t;

typedef void (*xcb_clear_func_ptr)(xcb_drawable_t drawable, uint32_t draw_w, uint32_t draw_h);
typedef void (*xcb_draw_func_ptr)(xcb_drawable_t drawable, uint32_t draw_w, uint32_t draw_h);

typedef void (*raw_clear_func_ptr)(const bo_t* bo_e);
typedef void (*raw_draw_func_ptr)(const bo_t* bo_e);

typedef struct draw_funcs_t
{
    xcb_clear_func_ptr xcb_clear;
    xcb_draw_func_ptr  xcb_draw;
    raw_clear_func_ptr raw_clear;
    raw_draw_func_ptr  raw_draw;
} draw_funcs_t;


// this function gets copy of passed arguments !!!
// draw_funcs - set of callback function to drawing, which functions to use
//              will be dedicated according to mode value
// mode - mode in which dri2_dri3 module will work
// window - window's id in which drawing will be occured
// num_of_bufs - number of buffers for use with Present extension
// Note: if you use PRESENT_MODE or DRI3_PRESENT_MODE size of pixmaps or bos
//       mayn't be equal size of window, now it isn't used, but maybe in future...
//       therefore xcb_clear_func_ptr and xcb_draw_func_ptr functions have passed
//       real size of pixmaps or bos as second and third arguments
__attribute__ ((visibility ("default")))
int init_dri2_dri3 (xcb_connection_t* dpy, draw_funcs_t* draw_funcs, int mode, xcb_window_t window,
                    uint32_t num_of_bufs);

// any dri2_dri3 module's functions return error code or message,
// only DRI2_DRI3_TRUE or DRI2_DRI3_FALSE
// use this function, if you need, to obtain more detail information about error
// you must free memory, pointed by returned value
__attribute__ ((visibility ("default")))
char* get_last_error (void);

// you can use this auxiliary functions to direct drawing in bo

// draws line(s) into tbm_bo (Bresenham algorithm)
// bo_e - pointer to struct returned by raw_draw or raw_clear function
// points_num - number of elements of xcb_point_t array
// points - array of points, which will be used to draw line(s)
// color - color of line(s)
// line(s) is(are) drawn between each pair of points
// any attempts to draw line(s), which is(are) out of pixmap/bo size are rejected
__attribute__ ((visibility ("default")))
int raw_draw_line (const bo_t* bo_e, uint32_t points_num, const xcb_point_t* points,
                   unsigned int color);

// draw filled rect into tbm_bo
// bo_e - pointer to struct returned by raw_draw or raw_clear function
// rect_num - amount of rectangles to draw (in current implemantation must be '1')
// rects - array of rectangles to draw
// color - color of rectangle
// any attempts to draw rectangle, which is out of pixmap/bo size are rejected
// draw only one rectangle !!!
__attribute__ ((visibility ("default")))
int raw_fill_rect (const bo_t* bo_e, uint32_t rect_num, const xcb_rectangle_t* rects,
                   unsigned int color);

#endif
