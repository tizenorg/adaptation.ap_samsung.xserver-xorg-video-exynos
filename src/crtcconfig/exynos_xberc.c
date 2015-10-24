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

#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <dirent.h>

#include <xorgVersion.h>
#include <tbm_bufmgr.h>
#include <xf86Crtc.h>
#include <xf86DDC.h>
#include <xf86cmap.h>
#include <xf86Priv.h>
#include <list.h>
#include <X11/Xatom.h>
#include <X11/extensions/dpmsconst.h>

#include "exynos.h"
#include "exynos_util.h"
#include "exynos_xberc.h"
#include "exynos_output.h"
#include "exynos_crtc.h"
#include "exynos_layer.h"
#include "exynos_wb.h"
#include "exynos_plane.h"
#include "exynos_prop.h"
#include "exynos_drmmode_dump.h"

#define XRRPROPERTY_ATOM	"X_RR_PROPERTY_REMOTE_CONTROLLER"
#define XBERC_BUF_SIZE  8192

static Atom rr_property_atom;

static void _exynosXbercSetReturnProperty(RRPropertyValuePtr value,
                                          const char *f, ...);

static Bool
EXYNOSXbercSetTvoutMode(int argc, char **argv, RRPropertyValuePtr value,
                        ScrnInfoPtr scrn)
{
    EXYNOSPtr pExynos = EXYNOSPTR(scrn);
    EXYNOSModePtr pExynosMode = pExynos->pExynosMode;
    const char *mode_string[] = { "Off", "Clone", "UiClone", "Extension" };
    EXYNOSDisplaySetMode mode;

    XDBG_DEBUG(MSEC, "%s value : %d\n", __FUNCTION__,
               *(unsigned int *) value->data);

    if (argc < 2) {
        _exynosXbercSetReturnProperty(value, "Error : too few arguments\n");
        return TRUE;
    }

    if (argc == 2) {
        _exynosXbercSetReturnProperty(value, "Current Tv Out mode is %d (%s)\n",
                                      pExynosMode->set_mode,
                                      mode_string[pExynosMode->set_mode]);
        return TRUE;
    }

    mode = (EXYNOSDisplaySetMode) atoi(argv[2]);

    if (mode < DISPLAY_SET_MODE_OFF) {
        _exynosXbercSetReturnProperty(value,
                                      "Error : value(%d) is out of range.\n",
                                      mode);
        return TRUE;
    }

    if (mode == pExynosMode->set_mode) {
        _exynosXbercSetReturnProperty(value, "[Xorg] already tvout : %s.\n",
                                      mode_string[mode]);
        return TRUE;
    }

    if (pExynosMode->conn_mode != DISPLAY_CONN_MODE_HDMI &&
        pExynosMode->conn_mode != DISPLAY_CONN_MODE_VIRTUAL) {
        _exynosXbercSetReturnProperty(value, "Error : not connected.\n");
        return TRUE;
    }

    exynosDisplaySetDispSetMode(scrn, mode);

    _exynosXbercSetReturnProperty(value, "[Xorg] tvout : %s.\n",
                                  mode_string[mode]);

    return TRUE;
}

static Bool
EXYNOSXbercSetConnectMode(int argc, char **argv, RRPropertyValuePtr value,
                          ScrnInfoPtr scrn)
{
    EXYNOSPtr pExynos = EXYNOSPTR(scrn);
    EXYNOSModePtr pExynosMode = pExynos->pExynosMode;
    const char *mode_string[] = { "Off", "HDMI", "Virtual" };
    EXYNOSDisplayConnMode mode;

    XDBG_DEBUG(MSEC, "%s value : %d\n", __FUNCTION__,
               *(unsigned int *) value->data);

    if (argc < 2) {
        _exynosXbercSetReturnProperty(value, "Error : too few arguments\n");
        return TRUE;
    }

    if (argc == 2) {
        _exynosXbercSetReturnProperty(value,
                                      "Current connect mode is %d (%s)\n",
                                      pExynosMode->conn_mode,
                                      mode_string[pExynosMode->conn_mode]);
        return TRUE;
    }

    mode = (EXYNOSDisplayConnMode) atoi(argv[2]);

    if (mode < DISPLAY_CONN_MODE_NONE || mode >= DISPLAY_CONN_MODE_MAX) {
        _exynosXbercSetReturnProperty(value,
                                      "Error : value(%d) is out of range.\n",
                                      mode);
        return TRUE;
    }

    if (mode == pExynosMode->conn_mode) {
        _exynosXbercSetReturnProperty(value, "[Xorg] already connect : %s.\n",
                                      mode_string[mode]);
        return TRUE;
    }

    exynosDisplaySetDispConnMode(scrn, mode);

    _exynosXbercSetReturnProperty(value, "[Xorg] connect : %s.\n",
                                  mode_string[mode]);

    return TRUE;
}

