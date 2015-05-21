#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <unistd.h>
#include <fcntl.h>

#include <xcb/dri2.h>

#include "fps.h"
#include "dri2_dri3_inner.h"

typedef struct dri2_info_t
{
    int drm_fd;
    drm_magic_t drm_magic;
    char* device_name;
    char* driver_name;
    tbm_bufmgr bufmgr;
    const xcb_query_extension_reply_t* dri2_ext;
}dri2_info_t;

static dri2_info_t dri2_info;

//
//========================================================================
static bo_t
get_bo (void)
{
    bo_t bo;
    uint32_t attachments;
    xcb_dri2_get_buffers_cookie_t get_buf_c;
    xcb_dri2_get_buffers_reply_t* get_buf_r = NULL;
    xcb_generic_error_t* xcb_errors = NULL;
    xcb_dri2_dri2_buffer_t* dri2_buf = NULL;

    attachments = XCB_DRI2_ATTACHMENT_BUFFER_BACK_LEFT;
    get_buf_c = xcb_dri2_get_buffers(display.dpy, display.wnd, 1, 1, &attachments);
    get_buf_r = xcb_dri2_get_buffers_reply (display.dpy, get_buf_c, &xcb_errors);
    if (xcb_errors)
    {
        free (xcb_errors);
        set_error ("Error while xcb_dri2_get_buffers call.\n");
        pthread_cancel(thread_id);
    }

    dri2_buf = xcb_dri2_get_buffers_buffers (get_buf_r);
    if (!dri2_buf)
    {
        set_error ("dri2_buf = NULL.\n");
        pthread_cancel(thread_id);
    }

    bo.bo = tbm_bo_import (dri2_info.bufmgr, dri2_buf->name);
    bo.stride = dri2_buf->pitch;
    bo.width = get_buf_r->width;
    bo.height = get_buf_r->height;

    // this memory will be used for next xcb_dri2_get_buffers_buffers calls,
    // so don't need to free it
    //free (dri2_buf);

#ifdef _DEBUG_
    printf ("xcb_dri2_get_buffers:\n"
            " bo.bo: %p, stride: %u, cpp: %u, flags: %u, width: %u, height: %u, count: %u\n",
            bo.bo, dri2_buf->pitch, dri2_buf->cpp, dri2_buf->flags, bo.width,
            bo.height, get_buf_r->count);
#endif

    free (get_buf_r);

    return bo;
}

//
//========================================================================
static void
draw (void)
{
    bo_t bo;

    bo = get_bo ();

    if (gr_ctx.draw_funcs.raw_clear)
        gr_ctx.draw_funcs.raw_clear (&bo);

    if (gr_ctx.draw_funcs.raw_draw)
        gr_ctx.draw_funcs.raw_draw (&bo);

    tbm_bo_unref (bo.bo);
}

//
//========================================================================
static void
swap_buffers (uint32_t msc_hi, uint32_t msc_lo)
{
    xcb_dri2_swap_buffers_cookie_t c;
    xcb_dri2_swap_buffers_reply_t* r = NULL;
    xcb_generic_error_t* xcb_errors = NULL;

    c = xcb_dri2_swap_buffers (display.dpy, display.wnd, msc_hi, msc_lo,
                               0, 0, 0 ,0);
    r = xcb_dri2_swap_buffers_reply (display.dpy, c, &xcb_errors);

    if (xcb_errors)
    {
        char* temp = malloc (1024);

        sprintf (temp, "Error while xcb_dri2_swap_buffers call:\n"
                 " error_code: %hhu, major_code: %hhu, minor_code: %hu, resource_id: %u\n",
                 xcb_errors->error_code, xcb_errors->major_code, xcb_errors->minor_code,
                 xcb_errors->resource_id);
        set_error (temp);

        free (temp);
        free (xcb_errors);
        pthread_cancel(thread_id);
    }

#ifdef _DEBUG_
    printf ("xcb_dri2_swap_buffers:\n swap_hi: %u, swap_lo: %u\n", r->swap_hi, r->swap_lo);
#endif

    free (r);
}


//========================================================================
//========================================================================
//========================================================================


