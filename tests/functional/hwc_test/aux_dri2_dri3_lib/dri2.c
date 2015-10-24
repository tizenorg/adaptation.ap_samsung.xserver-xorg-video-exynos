/**************************************************************************

 dri2 backend

 Copyright 2010 - 2015 Samsung Electronics co., Ltd. All Rights Reserved.

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

// this file implements preparing dri2 extension to use and dri2 events handling

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <unistd.h>
#include <fcntl.h>

#include <xcb/dri2.h>

#include "dri2_dri3_inner.h"


#define CACHE_BO_SIZE (3)

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

// cycle bo's cache
static bo_t bo_cache[CACHE_BO_SIZE];
static uint32_t bo_cache_idx;


// next two functions avoid call to tbm_bo_import, when we have tbm_bo for
// appropriate buf_name, returned by dri2 extension

// returns, if exist, tbm_bo for gem object's name 'buf_name'
//========================================================================
static tbm_bo
get_tbm_bo_from_cache (uint32_t buf_name)
{
    int i;

    for (i = 0; i < CACHE_BO_SIZE; i++)
    {
        if (bo_cache[i].name == buf_name) return bo_cache[i].bo;
    }

    return NULL;
}

// add bo to cycle bo's cache
//========================================================================
static void
add_bo_to_cache (const bo_t* bo)
{
    if (!bo) return;

    if (bo_cache[bo_cache_idx].bo)
        tbm_bo_unref(bo_cache[bo_cache_idx].bo);

    memcpy ((bo_cache + bo_cache_idx++), bo, sizeof(bo_t));
    bo_cache_idx %= CACHE_BO_SIZE;
}

// wrap over DRI2GetBuffers request
// return all information for rendering
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

    get_buf_c = xcb_dri2_get_buffers (display.dpy, display.wnd, 1, 1, &attachments);
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
        free (get_buf_r);
        set_error ("dri2_buf = NULL.\n");
        pthread_cancel(thread_id);
    }

    bo.stride = dri2_buf->pitch;
    bo.width = get_buf_r->width;
    bo.height = get_buf_r->height;
    bo.name = dri2_buf->name;

    // cache bos
    bo.bo = get_tbm_bo_from_cache (bo.name);
    if (!bo.bo)
    {
        bo.bo = tbm_bo_import (dri2_info.bufmgr, bo.name);
        add_bo_to_cache (&bo);
    }

    // this memory will be used for next xcb_dri2_get_buffers_buffers calls,
    // so don't need to free it
    //free (dri2_buf);

    DEBUG_OUT ("buf_name: %u, bo: %p, stride: %u, cpp: %u, flags: %u, width: %u, "
               "height: %u, count: %u\n",
               bo.name, bo.bo, bo.stride, dri2_buf->cpp, dri2_buf->flags,
               bo.width, bo.height, get_buf_r->count);

    free (get_buf_r);

    return bo;
}

// this function is called in event handling and calls app's callbacks to draw
//========================================================================
static void
draw (void)
{
    bo_t bo;

    TIME_START();
    bo = get_bo ();
    TIME_END ("get_bo");

    TIME_START();

    if (gr_ctx.draw_funcs.raw_clear)
        gr_ctx.draw_funcs.raw_clear (&bo);

    if (gr_ctx.draw_funcs.raw_draw)
        gr_ctx.draw_funcs.raw_draw (&bo);

    TIME_END ("draw");
}

// wrap over DRI2SwapBuffers request
// this function is called in event handling
//========================================================================
static void
swap_buffers (uint32_t msc_hi, uint32_t msc_lo)
{
    DEBUG_OUT ("ask to swap buffers for window: 0x%x at msc_hi: %u, msc_lo: %u\n",
               display.wnd, msc_hi, msc_lo);

    TIME_START();
    xcb_dri2_swap_buffers (display.dpy, display.wnd, msc_hi, msc_lo, 0, 0, 0 ,0);
    TIME_END ("xcb_dri2_swap_buffers");

#ifdef _DEBUG_
    printf ("----------------------------\n");
#endif
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

    // whait for main thread
    usleep (500000);

    // dri2_query_version

    query_ver_c = xcb_dri2_query_version (display.dpy, XCB_DRI2_MAJOR_VERSION, XCB_DRI2_MINOR_VERSION);
    query_ver_r = xcb_dri2_query_version_reply (display.dpy, query_ver_c, &xcb_errors);
    if (xcb_errors)
    {
        free (xcb_errors);
        set_error ("Error while dri2_query_version call.\n");
        return 1;
    }

    printf ("DRI2 Extension: %u.%u.\n", query_ver_r->major_version, query_ver_r->minor_version);

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

    // for determine dri2's events
    dri2_info.dri2_ext = xcb_get_extension_data (display.dpy, &xcb_dri2_id);

    dri2_info.bufmgr = tbm_bufmgr_init (dri2_info.drm_fd);

    return 0;
}

// dri2 events loop
//========================================================================
void
dri2_loop (void)
{
    xcb_generic_event_t* event = NULL;
#ifndef _DEBUG_
    struct timespec start;
#endif
    uint64_t target_msc = 0;

    draw ();
    swap_buffers (0, 0);

    while (1)
    {
        xcb_flush (display.dpy);
        DEBUG_OUT("before xcb_wait_for_event.\n");
        event = xcb_wait_for_event (display.dpy);
        DEBUG_OUT("after xcb_wait_for_event.\n");
        if (!event)
        {
            printf("event is NULL.\n");
            break;
        }

        switch (event->response_type)
        {
            default:
            {
                // if XCB_DRI2_BUFFER_SWAP_COMPLETE event
                if (event->response_type == dri2_info.dri2_ext->first_event +
                        XCB_DRI2_BUFFER_SWAP_COMPLETE)
                {
                    xcb_dri2_buffer_swap_complete_event_t* ev;
                    ev = (xcb_dri2_buffer_swap_complete_event_t*)event;

#ifdef _DEBUG_
                    DEBUG_OUT ("XCB_DRI2_BUFFER_SWAP_COMPLETE:\n");
                    switch (ev->event_type)
                    {
                        case XCB_DRI2_EVENT_TYPE_EXCHANGE_COMPLETE:
                            DEBUG_OUT (" EXCHANGE_COMPLETE.\n");
                        break;
                        case XCB_DRI2_EVENT_TYPE_BLIT_COMPLETE:
                            DEBUG_OUT (" BLIT_COMPLETE.\n");
                        break;
                        case XCB_DRI2_EVENT_TYPE_FLIP_COMPLETE:
                            DEBUG_OUT (" FLIP_COMPLETE.\n");
                        break;
                    }
                    DEBUG_OUT ("  drawable: 0x%x, msc_hi: %u, msc_lo: %u, sbc: %u, "
                               "ust_hi: %u, ust_lo: %u\n",
                               ev->drawable, ev->msc_hi, ev->msc_lo, ev->sbc,
                               ev->ust_hi, ev->ust_lo);

#endif
                    // adjust target_msc to real msc
                    target_msc = ((uint64_t)ev->msc_hi << 32) | (uint64_t)ev->msc_lo;

                    // prepare target_msc for use when DRI2InvalidateBuffers event is came
                    target_msc += 2*gr_ctx.params.swap_interval;
                }
                else if (event->response_type == dri2_info.dri2_ext->first_event +
                        XCB_DRI2_INVALIDATE_BUFFERS)
                {
#ifdef _DEBUG_
                    xcb_dri2_invalidate_buffers_event_t* ev;
                    ev = (xcb_dri2_invalidate_buffers_event_t*)event;

                    DEBUG_OUT ("XCB_DRI2_INVALIDATE_BUFFERS.\n  drawable: 0x%x\n", ev->drawable);
#else
                    fps_calc( &start );
                    clock_gettime( CLOCK_REALTIME, &start );
#endif
                    // we draw frame when obtained DRI2InvalidateBuffers event due to be able to
                    // handle situation when DRI2InvalidateBuffers event signals about mismatching
                    // drawable and buffer sizes and DRI2BufferSwapComplete event will not be issued and
                    // delivered to us and we will hang out in xcb_wait_for_event, so we draw frame
                    // here and for swap it use target_msc obtained from DRI2BufferSwapComplete event
                    // on previous swap
                    draw ();
                    swap_buffers ((uint32_t)(target_msc >> 32), (uint32_t)(target_msc & 0xFFFFFFFF));
                }
            }
            break;
        }
    }// while (1)
}