static Bool
EXYNOSXbercAsyncSwap(int argc, char **argv, RRPropertyValuePtr value,
                     ScrnInfoPtr scrn)
{
    ScreenPtr pScreen = scrn->pScreen;
    int bEnable;
    int status = -1;

    if (argc != 3) {
        status = exynosExaScreenAsyncSwap(pScreen, -1);
        if (status < 0) {
            _exynosXbercSetReturnProperty(value, "%s",
                                          "faili to set async swap\n");
            return TRUE;
        }

        _exynosXbercSetReturnProperty(value, "Async swap : %d\n", status);
        return TRUE;
    }

    bEnable = atoi(argv[2]);

    status = exynosExaScreenAsyncSwap(pScreen, bEnable);
    if (status < 0) {
        _exynosXbercSetReturnProperty(value, "%s", "faili to set async swap\n");
        return TRUE;
    }

    if (status)
        _exynosXbercSetReturnProperty(value, "%s", "Set async swap.\n");
    else
        _exynosXbercSetReturnProperty(value, "%s", "Unset async swap.\n");

    return TRUE;
}

static long
_parse_long(char *s)
{
    char *fmt = "%lu";
    long retval = 0L;
    int thesign = 1;

    if (s && s[0]) {
        char temp[12];

        snprintf(temp, sizeof(temp), "%s", s);
        s = temp;

        if (s[0] == '-')
            s++, thesign = -1;
        if (s[0] == '0')
            s++, fmt = "%lo";
        if (s[0] == 'x' || s[0] == 'X')
            s++, fmt = "%lx";
        (void) sscanf(s, fmt, &retval);
    }
    return (thesign * retval);
}

static Bool
EXYNOSXbercDump(int argc, char **argv, RRPropertyValuePtr value,
                ScrnInfoPtr scrn)
{
    EXYNOSPtr pExynos = EXYNOSPTR(scrn);
    int dump_mode;
    Bool flush = TRUE;
    char *c;
    int buf_cnt = 30;

    if (argc < 3)
        goto print_dump;

    pExynos->dump_xid = 0;
    dump_mode = 0;

    if (pExynos->dump_str)
        free(pExynos->dump_str);
    pExynos->dump_str = strdup(argv[2]);

    c = strtok(argv[2], ",");
    if (!c) {
        _exynosXbercSetReturnProperty(value, "[Xorg] fail: read option");
        return TRUE;
    }

    do {
        if (!strcmp(c, "off")) {
            dump_mode = 0;
            break;
        }
        else if (!strcmp(c, "clear")) {
            dump_mode = 0;
            flush = FALSE;
            break;
        }
        else if (!strcmp(c, "drawable")) {
            dump_mode = XBERC_DUMP_MODE_DRAWABLE;
            pExynos->dump_xid = _parse_long(argv[3]);
        }
        else if (!strcmp(c, "fb"))
            dump_mode |= XBERC_DUMP_MODE_FB;
        else if (!strcmp(c, "hwc"))
            dump_mode |= XBERC_DUMP_MODE_HWC;
        else if (!strcmp(c, "dri3"))
            dump_mode |= XBERC_DUMP_MODE_DRI3;
        else if (!strcmp(c, "present"))
            dump_mode |= XBERC_DUMP_MODE_PRESENT;
        else if (!strcmp(c, "all"))
            dump_mode |= (XBERC_DUMP_MODE_DRAWABLE | XBERC_DUMP_MODE_FB | XBERC_DUMP_MODE_DRI3 | XBERC_DUMP_MODE_PRESENT | XBERC_DUMP_MODE_HWC);
        else if (!strcmp(c, "ia"))
            dump_mode |= XBERC_DUMP_MODE_IA;
        else if (!strcmp(c, "ca"))
            dump_mode |= XBERC_DUMP_MODE_CA;
        else if (!strcmp(c, "ea"))
            dump_mode |= XBERC_DUMP_MODE_EA;
        else {
            _exynosXbercSetReturnProperty(value,
                                          "[Xorg] fail: unknown option('%s')\n",
                                          c);
            return TRUE;
        }
    } while ((c = strtok(NULL, ",")));

    snprintf(pExynos->dump_type, sizeof(pExynos->dump_type), DUMP_TYPE_PNG);
    if (argc > 3) {
        int i;

        for (i = 3; i < argc; i++) {
            c = argv[i];
            if (!strcmp(c, "-count"))
                buf_cnt = MIN((argv[i + 1]) ? atoi(argv[i + 1]) : 30, 100);
            else if (!strcmp(c, "-type")) {
                if (!strcmp(argv[i + 1], DUMP_TYPE_PNG)
                    || !strcmp(argv[i + 1], DUMP_TYPE_BMP)
                    || !strcmp(argv[i + 1], DUMP_TYPE_RAW)) {
                    snprintf(pExynos->dump_type, sizeof(pExynos->dump_type),
                             "%s", argv[i + 1]);
                }
            }
        }
    }

    if (dump_mode != 0) {
        char *dir = DUMP_DIR;
        DIR *dp;
        int ret = -1;

        if (!(dp = opendir(dir))) {
            ret = mkdir(dir, 0755);
            if (ret < 0) {
                _exynosXbercSetReturnProperty(value,
                                              "[Xorg] fail: mkdir '%s'\n", dir);
                return FALSE;
            }
        }
        else
            closedir(dp);
    }

    if (dump_mode != pExynos->dump_mode) {
        pExynos->dump_mode = dump_mode;

        if (dump_mode == 0) {
            if (flush)
                exynosUtilFlushDump(pExynos->dump_info);
            exynosUtilFinishDump(pExynos->dump_info);
            pExynos->dump_info = NULL;
            pExynos->flip_cnt = 0;
            goto print_dump;
        }
        else {
            if (pExynos->dump_info) {
                exynosUtilFlushDump(pExynos->dump_info);
                exynosUtilFinishDump(pExynos->dump_info);
                pExynos->dump_info = NULL;
                pExynos->flip_cnt = 0;
            }

            pExynos->dump_info = exynosUtilPrepareDump(scrn,
                                                       pExynos->
                                                       pExynosMode->main_lcd_mode.
                                                       hdisplay *
                                                       pExynos->
                                                       pExynosMode->main_lcd_mode.
                                                       vdisplay * 4, buf_cnt);
            if (pExynos->dump_info) {
                if (pExynos->dump_mode & ~XBERC_DUMP_MODE_DRAWABLE)
                    _exynosXbercSetReturnProperty(value,
                                                  "[Xorg] Dump buffer: %s(cnt:%d)\n",
                                                  pExynos->dump_str, buf_cnt);
                else
                    _exynosXbercSetReturnProperty(value,
                                                  "[Xorg] Dump buffer: %s(xid:0x%x,cnt:%d)\n",
                                                  pExynos->dump_str,
                                                  pExynos->dump_xid, buf_cnt);
            }
            else
                _exynosXbercSetReturnProperty(value,
                                              "[Xorg] Dump buffer: %s(fail)\n",
                                              pExynos->dump_str);
        }
    }
    else
        goto print_dump;

    return TRUE;
 print_dump:
    if (pExynos->dump_mode & XBERC_DUMP_MODE_DRAWABLE)
        _exynosXbercSetReturnProperty(value, "[Xorg] Dump buffer: %s(0x%x)\n",
                                      pExynos->dump_str, pExynos->dump_xid);
    else
        _exynosXbercSetReturnProperty(value, "[Xorg] Dump buffer: %s\n",
                                      pExynos->dump_str);

    return TRUE;
}

