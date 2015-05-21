#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <xcb/dri3.h>
#pragma pack(push, 1)
#include <xcb/present.h>
#pragma pack(pop)
#include "fps.h"
#include "dri2_dri3_inner.h"


typedef struct swap_buffer_mng_t
{
    // size of these four arrays can be found in gr_ctx.num_of_bufs field
    xcb_pixmap_t* pxmap;
    bo_t* bo;
    xcb_rectangle_t* pxmap_size; // bo's size is equal pixmap's size and can be found in bo field
    int* pxmap_busy;
    uint32_t current_back;
} swap_buffer_mng_t;

typedef struct dri3_info_t
{
    int drm_fd;
    tbm_bufmgr bufmgr;
    const xcb_query_extension_reply_t* present_ext;
} dri3_info_t;


static swap_buffer_mng_t swap_buffer_mng;
static dri3_info_t dri3_info;

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

    bf_from_pixmap_c = xcb_dri3_buffer_from_pixmap (display.dpy, pixmap);
    bf_from_pixmap_r = xcb_dri3_buffer_from_pixmap_reply (display.dpy, bf_from_pixmap_c,
                                                          &xcb_errors);
    if (xcb_errors)
    {
        free (xcb_errors);
        printf ("Error while DRI3 Extension buffer_from_pixmap call.\n");
        exit (1);
    }

#ifdef _DEBUG_
    printf (" size: %u, w: %hu, h: %hu, stride: %hu, bpp: %hhu, depth: %hhu.\n",
             (bf_from_pixmap_r)->size, (bf_from_pixmap_r)->width,
             (bf_from_pixmap_r)->height, (bf_from_pixmap_r)->stride,
             (bf_from_pixmap_r)->bpp, (bf_from_pixmap_r)->depth);
#endif

    dma_buf_fd = xcb_dri3_buffer_from_pixmap_reply_fds (display.dpy, bf_from_pixmap_r);

#ifdef _DEBUG_
    printf ("dma_buf fd, returned by dri3, for pixmap[0x%06x]: %d.\n", pixmap, *dma_buf_fd);
#endif

    bo.bo = tbm_bo_import_fd (dri3_info.bufmgr, (tbm_fd)(*dma_buf_fd));
    if (!bo.bo)
    {
        free (bf_from_pixmap_r);
        printf ("Error while tbm_bo_import_fd call.\n");
        exit (1);
    }

    bo.width = bf_from_pixmap_r->width;
    bo.height = bf_from_pixmap_r->height;
    bo.stride = bf_from_pixmap_r->stride;
    // now, I don't see any necessity to use these values
    // bf_from_pixmap_r->depth;
    // bf_from_pixmap_r->bpp;

#ifdef _DEBUG_
    printf (" OBSERVE: nfd returned by buffer_from_pixmap: %d.\n", bf_from_pixmap_r->nfd);
#endif

    // this memory will be used for next xcb_dri3_buffer_from_pixmap_reply_fds calls,
    // so don't need to free it
    //free (dma_buf_fd);
    free (bf_from_pixmap_r);

    return bo;
}

//
//========================================================================
static void
draw (void)
{
    // if pixmap is busy
    if (swap_buffer_mng.pxmap_busy[swap_buffer_mng.current_back]) return;

    if (gr_ctx.mode == DRI3_PRESENT_MODE || gr_ctx.mode == DRI3_MODE)
    {
        bo_t back_bo;

        // library client must not be able to do any changes in real bo object,
        // now it isn't important, but what about a future ?
        memcpy (&back_bo, &swap_buffer_mng.bo[swap_buffer_mng.current_back], sizeof (back_bo));

        if (gr_ctx.draw_funcs.raw_clear)
            gr_ctx.draw_funcs.raw_clear (&back_bo);

        if (gr_ctx.draw_funcs.raw_draw)
            gr_ctx.draw_funcs.raw_draw (&back_bo);
    }
    else if (gr_ctx.mode == PRESENT_MODE)
    {
        xcb_pixmap_t back_pxmap;
        xcb_rectangle_t pxmap_size;

        back_pxmap = swap_buffer_mng.pxmap[swap_buffer_mng.current_back];
        pxmap_size = swap_buffer_mng.pxmap_size[swap_buffer_mng.current_back];

        if (gr_ctx.draw_funcs.xcb_clear)
            gr_ctx.draw_funcs.xcb_clear (back_pxmap, pxmap_size.width, pxmap_size.height);

        if (gr_ctx.draw_funcs.xcb_draw)
            gr_ctx.draw_funcs.xcb_draw (back_pxmap, pxmap_size.width, pxmap_size.height);
    }
}

