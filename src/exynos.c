/**************************************************************************

xserver-xorg-video-exynos

Copyright 2011 Samsung Electronics co., Ltd. All Rights Reserved.

Contact: SooChan Lim <sc1.lim@samsung.com>

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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <xorg-server.h>
#include <sys/stat.h>
#include <fcntl.h>

/* all driver need this */
#include "xf86.h"
#include "xf86_OSproc.h"

#include "fb.h"
#include "mipointer.h"
#include "micmap.h"
#include "colormapst.h"
#include "xf86cmap.h"
#include "xf86xv.h"
#include "xf86Crtc.h"
#include "exynos.h"
#include "exynos_display.h"
#include "exynos_plane.h"
#include "exynos_accel.h"
#include "exynos_xberc.h"
#include "exynos_util.h"
#include "exynos_wb.h"
#include "exynos_crtc.h"
#include <tbm_bufmgr.h>
#include "fimg2d.h"
#include "exynos_output.h"
#include "exynos_layer_manager.h"

#define OPTION_FLIP_BUFFERS 0
#define EXYNOS_DRM_NAME "exynos"

/* prototypes */
static const OptionInfoRec *EXYNOSAvailableOptions(int chipid, int busid);
static void EXYNOSIdentify(int flags);
static Bool EXYNOSProbe(DriverPtr pDrv, int flags);
static Bool EXYNOSPreInit(ScrnInfoPtr pScrn, int flags);
static Bool EXYNOSScreenInit(ScreenPtr pScreen, int argc, char **argv);
static Bool EXYNOSSwitchMode(ScrnInfoPtr pScrn, DisplayModePtr pMode);
static void EXYNOSAdjustFrame(ScrnInfoPtr pScrn, int x, int y);
static Bool EXYNOSEnterVT(ScrnInfoPtr pScrn);
static void EXYNOSLeaveVT(ScrnInfoPtr pScrn);
static ModeStatus EXYNOSValidMode(ScrnInfoPtr pScrn, DisplayModePtr pMode,
                                  Bool verbose, int flags);
static Bool EXYNOSCloseScreen(ScreenPtr pScreen);
static Bool EXYNOSCreateScreenResources(ScreenPtr pScreen);

#if HAVE_UDEV
static void EXYNOSUdevEventsHandler(int fd, void *closure);
#endif

/* This DriverRec must be defined in the driver for Xserver to load this driver */
_X_EXPORT DriverRec EXYNOS = {
    EXYNOS_VERSION,
    EXYNOS_DRIVER_NAME,
    EXYNOSIdentify,
    EXYNOSProbe,
    EXYNOSAvailableOptions,
    NULL,
    0,
    NULL,
};

/* Supported "chipsets" */
static SymTabRec EXYNOSChipsets[] = {
    {0, "exynos"},
    {-1, NULL}
};

/* Supported options */
typedef enum {
    OPTION_DRI2,
    OPTION_EXA,
    OPTION_SWEXA,
    OPTION_ROTATE,
    OPTION_SNAPSHOT,
    OPTION_WB,
#if OPTION_FLIP_BUFFERS
    OPTION_FLIPBUFS,
#endif
    OPTION_CACHABLE,
    OPTION_SCANOUT,
    OPTION_ACCEL2D,
    OPTION_PRESENT,
    OPTION_DRI3,
    OPTION_PARTIAL_UPDATE,
    OPTION_HWC,
    OPTION_HWA,
    OPTION_SET_PLANE,
    OPTION_PAGE_FLIP
} EXYNOSOpts;

static const OptionInfoRec EXYNOSOptions[] = {
    {OPTION_DRI2, "dri2", OPTV_BOOLEAN, {0}, FALSE},
    {OPTION_EXA, "exa", OPTV_BOOLEAN, {0}, FALSE},
    {OPTION_SWEXA, "sw_exa", OPTV_BOOLEAN, {0}, FALSE},
    {OPTION_ROTATE, "rotate", OPTV_STRING, {0}, FALSE},
    {OPTION_SNAPSHOT, "snapshot", OPTV_STRING, {0}, FALSE},
    {OPTION_WB, "wb", OPTV_BOOLEAN, {0}, FALSE},
#if OPTION_FLIP_BUFFERS
    {OPTION_FLIPBUFS, "flip_bufs", OPTV_INTEGER, {0}, 3},
#endif
    {OPTION_CACHABLE, "cachable", OPTV_BOOLEAN, {0}, FALSE},
    {OPTION_SCANOUT, "scanout", OPTV_BOOLEAN, {0}, FALSE},
    {OPTION_ACCEL2D, "accel_2d", OPTV_BOOLEAN, {0}, FALSE},
    {OPTION_PRESENT, "present", OPTV_BOOLEAN, {0}, FALSE},
    {OPTION_DRI3, "dri3", OPTV_BOOLEAN, {0}, FALSE},
    {OPTION_PARTIAL_UPDATE, "partial_update", OPTV_BOOLEAN, {0}, FALSE},
    {OPTION_HWC, "hwc", OPTV_BOOLEAN, {0}, FALSE},
    {OPTION_HWA, "hwa", OPTV_BOOLEAN, {0}, FALSE},
    {OPTION_SET_PLANE, "set_plane", OPTV_BOOLEAN, {0}, FALSE},
    {OPTION_PAGE_FLIP, "flip", OPTV_BOOLEAN, {0}, FALSE},
    {-1, NULL, OPTV_NONE, {0}, FALSE}
};

/* -------------------------------------------------------------------- */
#ifdef XFree86LOADER

MODULESETUPPROTO(EXYNOSSetup);

static XF86ModuleVersionInfo EXYNOSVersRec = {
    "exynos",
    MODULEVENDORSTRING,
    MODINFOSTRING1,
    MODINFOSTRING2,
    XORG_VERSION_CURRENT,
    PACKAGE_VERSION_MAJOR,
    PACKAGE_VERSION_MINOR,
    PACKAGE_VERSION_PATCHLEVEL,
    ABI_CLASS_VIDEODRV,
    ABI_VIDEODRV_VERSION,
    NULL,
    {0, 0, 0, 0}
};

_X_EXPORT XF86ModuleData exynosModuleData =
    { &EXYNOSVersRec, EXYNOSSetup, NULL };

pointer
EXYNOSSetup(pointer module, pointer opts, int *errmaj, int *errmin)
{
    static Bool setupDone = FALSE;

    if (!setupDone) {
        setupDone = TRUE;
        xf86AddDriver(&EXYNOS, module, HaveDriverFuncs);
        return (pointer) 1;
    }
    else {
        if (errmaj)
            *errmaj = LDR_ONCEONLY;
        return NULL;
    }
}

#endif                          /* XFree86LOADER */
/* -------------------------------------------------------------------- */