static Bool
EXYNOSXbercCursorEnable(int argc, char **argv, RRPropertyValuePtr value,
                        ScrnInfoPtr scrn)
{
    EXYNOSPtr pExynos = EXYNOSPTR(scrn);

    Bool bEnable;

    if (argc != 3) {
        _exynosXbercSetReturnProperty(value, "Enable cursor : %d\n",
                                      pExynos->enableCursor);
        return TRUE;
    }

    bEnable = atoi(argv[2]);

    if (bEnable != pExynos->enableCursor) {
        pExynos->enableCursor = bEnable;
        if (exynosCrtcCursorEnable(scrn, bEnable)) {
            _exynosXbercSetReturnProperty(value, "[Xorg] cursor %s.\n",
                                          bEnable ? "enable" : "disable");
        }
        else {
            _exynosXbercSetReturnProperty(value, "[Xorg] Fail cursor %s.\n",
                                          bEnable ? "enable" : "disable");
        }
    }
    else {
        _exynosXbercSetReturnProperty(value, "[Xorg] already cursor %s.\n",
                                      bEnable ? "enabled" : "disabled");
    }

    return TRUE;
}

static Bool
EXYNOSXbercCursorRotate(int argc, char **argv, RRPropertyValuePtr value,
                        ScrnInfoPtr scrn)
{
    xf86CrtcPtr crtc = xf86CompatCrtc(scrn);
    EXYNOSCrtcPrivPtr fimd_crtc;
    int rotate, RR_rotate;

    if (!crtc)
        return TRUE;

    fimd_crtc = crtc->driver_private;

    if (argc != 3) {
        rotate = exynosUtilRotateToDegree(fimd_crtc->user_rotate);
        _exynosXbercSetReturnProperty(value,
                                      "Current cursor rotate value : %d\n",
                                      rotate);
        return TRUE;
    }

    rotate = atoi(argv[2]);
    RR_rotate = exynosUtilDegreeToRotate(rotate);
    if (!RR_rotate) {
        _exynosXbercSetReturnProperty(value,
                                      "[Xorg] Not support rotate(0, 90, 180, 270 only)\n");
        return TRUE;
    }

    if (exynosCrtcCursorRotate(crtc, RR_rotate)) {
        _exynosXbercSetReturnProperty(value, "[Xorg] cursor rotated %d.\n",
                                      rotate);
    }
    else {
        _exynosXbercSetReturnProperty(value, "[Xorg] Fail cursor rotate %d.\n",
                                      rotate);
    }

    return TRUE;
}

static Bool
EXYNOSXbercVideoPunch(int argc, char **argv, RRPropertyValuePtr value,
                      ScrnInfoPtr scrn)
{
    EXYNOSPtr pExynos = EXYNOSPTR(scrn);

    Bool video_punch;

    if (argc != 3) {
        _exynosXbercSetReturnProperty(value, "video_punch : %d\n",
                                      pExynos->pVideoPriv->video_punch);
        return TRUE;
    }

    video_punch = atoi(argv[2]);

    if (pExynos->pVideoPriv->video_punch != video_punch) {
        pExynos->pVideoPriv->video_punch = video_punch;
        _exynosXbercSetReturnProperty(value, "[Xorg] video_punch %s.\n",
                                      video_punch ? "enabled" : "disabled");
    }
    else
        _exynosXbercSetReturnProperty(value, "[Xorg] already punch %s.\n",
                                      video_punch ? "enabled" : "disabled");

    return TRUE;
}