//
//========================================================================
static void
swap_buffers (uint64_t msc)
{
    xcb_pixmap_t back_pxmap;

    back_pxmap = swap_buffer_mng.pxmap[swap_buffer_mng.current_back];

    xcb_present_pixmap (display.dpy, display.wnd, back_pxmap, swap_buffer_mng.current_back,
                        0, 0, 0, 0, 0, 0, 0, XCB_PRESENT_OPTION_NONE, msc, 0, 0, 0, NULL);

#ifdef _DEBUG_
    printf ("swap pixmap[%d]: 0x%x, msc: %llu\n", swap_buffer_mng.current_back, back_pxmap,
            msc);
#endif

    // block pixmap/bo to next draw and swap operations
    swap_buffer_mng.pxmap_busy[swap_buffer_mng.current_back] = 1;

    swap_buffer_mng.current_back++;
    swap_buffer_mng.current_back %= gr_ctx.num_of_bufs;
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
    int drm_fd = -1;
    uint32_t present_eid;
    int i;

    if (check_present_ext ()) return 1;

    if (gr_ctx.mode != PRESENT_MODE)
    {
        drm_fd = prepare_dri3_ext();
        if (drm_fd < 0) return 1;
    }

    present_eid = xcb_generate_id (display.dpy);
    xcb_present_select_input (display.dpy, present_eid, display.wnd,
                              XCB_PRESENT_EVENT_MASK_CONFIGURE_NOTIFY |
                              XCB_PRESENT_EVENT_MASK_COMPLETE_NOTIFY |
                              XCB_PRESENT_EVENT_MASK_IDLE_NOTIFY);

    dri3_info.present_ext = xcb_get_extension_data (display.dpy, &xcb_present_id);
    if (!dri3_info.present_ext)
    {
        printf ("Error while xcb_get_extension_data call.\n");
        return 1;
    }

    // allocate neccessary memory for control dri3 + present drawing and swaping
    swap_buffer_mng.pxmap = calloc (gr_ctx.num_of_bufs, sizeof (xcb_pixmap_t));
    swap_buffer_mng.bo = calloc (gr_ctx.num_of_bufs, sizeof (bo_t));
    swap_buffer_mng.pxmap_size = calloc (gr_ctx.num_of_bufs, sizeof (xcb_rectangle_t));
    swap_buffer_mng.pxmap_busy = calloc (gr_ctx.num_of_bufs, sizeof (int));

    for (i = 0; i < gr_ctx.num_of_bufs; i++)
    {       
        swap_buffer_mng.pxmap_size[i].width = wnd_pos.width;
        swap_buffer_mng.pxmap_size[i].height = wnd_pos.height;
        swap_buffer_mng.pxmap[i] = xcb_generate_id (display.dpy);
        xcb_create_pixmap (display.dpy, DEPTH, swap_buffer_mng.pxmap[i], display.wnd,
                           swap_buffer_mng.pxmap_size[i].width,
                           swap_buffer_mng.pxmap_size[i].height);
    }

    if (gr_ctx.mode != PRESENT_MODE)
    {
        dri3_info.drm_fd = drm_fd;
        dri3_info.bufmgr = tbm_bufmgr_init (dri3_info.drm_fd);

        // creates bos, linked with pixmap in X server, to do direct drawing in them
        for (i = 0; i < gr_ctx.num_of_bufs; i++)
            swap_buffer_mng.bo[i] = get_bo_from_pixmap (swap_buffer_mng.pxmap[i]);
    }


    return 0;
}