static int
_exynosOpenDRM(void)
{
    int fd = -1;

    fd = drmOpen(EXYNOS_DRM_NAME, NULL);
#ifdef HAVE_UDEV
    if (fd < 0) {
        struct udev *udev;
        struct udev_enumerate *e;
        struct udev_list_entry *entry;
        struct udev_device *device, *drm_device, *device_parent;
        const char *filename;

        xf86Msg(X_WARNING, "[DRM] Cannot open drm device.. search by udev\n");
        udev = udev_new();
        if (!udev) {
            xf86Msg(X_ERROR, "[DRM] fail to initialize udev context\n");
            goto close_l;
        }
        /* Will try to find sys path /exynos-drm/drm/card0 */
        e = udev_enumerate_new(udev);
        udev_enumerate_add_match_subsystem(e, "drm");
        udev_enumerate_add_match_sysname(e, "card[0-9]*");
        udev_enumerate_scan_devices(e);

        drm_device = NULL;
        udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(e)) {
            device = udev_device_new_from_syspath(udev_enumerate_get_udev(e),
                                                  udev_list_entry_get_name
                                                  (entry));
            device_parent = udev_device_get_parent(device);
            /* Not need unref device_parent. device_parent and device have same refcnt */
            if (device_parent) {
                if (strcmp(udev_device_get_sysname(device_parent), "exynos-drm")
                    == 0) {
                    drm_device = device;
                    xf86Msg(X_INFO, "Found drm device: '%s' (%s)\n",
                            udev_device_get_syspath(drm_device),
                            udev_device_get_sysname(device_parent));
                    break;
                }
            }
            udev_device_unref(device);
        }

        if (drm_device == NULL) {
            xf86Msg(X_ERROR, "[DRM] fail to find drm device\n");
            udev_enumerate_unref(e);
            udev_unref(udev);
            goto close_l;
        }

        filename = udev_device_get_devnode(drm_device);

        fd = open(filename, O_RDWR | O_CLOEXEC);
        if (fd < 0) {
            xf86Msg(X_ERROR, "[DRM] Cannot open drm device(%s)\n", filename);
        }
        udev_device_unref(drm_device);
        udev_enumerate_unref(e);
        udev_unref(udev);
    }
#endif
 close_l:
    return fd;
}

static void
_exynosCloseDRM(int fd)
{
    if (fd < 0)
        return;
    if (drmClose(fd) < 0) {
        xf86Msg(X_ERROR, "[DRM] Cannot close drm device fd=%d\n", fd);
    }
}

/*
 * Probing the device with the device node, this probing depend on the specific hw.
 * This function just verify whether the display hw is avaliable or not.
 */
static Bool
_exynosHwProbe(struct pci_device *pPci, char *device, char **namep)
{
    int fd = -1;

    fd = _exynosOpenDRM();
    if (fd < 0) {
        return FALSE;
    }

    _exynosCloseDRM(fd);
    return TRUE;
}

static tbm_bufmgr
_exynosInitBufmgr(int drm_fd, void *arg)
{
    tbm_bufmgr bufmgr = NULL;

    /* get buffer manager */
    setenv("BUFMGR_LOCK_TYPE", "once", 1);
    setenv("BUFMGR_MAP_CACHE", "true", 1);
    bufmgr = tbm_bufmgr_init(drm_fd);

    if (bufmgr == NULL)
        return NULL;

    return bufmgr;
}

static void
_exynosDeInitBufmgr(tbm_bufmgr bufmgr)
{
    if (bufmgr)
        tbm_bufmgr_deinit(bufmgr);
}

/* open drm */
static Bool
_openDrmMaster(ScrnInfoPtr pScrn)
{
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    int ret;

    /* open drm */
    pExynos->drm_fd = _exynosOpenDRM();
    if (pExynos->drm_fd < 0) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "[DRM] Cannot open drm device\n");
        goto fail_to_open_drm_master;
    }

    pExynos->drm_device_name = drmGetDeviceNameFromFd(pExynos->drm_fd);
    xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
               "[DRM] Succeed get drm device name:%s\n",
               pExynos->drm_device_name);

    /* enable drm vblank */
    ret = drmCtlInstHandler(pExynos->drm_fd, 217);
    if (ret) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "[DRM] Fail to enable drm VBlank(%d)\n", ret);
        goto fail_to_open_drm_master;
    }

    xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
               "[DRM] Enable drm VBlank(%d)\n", ret);

    /* initialize drm bufmgr */
    pExynos->tbm_bufmgr = _exynosInitBufmgr(pExynos->drm_fd, NULL);
    if (pExynos->tbm_bufmgr == NULL) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "[DRM] Error : bufmgr initialization failed\n");
        goto fail_to_open_drm_master;
    }

    xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "[DRM] Enable buffer manager\n");

    return TRUE;

 fail_to_open_drm_master:

    if (pExynos->tbm_bufmgr) {
        _exynosDeInitBufmgr(pExynos->tbm_bufmgr);
        pExynos->tbm_bufmgr = NULL;
    }

    if (pExynos->drm_device_name) {
        free(pExynos->drm_device_name);
        pExynos->drm_device_name = NULL;
    }

    if (pExynos->drm_fd >= 0) {
        _exynosCloseDRM(pExynos->drm_fd);
        pExynos->drm_fd = -1;
    }

    return FALSE;
}

/* close drm */
static void
_closeDrmMaster(ScrnInfoPtr pScrn)
{
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);

    if (pExynos->tbm_bufmgr) {
        _exynosDeInitBufmgr(pExynos->tbm_bufmgr);
        pExynos->tbm_bufmgr = NULL;
    }

    if (pExynos->drm_device_name) {
        free(pExynos->drm_device_name);
        pExynos->drm_device_name = NULL;
    }

    if (pExynos->drm_fd >= 0) {
        _exynosCloseDRM(pExynos->drm_fd);
        pExynos->drm_fd = -1;
    }
}

/*
 * Initialize the device Probing the device with the device node,
 * this probing depend on the specific hw.
 * This function just verify whether the display hw is avaliable or not.
 */
static Bool
_exynosHwInit(ScrnInfoPtr pScrn, struct pci_device *pPci, char *device)
{
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);

    /* init drm master */
    if (_openDrmMaster(pScrn) == TRUE)
        xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "DRM BLANK is enabled\n");
    else
        xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "DRM BLANK is disabled\n");

    if (g2d_init(pExynos->drm_fd)) {
        xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "G2D is enabled\n");
        pExynos->is_accel_2d = TRUE;
    }
    else
        xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "G2D is disabled\n");
#ifdef NO_CRTC_MODE
   /*** Temporary disable G2D acceleration for using PIXMAN ***/
    pExynos->is_accel_2d = FALSE;
#endif
    return TRUE;
}

/* SigHook */
OsSigWrapperPtr old_sig_wrapper;
int
_exynosOsSigWrapper(int sig)
{
    XDBG_KLOG(MSEC, "Catch SIG: %d\n", sig);

    return old_sig_wrapper(sig);        /*Contiue */
}

/*
 * DeInitialize the hw
 */
static void
_exynosHwDeinit(ScrnInfoPtr pScrn)
{
    g2d_fini();

    /* deinit drm master */
    _closeDrmMaster(pScrn);

    return;
}

static Bool
_allocScrnPrivRec(ScrnInfoPtr pScrn)
{
    if (pScrn->driverPrivate != NULL)
        return TRUE;

    pScrn->driverPrivate = calloc(sizeof(EXYNOSRec), 1);
    if (!pScrn->driverPrivate)
        return FALSE;

    return TRUE;
}

static void
_freeScrnPrivRec(ScrnInfoPtr pScrn)
{
    if (pScrn->driverPrivate == NULL)
        return;
    free(pScrn->driverPrivate);
    pScrn->driverPrivate = NULL;
}

/*
 * Check the driver option.
 * Set the option flags to the driver private
 */
