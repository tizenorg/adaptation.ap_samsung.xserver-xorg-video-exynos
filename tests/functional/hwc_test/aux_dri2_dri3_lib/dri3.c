/**************************************************************************

 dri3 backend

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

// this file implements preparing dri3 extension to use and dri3 events handling

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <unistd.h>

#include <xcb/dri3.h>
#pragma pack(push, 1)
#include <xcb/present.h>
#pragma pack(pop)


#include "dri2_dri3_inner.h"

typedef struct dri3_buffer_t
{
    xcb_pixmap_t pxmap;
    bo_t bo;
    xcb_rectangle_t pxmap_size; // bo size is equal pixmap size and can be found in bo field
    uint32_t busy;        // whether pixmap busy or not

    // whether pixmap must be reallocated, due to wnd size changes, or not
    uint32_t buff_must_be_realloc;
} dri3_buffer_t;

typedef struct dri3_present_info_t
{
    int drm_fd;
    tbm_bufmgr bufmgr;
    const xcb_query_extension_reply_t* present_ext;
} dri3_present_info_t;

static dri3_buffer_t* dri3_buffers;
static dri3_present_info_t dri3_present_info;

static uint64_t target_msc;

// check present extenstion
// return 0 if success, 1 otherwise
//===================================================================
static int
check_present_ext (void)
{
    xcb_present_query_version_cookie_t c;
    xcb_present_query_version_reply_t* r = NULL;
    xcb_generic_error_t* xcb_errors = NULL;

    c = xcb_present_query_version (display.dpy, XCB_PRESENT_MAJOR_VERSION, XCB_PRESENT_MINOR_VERSION);
    r = xcb_present_query_version_reply (display.dpy, c, &xcb_errors);

    if (xcb_errors)
    {
        free (xcb_errors);
        printf ("Error while Present Extension query_version call.\n");
        return 1;
    }
    else
        printf ("Present Extension: %d.%d.\n", r->major_version, r->minor_version);

    free (r);

    return 0;
}

// check dri3 extenstion and ask X server to send us auth drm_fd (fd of drm device file)
// return -1, if error was occured,
// otherwise drm_fd
//===================================================================
static int
prepare_dri3_ext (void)
{
    int* drm_fd_p = NULL;
    int drm_fd;
    xcb_dri3_query_version_cookie_t query_version_c;
    xcb_dri3_query_version_reply_t* query_version_r = NULL;
    xcb_generic_error_t* xcb_errors = NULL;

    xcb_dri3_open_cookie_t open_c;
    xcb_dri3_open_reply_t* open_r = NULL;

    // query version ---------------------

    query_version_c = xcb_dri3_query_version (display.dpy, XCB_DRI3_MAJOR_VERSION, XCB_DRI3_MINOR_VERSION);
    query_version_r = xcb_dri3_query_version_reply (display.dpy, query_version_c, &xcb_errors);

    if (xcb_errors)
    {
        free (xcb_errors);
        printf ("Error while DRI3 Extension query_version call.\n");
        return -1;
    }
    else
        printf ("DRI3 Extension: %d.%d.\n", query_version_r->major_version, query_version_r->minor_version);

    free (query_version_r);

    // open ------------------------------

    // I don't know why open call require drawable, maybe it dedicates screen and drm device
    // which is responsible for showing this screen ?
    // about third parameter I'm still in confuse.
    open_c = xcb_dri3_open (display.dpy, (xcb_drawable_t)display.screen->root, 0);
    open_r = xcb_dri3_open_reply (display.dpy, open_c, &xcb_errors);

    if (xcb_errors)
    {
        free (xcb_errors);
        printf ("Error while DRI3 Extension open call.\n");
        return -1;
    }

    drm_fd_p = xcb_dri3_open_reply_fds (display.dpy, open_r);
    drm_fd = *drm_fd_p;

    printf (" drm fd provided by DRI3: %d.\n", drm_fd);
    printf (" OBSERVE: nfd returned by open_dri: %d.\n", open_r->nfd);

    // this memory will be used for next xcb_dri3_open_reply_fds calls, if we will do them,
    // so don't need to free it
    //free (drm_fd_p);
    free (open_r);

    return drm_fd;
}

// obtains tbm_bo(bo_t) from pixmap via dri3 extension
// aborts app, if error was occured,
// otherwise returns tbm_bo(bo_t), that represents such gem object that pixmap represents
//===================================================================
static bo_t
get_bo_from_pixmap (xcb_pixmap_t pixmap)
{
    bo_t bo;
    int* dma_buf_fd = NULL;
    xcb_dri3_buffer_from_pixmap_cookie_t bf_from_pixmap_c;
    xcb_dri3_buffer_from_pixmap_reply_t* bf_from_pixmap_r = NULL;
    xcb_generic_error_t* xcb_errors = NULL;

    DEBUG_OUT ("before xcb_dri3_buffer_from_pixmap.\n");
    bf_from_pixmap_c = xcb_dri3_buffer_from_pixmap (display.dpy, pixmap);

    DEBUG_OUT ("before xcb_dri3_buffer_from_pixmap_reply.\n");
    bf_from_pixmap_r = xcb_dri3_buffer_from_pixmap_reply (display.dpy, bf_from_pixmap_c,
                                                          &xcb_errors);
    if (xcb_errors)
    {
        free (xcb_errors);
        printf ("Error while DRI3 Extension buffer_from_pixmap call.\n");
        exit (1);
    }

    DEBUG_OUT ("size: %u, w: %hu, h: %hu, stride: %hu, bpp: %hhu, depth: %hhu.\n",
               (bf_from_pixmap_r)->size, (bf_from_pixmap_r)->width,
               (bf_from_pixmap_r)->height, (bf_from_pixmap_r)->stride,
               (bf_from_pixmap_r)->bpp, (bf_from_pixmap_r)->depth);

    dma_buf_fd = xcb_dri3_buffer_from_pixmap_reply_fds (display.dpy, bf_from_pixmap_r);

    DEBUG_OUT ("dma_buf fd, returned by dri3, for pixmap[0x%06x]: %d.\n", pixmap, *dma_buf_fd);

    bo.bo = tbm_bo_import_fd (dri3_present_info.bufmgr, (tbm_fd)(*dma_buf_fd));
    if (!bo.bo)
    {
        free (bf_from_pixmap_r);
        printf ("Error while tbm_bo_import_fd call.\n");
        exit (1);
    }

    bo.dma_buf_fd = *dma_buf_fd;
    bo.width = bf_from_pixmap_r->width;
    bo.height = bf_from_pixmap_r->height;
    bo.stride = bf_from_pixmap_r->stride;
    // now, I don't see any necessity to use these values
    // bf_from_pixmap_r->depth;
    // bf_from_pixmap_r->bpp;

    DEBUG_OUT ("OBSERVE: nfd returned by buffer_from_pixmap: %d.\n", bf_from_pixmap_r->nfd);

    // this memory will be used for next xcb_dri3_buffer_from_pixmap_reply_fds calls,
    // so don't need to free it
    //free (dma_buf_fd);
    free (bf_from_pixmap_r);

    return bo;
}

// this function is called in event handling and calls app's callbacks to draw
//========================================================================
static void
draw (uint32_t idx)
{
    if (gr_ctx.mode == DRI3_PRESENT_MODE || gr_ctx.mode == DRI3_MODE)
    {
        bo_t back_bo;

        // library client must not be able to do any changes in real bo object,
        // now it isn't important, but what about a future ?
        memcpy (&back_bo, &dri3_buffers[idx].bo, sizeof (back_bo));

        if (gr_ctx.draw_funcs.raw_clear)
            gr_ctx.draw_funcs.raw_clear (&back_bo);

        if (gr_ctx.draw_funcs.raw_draw)
            gr_ctx.draw_funcs.raw_draw (&back_bo);

        DEBUG_OUT ("render in tbm bo: 0x%p (tied pixmap: 0x%x)\n", dri3_buffers[idx].bo.bo,
                   dri3_buffers[idx].pxmap);
    }
    else if (gr_ctx.mode == PRESENT_MODE)
    {
        xcb_pixmap_t back_pxmap;
        xcb_rectangle_t pxmap_size;

        back_pxmap = dri3_buffers[idx].pxmap;
        pxmap_size = dri3_buffers[idx].pxmap_size;

        if (gr_ctx.draw_funcs.xcb_clear)
            gr_ctx.draw_funcs.xcb_clear (back_pxmap, pxmap_size.width, pxmap_size.height);

        if (gr_ctx.draw_funcs.xcb_draw)
            gr_ctx.draw_funcs.xcb_draw (back_pxmap, pxmap_size.width, pxmap_size.height);

        DEBUG_OUT ("render in pixmap: 0x%x.\n", back_pxmap);
    }
}

// wrap over present pixmap request
//========================================================================
static void
swap_buffers (uint64_t msc, uint32_t idx)
{
    xcb_pixmap_t back_pxmap;

    back_pxmap = dri3_buffers[idx].pxmap;

    xcb_present_pixmap (display.dpy, display.wnd, back_pxmap, idx,
                        0, 0, 0, 0, 0, 0, 0, XCB_PRESENT_OPTION_NONE, msc, 0, 0, 0, NULL);

    DEBUG_OUT ("swap pixmap[%d]: 0x%x, msc: %llu\n", idx, back_pxmap, msc);

    // block pixmap/bo to next draw and swap operations
    dri3_buffers[idx].busy = 1;
}

// reallocate dri3_buff and corresponding pixmap and tbm bo
// idx - index inner array of dri3 buffers
//===================================================================
static void
realloc_dri3_buff (uint32_t idx, uint16_t width, uint16_t height)
{
    if (idx >= gr_ctx.params.num_of_bufs) return;

    // release dmabuf file, old bo and pixmap
    if (gr_ctx.mode != PRESENT_MODE)
    {
        if (dri3_buffers[idx].bo.bo)
        {
            tbm_bo_unref(dri3_buffers[idx].bo.bo);
            DEBUG_OUT ("tbm bo: 0x%p has been unrefed.\n", dri3_buffers[idx].bo.bo);
        }

        if (dri3_buffers[idx].bo.dma_buf_fd)
            close (dri3_buffers[idx].bo.dma_buf_fd);
    }

    if (dri3_buffers[idx].pxmap)
    {
        xcb_free_pixmap(display.dpy, dri3_buffers[idx].pxmap);
        DEBUG_OUT ("pixmap: 0x%x has been freed.\n", dri3_buffers[idx].pxmap);
    }

    // create new bo and pixmap
    dri3_buffers[idx].pxmap_size.width = width;
    dri3_buffers[idx].pxmap_size.height = height;
    dri3_buffers[idx].pxmap = xcb_generate_id (display.dpy);
    xcb_create_pixmap (display.dpy, DEPTH, dri3_buffers[idx].pxmap, display.wnd,
                       dri3_buffers[idx].pxmap_size.width,
                       dri3_buffers[idx].pxmap_size.height);

    DEBUG_OUT ("pixmap: 0x%x has been created.\n", dri3_buffers[idx].pxmap);

    // creates bo, tied toward pixmap, to do direct drawing in it
    if (gr_ctx.mode != PRESENT_MODE)
        dri3_buffers[idx].bo = get_bo_from_pixmap (dri3_buffers[idx].pxmap);

    dri3_buffers[idx].buff_must_be_realloc = 0;

    DEBUG_OUT ("buffer: %u has been reallocated (tbm_bo: 0x%p, pixamp: 0x%x, wxh: %hux%hu).\n",
               idx, dri3_buffers[idx].bo.bo, dri3_buffers[idx].pxmap, width, height);
}

//========================================================================
//========================================================================
//========================================================================


// checks dri3 and present extensions, does dri3_open request and initiates
// tizen buffer manager
// return 0 if success, 1 otherwise
//===================================================================
int
prepare_dri3_present_ext (void)
{
    uint32_t present_eid;
    int i;

    // present initialization

    if (check_present_ext ()) return 1;

    // ask present extension to notify us about events
    present_eid = xcb_generate_id (display.dpy);
    xcb_present_select_input (display.dpy, present_eid, display.wnd,
                              XCB_PRESENT_EVENT_MASK_CONFIGURE_NOTIFY |
                              XCB_PRESENT_EVENT_MASK_COMPLETE_NOTIFY |
                              XCB_PRESENT_EVENT_MASK_IDLE_NOTIFY);

    // for determine dri3's events
    dri3_present_info.present_ext = xcb_get_extension_data (display.dpy, &xcb_present_id);
    if (!dri3_present_info.present_ext)
    {
        printf ("Error while xcb_get_extension_data call.\n");
        return 1;
    }

    // dri3 initialization

    if (gr_ctx.mode != PRESENT_MODE)
    {
        dri3_present_info.drm_fd = prepare_dri3_ext();
        if (dri3_present_info.drm_fd < 0) return 1;
        dri3_present_info.bufmgr = tbm_bufmgr_init (dri3_present_info.drm_fd);
    }

    // allocate neccessary memory for dri3 buffers
    dri3_buffers = calloc (gr_ctx.params.num_of_bufs, sizeof (dri3_buffer_t));

    // set up dri3 buffers
    for (i = 0; i < gr_ctx.params.num_of_bufs; i++)       
        realloc_dri3_buff (i, wnd_pos.width, wnd_pos.height);

    printf ("%u buffer(s) will be used.\n", gr_ctx.params.num_of_bufs);

    return 0;
}

// dri3 events loop
//========================================================================
void
dri3_loop (void)
{
    xcb_generic_event_t* event = NULL;
    xcb_ge_generic_event_t* generic_event = NULL;
    int i;
#ifndef _DEBUG_
    struct timespec start;
#endif

    // ask to be notifed by present extension about current msc
    xcb_present_notify_msc (display.dpy, display.wnd, 0, 0, 0, 0);

    while (1)
    {
        xcb_flush (display.dpy);
        event = xcb_wait_for_event (display.dpy);
        if (!event)
        {
            DEBUG_OUT ("event returned by xcb_wait_for_event is NULL.\n");
            break;
        }

        switch (event->response_type)
        {
            case XCB_GE_GENERIC:
            {
                generic_event = (xcb_ge_generic_event_t*)event;

                // if event is from present Ext.
                if (generic_event->extension == dri3_present_info.present_ext->major_opcode)
                {
                    switch (generic_event->event_type)
                    {
                        case XCB_PRESENT_CONFIGURE_NOTIFY:
                        {
                            xcb_present_configure_notify_event_t* config_ev;
                            config_ev = (xcb_present_configure_notify_event_t*)event;

                            DEBUG_OUT ("XCB_PRESENT_CONFIGURE_NOTIFY: (%hd, %hd) [%hux%hu]\n",
                                       config_ev->x, config_ev->y, config_ev->width, config_ev->height);

                            for (i = 0; i < gr_ctx.params.num_of_bufs; i++)
                                dri3_buffers[i].buff_must_be_realloc = 1;

                            wnd_pos.x = config_ev->x;
                            wnd_pos.y = config_ev->y;
                            wnd_pos.width = config_ev->width;
                            wnd_pos.height = config_ev->height;
                        }
                        break;

                        case XCB_PRESENT_COMPLETE_NOTIFY:
                        {
                            DEBUG_OUT ("XCB_PRESENT_COMPLETE_NOTIFY:\n");

                            xcb_present_complete_notify_event_t* complete_ev;
                            complete_ev = (xcb_present_complete_notify_event_t*)event;

                            if (complete_ev->kind == XCB_PRESENT_COMPLETE_KIND_PIXMAP)
                            {
#ifdef _DEBUG_
                                DEBUG_OUT ("  XCB_PRESENT_COMPLETE_KIND_PIXMAP, serial = %u, msc = %llu, ",
                                           complete_ev->serial, complete_ev->msc);
                                switch (complete_ev->mode)
                                {
                                    case XCB_PRESENT_COMPLETE_MODE_COPY:
                                       printf ("COPY.\n");
                                    break;
                                    case XCB_PRESENT_COMPLETE_MODE_FLIP:
                                       printf ("FLIP.\n");
                                    break;
                                    case XCB_PRESENT_COMPLETE_MODE_SKIP:
                                       printf ("SKIP.\n");
                                    break;

                                    default:
                                        printf (" What is it?\n");
                                    break;
                                }                       
#endif
                            }
                            else if (complete_ev->kind == XCB_PRESENT_COMPLETE_KIND_NOTIFY_MSC)
                            {
                                DEBUG_OUT ("XCB_PRESENT_COMPLETE_KIND_NOTIFY_MSC\n");

                                // compute msc to use with present_pixmap request
                                target_msc = complete_ev->msc + gr_ctx.params.swap_interval;
                            }
#ifndef _DEBUG_
                            fps_calc( &start );
                            clock_gettime( CLOCK_REALTIME, &start );
#endif
                            // if some delays are occured
                            if (target_msc <= complete_ev->msc)
                                target_msc = complete_ev->msc + gr_ctx.params.swap_interval;

                            // complete_ev->msc - number of frame that is displayed now,
                            // or will be displayed soon, after vblank end.
                            // so we draw new frames in idle pixmaps and ask to present
                            // our frames to next mscs with some interval.

                            // if we obtained XCB_PRESENT_IDLE_NOTIFY event without
                            // XCB_PRESENT_COMPLETE_NOTIFY, this means that present
                            // drop our frame and we can draw new frame, and ask to
                            // present it immediately, without waiting for XCB_PRESENT_COMPLETE_NOTIFY
                            // event, that will be delivered with some delay, but in this case,
                            // we don't know about real msc and new present may results new drop,
                            // so we will do present only when handling XCB_PRESENT_COMPLETE_NOTIFY event,
                            // despite of some performance decrease
                            for (i = 0; i < gr_ctx.params.num_of_bufs; i++)
                            {
                                // if buffer is busy (non-idle)
                                if (dri3_buffers[i].busy)
                                    continue;

                                draw (i); // draw our mesh
                                swap_buffers (target_msc, i);

                                target_msc += gr_ctx.params.swap_interval;
                            }
                        }
                        break;

                        case XCB_PRESENT_IDLE_NOTIFY:
                        {
                            xcb_present_idle_notify_event_t* idle_ev;
                            idle_ev = (xcb_present_idle_notify_event_t*)event;

                            if (idle_ev->serial >= gr_ctx.params.num_of_bufs)
                            {
                                set_error ("invalid parameters from present extension.\n");
                                pthread_cancel(thread_id);
                            }

                            DEBUG_OUT ("----------------------------\n"
                                       "XCB_PRESENT_IDLE_NOTIFY: pixmap: 0x%x (tied tbm bo: 0x%p), serial: %u, event: %u\n",
                                       idle_ev->pixmap, dri3_buffers[idle_ev->serial].bo.bo, idle_ev->serial, idle_ev->event);

                            // unblock pixmap/bo to next draw and swap operations
                            dri3_buffers[idle_ev->serial].busy = 0;

                            // adjust pixmap/bo size to wnd's size before start render into it
                            if (dri3_buffers[idle_ev->serial].buff_must_be_realloc)
                                realloc_dri3_buff (idle_ev->serial, wnd_pos.width, wnd_pos.height);
                        }
                        break;

                        default:
                        break;
                    }
                }
            } //case XCB_GE_GENERIC:
            break;

            default:
                DEBUG_OUT ("dri3_loop: default case.\n  event->response_type = %d.\n",
                           event->response_type);
            break;
        } //switch (event->response_type)
    } //while (1)
}