static Bool
EXYNOSXbercVideoOffset(int argc, char **argv, RRPropertyValuePtr value,
                       ScrnInfoPtr scrn)
{
    EXYNOSPtr pExynos = EXYNOSPTR(scrn);

    if (argc != 3) {
        _exynosXbercSetReturnProperty(value, "video_offset : %d,%d.\n",
                                      pExynos->pVideoPriv->video_offset_x,
                                      pExynos->pVideoPriv->video_offset_y);
        return TRUE;
    }

    if (!exynosPropVideoOffset(argv[2], value, scrn)) {
        _exynosXbercSetReturnProperty(value, "ex) xberc video_offset 0,100.\n");
        return TRUE;
    }

    return TRUE;
}

static Bool
EXYNOSXbercVideoFps(int argc, char **argv, RRPropertyValuePtr value,
                    ScrnInfoPtr scrn)
{
    EXYNOSPtr pExynos = EXYNOSPTR(scrn);

    Bool video_fps;

    if (argc != 3) {
        _exynosXbercSetReturnProperty(value, "video_fps : %d\n",
                                      pExynos->pVideoPriv->video_fps);
        return TRUE;
    }

    video_fps = atoi(argv[2]);

    if (pExynos->pVideoPriv->video_fps != video_fps) {
        pExynos->pVideoPriv->video_fps = video_fps;
        _exynosXbercSetReturnProperty(value, "[Xorg] video_fps %s.\n",
                                      video_fps ? "enabled" : "disabled");
    }
    else
        _exynosXbercSetReturnProperty(value, "[Xorg] already video_fps %s.\n",
                                      video_fps ? "enabled" : "disabled");

    return TRUE;
}

static Bool
EXYNOSXbercVideoSync(int argc, char **argv, RRPropertyValuePtr value,
                     ScrnInfoPtr scrn)
{
    EXYNOSPtr pExynos = EXYNOSPTR(scrn);

    Bool video_sync;

    if (argc != 3) {
        _exynosXbercSetReturnProperty(value, "video_sync : %d\n",
                                      pExynos->pVideoPriv->video_sync);
        return TRUE;
    }

    video_sync = atoi(argv[2]);

    if (pExynos->pVideoPriv->video_sync != video_sync) {
        pExynos->pVideoPriv->video_sync = video_sync;
        _exynosXbercSetReturnProperty(value, "[Xorg] video_sync %s.\n",
                                      video_sync ? "enabled" : "disabled");
    }
    else
        _exynosXbercSetReturnProperty(value, "[Xorg] already video_sync %s.\n",
                                      video_sync ? "enabled" : "disabled");

    return TRUE;
}

static Bool
EXYNOSXbercVideoNoRetbuf(int argc, char **argv, RRPropertyValuePtr value,
                         ScrnInfoPtr scrn)
{
    EXYNOSPtr pExynos = EXYNOSPTR(scrn);

    if (argc != 3) {
        _exynosXbercSetReturnProperty(value, "[Xorg] %s wait retbuf\n",
                                      (pExynos->pVideoPriv->
                                       no_retbuf) ? "No" : "");
        return TRUE;
    }

    pExynos->pVideoPriv->no_retbuf = atoi(argv[2]);

    _exynosXbercSetReturnProperty(value, "[Xorg] %s wait retbuf\n",
                                  (pExynos->pVideoPriv->no_retbuf) ? "No" : "");

    return TRUE;
}

static Bool
EXYNOSXbercVideoOutput(int argc, char **argv, RRPropertyValuePtr value,
                       ScrnInfoPtr scrn)
{
    EXYNOSPtr pExynos = EXYNOSPTR(scrn);
    const char *output_string[] = { "None", "default", "video", "ext_only" };
    int video_output;

    if (argc != 3) {
        _exynosXbercSetReturnProperty(value, "video_output : %d\n",
                                      output_string[pExynos->pVideoPriv->
                                                    video_output]);
        return TRUE;
    }

    video_output = atoi(argv[2]);

    if (video_output < OUTPUT_MODE_DEFAULT ||
        video_output > OUTPUT_MODE_EXT_ONLY) {
        _exynosXbercSetReturnProperty(value,
                                      "Error : value(%d) is out of range.\n",
                                      video_output);
        return TRUE;
    }

    video_output += 1;

    if (pExynos->pVideoPriv->video_output != video_output) {
        pExynos->pVideoPriv->video_output = video_output;
        _exynosXbercSetReturnProperty(value, "[Xorg] video_output : %s.\n",
                                      output_string[video_output]);
    }
    else
        _exynosXbercSetReturnProperty(value,
                                      "[Xorg] already video_output : %s.\n",
                                      output_string[video_output]);

    return TRUE;
}

static Bool
EXYNOSXbercWbFps(int argc, char **argv, RRPropertyValuePtr value,
                 ScrnInfoPtr scrn)
{
    EXYNOSPtr pExynos = EXYNOSPTR(scrn);

    Bool wb_fps;

    if (argc != 3) {
        _exynosXbercSetReturnProperty(value, "wb_fps : %d\n", pExynos->wb_fps);
        return TRUE;
    }

    wb_fps = atoi(argv[2]);

    if (pExynos->wb_fps != wb_fps) {
        pExynos->wb_fps = wb_fps;
        _exynosXbercSetReturnProperty(value, "[Xorg] wb_fps %s.\n",
                                      wb_fps ? "enabled" : "disabled");
    }
    else
        _exynosXbercSetReturnProperty(value, "[Xorg] already wb_fps %s.\n",
                                      wb_fps ? "enabled" : "disabled");

    return TRUE;
}