static void
_checkDriverOptions(ScrnInfoPtr pScrn)
{
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    const char *s;
    int flip_bufs = 3;

    /* exa */
    if (xf86ReturnOptValBool(pExynos->Options, OPTION_EXA, TRUE))
        pExynos->is_exa = TRUE;

    /* sw exa */
    if (pExynos->is_exa) {
        if (xf86ReturnOptValBool(pExynos->Options, OPTION_SWEXA, TRUE))
            pExynos->is_sw_exa = TRUE;
    }

    /* dri2 */
    if (xf86ReturnOptValBool(pExynos->Options, OPTION_DRI2, FALSE)) {
        pExynos->is_dri2 = TRUE;

        /* number of the flip buffers */
#if OPTION_FLIP_BUFFERS
        if (xf86GetOptValInteger(pExynos->Options, OPTION_FLIPBUFS, &flip_bufs))
            pExynos->flip_bufs = flip_bufs;
        else
#endif
        {
            /* default is 3 */
            flip_bufs = 3;
            pExynos->flip_bufs = flip_bufs;
        }
    }

#ifdef HAVE_DRI3_PRESENT_H
    /* present */
    if (xf86ReturnOptValBool(pExynos->Options, OPTION_PRESENT, FALSE)) {
        pExynos->is_present = TRUE;
    }

    /* dri3 */
    if (xf86ReturnOptValBool(pExynos->Options, OPTION_DRI3, FALSE)) {
        pExynos->is_dri3 = TRUE;
    }
#endif

    /* rotate */
    pExynos->rotate = RR_Rotate_0;
    if ((s = xf86GetOptValString(pExynos->Options, OPTION_ROTATE))) {
        if (!xf86NameCmp(s, "CW")) {
            pExynos->rotate = RR_Rotate_90;
            xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
                       "rotating screen clockwise\n");
        }
        else if (!xf86NameCmp(s, "CCW")) {
            pExynos->rotate = RR_Rotate_270;
            xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
                       "rotating screen counter-clockwise\n");
        }
        else if (!xf86NameCmp(s, "UD")) {
            pExynos->rotate = RR_Rotate_180;
            xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
                       "rotating screen upside-down\n");
        }
        else {
            xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "\"%s\" is not valid option",
                       s);
        }
    }

    /* wb */
    if (xf86ReturnOptValBool(pExynos->Options, OPTION_WB, FALSE)) {
        if (xf86ReturnOptValBool(pExynos->Options, OPTION_WB, TRUE))
            pExynos->is_wb_clone = TRUE;
    }

    /* cachable */
    if (xf86ReturnOptValBool(pExynos->Options, OPTION_CACHABLE, FALSE)) {
        if (xf86ReturnOptValBool(pExynos->Options, OPTION_CACHABLE, TRUE)) {
            pExynos->cachable = TRUE;
            xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "Use cachable buffer.\n");
        }
    }

    /* scanout */
    if (xf86ReturnOptValBool(pExynos->Options, OPTION_SCANOUT, FALSE)) {
        if (xf86ReturnOptValBool(pExynos->Options, OPTION_SCANOUT, TRUE)) {
            pExynos->scanout = TRUE;
            xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "Use scanout buffer.\n");
        }
    }

    /* hw_2d */
    if (xf86ReturnOptValBool(pExynos->Options, OPTION_ACCEL2D, FALSE)) {
        if (xf86ReturnOptValBool(pExynos->Options, OPTION_ACCEL2D, TRUE)) {
            pExynos->is_accel_2d = TRUE;
            xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "Use 2d accelerator.\n");
        }
    }

    /* use_partial_update */
    if (xf86ReturnOptValBool(pExynos->Options, OPTION_PARTIAL_UPDATE, FALSE)) {
        if (xf86ReturnOptValBool(pExynos->Options, OPTION_PARTIAL_UPDATE, TRUE)) {
            pExynos->use_partial_update = TRUE;
            xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "Use partial update.\n");
        }
    }

    /* page_flip */
    pExynos->use_flip = TRUE;
    if (!xf86ReturnOptValBool(pExynos->Options, OPTION_PAGE_FLIP, TRUE)) {
        pExynos->use_flip = FALSE;
        xf86DrvMsg(pScrn->scrnIndex, X_WARNING, "Page flip DISABLE\n");
    }

    /* HWC default */
    pExynos->use_hwc = FALSE;
    /* default value without HWC */
    pExynos->use_setplane = FALSE;
#ifdef HAVE_HWC_H
    if (xf86ReturnOptValBool(pExynos->Options, OPTION_HWC, FALSE)) {
        pExynos->use_hwc = TRUE;
        xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "Use HWC extension.\n");
        /* default value with HWC */
        pExynos->use_setplane = TRUE;
    }
#endif

#ifdef HAVE_HWA_H
    if (xf86ReturnOptValBool(pExynos->Options, OPTION_HWA, FALSE)) {
        pExynos->use_hwa = TRUE;
        xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "Use HWA extension.\n");
    }
#endif

    /* set_plane */
    if (xf86ReturnOptValBool(pExynos->Options, OPTION_SET_PLANE, FALSE)) {
        pExynos->use_setplane = TRUE;
    }

    if (pExynos->use_flip) {
        if (pExynos->use_setplane)
            xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "Page flip using plane\n");
        else
            xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
                       "Page flip using crtc (drmModePegeFlip())\n");
    }

}

#if HAVE_UDEV
static void
_exynosUdevInit(ScrnInfoPtr pScrn)
{
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    struct udev *u;
    struct udev_monitor *mon;

    xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "hotplug detection\n");

    u = udev_new();
    if (!u)
        return;

    mon = udev_monitor_new_from_netlink(u, "udev");
    if (!mon) {
        udev_unref(u);
        return;
    }

    if (udev_monitor_filter_add_match_subsystem_devtype(mon, "drm", "drm_minor")
        > 0 || udev_monitor_enable_receiving(mon) < 0) {
        udev_monitor_unref(mon);
        udev_unref(u);
        return;
    }

    pExynos->uevent_handler =
        xf86AddGeneralHandler(udev_monitor_get_fd(mon), EXYNOSUdevEventsHandler,
                              pScrn);
    if (!pExynos->uevent_handler) {
        udev_monitor_unref(mon);
        udev_unref(u);
        return;
    }

    pExynos->uevent_monitor = mon;
}

static void
_exynosUdevDeinit(ScrnInfoPtr pScrn)
{
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);

    if (pExynos->uevent_handler) {
        struct udev *u = udev_monitor_get_udev(pExynos->uevent_monitor);

        udev_monitor_unref(pExynos->uevent_monitor);
        udev_unref(u);
        pExynos->uevent_handler = NULL;
        pExynos->uevent_monitor = NULL;
    }

}

static Bool
EXYNOSSaveScreen(ScreenPtr pScreen, int mode)
{
    /* dummpy save screen */
    return TRUE;
}

static void
EXYNOSUdevEventsHandler(int fd, void *closure)
{
    ScrnInfoPtr pScrn = closure;
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    struct udev_device *dev;
    const char *hotplug;
    struct stat s;
    dev_t udev_devnum;
    int ret;

    dev = udev_monitor_receive_device(pExynos->uevent_monitor);
    if (!dev)
        return;

    udev_devnum = udev_device_get_devnum(dev);

    ret = fstat(pExynos->drm_fd, &s);
    if (ret == -1)
        return;

    /*
     * Check to make sure this event is directed at our
     * device (by comparing dev_t values), then make
     * sure it's a hotplug event (HOTPLUG=1)
     */
    hotplug = udev_device_get_property_value(dev, "HOTPLUG");

    if (memcmp(&s.st_rdev, &udev_devnum, sizeof(dev_t)) == 0 &&
        hotplug && atoi(hotplug) == 1) {
        XDBG_INFO(MSEC, "EXYNOS-UDEV: HotPlug\n");
        exynosOutputDrmUpdate(pScrn);
        RRGetInfo(xf86ScrnToScreen(pScrn), TRUE);
#ifdef NO_CRTC_MODE
//        exynosDisplayChangeMode(pScrn);
#endif
    }

    udev_device_unref(dev);
}
#endif                          /* UDEV_HAVE */

static const OptionInfoRec *
EXYNOSAvailableOptions(int chipid, int busid)
{
    return EXYNOSOptions;
}

static void
EXYNOSIdentify(int flags)
{
    xf86PrintChipsets(EXYNOS_NAME, "driver for Exynos Chipsets",
                      EXYNOSChipsets);
}

/* The purpose of this function is to identify all instances of hardware supported
 * by the driver. The probe must find the active device exynostions that match the driver
 * by calling xf86MatchDevice().
 */