// check dri2 extension, connect, authenticate, create dri2 drawable
// and initiate tizen buffer manager
// return 0 if success, 1 otherwise
//===================================================================
int
prepare_dri2_ext (void)
{
    xcb_dri2_query_version_cookie_t query_ver_c;
    xcb_dri2_query_version_reply_t* query_ver_r = NULL;
    xcb_generic_error_t* xcb_errors = NULL;

    xcb_dri2_connect_cookie_t connect_c;
    xcb_dri2_connect_reply_t* connect_r = NULL;

    xcb_dri2_authenticate_cookie_t auth_c;
    xcb_dri2_authenticate_reply_t* auth_r = NULL;

    int res;

    // dri2_query_version

    query_ver_c = xcb_dri2_query_version (display.dpy, XCB_DRI2_MAJOR_VERSION, XCB_DRI2_MINOR_VERSION);
    query_ver_r = xcb_dri2_query_version_reply (display.dpy, query_ver_c, &xcb_errors);
    if (xcb_errors)
    {
        free (xcb_errors);
        set_error ("Error while dri2_query_version call.\n");
        return 1;
    }

    printf ("DRI2 Extension: %d.%d.\n", query_ver_r->major_version, query_ver_r->minor_version);

    free (query_ver_r);

    // dri2_connect

    connect_c = xcb_dri2_connect (display.dpy, display.screen->root, XCB_DRI2_DRIVER_TYPE_DRI);
    connect_r = xcb_dri2_connect_reply (display.dpy, connect_c, &xcb_errors);
    if (xcb_errors)
    {
        free (xcb_errors);
        set_error ("Error while dri2_connect call.\n");
        return 1;
    }

    dri2_info.driver_name = xcb_dri2_connect_driver_name (connect_r);
    dri2_info.device_name = xcb_dri2_connect_device_name (connect_r);
    free (connect_r);

    printf ("DRI2 driver_name: %s, device_name: %s.\n", dri2_info.driver_name, dri2_info.device_name);

    // open drm device
    dri2_info.drm_fd = open (dri2_info.device_name, O_RDWR);
    if (dri2_info.drm_fd < 0)
    {
        char* temp = malloc (512);

        sprintf (temp, "Error while open call for file: %s.\n", dri2_info.device_name);
        set_error (temp);
        free (temp);

        return 1;
    }

    // dri2_authenticate
    res = drmGetMagic (dri2_info.drm_fd, &dri2_info.drm_magic);
    if (res)
    {
        close (dri2_info.drm_fd);
        set_error ("Error while drmGetMagic call.\n");
        return 1;
    }

    auth_c = xcb_dri2_authenticate (display.dpy, display.screen->root, (uint32_t)dri2_info.drm_magic);
    auth_r = xcb_dri2_authenticate_reply (display.dpy, auth_c, &xcb_errors);
    if (xcb_errors)
    {
        close (dri2_info.drm_fd);
        free (xcb_errors);
        set_error ("Error while dri2_authenticate call.\n");
        return 1;
    }

    if (!auth_r->authenticated)
    {
        close (dri2_info.drm_fd);
        set_error ("Error while dri2_authenticate call.\n");
        return 1;
    }

    free (auth_r);

    xcb_dri2_create_drawable (display.dpy, display.wnd);

    dri2_info.dri2_ext = xcb_get_extension_data (display.dpy, &xcb_dri2_id);

    dri2_info.bufmgr = tbm_bufmgr_init (dri2_info.drm_fd);

    return 0;
}

// dri2 event loop
//========================================================================
void
dri2_loop (void)
{
    xcb_generic_event_t* event = NULL;
    struct timespec start;

    draw ();
    swap_buffers (0, 0);

    while (1)
    {
        xcb_flush (display.dpy);
        event = xcb_wait_for_event (display.dpy);
        if (!event) break;

        switch (event->response_type)
        {
            default:
            {
                if (event->response_type == dri2_info.dri2_ext->first_event +
                        XCB_DRI2_BUFFER_SWAP_COMPLETE)
                {
                    xcb_dri2_buffer_swap_complete_event_t* ev;
                    ev = (xcb_dri2_buffer_swap_complete_event_t*)event;

#ifdef _DEBUG_
                    printf ("XCB_DRI2_BUFFER_SWAP_COMPLETE:\n"
                            " drawable: 0x%x, msc_hi: %u, msc_lo: %u, sbc: %u, "
                            "ust_hi: %u, ust_lo: %u\n"
                            "----------------------------\n",
                            ev->drawable, ev->msc_hi, ev->msc_lo, ev->sbc,
                            ev->ust_hi, ev->ust_lo);
#endif
                    fps_calc( &start );
                    clock_gettime( CLOCK_REALTIME, &start );

                    draw ();
                    swap_buffers (ev->msc_hi, ev->msc_lo);
                }
                else if (event->response_type == dri2_info.dri2_ext->first_event +
                        XCB_DRI2_INVALIDATE_BUFFERS)
                {
#ifdef _DEBUG_
                    xcb_dri2_invalidate_buffers_event_t* ev;
                    ev = (xcb_dri2_invalidate_buffers_event_t*)event;

                    printf ("XCB_DRI2_INVALIDATE_BUFFERS.\ndrawable: 0x%x\n", ev->drawable);
#endif
                }
                else
                {
                    printf ("kyky\n");
                }
            }
            break;
        }
    }// while (1)
}
