#include <stdlib.h>
#include <stdio.h>
#include "data.h"
#include <xcb/shm.h>
#include <xcb/dri2.h>
#include <tbm_bufmgr.h>
#include <xf86drm.h>
#include <assert.h>
#include <sys/shm.h>
#include <fcntl.h>
#include "xv_types.h"
#include <string.h>

static tbm_bufmgr bufmgr_p = NULL;
typedef struct
{
    int fd;
    error_s error;
} dri_fd_s;

static dri_fd_s
open_dri2 (xcb_connection_t *c, xcb_screen_t *screen)
{
    xcb_dri2_connect_cookie_t dri2_conn_ck;
    xcb_dri2_connect_reply_t *dri2_conn_reply_p = NULL;
    xcb_dri2_authenticate_cookie_t dri2_auth_ck;
    xcb_dri2_authenticate_reply_t *dri2_auth_reply_p = NULL;
    xcb_generic_error_t  *error_p = NULL;
    int error = -1;
    dri_fd_s ret = {};
    char *device_name_p = NULL;
    drm_magic_t drm_magic;
    xcb_window_t window_id = screen->root;

    dri2_conn_ck = xcb_dri2_connect(c, window_id, XCB_DRI2_DRIVER_TYPE_DRI);
    dri2_conn_reply_p = xcb_dri2_connect_reply(c, dri2_conn_ck, &error_p);
    if (error_p)
    {
      ret.error.error_str = "Can't connect to DRI2 driver";
      ret.error.error_code = error_p->error_code;
      goto close;
    }

    device_name_p = strndup(xcb_dri2_connect_device_name (dri2_conn_reply_p),
             xcb_dri2_connect_device_name_length (dri2_conn_reply_p));

    ret.fd = open(device_name_p, O_RDWR);

    if (ret.fd < 0)
    {
        ret.error.error_str = "Can't open DRI2 file descriptor";
        ret.error.error_code = ret.fd;
        goto close;
    }

    error = drmGetMagic(ret.fd, &drm_magic);
    if (error < 0)
    {
        ret.error.error_str = "Can't get drm magic";
        ret.error.error_code = error;
        goto close;
    }

    dri2_auth_ck = xcb_dri2_authenticate(c, window_id, drm_magic);
    dri2_auth_reply_p = xcb_dri2_authenticate_reply(c, dri2_auth_ck, &error_p);
    if (error_p)
    {
        ret.error.error_str = "Can't auth DRI2";
        ret.error.error_code = error_p->error_code;
        goto close;
    }

close:
    if (error_p)
        free(error_p);
    if (dri2_conn_reply_p)
        free(dri2_conn_reply_p);
    if (dri2_auth_reply_p)
        free(dri2_auth_reply_p);
    if (device_name_p)
        free(device_name_p);
    return ret;
}

static shm_data_s
open_shm(xcb_connection_t *c, size_t data_size)
{
    xcb_generic_error_t *error_p = NULL;
    shm_data_s ret_shm = {};

    ret_shm.shm_seg_id = xcb_generate_id (c);

    int shm_id = shmget (IPC_PRIVATE, data_size, IPC_CREAT | 0777);
    if (shm_id == -1)
    {
        ret_shm.error.error_str = "shm alloc error";
        ret_shm.error.error_code = shm_id;
        goto close;
    }

    ret_shm.shm_seg_addr = shmat (shm_id, NULL, 0);
    if (!ret_shm.shm_seg_addr)
    {
        ret_shm.error.error_str = "shm attach error";
        goto close;
    }
    xcb_void_cookie_t xcb_shm_c =
            xcb_shm_attach_checked (c, ret_shm.shm_seg_id, shm_id, 0);
    error_p = xcb_request_check (c, xcb_shm_c);
    if (error_p)
    {
        ret_shm.error.error_str = "xcb_shm attach error";
        goto close;
    }
close:
    shmctl (shm_id, IPC_RMID, NULL);
    if (error_p)
        free(error_p);
    return ret_shm;
}

static tbm_bo
get_data_random (size_t size, tbm_bufmgr bufmgr)
{
    FILE *fp = fopen ("/dev/urandom", "r");
    if (fp == NULL)
    {
        return NULL;
    }

    tbm_bo tbo = tbm_bo_alloc (bufmgr, size, TBM_BO_DEFAULT);
    if (tbo == NULL)
    {
        return NULL;
    }

    /* copy raw data to tizen buffer object */
    tbm_bo_handle bo_handle = tbm_bo_map (tbo, TBM_DEVICE_CPU, TBM_OPTION_WRITE);
    size_t error = fread (bo_handle.ptr, 1, size , fp);
    tbm_bo_unmap (tbo);
    fclose (fp);
    if (error == 0)
    {
        tbm_bo_unref(tbo);
        tbo = NULL;
    }
    return tbo;
}

shm_data_s
get_image_random (xcb_connection_t *p_xcb_conn, xcb_screen_t *screen,
                  size_t *sizes, int num_planes)
{
    shm_data_s ret = {0,};
    XV_DATA_PTR xv_data = NULL;
    if (bufmgr_p == NULL)
    {
        dri_fd_s dri2 = open_dri2 (p_xcb_conn, screen);
        if (dri2.error.error_str)
        {
            ret.error = dri2.error;
            goto close_l;
        }
        bufmgr_p = tbm_bufmgr_init (dri2.fd);
        if (!bufmgr_p)
        {
            ret.error.error_str = "Can't open TBM connection\n";
            ret.error.error_code = 0;
            goto close_l;
        }
    }

    if (sizes == NULL)
    {
        ret.error.error_str = "Sizes argument is NULL";
        goto close_l;
    }

    if (num_planes > 3 || num_planes < 1)
    {
        ret.error.error_str = "Wrong num_planes argument";
        goto close_l;
    }
    size_t full_size = 0;
    int i;
    for (i = 0; i < num_planes; i++)
    {
        full_size += sizes[i];
    }
    ret = open_shm(p_xcb_conn, full_size);
    if (ret.error.error_str)
        goto close_l;

    xv_data = ret.shm_seg_addr;
    XV_INIT_DATA (xv_data);
    tbm_bo temp_bo = NULL;
    switch (num_planes)
    {
    case 3:
        temp_bo = get_data_random (sizes[2], bufmgr_p);
        if (temp_bo == NULL)
        {
            ret.error.error_str = "Can't alloc tbm object";
            goto close_l;
        }
        xv_data->CrBuf = tbm_bo_export(temp_bo);
    case 2:
        temp_bo = get_data_random (sizes[1], bufmgr_p);
        if (temp_bo == NULL)
        {
            ret.error.error_str = "Can't alloc tbm object";
            goto close_l;
        }
        xv_data->CbBuf = tbm_bo_export(temp_bo);
    case 1:
        temp_bo = get_data_random (sizes[0], bufmgr_p);
        if (temp_bo == NULL)
        {
            ret.error.error_str = "Can't alloc tbm object\n";
            goto close_l;
        }
        xv_data->YBuf = tbm_bo_export(temp_bo);
        break;
    }

    return ret;
close_l:
    // TODO: Close DRI
    // TODO: Close tbm
    if (xv_data)
    {
        if (xv_data->YBuf != 0)
            tbm_bo_unref(tbm_bo_import(bufmgr_p, xv_data->YBuf));
        if (xv_data->CbBuf != 0)
            tbm_bo_unref(tbm_bo_import(bufmgr_p, xv_data->CbBuf));
        if (xv_data->CrBuf != 0)
            tbm_bo_unref(tbm_bo_import(bufmgr_p, xv_data->CrBuf));
    }

    return ret;
}