static Bool
EXYNOSProbe(DriverPtr pDrv, int flags)
{
    int i;
    ScrnInfoPtr pScrn;
    GDevPtr *ppDevSections = NULL;
    int numDevSections;
    int entity;
    Bool foundScreen = FALSE;

    /* check the drm mode setting */
    if (!_exynosHwProbe(NULL, NULL, NULL)) {
        return FALSE;
    }

    numDevSections = xf86MatchDevice(EXYNOS_DRIVER_NAME, &ppDevSections);

    if ((flags & PROBE_DETECT) && (numDevSections <= 0)) {
        numDevSections = 1;
    }

    for (i = 0; i < numDevSections; i++) {

        if (flags & PROBE_DETECT) {
            xf86AddBusDeviceToConfigure(EXYNOS_DRIVER_NAME, BUS_NONE, NULL, i);
            foundScreen = TRUE;
            continue;
        }

        pScrn = xf86AllocateScreen(pDrv, flags);

        if (ppDevSections) {
            entity = xf86ClaimNoSlot(pDrv, 0, ppDevSections[i], TRUE);
            xf86AddEntityToScreen(pScrn, entity);
        }

        if (pScrn) {
            foundScreen = TRUE;

            pScrn->driverVersion = EXYNOS_VERSION;
            pScrn->driverName = EXYNOS_DRIVER_NAME;
            pScrn->name = EXYNOS_NAME;
            pScrn->Probe = EXYNOSProbe;
            pScrn->PreInit = EXYNOSPreInit;
            pScrn->ScreenInit = EXYNOSScreenInit;
            pScrn->SwitchMode = EXYNOSSwitchMode;
            pScrn->AdjustFrame = EXYNOSAdjustFrame;
            pScrn->EnterVT = EXYNOSEnterVT;
            pScrn->LeaveVT = EXYNOSLeaveVT;
            pScrn->ValidMode = EXYNOSValidMode;

            xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                       "using drm mode setting device\n");
        }
    }
    if (ppDevSections) {
        free(ppDevSections);
    }

    return foundScreen;
}

/*
 * This is called before ScreenInit to probe the screen configuration.
 * The main tasks to do in this funtion are probing, module loading, option handling,
 * card mapping, and mode setting setup.
 */
static Bool
EXYNOSPreInit(ScrnInfoPtr pScrn, int flags)
{
    EXYNOSPtr pExynos;
    Gamma defualt_gamma = { 0.0, 0.0, 0.0 };
    rgb default_weight = { 0, 0, 0 };
    int flag24;

    if (flags & PROBE_DETECT)
        return FALSE;

    /* allocate private */
    if (!_allocScrnPrivRec(pScrn))
        return FALSE;
    pExynos = EXYNOSPTR(pScrn);

    /* Check the number of entities, and fail if it isn't one. */
    if (pScrn->numEntities != 1)
        return FALSE;

    pExynos->pEnt = xf86GetEntityInfo(pScrn->entityList[0]);

    /* initialize the hardware specifics */
    if (!_exynosHwInit(pScrn, NULL, NULL)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "fail to initialize hardware\n");
        goto bail1;
    }

    pScrn->displayWidth = 640;  /*default width */
    pScrn->monitor = pScrn->confScreen->monitor;
    pScrn->progClock = TRUE;
    pScrn->rgbBits = 8;

    /* set the depth and the bpp to pScrn */
    flag24 = Support24bppFb | Support32bppFb;
    if (!xf86SetDepthBpp(pScrn, 0, 0, 0, flag24)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "fail to find the depth\n");
        goto bail1;
    }
    xf86PrintDepthBpp(pScrn);   /* just print out the depth and the bpp */

    /* color weight */
    if (!xf86SetWeight(pScrn, default_weight, default_weight)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "fail to set the color weight of RGB\n");
        goto bail1;
    }

    /* visual init, make a TrueColor, -1 */
    if (!xf86SetDefaultVisual(pScrn, -1)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "fail to initialize the default visual\n");
        goto bail1;
    }

    /* Collect all the option flags (fill in pScrn->options) */
    xf86CollectOptions(pScrn, NULL);

    /*
     * Process the options based on the information EXYNOSOptions.
     * The results are written to pExynos->Options. If all the options
     * processing is done within this fuction a local variable "options"
     * can be used instead of pExynos->Options
     */
    if (!(pExynos->Options = malloc(sizeof(EXYNOSOptions))))
        goto bail1;
    memcpy(pExynos->Options, EXYNOSOptions, sizeof(EXYNOSOptions));
    xf86ProcessOptions(pScrn->scrnIndex, pExynos->pEnt->device->options,
                       pExynos->Options);

    /* Check with the driver options */
    _checkDriverOptions(pScrn);

    /* use a fake root pixmap when rotation angle is 90 or 270 */
    pExynos->fake_root =
        ((pExynos->rotate & (RR_Rotate_90 | RR_Rotate_270)) != 0);

    /* drm mode init:: Set the Crtc,  the default Output, and the current Mode */
    if (!exynosModePreInit(pScrn, pExynos->drm_fd)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "fail to initialize drm mode setting\n");
        goto bail1;
    }

    /* set gamma */
    if (!xf86SetGamma(pScrn, defualt_gamma)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "fail to set the gamma\n");
        goto bail1;
    }
#ifdef NO_CRTC_MODE
    if (pScrn->modes == NULL) {
        pScrn->modes = xf86ModesAdd(pScrn->modes,
                                    xf86CVTMode(pScrn->virtualX,
                                                pScrn->virtualY, 60, 0, 0));
    }
#endif
    pScrn->currentMode = pScrn->modes;

    pScrn->displayWidth = pScrn->virtualX;
    xf86PrintModes(pScrn);      /* just print the current mode */

    /* set dpi */
    xf86SetDpi(pScrn, 0, 0);

    /* Load modules */
    if (!xf86LoadSubModule(pScrn, "fb")) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "fail to load fb module\n");
        goto bail1;
    }

    if (!xf86LoadSubModule(pScrn, "exa")) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "fail to load exa module\n");
        goto bail1;
    }

    if (!xf86LoadSubModule(pScrn, "dri2")) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "fail to load dri2 module\n");
        goto bail1;
    }

    old_sig_wrapper = OsRegisterSigWrapper(_exynosOsSigWrapper);
    return TRUE;

 bail1:
    _freeScrnPrivRec(pScrn);
    _exynosHwDeinit(pScrn);
    return FALSE;
}

static Bool
EXYNOSScreenInit(ScreenPtr pScreen, int argc, char **argv)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    VisualPtr visual;
    int init_picture = 0;
    EXYNOSFbPtr pFb = NULL;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
               "Infomation of Visual is \n\tbitsPerPixel=%d, depth=%d, defaultVisual=%s\n"
               "\tmask: %x,%x,%x, offset: %d,%d,%d\n",
               pScrn->bitsPerPixel,
               pScrn->depth,
               xf86GetVisualName(pScrn->defaultVisual),
               (unsigned int) pScrn->mask.red,
               (unsigned int) pScrn->mask.green,
               (unsigned int) pScrn->mask.blue,
               (int) pScrn->offset.red,
               (int) pScrn->offset.green, (int) pScrn->offset.blue);
