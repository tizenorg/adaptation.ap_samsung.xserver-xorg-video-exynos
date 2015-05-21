#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <pthread.h>

#include "dri2_dri3_inner.h"

display_t display;
graphics_ctx gr_ctx;
pthread_t thread_id;
xcb_rectangle_t wnd_pos;

static char* error;

static void* dri2_dri3 (void* arg);
static int init_graphic_context (void);

// this function gets copy of passed arguments !!!
// draw_funcs - set of callback function to drawing, which functions to use
//              will be dedicated according to mode value
// mode - mode in which dri2_dri3 module will work
// window - window's id in which drawing will be occured
// num_of_bufs - number of buffers for use with Present extension
//=========================================================================
int init_dri2_dri3 (xcb_connection_t* dpy, draw_funcs_t* draw_funcs, int mode, xcb_window_t window,
                    uint32_t num_of_bufs)
{
    int res;

    if (!draw_funcs || !dpy)
    {
        set_error ("invalid input arguments");
        return 1;
    }

    display.client_dpy = dpy;   // used for obtain window's geometry
    display.dpy = xcb_connect (NULL, NULL); // dpy from client cann't be used
                                            // for processing dri2/dri2 events

    display.screen = xcb_setup_roots_iterator (xcb_get_setup (display.dpy)).data;
    display.wnd = window;

    gr_ctx.mode = mode;
    memcpy (&gr_ctx.draw_funcs, draw_funcs, sizeof (gr_ctx.draw_funcs));
    gr_ctx.num_of_bufs = num_of_bufs;

    get_wnd_geometry (display.wnd, &wnd_pos);

    res = pthread_create (&thread_id, NULL, dri2_dri3, NULL);
    if (res)
    {
        set_error ("error while pthread_create");
        return 1;
    }

    return 0;
}

// any dri2_dri3 module's functions return error code or message,
// only DRI2_DRI3_TRUE or DRI2_DRI3_FALSE
// use this function, if you need, to obtain more detail information about error
// you must free memory, pointed by returned value
//=========================================================================
char* get_last_error (void)
{
    char* temp;

    if (!error) return NULL;

    // TODO: need thread synchronize
    temp = strdup (error);
    free (error);
    error = NULL;

    return temp;
}


//========================================================================
//========================================================================
//========================================================================


// dri2/dri3 xcb event loop
//========================================================================
static void*
dri2_dri3 (void* arg)
{
    int res;

    res = init_graphic_context ();
    if (res)
        pthread_cancel(thread_id);

    switch (gr_ctx.mode)
    {
        case DRI2_MODE:
        {
            dri2_loop ();
        }
        break;

        case DRI3_PRESENT_MODE:
        case PRESENT_MODE:
        {
            dri3_loop ();
        }
        break;
    }

    return NULL;
}

// fill gr_ctx struct to use it on drawing and presenting on screen phases
// return 0 if success, 1 otherwise
//===================================================================
static int
init_graphic_context (void)
{
    int res;

    switch (gr_ctx.mode)
    {
        case DRI2_MODE:
        {
            res = prepare_dri2_ext ();
            if (res) return 1;

            printf ("usecase: dri2.\n");
        }
        break;

        case DRI3_PRESENT_MODE:
        case PRESENT_MODE:
        {
            res = prepare_dri3_present_ext ();
            if (res) return 1;

            if (gr_ctx.mode == DRI3_PRESENT_MODE)
                printf ("usecase: dri3 + present.\n");
            else
                printf ("usecase: present.\n");
        }
        break;

        default:
            printf ("invalid gr_ctx.mode.\n");
        break;
    }

    return 0;
}

// return geometry of specified, by win_id, window
//========================================================================
void
get_wnd_geometry (xcb_window_t win_id, xcb_rectangle_t* geometry)
{
    xcb_get_geometry_cookie_t cookie;
    xcb_get_geometry_reply_t* reply = NULL;
    xcb_generic_error_t* error = NULL;

    if (!geometry)
    {
        printf ("error while get_wnd_geometry call.\n");
        exit (1);
    }

    cookie = xcb_get_geometry (display.client_dpy, win_id);
    reply = xcb_get_geometry_reply (display.client_dpy, cookie, &error);
    if (error)
    {
        printf ("error while xcb_get_geometry call.\n");
        free (error);
        exit (1);
    }

    geometry->x = reply->x;
    geometry->y = reply->y;
    geometry->width =  reply->width;
    geometry->height = reply->height;

    free (reply);
}

//
//===================================================================
void
set_error (const char* str)
{
    // to prevent rewriting of previous error message
    if (!error) error = strdup (str);
}