static Bool
EXYNOSXbercWbHz(int argc, char **argv, RRPropertyValuePtr value,
                ScrnInfoPtr scrn)
{
    EXYNOSPtr pExynos = EXYNOSPTR(scrn);

    Bool wb_hz;

    if (argc != 3) {
        _exynosXbercSetReturnProperty(value, "wb_hz : %d\n", pExynos->wb_hz);
        return TRUE;
    }

    wb_hz = atoi(argv[2]);

    if (pExynos->wb_hz != wb_hz) {
        pExynos->wb_hz = wb_hz;
        _exynosXbercSetReturnProperty(value, "[Xorg] wb_hz %d.\n", wb_hz);
    }
    else
        _exynosXbercSetReturnProperty(value, "[Xorg] already wb_hz %d.\n",
                                      wb_hz);

    return TRUE;
}

static Bool
EXYNOSXbercXvPerf(int argc, char **argv, RRPropertyValuePtr value,
                  ScrnInfoPtr scrn)
{
    EXYNOSPtr pExynos = EXYNOSPTR(scrn);
    char *c;

    if (argc < 3) {
        _exynosXbercSetReturnProperty(value, "[Xorg] xvperf: %s\n",
                                      (pExynos->xvperf) ? pExynos->
                                      xvperf : "off");
        return TRUE;
    }

    if (pExynos->xvperf)
        free(pExynos->xvperf);
    pExynos->xvperf = strdup(argv[2]);

    c = strtok(argv[2], ",");
    if (!c) {
        _exynosXbercSetReturnProperty(value, "[Xorg] fail: read option\n");
        return TRUE;
    }

    do {
        if (!strcmp(c, "off"))
            pExynos->xvperf_mode = 0;
        else if (!strcmp(c, "ia"))
            pExynos->xvperf_mode |= XBERC_XVPERF_MODE_IA;
        else if (!strcmp(c, "ca"))
            pExynos->xvperf_mode |= XBERC_XVPERF_MODE_CA;
        else if (!strcmp(c, "cvt"))
            pExynos->xvperf_mode |= XBERC_XVPERF_MODE_CVT;
        else if (!strcmp(c, "wb"))
            pExynos->xvperf_mode |= XBERC_XVPERF_MODE_WB;
        else if (!strcmp(c, "access"))
            pExynos->xvperf_mode |= XBERC_XVPERF_MODE_ACCESS;
        else {
            _exynosXbercSetReturnProperty(value,
                                          "[Xorg] fail: unknown option('%s')\n",
                                          c);
            return TRUE;
        }
    } while ((c = strtok(NULL, ",")));

    _exynosXbercSetReturnProperty(value, "[Xorg] xvperf: %s\n",
                                  (pExynos->xvperf) ? pExynos->xvperf : "off");

    return TRUE;
}

static Bool
EXYNOSXbercSwap(int argc, char **argv, RRPropertyValuePtr value,
                ScrnInfoPtr scrn)
{
    if (argc != 2) {
        _exynosXbercSetReturnProperty(value, "Error : too few arguments\n");
        return TRUE;
    }

    exynosVideoSwapLayers(scrn->pScreen);

    _exynosXbercSetReturnProperty(value, "%s", "Video layers swapped.\n");

    return TRUE;
}

static Bool
EXYNOSXbercDrmmodeDump(int argc, char **argv, RRPropertyValuePtr value,
                       ScrnInfoPtr scrn)
{
    EXYNOSPtr pExynos = EXYNOSPTR(scrn);
    char reply[XBERC_BUF_SIZE] = { 0, };
    int len = sizeof(reply);

    if (argc != 2) {
        _exynosXbercSetReturnProperty(value, "Error : too few arguments\n");
        return TRUE;
    }

    exynos_drmmode_dump(pExynos->drm_fd, reply, &len);
    _exynosXbercSetReturnProperty(value, "%s", reply);

    return TRUE;
}