#ifdef LAYER_MANAGER
    /* Init Layer manager */
    if (!exynosLayerMngInit(pScrn)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Fail to initialize layer manager\n");
        return FALSE;
    }
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "[LYRM] Initialized layer manager\n");
#endif
    /* initialize the framebuffer */
    /* soolim :: think rotations */

    pFb = exynosFbAllocate(pScrn, pScrn->virtualX, pScrn->virtualY);
    if (!pFb) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "cannot allocate framebuffer\n");
        return FALSE;
    }
    pExynos->pFb = pFb;

    /* mi layer */
    miClearVisualTypes();
    if (!miSetVisualTypes
        (pScrn->depth, TrueColorMask, pScrn->rgbBits, TrueColor)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "visual type setup failed for %d bits per pixel [1]\n",
                   pScrn->bitsPerPixel);
        return FALSE;
    }

    if (!miSetPixmapDepths()) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "pixmap depth setup failed\n");
        return FALSE;
    }

    switch (pScrn->bitsPerPixel) {
    case 16:
    case 24:
    case 32:
        if (!fbScreenInit(pScreen, (void *) ROOT_FB_ADDR, pScrn->virtualX, pScrn->virtualY, pScrn->xDpi, pScrn->yDpi, pScrn->virtualX,  /*Pixel width for framebuffer */
                          pScrn->bitsPerPixel))
            return FALSE;

        init_picture = 1;

        break;
    default:
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "internal error: invalid number of bits per pixel (%d) encountered\n",
                   pScrn->bitsPerPixel);
        break;
    }

    if (pScrn->bitsPerPixel > 8) {
        /* Fixup RGB ordering */
        visual = pScreen->visuals + pScreen->numVisuals;
        while (--visual >= pScreen->visuals) {
            if ((visual->class | DynamicClass) == DirectColor) {
                visual->offsetRed = pScrn->offset.red;
                visual->offsetGreen = pScrn->offset.green;
                visual->offsetBlue = pScrn->offset.blue;
                visual->redMask = pScrn->mask.red;
                visual->greenMask = pScrn->mask.green;
                visual->blueMask = pScrn->mask.blue;
            }
        }
    }

    /* must be after RGB ordering fixed */
    if (init_picture && !fbPictureInit(pScreen, NULL, 0)) {
        xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                   "Render extension initialisation failed\n");
    }

    /* init the exa */
    if (pExynos->is_exa) {
        if (!exynosExaInit(pScreen)) {
            xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                       "EXA initialization failed\n");
        }
        else {
            /* init the dri2 */
            if (pExynos->is_dri2) {
                if (!exynosDri2Init(pScreen)) {
                    xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                               "DRI2 initialization failed\n");
                }
            }

#ifdef HAVE_DRI3_PRESENT_H
            if (pExynos->is_present) {
                if (!exynosPresentScreenInit(pScreen)) {
                    xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                               "Present initialization failed\n");
                }
            }

            /* init the dri3 */
            if (pExynos->is_dri3) {
                if (!exynosDri3ScreenInit(pScreen)) {
                    xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                               "DRI3 initialization failed\n");
                }
            }
#endif
        }
    }

#ifdef HAVE_HWC_H
    if (pExynos->use_hwc) {
        if (!exynosHwcInit(pScreen)) {
            xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                       "HWC initialization failed\n");
        }
        else {
            xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                       "HWC initialization succeed\n");
        }
    }
#endif

#ifdef HAVE_HWA_H
    if (pExynos->use_hwa) {
        if (!exynosHwaInit(pScreen)) {
            xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                       "HWA initialization failed\n");
        }
        else {
            xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                       "HWA initialization succeed\n");
        }
    }
#endif

    /* XVideo Initiailization here */
    if (!exynosVideoInit(pScreen))
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "XVideo extention initialization failed\n");

    xf86SetBlackWhitePixels(pScreen);
    xf86SetBackingStore(pScreen);

    /* use dummy hw_cursro instead of sw_cursor */
    miDCInitialize(pScreen, xf86GetPointerScreenFuncs());
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Initializing HW Cursor\n");
#if 1
    if (!xf86_cursors_init(pScreen, EXYNOS_CURSOR_W, EXYNOS_CURSOR_H,
                           (HARDWARE_CURSOR_TRUECOLOR_AT_8BPP |
                            HARDWARE_CURSOR_BIT_ORDER_MSBFIRST |
                            HARDWARE_CURSOR_INVERT_MASK |
                            HARDWARE_CURSOR_SWAP_SOURCE_AND_MASK |
                            HARDWARE_CURSOR_AND_SOURCE_WITH_MASK |
                            HARDWARE_CURSOR_SOURCE_MASK_INTERLEAVE_64 |
                            HARDWARE_CURSOR_ARGB))) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Hardware cursor initialization failed\n");
    }
#endif
    /* crtc init */
    if (!xf86CrtcScreenInit(pScreen))
        return FALSE;

    /* set the desire mode : set the mode to xf86crtc here */
    xf86SetDesiredModes(pScrn);

    /* colormap */
    if (!miCreateDefColormap(pScreen)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "internal error: miCreateDefColormap failed \n");
        return FALSE;
    }

    if (!xf86HandleColormaps(pScreen, 256, 8, exynosModeLoadPalette, NULL,
                             CMAP_PALETTED_TRUECOLOR))
        return FALSE;

    /* dpms */
    xf86DPMSInit(pScreen, xf86DPMSSet, 0);

    /* screen saver */
    pScreen->SaveScreen = EXYNOSSaveScreen;

    exynosModeInit(pScrn);

    /* Wrap the current CloseScreen function */
    pExynos->CloseScreen = pScreen->CloseScreen;
    pScreen->CloseScreen = EXYNOSCloseScreen;

    /* Wrap the current CloseScreen function */
    pExynos->CreateScreenResources = pScreen->CreateScreenResources;
    pScreen->CreateScreenResources = EXYNOSCreateScreenResources;

#if HAVE_UDEV
    _exynosUdevInit(pScrn);
#endif

#if 0
    /* Init Hooks for memory flush */
    exynosMemoryInstallHooks();
#endif

#if USE_XDBG
    xDbgLogPListInit(pScreen);
#endif
#if 0
    xDbgLogSetLevel(MDRI2, 0);
    xDbgLogSetLevel(MEXA, 0);
    xDbgLogSetLevel(MLYRM, 0);
#endif
#ifdef NO_CRTC_MODE
    pExynos->isCrtcOn = exynosCrtcCheckInUseAll(pScrn);
#endif
    XDBG_KLOG(MSEC, "Init Screen\n");
    return TRUE;
}

static Bool
EXYNOSSwitchMode(ScrnInfoPtr pScrn, DisplayModePtr pMode)
{
    return xf86SetSingleMode(pScrn, pMode, RR_Rotate_0);
}

static void
EXYNOSAdjustFrame(ScrnInfoPtr pScrn, int x, int y)
{
}

static Bool
EXYNOSEnterVT(ScrnInfoPtr pScrn)
{
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    xf86OutputPtr pOutput;
    EXYNOSCrtcPrivPtr pCrtcPriv;
    xf86CrtcConfigPtr pXf86CrtcConfig;
    int ret;
    int i;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
               "EnterVT::Hardware state at EnterVT:\n");

    if (pExynos->drm_fd > 0) {
        ret = drmSetMaster(pExynos->drm_fd);
        if (ret)
            xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                       "EnterVT::failed to set drm_fd %d as master.\n",
                       pExynos->drm_fd);
    }

    pXf86CrtcConfig = XF86_CRTC_CONFIG_PTR(pScrn);
    for (i = 0; i < pXf86CrtcConfig->num_output; i++) {
        pOutput = pXf86CrtcConfig->output[i];
        if (!strcmp(pOutput->name, "LVDS1") || !strcmp(pOutput->name, "HDMI1")) {
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "EnterVT::Find %s Output:\n",
                       pOutput->name);

            pCrtcPriv = pOutput->crtc->driver_private;

            if (pCrtcPriv->bAccessibility ||
                pCrtcPriv->screen_rotate_degree > 0) {
                tbm_bo src_bo = pCrtcPriv->front_bo;
                tbm_bo dst_bo = pCrtcPriv->accessibility_back_bo;

                if (!exynosCrtcExecAccessibility(pOutput->crtc, src_bo, dst_bo)) {
                    xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                               "EnterVT::Fail execute accessibility(output name, %s)\n",
                               pOutput->name);
                }
            }

            /* set current fb to crtc */
            if (!exynosCrtcApply(pOutput->crtc)) {
                xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                           "EnterVT::Fail crtc apply(output name, %s)\n",
                           pOutput->name);
            }
            return TRUE;
        }
    }

    xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
               "EnterVT::failed to find Output.\n");

    return TRUE;
}

