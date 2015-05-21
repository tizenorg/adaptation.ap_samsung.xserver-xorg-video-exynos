#ifndef DATA_H
#define DATA_H
#include <xcb/xcb.h>
#include <xcb/shm.h>
#include <xcb/xv.h>
#include "xv_test.h"

typedef struct
{
    xcb_shm_seg_t shm_seg_id;
    void          *shm_seg_addr;
    error_s error;
} shm_data_s;

shm_data_s get_image_random (xcb_connection_t *xcb_conn_p, xcb_screen_t *screen,
                             size_t * sizes, int num_planes);

#endif // DATA_H