static Bool
EXYNOSXbercAccessibility(int argc, char **argv, RRPropertyValuePtr value,
                         ScrnInfoPtr scrn)
{
    Bool found = FALSE;
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(scrn);
    xf86OutputPtr pOutput = NULL;
    xf86CrtcPtr pCrtc = NULL;
    EXYNOSCrtcPrivPtr pCrtcPriv = NULL;
    int output_w = 0, output_h = 0;

    char *opt;
    char *mode;
    int i;

    int accessibility_status;
    int bScale;
    _X_UNUSED Bool bChange = FALSE;

    char seps[] = "x+-";
    char *tr;
    int geo[10], g = 0;

    for (i = 0; i < xf86_config->num_output; i++) {
        pOutput = xf86_config->output[i];
        if (!pOutput->crtc->enabled)
            continue;

        /* modify the physical size of monitor */
        if (!strcmp(pOutput->name, "LVDS1")) {
            found = TRUE;
            break;
        }
    }

    if (!found) {
        _exynosXbercSetReturnProperty(value, "Error : cannot found LVDS1\n");
        return TRUE;
    }

    pCrtc = pOutput->crtc;
    pCrtcPriv = pCrtc->driver_private;

    output_w = pCrtc->mode.HDisplay;
    output_h = pCrtc->mode.VDisplay;

    for (i = 0; i < argc; i++) {
        opt = argv[i];
        if (*opt != '-')
            continue;

        if (!strcmp(opt, "-n")) {
            accessibility_status = atoi(argv[++i]);
            if (pCrtcPriv->accessibility_status != accessibility_status) {
                pCrtcPriv->accessibility_status = accessibility_status;
                bChange = TRUE;
            }
        }
        else if (!strcmp(opt, "-scale")) {
            bScale = atoi(argv[++i]);

            pCrtcPriv->bScale = bScale;
            bChange = TRUE;
            //ErrorF("[XORG] Set Scale = %d\n", bScale);

            if (bScale) {
                int x, y, w, h;

                mode = argv[++i];
                tr = strtok(mode, seps);
                while (tr != NULL) {
                    geo[g++] = atoi(tr);
                    tr = strtok(NULL, seps);
                }

                if (g < 4) {
                    _exynosXbercSetReturnProperty(value,
                                                  "[Xberc] Invalid geometry(%s)\n",
                                                  mode);
                    continue;
                }

                w = geo[0];
                h = geo[1];
                x = geo[2];
                y = geo[3];

                /*Check invalidate region */
                if (x < 0)
                    x = 0;
                if (y < 0)
                    y = 0;
                if (x + w > output_w)
                    w = output_w - x;
                if (y + h > output_h)
                    h = output_h - y;

                if (pCrtcPriv->rotate == RR_Rotate_90) {
                    pCrtcPriv->sx = y;
                    pCrtcPriv->sy = output_w - (x + w);
                    pCrtcPriv->sw = h;
                    pCrtcPriv->sh = w;
                }
                else if (pCrtcPriv->rotate == RR_Rotate_270) {
                    pCrtcPriv->sx = output_h - (y + h);
                    pCrtcPriv->sy = x;
                    pCrtcPriv->sw = h;
                    pCrtcPriv->sh = w;
                }
                else if (pCrtcPriv->rotate == RR_Rotate_180) {
                    pCrtcPriv->sx = output_w - (x + w);
                    pCrtcPriv->sy = output_h - (y + h);
                    pCrtcPriv->sw = w;
                    pCrtcPriv->sh = h;
                }
                else {
                    pCrtcPriv->sx = x;
                    pCrtcPriv->sy = y;
                    pCrtcPriv->sw = w;
                    pCrtcPriv->sh = h;
                }
            }
        }
    }

    exynosCrtcEnableAccessibility(pCrtc);

    return TRUE;
}

static Bool
EXYNOSXbercEnableFb(int argc, char **argv, RRPropertyValuePtr value,
                    ScrnInfoPtr scrn)
{
    Bool always = FALSE;

    if (argc == 2) {
        char ret_buf[XBERC_BUF_SIZE] = { 0, };
        char temp[1024] = { 0, };
        xf86CrtcConfigPtr pCrtcConfig;
        int i, len, remain = XBERC_BUF_SIZE;
        char *buf = ret_buf;

        pCrtcConfig = XF86_CRTC_CONFIG_PTR(scrn);
        if (!pCrtcConfig)
            goto fail_enable_fb;

        for (i = 0; i < pCrtcConfig->num_output; i++) {
            xf86OutputPtr pOutput = pCrtcConfig->output[i];

            if (pOutput->crtc) {
                EXYNOSCrtcPrivPtr pCrtcPriv = pOutput->crtc->driver_private;

                snprintf(temp, sizeof(temp), "crtc(%d)   : %s%s\n",
                         pCrtcPriv->mode_crtc->crtc_id,
                         (pCrtcPriv->onoff) ? "ON" : "OFF",
                         (pCrtcPriv->onoff_always) ? "(always)." : ".");
                len = MIN(remain, strlen(temp));
                strncpy(buf, temp, len);
                buf += len;
                remain -= len;
            }
        }

        exynosPlaneDump(buf, &remain);

        _exynosXbercSetReturnProperty(value, "%s", ret_buf);

        return TRUE;
    }

    if (argc > 4)
        goto fail_enable_fb;

    if (!strcmp("always", argv[3]))
        always = TRUE;

    if (!exynosPropFbVisible(argv[2], always, value, scrn))
        goto fail_enable_fb;

    return TRUE;

 fail_enable_fb:
    _exynosXbercSetReturnProperty(value,
                                  "ex) xberc fb [output]:[zpos]:[onoff] {always}.\n");

    return TRUE;
}

static Bool
EXYNOSXbercScreenRotate(int argc, char **argv, RRPropertyValuePtr value,
                        ScrnInfoPtr scrn)
{
    xf86CrtcPtr crtc = xf86CompatCrtc(scrn);
    EXYNOSCrtcPrivPtr fimd_crtc;

    if (!crtc)
        return TRUE;

    fimd_crtc = crtc->driver_private;

    if (argc != 3) {
        _exynosXbercSetReturnProperty(value,
                                      "Current screen rotate value : %d\n",
                                      fimd_crtc->screen_rotate_degree);
        return TRUE;
    }

    exynosPropScreenRotate(argv[2], value, scrn);

    return TRUE;
}