static void
EXYNOSLeaveVT(ScrnInfoPtr pScrn)
{
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    int ret;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
               "LeaveVT::Hardware state at LeaveVT:\n");

    if (pExynos->drm_fd > 0) {
        ret = drmDropMaster(pExynos->drm_fd);
        if (ret)
            xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                       "LeaveVT::drmDropMaster failed for drmFD %d: %s\n",
                       pExynos->drm_fd, strerror(errno));
        else
            xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                       "LeaveVT::drmFD dropped master.\n");
    }
}

static ModeStatus
EXYNOSValidMode(ScrnInfoPtr pScrn, DisplayModePtr pMode, Bool verbose,
                int flags)
{
    return MODE_OK;
}

/**
 * Adjust the screen pixmap for the current location of the front buffer.
 * This is done at EnterVT when buffers are bound as long as the resources
 * have already been created, but the first EnterVT happens before
 * CreateScreenResources.
 */
static Bool
EXYNOSCreateScreenResources(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);

    pScreen->CreateScreenResources = pExynos->CreateScreenResources;
    if (!(*pScreen->CreateScreenResources) (pScreen))
        return FALSE;

    /*
     * [TODO]:::::
     * create screen resources
     * set the bo to the screen pixamp private here
     * or create the shadow pixmap for the screen pixamp here
     * or set the fake rotated screen infomation here.
     */

    return TRUE;
}

static Bool
EXYNOSCloseScreen(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);

    exynosWbDestroy();

#if HAVE_UDEV
    _exynosUdevDeinit(pScrn);
#endif

    exynosHwcDeinit(pScreen);
#ifdef LAYER_MANAGER
    exynosLayerMngDeInit(pScrn);
#endif
    exynosVideoFini(pScreen);
    exynosExaDeinit(pScreen);
    exynosModeDeinit(pScrn);
    if (pExynos->pFb) {
        exynosFbFree(pExynos->pFb);
        pExynos->pFb = NULL;
    }

    _exynosHwDeinit(pScrn);

    pScrn->vtSema = FALSE;

    pScreen->CreateScreenResources = pExynos->CreateScreenResources;
    pScreen->CloseScreen = pExynos->CloseScreen;

    XDBG_KLOG(MSEC, "Close Screen\n");
    return (*pScreen->CloseScreen) (pScreen);
}

#define CONV_POINT_TO_KEY(x, y, key)	key = (unsigned long)((((unsigned short)(x&0xFFFF)) << 16) | ((unsigned short)(y&0xFFFF )))
#define CONT_KEY_TO_POINT(key, x, y)	x = (unsigned short)((key&0xFFFF0000)>>16); y=(unsigned short)(key&0xFFFF)

typedef struct {
    tbm_bo bo;
    struct xorg_list link;
} SecFbBoItem, *SecFbBoItemPtr;

static void
_exynosFbFreeBoData(void *data)
{
    XDBG_RETURN_IF_FAIL(data != NULL);

    ScrnInfoPtr pScrn;
    EXYNOSPtr pExynos;
    EXYNOSFbBoDataPtr bo_data = (EXYNOSFbBoDataPtr) data;

    pScrn = bo_data->pScrn;
    pExynos = EXYNOSPTR(pScrn);

    XDBG_DEBUG(MFB, "FreeRender bo_data gem:%d, fb_id:%d, %dx%d+%d+%d\n",
               bo_data->gem_handle, bo_data->fb_id,
               bo_data->pos.x2 - bo_data->pos.x1,
               bo_data->pos.y2 - bo_data->pos.y1, bo_data->pos.x1,
               bo_data->pos.y1);

    if (bo_data->fb_id) {
        drmModeRmFB(pExynos->drm_fd, bo_data->fb_id);
        bo_data->fb_id = 0;
    }

    if (bo_data->pPixmap) {
        pScrn->pScreen->DestroyPixmap(bo_data->pPixmap);
        bo_data->pPixmap = NULL;
    }

    free(bo_data);
    bo_data = NULL;
}

static tbm_bo
_exynosFbCreateBo2(EXYNOSFbPtr pFb, int x, int y, int width, int height,
                   tbm_bo prev_bo, Bool clear)
{
    XDBG_RETURN_VAL_IF_FAIL((pFb != NULL), NULL);
    XDBG_RETURN_VAL_IF_FAIL((width > 0), NULL);
    XDBG_RETURN_VAL_IF_FAIL((height > 0), NULL);

    EXYNOSPtr pExynos = EXYNOSPTR(pFb->pScrn);

    tbm_bo bo = NULL;
    tbm_bo_handle bo_handle2;
    EXYNOSFbBoDataPtr bo_data = NULL;
    unsigned int pitch;
    unsigned int fb_id = 0;
    int ret;
    int flag;

    pitch = width * 4;

    if (!pExynos->cachable)
        flag = TBM_BO_WC;
    else
        flag = TBM_BO_DEFAULT;

    if (prev_bo != NULL) {
        XDBG_RETURN_VAL_IF_FAIL(tbm_bo_size(prev_bo) >= (pitch * height), NULL);

        tbm_bo_delete_user_data(prev_bo, TBM_BO_DATA_FB);

        /* TODO: check flags */

        bo = prev_bo;
    }
    else {
        bo = tbm_bo_alloc(pExynos->tbm_bufmgr, pitch * height, flag);
        XDBG_GOTO_IF_FAIL(bo != NULL, fail);
    }

    if (clear) {
        tbm_bo_handle bo_handle1;

        /* memset 0x0 */
        bo_handle1 = tbm_bo_map(bo, TBM_DEVICE_CPU, TBM_OPTION_WRITE);
        XDBG_RETURN_VAL_IF_FAIL(bo_handle1.ptr != NULL, NULL);

        memset(bo_handle1.ptr, 0x0, pitch * height);
        tbm_bo_unmap(bo);
    }

    bo_handle2 = tbm_bo_get_handle(bo, TBM_DEVICE_DEFAULT);

    /* Create drm fb */
    ret =
        drmModeAddFB(pExynos->drm_fd, width, height, pFb->pScrn->bitsPerPixel,
                     pFb->pScrn->bitsPerPixel, pitch, bo_handle2.u32, &fb_id);
    XDBG_GOTO_IF_ERRNO(ret == Success, fail, -ret);

    if (x == -1)
        x = 0;
    if (y == -1)
        y = 0;

    /* Set bo user data */
    bo_data = calloc(1, sizeof(EXYNOSFbBoDataRec));
    XDBG_GOTO_IF_FAIL(bo_data != NULL, fail);
    bo_data->pFb = pFb;
    bo_data->gem_handle = bo_handle2.u32;
    bo_data->pitch = pitch;
    bo_data->fb_id = fb_id;
    bo_data->pos.x1 = x;
    bo_data->pos.y1 = y;
    bo_data->pos.x2 = x + width;
    bo_data->pos.y2 = y + height;
    bo_data->size = tbm_bo_size(bo);
    bo_data->pScrn = pFb->pScrn;
    XDBG_GOTO_IF_FAIL(tbm_bo_add_user_data
                      (bo, TBM_BO_DATA_FB, _exynosFbFreeBoData), fail);
    XDBG_GOTO_IF_FAIL(tbm_bo_set_user_data
                      (bo, TBM_BO_DATA_FB, (void *) bo_data), fail);

    XDBG_DEBUG(MFB, "CreateRender bo(name:%d, gem:%d, fb_id:%d, %dx%d+%d+%d\n",
               tbm_bo_export(bo), bo_data->gem_handle, bo_data->fb_id,
               bo_data->pos.x2 - bo_data->pos.x1,
               bo_data->pos.y2 - bo_data->pos.y1, bo_data->pos.x1,
               bo_data->pos.y1);

    return bo;
 fail:
    if (bo) {
        exynosRenderBoUnref(bo);
    }

    if (fb_id) {
        drmModeRmFB(pExynos->drm_fd, fb_id);
    }

    if (bo_data) {
        free(bo_data);
        bo_data = NULL;
    }

    return NULL;
}

