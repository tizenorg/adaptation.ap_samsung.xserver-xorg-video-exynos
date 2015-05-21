/**************************************************************************

 rotated snowflake

 Copyright 2010 - 2015 Samsung Electronics co., Ltd. All Rights Reserved.

 Contact: Roman Peresipkyn <r.peresipkyn@samsung.com>
 Contact: Sergey Sizonov <s.sizonov@samsung.com>

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
#include <stdlib.h>

#include <unistd.h>
#include <math.h>
#include <time.h>

#include "dri2_dri3.h"


#define DEPTH (24)

#define ROTATE_SPEED (-180.0) // degrees per second
#define MARGIN (10)
#define SNOWFLAKE_POINTS (36)

// if you want to do some specific for your app - implement yourself's cmd parsing
extern void cmd_parse (int argc, char* const argv[]);


typedef struct point_t
{
    double x;
    double y;
} point;

typedef struct matrix_t
{
    double a1;
    double a2;
    double b1;
    double b2;
} matrix;

xcb_connection_t* dpy = NULL;
xcb_screen_t* screen = NULL;
xcb_rectangle_t wnd_pos = {100, 100, 200, 200};

// mode of work (dri2/dri3+present)
int mode = DRI3_PRESENT_MODE;   // by default dri3 + present will be used
int stop;
double rotate_speed = ROTATE_SPEED;
uint32_t present_bufs = 3;

// matrix for rotation
matrix current_rot;

// our mesh
const point snowflake[SNOWFLAKE_POINTS] = {
        // up-down
        {0.0, -4.0 * 0.25}, {0.0, 4.0 * 0.25},

        // left-right
        {-4.0 * 0.25, 0.0}, {4.0 * 0.25, 0.0},

        // upleft-downright
        {-3.0 * 0.25, 3.0 * 0.25}, {3.0 * 0.25, -3.0 * 0.25},

        // upright-downleft
        {3.0 * 0.25, 3.0 * 0.25}, {-3.0 * 0.25, -3.0 * 0.25},

        // branch up-right
        {2.0 * 0.25, 3.0 * 0.25}, {2.0 * 0.25, 2.0 * 0.25}, {3.0 * 0.25, 2.0 * 0.25},

        // branch up
        {-1.0 * 0.25, 4.0 * 0.25}, {0.0, 3.0 * 0.25}, {1.0 * 0.25, 4.0 * 0.25},

        // branch up-left
        {-2.0 * 0.25, 3.0 * 0.25}, {-2.0 * 0.25, 2.0 * 0.25}, {-3.0 * 0.25, 2.0 * 0.25},

        // branch left
        {-4.0 * 0.25, 1.0 * 0.25}, {-3.0 * 0.25, 0.0 * 0.25}, {-4.0 * 0.25, -1.0 * 0.25},

        // branch down-left
        {-3.0 * 0.25, -2.0 * 0.25}, {-2.0 * 0.25, -2.0 * 0.25}, {-2.0 * 0.25, -3.0 * 0.25},

        // branch down
        {-1.0 * 0.25, -4.0 * 0.25}, {0.0 * 0.25, -3.0 * 0.25}, {1.0 * 0.25, -4.0 * 0.25},

        // branch down-right
        {2.0 * 0.25, -3.0 * 0.25}, {2.0 * 0.25, -2.0 * 0.25}, {3.0 * 0.25, -2.0 * 0.25},

        // branch right
        {4.0 * 0.25, -1.0 * 0.25}, {3.0 * 0.25, 0.0 * 0.25}, {4.0 * 0.25, 1.0 * 0.25},

        //
        {-1.5 * 0.25, 0.0 * 0.25}, {0.0 * 0.25, 1.5 * 0.25}, {1.5 * 0.25, 0.0 * 0.25}, {0.0 * 0.25, -1.5 * 0.25}
};

//
//===================================================================
inline void
set_rotation_matrix (double alpha, matrix* mtrx)
{
    mtrx->a1 = cos (alpha);
    mtrx->a2 = -sin (alpha);
    mtrx->b1 = -mtrx->a2;
    mtrx->b2 = mtrx->a1;
}


//
//===================================================================
void
rotate (void)
{
    #define REFRESHE_TIME (16.0) // in ms

    static double angle;
    struct timespec current;
    static struct timespec prev;
    double time_diff;

    clock_gettime (CLOCK_REALTIME, &current);

    // get difference in nanoseconds
    time_diff = (double)( ( current.tv_sec - prev.tv_sec ) * 1000000000 +
                          ( current.tv_nsec - prev.tv_nsec ) );

    if (time_diff / 1000000.0 > REFRESHE_TIME)
    {
        prev = current;

        angle += rotate_speed * 2.0 * M_PI / 360.0 * (REFRESHE_TIME/1000.0);

        set_rotation_matrix (angle, &current_rot);
    }

    #undef REFRESHE_TIME
}

//
//===================================================================
xcb_window_t
create_window (uint32_t background)
{
    uint32_t window_mask;
    uint32_t window_values[2];
    xcb_window_t wnd;

    window_mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
    window_values[0] = background;
    window_values[1] = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_BUTTON_PRESS;

    wnd = xcb_generate_id (dpy);
    xcb_create_window (dpy, DEPTH, wnd, screen->root, wnd_pos.x, wnd_pos.y,
                       wnd_pos.width, wnd_pos.height, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                       screen->root_visual, window_mask, window_values);

    // hwc-sample and square_bubbles manipulate only named windows
    xcb_change_property (dpy, XCB_PROP_MODE_REPLACE, wnd, XCB_ATOM_WM_NAME,
                         XCB_ATOM_STRING, 8, strlen ("rotated snowflake"), "rotated snowflake");

    // Make window visible
    xcb_map_window (dpy, wnd);

    return wnd;
}

//
//===================================================================
void
raw_clear (const bo_t* bo_e)
{
    xcb_rectangle_t rect;

    if (!bo_e)
    {
        printf ("raw_clear: invalid parameters.\n");
        return;
    }

    rect.x = 0;
    rect.y = 0;
    rect.width = wnd_pos.width;
    rect.height = wnd_pos.height;

    // simple draw black rectangle with size as window has
    raw_fill_rect (bo_e, 1, &rect, screen->black_pixel);
}

//
//===================================================================
void
raw_draw (const bo_t* bo_e)
{
    int i;
    point temp[SNOWFLAKE_POINTS];
    xcb_point_t xcb_points[SNOWFLAKE_POINTS + 1];

    if (!bo_e)
    {
        printf ("raw_draw: invalid parameters.\n");
        return;
    }

    for (i = 0; i < SNOWFLAKE_POINTS; i++)
    {
        // calculate point's coords (in OpenGL world) of mesh taking in account
        // current rotate angle (current_rot matrix)
        temp[i].x = snowflake[i].x * current_rot.a1 + snowflake[i].y * current_rot.a2;
        temp[i].y = snowflake[i].x * current_rot.b1 + snowflake[i].y * current_rot.b2;

        // convert coordinates into X world
        xcb_points[i].x = MARGIN + (1.0 / 2.0) * (temp[i].x + 1.0) * (wnd_pos.width - 2 * MARGIN);
        xcb_points[i].y = MARGIN + (-1.0 / 2.0) * (temp[i].y - 1.0) * (wnd_pos.height - 2 * MARGIN);
    }

    raw_draw_line (bo_e, 2, &xcb_points[0], screen->white_pixel);
    raw_draw_line (bo_e, 2, &xcb_points[2], screen->white_pixel);
    raw_draw_line (bo_e, 2, &xcb_points[4], screen->white_pixel);
    raw_draw_line (bo_e, 2, &xcb_points[6], screen->white_pixel);

    raw_draw_line (bo_e, 3, &xcb_points[8], screen->white_pixel);
    raw_draw_line (bo_e, 3, &xcb_points[11], screen->white_pixel);
    raw_draw_line (bo_e, 3, &xcb_points[14], screen->white_pixel);
    raw_draw_line (bo_e, 3, &xcb_points[17], screen->white_pixel);
    raw_draw_line (bo_e, 3, &xcb_points[20], screen->white_pixel);
    raw_draw_line (bo_e, 3, &xcb_points[23], screen->white_pixel);
    raw_draw_line (bo_e, 3, &xcb_points[26], screen->white_pixel);
    raw_draw_line (bo_e, 3, &xcb_points[29], screen->white_pixel);
    raw_draw_line (bo_e, 3, &xcb_points[29], screen->white_pixel);

    xcb_points[SNOWFLAKE_POINTS] = xcb_points[32];  // loop
    raw_draw_line (bo_e, 5, &xcb_points[32], screen->white_pixel);

    if (stop) return;

    rotate ();
}

// parse command line
//========================================================================
void
cmd_parse_inner (int argc, char* const argv[])
{
    int i;

    if (argc < 2) return;

    // if we want to set rotate speed.
    for (i = 1; i < argc; i++)
    {
        if (!strcmp (argv[i], "-rot_speed") && (argc - i > 1))
        {
            sscanf (argv[i + 1], "%lf", &rotate_speed);
            break;
        }
    }

    cmd_parse (argc, argv);
 }

//
//===================================================================
int
main (int argc, char** argv)
{
    xcb_generic_event_t* event = NULL;
    xcb_window_t main_wnd;
    draw_funcs_t draw_func;
    int res;

    cmd_parse_inner (argc, argv);

    dpy = xcb_connect (NULL, NULL);

    // get first screen of display
    screen = xcb_setup_roots_iterator (xcb_get_setup (dpy)).data;

    main_wnd = create_window (0xff);

    draw_func.raw_clear = raw_clear;
    draw_func.raw_draw = raw_draw;
    draw_func.xcb_clear = NULL;
    draw_func.xcb_draw = NULL;

    res = init_dri2_dri3 (dpy, &draw_func, mode, main_wnd, present_bufs);
    if (res)
    {
        printf ("init_dri2_dri3: %s.\n", get_last_error());
        exit (1);
    }

    printf ("before xcb loop.\n");

    while (1)
    {
        xcb_flush (dpy);
        event = xcb_wait_for_event (dpy);
        if (!event) break;

        switch (event->response_type)
        {
            case XCB_EXPOSE:
                printf ("XCB_MAP_WINDOW\n");
            break;

            case XCB_BUTTON_PRESS:
                stop ^= 1;
            break;

            default:
            break;
        }
    }

    return 0;
}
