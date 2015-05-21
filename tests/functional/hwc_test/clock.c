#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <unistd.h>
#include <math.h>

#include "dri2_dri3.h"


#define DEGREE_PER_SECOND (6)


// if you want to do some specific for your app - implement yourself's cmd parsing
extern void cmd_parse (int argc, char* const argv[]);


typedef struct _point
{
    int x, y;
    int color;
} PointRec, *PointPtr;

typedef struct _circle
{
    PointRec center;
    int R;
} CircleRec, *CirclePtr;

typedef struct _warch
{
    CircleRec crcl;
    int time;
} WatchRec, *WatchPtr;


xcb_connection_t* dpy = NULL;
xcb_screen_t* screen = NULL;
xcb_rectangle_t wnd_pos = {100, 100, 200, 200};
\
// mode of work (dri2/dri3+present)
int mode = DRI3_PRESENT_MODE;   // by default dri3 + present will be used
int stop;
WatchRec watch;
uint32_t present_bufs = 3;

inline void
set_point(PointPtr p, uint32_t *map, uint32_t pitch, uint32_t size)
{
    unsigned int off = (pitch * p->y + p->x);
    if (off < size && off >= 0)
        map[off] = p->color;
}

inline void
draw_point(PointPtr p, uint32_t *map, uint32_t pitch, uint32_t size)
{
    set_point(p, map, pitch, size);

    p->x += 1;
    set_point(p, map, pitch, size);
    p->x -= 1;

    p->y += 1;
    set_point(p, map, pitch, size);
    p->y -= 1;

    p->x -= 1;
    set_point(p, map, pitch, size);
    p->x += 1;

    p->y -= 1;
    set_point(p, map, pitch, size);
    p->y += 1;
}

void
draw_circle(CirclePtr c, uint32_t *map, uint32_t pitch, uint32_t size)
{
    double x0 = c->center.x;
    double y0 = c->center.y;
    double R = c->R;

    PointRec p = { 0, 0, c->center.color };

    double fi;
    int a;
    for (a = 0; a < 360; a += DEGREE_PER_SECOND)
    {
        fi = M_PI / 180 * a;
        p.x = (int) (x0 + R * cos(fi));
        p.y = (int) (y0 + R * sin(fi));
        draw_point(&p, map, pitch, size);
    }
}

void
draw_watch(WatchPtr w, uint32_t *map, uint32_t pitch, uint32_t size)
{
    draw_circle(&w->crcl, map, pitch, size);

    //drawing hand of the clock, use the change of radius
    PointRec p = { 0, 0, w->crcl.center.color };

    double fi = M_PI / 180 * (w->time * DEGREE_PER_SECOND);

    int R;
    for (R = 0; R < (w->crcl.R - 3); R += 10)
    {
        p.x = (int) (w->crcl.center.x + R * cos(fi));
        p.y = (int) (w->crcl.center.y + R * sin(fi));
        draw_point(&p, map, pitch, size);
    }
}

/*
//
//===================================================================
void
rotate (void)
{
    #define REFRESHE_TIME (16.0) // in ms

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

        watch.time++;
    }

    #undef REFRESHE_TIME
}
*/

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
    tbm_bo_handle hndl;

    if (!bo_e)
    {
        printf ("raw_draw: invalid parameters.\n");
        return;
    }

    hndl = tbm_bo_map (bo_e->bo, TBM_DEVICE_CPU, TBM_OPTION_WRITE);
    if (!hndl.ptr)
    {
        printf ("raw_draw_line: Error while tbm_bo_map.\n");
        exit (1);
    }

    memset (hndl.ptr, 0, bo_e->stride * bo_e->height);
    draw_watch (&watch, (uint32_t*)hndl.ptr, bo_e->stride/4,
                tbm_bo_size (bo_e->bo)/4);

    if (!stop)
        watch.time++;//rotate ();

    tbm_bo_unmap (bo_e->bo);
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

    cmd_parse (argc, argv);

    dpy = xcb_connect (NULL, NULL);

    // get first screen of display
    screen = xcb_setup_roots_iterator (xcb_get_setup (dpy)).data;

    main_wnd = create_window (0xff);

    watch.crcl.R = wnd_pos.width/2;
    watch.crcl.center.x = wnd_pos.width / 2;
    watch.crcl.center.y = wnd_pos.height / 2;
    watch.crcl.center.color = 0x000FF00;

    draw_func.raw_clear = NULL;
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