static tbm_bo
_exynosFbCreateBo(EXYNOSFbPtr pFb, int x, int y, int width, int height)
{
    return _exynosFbCreateBo2(pFb, x, y, width, height, NULL, TRUE);
}

static tbm_bo
_exynosFbRefBo(tbm_bo bo)
{
    return tbm_bo_ref(bo);
}

static int
_exynosFbUnrefBo(tbm_bo bo)
{
    tbm_bo_unref(bo);
    bo = NULL;

    return TRUE;
}

EXYNOSFbPtr
exynosFbAllocate(ScrnInfoPtr pScrn, int width, int height)
{
    //exynosLogSetLevel(MFB, 0);

    XDBG_RETURN_VAL_IF_FAIL((pScrn != NULL), NULL);
    XDBG_RETURN_VAL_IF_FAIL((width > 0), NULL);
    XDBG_RETURN_VAL_IF_FAIL((height > 0), NULL);

    EXYNOSFbPtr pFb = calloc(1, sizeof(EXYNOSFbRec));

    XDBG_GOTO_IF_FAIL((pFb != NULL), fail);

    pFb->pScrn = pScrn;
    pFb->num_bo = 0;
    pFb->width = width;
    pFb->height = height;

    xorg_list_init(&pFb->list_bo);

    /* Create default buffer */
    pFb->default_bo = _exynosFbCreateBo(pFb, 0, 0, width, height);

    XDBG_TRACE(MFB, "Allocate %dx%d\n", width, height);

    return pFb;

 fail:

    return NULL;
}

void
exynosFbFree(EXYNOSFbPtr pFb)
{
    XDBG_RETURN_IF_FAIL(pFb != NULL);

    XDBG_TRACE(MFB, "Free %dx%d, num:%d\n", pFb->width, pFb->height,
               pFb->num_bo);

    if (!xorg_list_is_empty(&pFb->list_bo)) {
        SecFbBoItemPtr item = NULL, tmp = NULL;

        xorg_list_for_each_entry_safe(item, tmp, &pFb->list_bo, link) {
            xorg_list_del(&item->link);
            _exynosFbUnrefBo(item->bo);
            free(item);
            item = NULL;
        }
    }

    if (pFb->default_bo) {
        exynosRenderBoUnref(pFb->default_bo);
        pFb->default_bo = NULL;
    }

    free(pFb);
    pFb = NULL;
}

tbm_bo
exynosFbGetBo(EXYNOSFbPtr pFb, int x, int y, int width, int height,
              Bool onlyIfExists)
{
    EXYNOSFbBoDataPtr bo_data = NULL;
    tbm_bo bo = NULL;
    _X_UNUSED unsigned long key;
    BoxRec box;
    BoxPtr b1, b2;
    int ret = rgnOUT;

    box.x1 = x;
    box.y1 = y;
    box.x2 = x + width;
    box.y2 = y + height;
    b2 = &box;

    if (!xorg_list_is_empty(&pFb->list_bo)) {
        SecFbBoItemPtr item = NULL, tmp = NULL;

        xorg_list_for_each_entry_safe(item, tmp, &pFb->list_bo, link) {
            bo = item->bo;

            tbm_bo_get_user_data(bo, TBM_BO_DATA_FB, (void * *) &bo_data);
            b1 = &bo_data->pos;

            ret = exynosUtilBoxInBox(b1, b2);

            if (ret == rgnIN || ret == rgnSAME) {
                return bo;
            }
#if 0
            else if (ret == rgnPART) {
                if (!onlyIfExists)
                    continue;

                int r2 = exynosUtilBoxInBox(b2, b1);

                if (r2 == rgnIN) {
                    xorg_list_del(&item->link);
                    _exynosFbUnrefBo(bo);
                    free(item);
                    item = NULL;
                    pFb->num_bo--;
                    ret = rgnOUT;
                    break;
                }
            }
#endif
            else if (ret == rgnOUT || ret == rgnPART) {
                continue;
            }
            else {
                return NULL;
            }
        }
    }

    if ((ret == rgnOUT || ret == rgnPART) && !onlyIfExists) {
        SecFbBoItemPtr item;

        CONV_POINT_TO_KEY(x, y, key);

        item = calloc(1, sizeof(SecFbBoItem));
        XDBG_RETURN_VAL_IF_FAIL(item != NULL, NULL);
        if (width == pFb->width && height == pFb->height && x == 0 && y == 0) {
            bo = _exynosFbRefBo(pFb->default_bo);
        }
        else {
            bo = _exynosFbCreateBo(pFb, x, y, width, height);
            if (!bo) {
                free(item);
                item = NULL;
                return NULL;
            }
        }

        item->bo = bo;
        xorg_list_add(&item->link, &pFb->list_bo);
        pFb->num_bo++;

        XDBG_TRACE(MFB, "GetBO num:%d bo:%p name:%d, %dx%d+%d+%d\n",
                   pFb->num_bo, bo, tbm_bo_export(bo), width, height, x, y);
        return bo;
    }

    return NULL;
}

tbm_bo
exynosFbSwapBo(EXYNOSFbPtr pFb, tbm_bo back_bo)
{
    EXYNOSFbBoDataPtr back_bo_data = NULL;
    EXYNOSFbBoDataPtr bo_data = NULL;
    EXYNOSFbBoDataRec tmp_bo_data;
    tbm_bo bo;
    BoxPtr b1, b2;
    SecFbBoItemPtr item = NULL, tmp = NULL;

    XDBG_RETURN_VAL_IF_FAIL(pFb != NULL, NULL);
    XDBG_RETURN_VAL_IF_FAIL(FALSE == xorg_list_is_empty(&pFb->list_bo), NULL);
    XDBG_RETURN_VAL_IF_FAIL(tbm_bo_get_user_data
                            (back_bo, TBM_BO_DATA_FB, (void * *) &back_bo_data),
                            NULL);
    XDBG_RETURN_VAL_IF_FAIL(back_bo_data, NULL);

    b2 = &back_bo_data->pos;

    xorg_list_for_each_entry_safe(item, tmp, &pFb->list_bo, link) {
        bo = item->bo;

        tbm_bo_get_user_data(bo, TBM_BO_DATA_FB, (void * *) &bo_data);
        b1 = &bo_data->pos;
        if (rgnSAME == exynosUtilBoxInBox(b1, b2)) {
            XDBG_DEBUG(MFB, "SwapBO(Back:%d, Front:%d)\n",
                       tbm_bo_export(back_bo), tbm_bo_export(bo));

            if (tbm_bo_swap(bo, back_bo)) {
                memcpy(&tmp_bo_data, bo_data, sizeof(EXYNOSFbBoDataRec));
                memcpy(bo_data, back_bo_data, sizeof(EXYNOSFbBoDataRec));
                memcpy(back_bo_data, &tmp_bo_data, sizeof(EXYNOSFbBoDataRec));
            }
            else
                return NULL;

            return bo;
        }
    }

    return NULL;
}