//
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

    draw ();
    swap_buffers (0);
    swap_buffer_mng.current_back = 0;

    while (1)
    {
        xcb_flush (display.dpy);
        event = xcb_wait_for_event (display.dpy);
        if (!event)
        {
#ifdef _DEBUG_
            printf ("event returned by xcb_wait_for_event is NULL.\n");
#endif
            break;
        }

        switch (event->response_type)
        {
            case XCB_GE_GENERIC:
            {
                generic_event = (xcb_ge_generic_event_t*)event;

                // if event is from present Ext.
                if (generic_event->extension == dri3_info.present_ext->major_opcode)
                {
                    switch (generic_event->event_type)
                    {
                        case XCB_PRESENT_CONFIGURE_NOTIFY:
                        {
#ifdef _DEBUG_
                            xcb_present_configure_notify_event_t* config_ev;
                            config_ev = (xcb_present_configure_notify_event_t*)event;

                            printf ("XCB_PRESENT_CONFIGURE_NOTIFY: (%d, %d) [%dx%d]\n",
                                    config_ev->x, config_ev->y, config_ev->width, config_ev->height);
#endif
                        }
                        break;

                        case XCB_PRESENT_COMPLETE_NOTIFY:
                        {
#ifdef _DEBUG_
                            printf ("XCB_PRESENT_COMPLETE_NOTIFY:\n");
#endif
                            xcb_present_complete_notify_event_t* complete_ev;
                            complete_ev = (xcb_present_complete_notify_event_t*)event;

                            if (complete_ev->kind == XCB_PRESENT_COMPLETE_KIND_PIXMAP)
                            {
#ifdef _DEBUG_
                                printf ("  XCB_PRESENT_COMPLETE_KIND_PIXMAP, msc = %llu, ", complete_ev->msc);
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
#else
                                fps_calc( &start );
                                clock_gettime( CLOCK_REALTIME, &start );
#endif
                                // complete_ev->msc it's number of frame that will be displayed after
                                // vblank end, in my opininion,
                                // but we haven't much time to draw and present our frame to
                                // this msc, so we make one frame stock

                                // draw several frames in bo or pixmap and present these
                                // pixmaps to consistent msc
                                for (i = 0; i < gr_ctx.num_of_bufs; i++)
                                {
                                    draw (); // draw our mesh
                                    swap_buffers (i + complete_ev->msc + 1);
                                }
                            }
                            else if (complete_ev->kind == XCB_PRESENT_COMPLETE_KIND_NOTIFY_MSC)
                            {
#ifdef _DEBUG_
                                printf ("XCB_PRESENT_COMPLETE_KIND_NOTIFY_MSC\n");
#endif
                            }
                        }
                        break;

                        case XCB_PRESENT_IDLE_NOTIFY:
                        {
                            xcb_present_idle_notify_event_t* idle_ev;
                            idle_ev = (xcb_present_idle_notify_event_t*)event;

                            if (idle_ev->serial >= gr_ctx.num_of_bufs)
                            {
                                set_error ("invalid parameters from present extension.\n");
                                pthread_cancel(thread_id);
                            }

                            // unblock pixmap/bo to next draw and swap operations
                            swap_buffer_mng.pxmap_busy[idle_ev->serial] = 0;

#ifdef _DEBUG_
                            printf ("XCB_PRESENT_IDLE_NOTIFY: pixmap: 0x%x, serial: %u\n",
                                    idle_ev->pixmap, idle_ev->serial);
#endif
                        }
                        break;

                        default:
                        break;
                    }
                }
            } //case XCB_GE_GENERIC:
            break;

            default:
#ifdef _DEBUG_

                printf ("dri3_loop: default case.\n"
                        "  event->response_type = %d.\n", event->response_type);
#endif
            break;
        } //switch (event->response_type)
    } //while (1)
}