static struct {
    const char *Cmd;
    const char *Description;
    const char *Options;

    const char *(*DynamicUsage) (int);
    const char *DetailedUsage;

    Bool (*set_property) (int argc, char **argv, RRPropertyValuePtr value,
                          ScrnInfoPtr scrn);
    Bool (*get_property) (RRPropertyValuePtr value);
} xberc_property_proc[] = {
    {
    "tvout", "to set Tv Out Mode", "[0-4]",
            NULL, "[Off:0 / Clone:1 / UiClone:2 / Extension:3]",
            EXYNOSXbercSetTvoutMode, NULL,}, {
    "connect", "to set connect mode", "[0-2]",
            NULL, "[Off:0 / HDMI:1 / Virtual:2]",
            EXYNOSXbercSetConnectMode, NULL,}, {
    "async_swap", "not block by vsync", "[0 or 1]",
            NULL, "[0/1]", EXYNOSXbercAsyncSwap, NULL}, {
    "dump", "to dump buffers, default dump-format is png",
            "[off,clear,drawable,fb,all]", NULL,
            "[off,clear,drawable,fb,all] -count [n] -type [raw|bmp|png]",
            EXYNOSXbercDump, NULL}, {
    "cursor_enable", "to enable/disable cursor", "[0 or 1]", NULL,
            "[Enable:1 / Disable:0]", EXYNOSXbercCursorEnable, NULL}, {
    "cursor_rotate", "to set cursor rotate degree", "[0,90,180,270]", NULL,
            "[0,90,180,270]", EXYNOSXbercCursorRotate, NULL}, {
    "video_punch", "to punch screen when XV put image on screen",
            "[0 or 1]", NULL, "[0/1]", EXYNOSXbercVideoPunch, NULL}, {
    "video_offset", "to add x,y to the position video", "[x,y]", NULL,
            "[x,y]", EXYNOSXbercVideoOffset, NULL}, {
    "video_fps", "to print fps of video", "[0 or 1]", NULL, "[0/1]",
            EXYNOSXbercVideoFps, NULL}, {
    "video_sync", "to sync video", "[0 or 1]", NULL, "[0/1]",
            EXYNOSXbercVideoSync, NULL}, {
    "video_output", "to set output", "[0,1,2]", NULL,
            "[default:0 / video:1 / ext_only:2]", EXYNOSXbercVideoOutput, NULL},
    {
    "video_no_retbuf", "no wait until buffer returned", "[0,1]", NULL,
            "[disable:0 / enable:1]", EXYNOSXbercVideoNoRetbuf, NULL}, {
    "wb_fps", "to print fps of writeback", "[0 or 1]", NULL, "[0/1]",
            EXYNOSXbercWbFps, NULL}, {
    "wb_hz", "to set hz of writeback", "[0, 12, 15, 20, 30, 60]", NULL,
            "[0, 12, 15, 20, 30, 60]", EXYNOSXbercWbHz, NULL}, {
    "xv_perf", "to print xv elapsed time", "[off,ia,ca,cvt,wb]", NULL,
            "[off,ia,ca,cvt,wb]", EXYNOSXbercXvPerf, NULL}, {
    "swap", "to swap video layers", "", NULL, "", EXYNOSXbercSwap, NULL}, {
    "drmmode_dump", "to print drmmode resources", "",
            NULL, "", EXYNOSXbercDrmmodeDump, NULL}, {
    "accessibility", "to set accessibility",
            "-n [0 or 1] -scale [0 or 1] [{width}x{height}+{x}+{y}]", NULL,
            "-n [0 or 1] -scale [0 or 1] [{width}x{height}+{x}+{y}]",
            EXYNOSXbercAccessibility, NULL}, {
    "fb", "to turn framebuffer on/off", "[0~1]:[0~4]:[0~1] {always}", NULL,
            "[output : 0(lcd)~1(ext)]:[zpos : 0 ~ 4]:[onoff : 0(on)~1(off)] {always}",
            EXYNOSXbercEnableFb, NULL}, {
"screen_rotate", "to set screen orientation",
            "[normal,inverted,left,right,0,1,2,3]", NULL,
            "[normal,inverted,left,right,0,1,2,3]",
            EXYNOSXbercScreenRotate, NULL},};