void
exynosFbResize(EXYNOSFbPtr pFb, int width, int height)
{
    XDBG_RETURN_IF_FAIL(pFb != NULL);

    EXYNOSFbBoDataPtr bo_data = NULL;
    tbm_bo bo, old_bo;
    int ret;
    BoxRec box;
    BoxPtr b1, b2;

    if (pFb->width == width && pFb->height == height)
        return;

    old_bo = pFb->default_bo;

    pFb->width = width;
    pFb->height = height;
    XDBG_TRACE(MFB, "Resize %dx%d, num:%d\n", pFb->width, pFb->height,
               pFb->num_bo);

    box.x1 = 0;
    box.y1 = 0;
    box.x2 = width;
    box.y2 = height;
    b1 = &box;

    if (!xorg_list_is_empty(&pFb->list_bo)) {
        SecFbBoItemPtr item = NULL, tmp = NULL;

        xorg_list_for_each_entry_safe(item, tmp, &pFb->list_bo, link) {
            bo = item->bo;

            tbm_bo_get_user_data(bo, TBM_BO_DATA_FB, (void * *) &bo_data);
            b2 = &bo_data->pos;
            ret = exynosUtilBoxInBox(b1, b2);

            if (ret == rgnIN || ret == rgnSAME)
                continue;

            /* Remove bo */
            XDBG_DEBUG(MFB,
                       "\t unref bo(name:%d, gem:%d, fb_id:%d, %dx%d+%d+%d\n",
                       tbm_bo_export(bo), bo_data->gem_handle, bo_data->fb_id,
                       bo_data->pos.x2 - bo_data->pos.x1,
                       bo_data->pos.y2 - bo_data->pos.y1, bo_data->pos.x1,
                       bo_data->pos.y1);

            xorg_list_del(&item->link);
            exynosRenderBoUnref(bo);
            pFb->num_bo--;
            free(item);
            item = NULL;
        }
    }

    pFb->default_bo = _exynosFbCreateBo(pFb, 0, 0, width, height);
    if (old_bo)
        exynosRenderBoUnref(old_bo);
}

int
exynosFbFindBo(EXYNOSFbPtr pFb, int x, int y, int width, int height,
               int *num_bo, tbm_bo ** bos)
{
    EXYNOSFbBoDataPtr bo_data = NULL;
    int num = 0;
    tbm_bo *l = NULL;
    tbm_bo bo;
    int ret = rgnOUT;
    BoxRec box;
    BoxPtr b1, b2;
    SecFbBoItemPtr item = NULL, tmp = NULL;

    if (xorg_list_is_empty(&pFb->list_bo)) {
        return rgnOUT;
    }

    box.x1 = x;
    box.y1 = y;
    box.x2 = x + width;
    box.y2 = y + height;
    b2 = &box;

    l = calloc(pFb->num_bo, sizeof(tbm_bo));

    xorg_list_for_each_entry_safe(item, tmp, &pFb->list_bo, link) {
        bo = item->bo;

        tbm_bo_get_user_data(bo, TBM_BO_DATA_FB, (void * *) &bo_data);
        if (bo_data == NULL) {
            free(l);
            return rgnOUT;
        }

        b1 = &bo_data->pos;
        ret = exynosUtilBoxInBox(b1, b2);
        XDBG_DEBUG(MFB, "[%d/%d] ret:%d bo(%d,%d,%d,%d) fb(%d,%d,%d,%d)\n",
                   num + 1, pFb->num_bo, ret,
                   b1->x1, b1->y1, b1->x2, b1->y2,
                   b2->x1, b2->y1, b2->x2, b2->y2);

        if (ret == rgnSAME || ret == rgnIN) {
            l[num++] = bo;
            break;
        }
        else if (ret == rgnPART) {
//            l[num++] = bo;
            continue;
        }
        else {
            continue;
        }
    }

    if (num_bo)
        *num_bo = num;
    if (bos) {
        *bos = l;
    }
    else {
        free(l);
    }

    return ret;
}

tbm_bo
exynosFbFindBoByPoint(EXYNOSFbPtr pFb, int x, int y)
{
    EXYNOSFbBoDataPtr bo_data = NULL;
    tbm_bo bo;
    SecFbBoItemPtr item = NULL, tmp = NULL;

    if (xorg_list_is_empty(&pFb->list_bo)) {
        return NULL;
    }

    xorg_list_for_each_entry_safe(item, tmp, &pFb->list_bo, link) {
        bo = item->bo;
        tbm_bo_get_user_data(bo, TBM_BO_DATA_FB, (void * *) &bo_data);
        if ((x >= bo_data->pos.x1) &&
            (x < bo_data->pos.x2) &&
            (y >= bo_data->pos.y1) && (y < bo_data->pos.y2)) {
            return bo;
        }
    }

    return NULL;
}

PixmapPtr
exynosRenderBoGetPixmap(EXYNOSFbPtr pFb, tbm_bo bo)
{
    ScreenPtr pScreen = pFb->pScrn->pScreen;
    PixmapPtr pPixmap;
    EXYNOSFbBoDataPtr bo_data;
    int ret;

    XDBG_RETURN_VAL_IF_FAIL(bo != NULL, NULL);
    XDBG_RETURN_VAL_IF_FAIL(tbm_bo_get_user_data
                            (bo, TBM_BO_DATA_FB, (void **) &bo_data), NULL);

    if (bo_data->pPixmap == NULL) {
        pPixmap = pScreen->CreatePixmap(pFb->pScrn->pScreen, 0, 0,
                                        pFb->pScrn->depth,
                                        CREATE_PIXMAP_USAGE_SUB_FB);
        XDBG_GOTO_IF_FAIL(pPixmap != NULL, fail);

        ret = pScreen->ModifyPixmapHeader(pPixmap,
                                          bo_data->pos.x2 - bo_data->pos.x1,
                                          bo_data->pos.y2 - bo_data->pos.y1,
                                          pFb->pScrn->depth,
                                          pFb->pScrn->bitsPerPixel,
                                          bo_data->pitch, (void *) bo);
        XDBG_GOTO_IF_FAIL(ret != FALSE, fail);
        bo_data->pPixmap = pPixmap;
        XDBG_DEBUG(MFB, "CreateRenderPixmap:%p\n", pPixmap);
    }

    return bo_data->pPixmap;

 fail:
    XDBG_ERROR(MFB, "ERR: CreateRenderPixmap\n");
    if (pPixmap) {
        pScreen->DestroyPixmap(pPixmap);
    }
    return NULL;
}

tbm_bo
exynosRenderBoCreate(ScrnInfoPtr pScrn, int width, int height)
{
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);

    return _exynosFbCreateBo(pExynos->pFb, -1, -1, width, height);
}

int
exynosSwapToRenderBo(ScrnInfoPtr pScrn, int width, int height, tbm_bo carr_bo,
                     Bool clear)
{
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);

    tbm_bo tbo =
        _exynosFbCreateBo2(pExynos->pFb, -1, -1, width, height, carr_bo, clear);

    return tbo ? 1 : 0;
}

tbm_bo
exynosRenderBoRef(tbm_bo bo)
{
    return _exynosFbRefBo(bo);
}

void
exynosRenderBoUnref(tbm_bo bo)
{
    _exynosFbUnrefBo(bo);
}

void
exynosRenderBoSetPos(tbm_bo bo, int x, int y)
{
    EXYNOSFbBoDataPtr bo_data;
    int width, height;

    XDBG_RETURN_IF_FAIL(bo != NULL);
    XDBG_RETURN_IF_FAIL(x >= 0);
    XDBG_RETURN_IF_FAIL(y >= 0);
    XDBG_RETURN_IF_FAIL(tbm_bo_get_user_data
                        (bo, TBM_BO_DATA_FB, (void **) &bo_data));

    width = bo_data->pos.x2 - bo_data->pos.x1;
    height = bo_data->pos.y2 - bo_data->pos.y1;

    bo_data->pos.x1 = x;
    bo_data->pos.y1 = y;
    bo_data->pos.x2 = x + width;
    bo_data->pos.y2 = y + height;
}