static int
_exynosXbercPrintUsage(char *buf, int size, const char *exec)
{
    char *begin = buf;
    char temp[1024];
    int i, len, remain = size;

    int option_cnt =
        sizeof(xberc_property_proc) / sizeof(xberc_property_proc[0]);

    snprintf(temp, sizeof(temp), "Usage : %s [cmd] [options]\n", exec);
    len = MIN(remain, strlen(temp));
    strncpy(buf, temp, len);
    buf += len;
    remain -= len;

    if (remain <= 0)
        return (buf - begin);

    snprintf(temp, sizeof(temp), "     ex)\n");
    len = MIN(remain, strlen(temp));
    strncpy(buf, temp, len);
    buf += len;
    remain -= len;

    if (remain <= 0)
        return (buf - begin);

    for (i = 0; i < option_cnt; i++) {
        snprintf(temp, sizeof(temp), "     	%s %s %s\n", exec,
                 xberc_property_proc[i].Cmd, xberc_property_proc[i].Options);
        len = MIN(remain, strlen(temp));
        strncpy(buf, temp, len);
        buf += len;
        remain -= len;

        if (remain <= 0)
            return (buf - begin);
    }

    snprintf(temp, sizeof(temp), " options :\n");
    len = MIN(remain, strlen(temp));
    strncpy(buf, temp, len);
    buf += len;
    remain -= len;

    if (remain <= 0)
        return (buf - begin);

    for (i = 0; i < option_cnt; i++) {
        if (xberc_property_proc[i].Cmd && xberc_property_proc[i].Description)
            snprintf(temp, sizeof(temp), "  %s (%s)\n",
                     xberc_property_proc[i].Cmd,
                     xberc_property_proc[i].Description);
        else
            snprintf(temp, sizeof(temp), "  Cmd(%p) or Descriptiont(%p).\n",
                     xberc_property_proc[i].Cmd,
                     xberc_property_proc[i].Description);
        len = MIN(remain, strlen(temp));
        strncpy(buf, temp, len);
        buf += len;
        remain -= len;

        if (remain <= 0)
            return (buf - begin);

        if (xberc_property_proc[i].DynamicUsage) {
            snprintf(temp, sizeof(temp), "     [MODULE:%s]\n",
                     xberc_property_proc[i].DynamicUsage(MODE_NAME_ONLY));
            len = MIN(remain, strlen(temp));
            strncpy(buf, temp, len);
            buf += len;
            remain -= len;

            if (remain <= 0)
                return (buf - begin);
        }

        if (xberc_property_proc[i].DetailedUsage)
            snprintf(temp, sizeof(temp), "     %s\n",
                     xberc_property_proc[i].DetailedUsage);
        else
            snprintf(temp, sizeof(temp), "  DetailedUsage(%p).\n",
                     xberc_property_proc[i].DetailedUsage);
        len = MIN(remain, strlen(temp));
        strncpy(buf, temp, len);
        buf += len;
        remain -= len;

        if (remain <= 0)
            return (buf - begin);
    }

    return (buf - begin);
}

static unsigned int
_exynosXbercInit()
{
    XDBG_DEBUG(MSEC, "%s()\n", __FUNCTION__);

    static Bool g_property_init = FALSE;
    static unsigned int nProperty =
        sizeof(xberc_property_proc) / sizeof(xberc_property_proc[0]);

    if (g_property_init == FALSE) {
        rr_property_atom =
            MakeAtom(XRRPROPERTY_ATOM, strlen(XRRPROPERTY_ATOM), TRUE);
        g_property_init = TRUE;
    }

    return nProperty;
}

static int
_exynosXbercParseArg(int *argc, char **argv, RRPropertyValuePtr value)
{
    int i;
    char *data;

    if (argc == NULL || value == NULL || argv == NULL || value->data == NULL)
        return FALSE;

    data = value->data;

    if (value->format != 8)
        return FALSE;

    if (value->size < 3 || data[value->size - 2] != '\0' ||
        data[value->size - 1] != '\0')
        return FALSE;

    for (i = 0; *data; i++) {
        argv[i] = data;
        data += strlen(data) + 1;
        if (data - (char *) value->data > value->size)
            return FALSE;
    }
    argv[i] = data;
    *argc = i;

    return TRUE;
}

static void
_exynosXbercSetReturnProperty(RRPropertyValuePtr value, const char *f, ...)
{
    int len;
    va_list args;
    char buf[XBERC_BUF_SIZE];

    if (value->data) {
        free(value->data);
        value->data = NULL;
    }
    va_start(args, f);
    len = vsnprintf(buf, sizeof(buf), f, args) + 1;
    va_end(args);

    value->data = calloc(1, len);
    value->format = 8;
    value->size = len;

    if (value->data)
        strncpy(value->data, buf, len - 1);
}

int
exynosXbercSetProperty(xf86OutputPtr output, Atom property,
                       RRPropertyValuePtr value)
{
    XDBG_TRACE(MXBRC, "%s\n", __FUNCTION__);

    unsigned int nProperty = _exynosXbercInit();
    unsigned int p;

    int argc;
    char *argv[1024];
    char buf[XBERC_BUF_SIZE] = { 0, };

    if (rr_property_atom != property) {
        _exynosXbercSetReturnProperty(value,
                                      "[Xberc]: Unrecognized property name.\n");
        return FALSE;
    }

    if (_exynosXbercParseArg(&argc, argv, value) == FALSE || argc < 1) {
        _exynosXbercSetReturnProperty(value, "[Xberc]: Parse error.\n");
        return TRUE;
    }

    if (argc < 2) {
        _exynosXbercPrintUsage(buf, sizeof(buf), argv[0]);
        _exynosXbercSetReturnProperty(value, buf);

        return TRUE;
    }

    for (p = 0; p < nProperty; p++) {
        if (!strcmp(argv[1], xberc_property_proc[p].Cmd) ||
            (argv[1][0] == '-' &&
             !strcmp(1 + argv[1], xberc_property_proc[p].Cmd))) {
            xberc_property_proc[p].set_property(argc, argv, value,
                                                output->scrn);
            return TRUE;
        }
    }

    _exynosXbercPrintUsage(buf, sizeof(buf), argv[0]);
    _exynosXbercSetReturnProperty(value, buf);

    return TRUE;
}
