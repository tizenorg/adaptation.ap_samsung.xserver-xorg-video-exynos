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

#include "sec.h"
#include "sec_util.h"
#include "sec_xberc.h"
#include "sec_output.h"
#include "sec_crtc.h"
#include "sec_layer.h"
#include "sec_wb.h"
#include "sec_plane.h"
#include "sec_prop.h"
#include "sec_drmmode_dump.h"

#define XRRPROPERTY_ATOM	"X_RR_PROPERTY_REMOTE_CONTROLLER"
#define XBERC_BUF_SIZE  8192

static Atom rr_property_atom;

static void _secXbercSetReturnProperty (RRPropertyValuePtr value, const char * f, ...);

static Bool SECXbercSetTvoutMode (int argc, char ** argv, RRPropertyValuePtr value, ScrnInfoPtr scrn)
{
    SECPtr pSec = SECPTR (scrn);
    SECModePtr pSecMode = pSec->pSecMode;
    const char * mode_string[] = {"Off", "Clone", "UiClone", "Extension"};
    SECDisplaySetMode mode;

    XDBG_DEBUG (MSEC, "%s value : %d\n", __FUNCTION__, *(unsigned int*)value->data);

    if (argc < 2)
    {
        _secXbercSetReturnProperty (value, "Error : too few arguments\n");
        return TRUE;
    }

    if (argc == 2)
    {
        _secXbercSetReturnProperty (value, "Current Tv Out mode is %d (%s)\n",
                                    pSecMode->set_mode, mode_string[pSecMode->set_mode]);
        return TRUE;
    }

    mode = (SECDisplaySetMode)atoi (argv[2]);

    if (mode < DISPLAY_SET_MODE_OFF)
    {
        _secXbercSetReturnProperty (value, "Error : value(%d) is out of range.\n", mode);
        return TRUE;
    }

    if (mode == pSecMode->set_mode)
    {
        _secXbercSetReturnProperty (value, "[Xorg] already tvout : %s.\n", mode_string[mode]);
        return TRUE;
    }

    if (pSecMode->conn_mode != DISPLAY_CONN_MODE_HDMI && pSecMode->conn_mode != DISPLAY_CONN_MODE_VIRTUAL)
    {
        _secXbercSetReturnProperty (value, "Error : not connected.\n");
        return TRUE;
    }

    secDisplaySetDispSetMode (scrn, mode);

    _secXbercSetReturnProperty (value, "[Xorg] tvout : %s.\n", mode_string[mode]);

    return TRUE;
}

static Bool SECXbercSetConnectMode (int argc, char ** argv, RRPropertyValuePtr value, ScrnInfoPtr scrn)
{
    SECPtr pSec = SECPTR (scrn);
    SECModePtr pSecMode = pSec->pSecMode;
    const char * mode_string[] = {"Off", "HDMI", "Virtual"};
    SECDisplayConnMode mode;

    XDBG_DEBUG (MSEC, "%s value : %d\n", __FUNCTION__, *(unsigned int*)value->data);

    if (argc < 2)
    {
        _secXbercSetReturnProperty (value, "Error : too few arguments\n");
        return TRUE;
    }

    if (argc == 2)
    {
        _secXbercSetReturnProperty (value, "Current connect mode is %d (%s)\n",
                                    pSecMode->conn_mode, mode_string[pSecMode->conn_mode]);
        return TRUE;
    }

    mode = (SECDisplayConnMode)atoi (argv[2]);

    if (mode < DISPLAY_CONN_MODE_NONE || mode >= DISPLAY_CONN_MODE_MAX)
    {
        _secXbercSetReturnProperty (value, "Error : value(%d) is out of range.\n", mode);
        return TRUE;
    }

    if (mode == pSecMode->conn_mode)
    {
        _secXbercSetReturnProperty (value, "[Xorg] already connect : %s.\n", mode_string[mode]);
        return TRUE;
    }

    secDisplaySetDispConnMode (scrn, mode);

    _secXbercSetReturnProperty (value, "[Xorg] connect : %s.\n", mode_string[mode]);

    return TRUE;
}

static Bool SECXbercAsyncSwap (int argc, char ** argv, RRPropertyValuePtr value, ScrnInfoPtr scrn)
{
    ScreenPtr pScreen = scrn->pScreen;
    int bEnable;
    int status = -1;

    if (argc !=3 )
    {
        status = secExaScreenAsyncSwap (pScreen, -1);
        if (status < 0)
        {
            _secXbercSetReturnProperty (value, "%s", "faili to set async swap\n");
            return TRUE;
        }

        _secXbercSetReturnProperty (value, "Async swap : %d\n", status);
        return TRUE;
    }

    bEnable = atoi (argv[2]);

    status = secExaScreenAsyncSwap (pScreen, bEnable);
    if (status < 0)
    {
        _secXbercSetReturnProperty (value, "%s", "faili to set async swap\n");
        return TRUE;
    }

    if (status)
        _secXbercSetReturnProperty (value, "%s", "Set async swap.\n");
    else
        _secXbercSetReturnProperty (value, "%s", "Unset async swap.\n");

    return TRUE;
}

static long
_parse_long (char *s)
{
    char *fmt = "%lu";
    long retval = 0L;
    int thesign = 1;

    if (s && s[0])
    {
        char temp[12];
        snprintf (temp, sizeof (temp), "%s", s);
        s = temp;

        if (s[0] == '-')
            s++, thesign = -1;
        if (s[0] == '0')
            s++, fmt = "%lo";
        if (s[0] == 'x' || s[0] == 'X')
            s++, fmt = "%lx";
        (void) sscanf (s, fmt, &retval);
    }
    return (thesign * retval);
}

static Bool SECXbercDump (int argc, char ** argv, RRPropertyValuePtr value, ScrnInfoPtr scrn)
{
    SECPtr pSec = SECPTR (scrn);
    int dump_mode;
    Bool flush = TRUE;
    char *c;
    int buf_cnt = 30;

    if (argc < 3)
        goto print_dump;

    pSec->dump_xid = 0;
    dump_mode = 0;

    if (pSec->dump_str)
        free (pSec->dump_str);
    pSec->dump_str = strdup (argv[2]);

    c = strtok (argv[2], ",");
    do
    {
        if (!strcmp (c, "off"))
        {
            dump_mode = 0;
            break;
        }
        else if (!strcmp (c, "clear"))
        {
            dump_mode = 0;
            flush = FALSE;
            break;
        }
        else if (!strcmp (c, "drawable"))
        {
            dump_mode = XBERC_DUMP_MODE_DRAWABLE;
            pSec->dump_xid = _parse_long (argv[3]);
        }
        else if (!strcmp (c, "fb"))
            dump_mode |= XBERC_DUMP_MODE_FB;
        else if (!strcmp (c, "all"))
            dump_mode |= (XBERC_DUMP_MODE_DRAWABLE|XBERC_DUMP_MODE_FB);
        else if (!strcmp (c, "ia"))
            dump_mode |= XBERC_DUMP_MODE_IA;
        else if (!strcmp (c, "ca"))
            dump_mode |= XBERC_DUMP_MODE_CA;
        else if (!strcmp (c, "ea"))
            dump_mode |= XBERC_DUMP_MODE_EA;
        else
        {
            _secXbercSetReturnProperty (value, "[Xorg] fail: unknown option('%s')\n", c);
            return TRUE;
        }
    } while ((c = strtok (NULL, ",")));

    snprintf (pSec->dump_type, sizeof (pSec->dump_type), "bmp");
    if (argc > 3)
    {
        int i;
        for (i = 3; i < argc; i++)
        {
            c = argv[i];
            if (!strcmp (c, "-count"))
                buf_cnt = MIN((argv[i+1])?atoi(argv[i+1]):30,100);
            else if (!strcmp (c, "-type"))
            {
                if (!strcmp (argv[i+1], "bmp") || !strcmp (argv[i+1], "raw"))
                    snprintf (pSec->dump_type, sizeof (pSec->dump_type), "%s", argv[i+1]);
            }
        }
    }

    if (dump_mode != 0)
    {
        char *dir = DUMP_DIR;
        DIR *dp;
        int ret = -1;

        if (!(dp = opendir (dir)))
        {
            ret = mkdir (dir, 0755);
            if (ret < 0)
            {
                _secXbercSetReturnProperty (value, "[Xorg] fail: mkdir '%s'\n", dir);
                return FALSE;
            }
        }
        else
            closedir (dp);
    }

    if (dump_mode != pSec->dump_mode)
    {
        pSec->dump_mode = dump_mode;

        if (dump_mode == 0)
        {
            if (flush)
                secUtilFlushDump (pSec->dump_info);
            secUtilFinishDump (pSec->dump_info);
            pSec->dump_info = NULL;
            pSec->flip_cnt = 0;
            goto print_dump;
        }
        else
        {
            if (pSec->dump_info)
            {
                secUtilFlushDump (pSec->dump_info);
                secUtilFinishDump (pSec->dump_info);
                pSec->dump_info = NULL;
                pSec->flip_cnt = 0;
            }

            pSec->dump_info = secUtilPrepareDump (scrn,
                                                  pSec->pSecMode->main_lcd_mode.hdisplay * pSec->pSecMode->main_lcd_mode.vdisplay * 4,
                                                  buf_cnt);
            if (pSec->dump_info)
            {
                if (pSec->dump_mode & ~XBERC_DUMP_MODE_DRAWABLE)
                    _secXbercSetReturnProperty (value, "[Xorg] Dump buffer: %s(cnt:%d)\n",
                                                pSec->dump_str, buf_cnt);
                else
                    _secXbercSetReturnProperty (value, "[Xorg] Dump buffer: %s(xid:0x%x,cnt:%d)\n",
                                                pSec->dump_str, pSec->dump_xid, buf_cnt);
            }
            else
                _secXbercSetReturnProperty (value, "[Xorg] Dump buffer: %s(fail)\n", pSec->dump_str);
        }
    }
    else
        goto print_dump;

    return TRUE;
print_dump:
    if (pSec->dump_mode & XBERC_DUMP_MODE_DRAWABLE)
        _secXbercSetReturnProperty (value, "[Xorg] Dump buffer: %s(0x%x)\n", pSec->dump_str, pSec->dump_xid);
    else
        _secXbercSetReturnProperty (value, "[Xorg] Dump buffer: %s\n", pSec->dump_str);

    return TRUE;
}

static Bool SECXbercCursorEnable (int argc, char ** argv, RRPropertyValuePtr value, ScrnInfoPtr scrn)
{
    SECPtr pSec = SECPTR (scrn);

    Bool bEnable;

    if (argc != 3)
    {
        _secXbercSetReturnProperty (value, "Enable cursor : %d\n", pSec->enableCursor);
        return TRUE;
    }

    bEnable = atoi (argv[2]);

    if (bEnable!=pSec->enableCursor)
    {
        pSec->enableCursor = bEnable;
        if (secCrtcCursorEnable (scrn, bEnable))
        {
            _secXbercSetReturnProperty (value, "[Xorg] cursor %s.\n", bEnable?"enable":"disable");
        }
        else
        {
            _secXbercSetReturnProperty (value, "[Xorg] Fail cursor %s.\n", bEnable?"enable":"disable");
        }
    }
    else
    {
        _secXbercSetReturnProperty (value, "[Xorg] already cursor %s.\n", bEnable?"enabled":"disabled");
    }

    return TRUE;
}

static Bool SECXbercCursorRotate (int argc, char ** argv, RRPropertyValuePtr value, ScrnInfoPtr scrn)
{
    xf86CrtcPtr crtc = xf86CompatCrtc (scrn);
    SECCrtcPrivPtr fimd_crtc;
    int rotate, RR_rotate;

    if (!crtc)
        return TRUE;

    fimd_crtc = crtc->driver_private;

    if (argc != 3)
    {
        rotate = secUtilRotateToDegree (fimd_crtc->user_rotate);
        _secXbercSetReturnProperty (value, "Current cursor rotate value : %d\n", rotate);
        return TRUE;
    }

    rotate = atoi (argv[2]);
    RR_rotate = secUtilDegreeToRotate (rotate);
    if (!RR_rotate)
    {
        _secXbercSetReturnProperty (value, "[Xorg] Not support rotate(0, 90, 180, 270 only)\n");
        return TRUE;
    }

    if (secCrtcCursorRotate (crtc, RR_rotate))
    {
        _secXbercSetReturnProperty (value, "[Xorg] cursor rotated %d.\n", rotate);
    }
    else
    {
        _secXbercSetReturnProperty (value, "[Xorg] Fail cursor rotate %d.\n", rotate);
    }

    return TRUE;
}

static Bool SECXbercVideoPunch (int argc, char ** argv, RRPropertyValuePtr value, ScrnInfoPtr scrn)
{
    SECPtr pSec = SECPTR (scrn);

    Bool video_punch;

    if (argc != 3)
    {
        _secXbercSetReturnProperty (value, "video_punch : %d\n", pSec->pVideoPriv->video_punch);
        return TRUE;
    }

    video_punch = atoi (argv[2]);

    if (pSec->pVideoPriv->video_punch != video_punch)
    {
        pSec->pVideoPriv->video_punch = video_punch;
        _secXbercSetReturnProperty (value, "[Xorg] video_punch %s.\n", video_punch?"enabled":"disabled");
    }
    else
        _secXbercSetReturnProperty (value, "[Xorg] already punch %s.\n", video_punch?"enabled":"disabled");

    return TRUE;
}

static Bool SECXbercVideoOffset (int argc, char ** argv, RRPropertyValuePtr value, ScrnInfoPtr scrn)
{
    SECPtr pSec = SECPTR (scrn);

    if (argc != 3)
    {
        _secXbercSetReturnProperty (value, "video_offset : %d,%d.\n",
                                    pSec->pVideoPriv->video_offset_x,
                                    pSec->pVideoPriv->video_offset_y);
        return TRUE;
    }

    if (!secPropVideoOffset (argv[2], value, scrn))
    {
        _secXbercSetReturnProperty (value, "ex) xberc video_offset 0,100.\n");
        return TRUE;
    }

    return TRUE;
}

static Bool SECXbercVideoFps (int argc, char ** argv, RRPropertyValuePtr value, ScrnInfoPtr scrn)
{
    SECPtr pSec = SECPTR (scrn);

    Bool video_fps;

    if (argc != 3)
    {
        _secXbercSetReturnProperty (value, "video_fps : %d\n", pSec->pVideoPriv->video_fps);
        return TRUE;
    }

    video_fps = atoi (argv[2]);

    if (pSec->pVideoPriv->video_fps != video_fps)
    {
        pSec->pVideoPriv->video_fps = video_fps;
        _secXbercSetReturnProperty (value, "[Xorg] video_fps %s.\n", video_fps?"enabled":"disabled");
    }
    else
        _secXbercSetReturnProperty (value, "[Xorg] already video_fps %s.\n", video_fps?"enabled":"disabled");

    return TRUE;
}

static Bool SECXbercVideoSync (int argc, char ** argv, RRPropertyValuePtr value, ScrnInfoPtr scrn)
{
    SECPtr pSec = SECPTR (scrn);

    Bool video_sync;

    if (argc != 3)
    {
        _secXbercSetReturnProperty (value, "video_sync : %d\n", pSec->pVideoPriv->video_sync);
        return TRUE;
    }

    video_sync = atoi (argv[2]);

    if (pSec->pVideoPriv->video_sync != video_sync)
    {
        pSec->pVideoPriv->video_sync = video_sync;
        _secXbercSetReturnProperty (value, "[Xorg] video_sync %s.\n", video_sync?"enabled":"disabled");
    }
    else
        _secXbercSetReturnProperty (value, "[Xorg] already video_sync %s.\n", video_sync?"enabled":"disabled");

    return TRUE;
}

static Bool SECXbercVideoNoRetbuf (int argc, char ** argv, RRPropertyValuePtr value, ScrnInfoPtr scrn)
{
    SECPtr pSec = SECPTR (scrn);

    if (argc != 3)
    {
        _secXbercSetReturnProperty (value, "[Xorg] %s wait retbuf\n", (pSec->pVideoPriv->no_retbuf)?"No":"");
        return TRUE;
    }

    pSec->pVideoPriv->no_retbuf = atoi (argv[2]);

    _secXbercSetReturnProperty (value, "[Xorg] %s wait retbuf\n", (pSec->pVideoPriv->no_retbuf)?"No":"");

    return TRUE;
}

static Bool SECXbercVideoOutput (int argc, char ** argv, RRPropertyValuePtr value, ScrnInfoPtr scrn)
{
    SECPtr pSec = SECPTR (scrn);
    const char * output_string[] = {"None", "default", "video", "ext_only"};
    int video_output;

    if (argc != 3)
    {
        _secXbercSetReturnProperty (value, "video_output : %d\n", output_string[pSec->pVideoPriv->video_output]);
        return TRUE;
    }

    video_output = atoi (argv[2]);

    if (video_output < OUTPUT_MODE_DEFAULT || video_output > OUTPUT_MODE_EXT_ONLY)
    {
        _secXbercSetReturnProperty (value, "Error : value(%d) is out of range.\n", video_output);
        return TRUE;
    }

    video_output += 1;

    if (pSec->pVideoPriv->video_output != video_output)
    {
        pSec->pVideoPriv->video_output = video_output;
        _secXbercSetReturnProperty (value, "[Xorg] video_output : %s.\n", output_string[video_output]);
    }
    else
        _secXbercSetReturnProperty (value, "[Xorg] already video_output : %s.\n", output_string[video_output]);

    return TRUE;
}

static Bool SECXbercWbFps (int argc, char ** argv, RRPropertyValuePtr value, ScrnInfoPtr scrn)
{
    SECPtr pSec = SECPTR (scrn);

    Bool wb_fps;

    if (argc != 3)
    {
        _secXbercSetReturnProperty (value, "wb_fps : %d\n", pSec->wb_fps);
        return TRUE;
    }

    wb_fps = atoi (argv[2]);

    if (pSec->wb_fps != wb_fps)
    {
        pSec->wb_fps = wb_fps;
        _secXbercSetReturnProperty (value, "[Xorg] wb_fps %s.\n", wb_fps?"enabled":"disabled");
    }
    else
        _secXbercSetReturnProperty (value, "[Xorg] already wb_fps %s.\n", wb_fps?"enabled":"disabled");

    return TRUE;
}

static Bool SECXbercWbHz (int argc, char ** argv, RRPropertyValuePtr value, ScrnInfoPtr scrn)
{
    SECPtr pSec = SECPTR (scrn);

    Bool wb_hz;

    if (argc != 3)
    {
        _secXbercSetReturnProperty (value, "wb_hz : %d\n", pSec->wb_hz);
        return TRUE;
    }

    wb_hz = atoi (argv[2]);

    if (pSec->wb_hz != wb_hz)
    {
        pSec->wb_hz = wb_hz;
        _secXbercSetReturnProperty (value, "[Xorg] wb_hz %d.\n", wb_hz);
    }
    else
        _secXbercSetReturnProperty (value, "[Xorg] already wb_hz %d.\n", wb_hz);

    return TRUE;
}

static Bool SECXbercXvPerf (int argc, char ** argv, RRPropertyValuePtr value, ScrnInfoPtr scrn)
{
    SECPtr pSec = SECPTR (scrn);
    char *c;

    if (argc < 3)
    {
        _secXbercSetReturnProperty (value, "[Xorg] xvperf: %s\n",
                                    (pSec->xvperf)?pSec->xvperf:"off");
        return TRUE;
    }

    if (pSec->xvperf)
        free (pSec->xvperf);
    pSec->xvperf = strdup (argv[2]);

    c = strtok (argv[2], ",");
    do
    {
        if (!strcmp (c, "off"))
            pSec->xvperf_mode = 0;
        else if (!strcmp (c, "ia"))
            pSec->xvperf_mode |= XBERC_XVPERF_MODE_IA;
        else if (!strcmp (c, "ca"))
            pSec->xvperf_mode |= XBERC_XVPERF_MODE_CA;
        else if (!strcmp (c, "cvt"))
            pSec->xvperf_mode |= XBERC_XVPERF_MODE_CVT;
        else if (!strcmp (c, "wb"))
            pSec->xvperf_mode |= XBERC_XVPERF_MODE_WB;
        else if (!strcmp (c, "access"))
            pSec->xvperf_mode |= XBERC_XVPERF_MODE_ACCESS;
        else
        {
            _secXbercSetReturnProperty (value, "[Xorg] fail: unknown option('%s')\n", c);
            return TRUE;
        }
    } while ((c = strtok (NULL, ",")));

    _secXbercSetReturnProperty (value, "[Xorg] xvperf: %s\n",
                                (pSec->xvperf)?pSec->xvperf:"off");

    return TRUE;
}

static Bool SECXbercSwap (int argc, char ** argv, RRPropertyValuePtr value, ScrnInfoPtr scrn)
{
    if (argc != 2)
    {
        _secXbercSetReturnProperty (value, "Error : too few arguments\n");
        return TRUE;
    }

    secVideoSwapLayers (scrn->pScreen);

    _secXbercSetReturnProperty (value, "%s", "Video layers swapped.\n");

    return TRUE;
}

static Bool SECXbercDrmmodeDump (int argc, char ** argv, RRPropertyValuePtr value, ScrnInfoPtr scrn)
{
    SECPtr pSec = SECPTR (scrn);
    char reply[XBERC_BUF_SIZE] = {0,};
    int len = sizeof (reply);

    if (argc != 2)
    {
        _secXbercSetReturnProperty (value, "Error : too few arguments\n");
        return TRUE;
    }

    sec_drmmode_dump (pSec->drm_fd, reply, &len);
    _secXbercSetReturnProperty (value, "%s", reply);

    return TRUE;
}

static Bool SECXbercAccessibility (int argc, char ** argv, RRPropertyValuePtr value, ScrnInfoPtr scrn)
{
    Bool found = FALSE;
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR (scrn);
    xf86OutputPtr pOutput = NULL;
    xf86CrtcPtr pCrtc = NULL;
    SECCrtcPrivPtr pCrtcPriv = NULL;
    int output_w = 0, output_h = 0;

    char *opt;
    char *mode;
    int i;

    int accessibility_status;
    int bScale;
    _X_UNUSED Bool bChange = FALSE;

    char seps[]="x+-";
    char *tr;
    int geo[10], g=0;

    for (i = 0; i < xf86_config->num_output; i++)
    {
        pOutput = xf86_config->output[i];
        if (!pOutput->crtc->enabled)
            continue;

        /* modify the physical size of monitor */
        if (!strcmp(pOutput->name, "LVDS1"))
        {
            found = TRUE;
            break;
        }
    }

    if (!found)
    {
        _secXbercSetReturnProperty (value, "Error : cannot found LVDS1\n");
        return TRUE;
    }

    pCrtc = pOutput->crtc;
    pCrtcPriv = pCrtc->driver_private;

    output_w = pCrtc->mode.HDisplay;
    output_h = pCrtc->mode.VDisplay;

    for(i=0; i<argc; i++)
    {
        opt = argv[i];
        if(*opt != '-') continue;

        if(!strcmp(opt, "-n") )
        {
			accessibility_status = atoi(argv[++i]);
			if(pCrtcPriv->accessibility_status != accessibility_status)
			{
				pCrtcPriv->accessibility_status = accessibility_status;
				bChange = TRUE;
			}
        }
        else if(!strcmp(opt, "-scale"))
        {
			bScale = atoi(argv[++i]);

		    pCrtcPriv->bScale = bScale;
			bChange = TRUE;
			//ErrorF("[XORG] Set Scale = %d\n", bScale);

			if(bScale)
			{
				int x,y,w,h;

				mode = argv[++i];
				tr = strtok(mode, seps);
				while(tr != NULL)
				{
					geo[g++] = atoi(tr);
					tr=strtok(NULL, seps);
				}

				if(g < 4)
				{
                    _secXbercSetReturnProperty (value, "[Xberc] Invalid geometry(%s)\n", mode);
					continue;
				}

				w = geo[0];
				h = geo[1];
				x = geo[2];
				y = geo[3];

				/*Check invalidate region */
				if(x<0) x=0;
				if(y<0) y=0;
				if(x+w > output_w) w = output_w-x;
				if(y+h > output_h) h = output_h-y;

				if(pCrtcPriv->rotate == RR_Rotate_90)
				{
					pCrtcPriv->sx = y;
					pCrtcPriv->sy = output_w - (x+w);
					pCrtcPriv->sw = h;
					pCrtcPriv->sh = w;
				}
				else if(pCrtcPriv->rotate == RR_Rotate_270)
				{
					pCrtcPriv->sx = output_h - (y+h);
					pCrtcPriv->sy = x;
					pCrtcPriv->sw = h;
					pCrtcPriv->sh = w;
				}
				else if(pCrtcPriv->rotate == RR_Rotate_180)
				{
					pCrtcPriv->sx = output_w - (x+w);
					pCrtcPriv->sy = output_h - (y+h);
					pCrtcPriv->sw = w;
					pCrtcPriv->sh = h;
				}
				else
				{
					pCrtcPriv->sx = x;
					pCrtcPriv->sy = y;
					pCrtcPriv->sw = w;
					pCrtcPriv->sh = h;
				}
			}
        }
    }

    secCrtcEnableAccessibility (pCrtc);

    return TRUE;
}

static Bool SECXbercEnableFb (int argc, char ** argv, RRPropertyValuePtr value, ScrnInfoPtr scrn)
{
    Bool always = FALSE;

    if (argc == 2)
    {
        char ret_buf[XBERC_BUF_SIZE] = {0,};
        char temp[1024] = {0,};
        xf86CrtcConfigPtr pCrtcConfig;
        int i, len, remain = XBERC_BUF_SIZE;
        char *buf = ret_buf;

        pCrtcConfig = XF86_CRTC_CONFIG_PTR (scrn);
        if (!pCrtcConfig)
            goto fail_enable_fb;

        for (i = 0; i < pCrtcConfig->num_output; i++)
        {
            xf86OutputPtr pOutput = pCrtcConfig->output[i];
            if (pOutput->crtc)
            {
                SECCrtcPrivPtr pCrtcPriv = pOutput->crtc->driver_private;
                snprintf (temp, sizeof (temp), "crtc(%d)   : %s%s\n",
                          pCrtcPriv->mode_crtc->crtc_id,
                          (pCrtcPriv->onoff)?"ON":"OFF",
                          (pCrtcPriv->onoff_always)?"(always).":".");
                len = MIN (remain, strlen (temp));
                strncpy (buf, temp, len);
                buf += len;
                remain -= len;
            }
        }

        secPlaneDump (buf, &remain);

        _secXbercSetReturnProperty (value, "%s", ret_buf);

        return TRUE;
    }

    if (argc > 4)
        goto fail_enable_fb;

    if (!strcmp ("always", argv[3]))
        always = TRUE;

    if (!secPropFbVisible (argv[2], always, value, scrn))
        goto fail_enable_fb;

    return TRUE;

fail_enable_fb:
    _secXbercSetReturnProperty (value, "ex) xberc fb [output]:[zpos]:[onoff] {always}.\n");

    return TRUE;
}

static Bool SECXbercScreenRotate (int argc, char ** argv, RRPropertyValuePtr value, ScrnInfoPtr scrn)
{
    xf86CrtcPtr crtc = xf86CompatCrtc (scrn);
    SECCrtcPrivPtr fimd_crtc;

    if (!crtc)
        return TRUE;

    fimd_crtc = crtc->driver_private;

    if (argc != 3)
    {
        _secXbercSetReturnProperty (value, "Current screen rotate value : %d\n", fimd_crtc->screen_rotate_degree);
        return TRUE;
    }

    secPropScreenRotate (argv[2], value, scrn);

    return TRUE;
}

static struct
{
    const char * Cmd;
    const char * Description;
    const char * Options;

    const char *(*DynamicUsage) (int);
    const char * DetailedUsage;

    Bool (*set_property) (int argc, char ** argv, RRPropertyValuePtr value, ScrnInfoPtr scrn);
    Bool (*get_property) (RRPropertyValuePtr value);
} xberc_property_proc[] =
{
    {
        "tvout", "to set Tv Out Mode", "[0-4]",
        NULL, "[Off:0 / Clone:1 / UiClone:2 / Extension:3]",
        SECXbercSetTvoutMode, NULL,
    },

    {
        "connect", "to set connect mode", "[0-2]",
        NULL, "[Off:0 / HDMI:1 / Virtual:2]",
        SECXbercSetConnectMode, NULL,
    },

    {
        "async_swap", "not block by vsync", "[0 or 1]",
        NULL, "[0/1]",
        SECXbercAsyncSwap, NULL
    },

    {
        "dump", "to dump buffers", "[off,clear,drawable,fb,all]",
        NULL, "[off,clear,drawable,fb,all] -count [n] -type [raw|bmp]",
        SECXbercDump, NULL
    },

    {
        "cursor_enable", "to enable/disable cursor", "[0 or 1]",
        NULL, "[Enable:1 / Disable:0]",
        SECXbercCursorEnable, NULL
    },

    {
        "cursor_rotate", "to set cursor rotate degree", "[0,90,180,270]",
        NULL, "[0,90,180,270]",
        SECXbercCursorRotate, NULL
    },

    {
        "video_punch", "to punch screen when XV put image on screen", "[0 or 1]",
        NULL, "[0/1]",
        SECXbercVideoPunch, NULL
    },

    {
        "video_offset", "to add x,y to the position video", "[x,y]",
        NULL, "[x,y]",
        SECXbercVideoOffset, NULL
    },

    {
        "video_fps", "to print fps of video", "[0 or 1]",
        NULL, "[0/1]",
        SECXbercVideoFps, NULL
    },

    {
        "video_sync", "to sync video", "[0 or 1]",
        NULL, "[0/1]",
        SECXbercVideoSync, NULL
    },

    {
        "video_output", "to set output", "[0,1,2]",
        NULL, "[default:0 / video:1 / ext_only:2]",
        SECXbercVideoOutput, NULL
    },

    {
        "video_no_retbuf", "no wait until buffer returned", "[0,1]",
        NULL, "[disable:0 / enable:1]",
        SECXbercVideoNoRetbuf, NULL
    },

    {
        "wb_fps", "to print fps of writeback", "[0 or 1]",
        NULL, "[0/1]",
        SECXbercWbFps, NULL
    },

    {
        "wb_hz", "to set hz of writeback", "[0, 12, 15, 20, 30, 60]",
        NULL, "[0, 12, 15, 20, 30, 60]",
        SECXbercWbHz, NULL
    },

    {
        "xv_perf", "to print xv elapsed time", "[off,ia,ca,cvt,wb]",
        NULL, "[off,ia,ca,cvt,wb]",
        SECXbercXvPerf, NULL
    },

    {
        "swap", "to swap video layers", "",
        NULL, "",
        SECXbercSwap, NULL
    },

    {
        "drmmode_dump", "to print drmmode resources", "",
        NULL, "",
        SECXbercDrmmodeDump, NULL
    },

    {
        "accessibility", "to set accessibility", "-n [0 or 1] -scale [0 or 1] [{width}x{height}+{x}+{y}]",
        NULL, "-n [0 or 1] -scale [0 or 1] [{width}x{height}+{x}+{y}]",
        SECXbercAccessibility, NULL
    },

    {
        "fb", "to turn framebuffer on/off", "[0~1]:[0~4]:[0~1] {always}",
        NULL, "[output : 0(lcd)~1(ext)]:[zpos : 0 ~ 4]:[onoff : 0(on)~1(off)] {always}",
        SECXbercEnableFb, NULL
    },

    {
        "screen_rotate", "to set screen orientation", "[normal,inverted,left,right,0,1,2,3]",
        NULL, "[normal,inverted,left,right,0,1,2,3]",
        SECXbercScreenRotate, NULL
    },
};

static int _secXbercPrintUsage (char *buf, int size, const char * exec)
{
    char * begin = buf;
    char temp[1024];
    int i, len, remain = size;

    int option_cnt = sizeof (xberc_property_proc) / sizeof (xberc_property_proc[0]);

    snprintf (temp, sizeof (temp), "Usage : %s [cmd] [options]\n", exec);
    len = MIN (remain, strlen (temp));
    strncpy (buf, temp, len);
    buf += len;
    remain -= len;

    if (remain <= 0)
        return (buf - begin);

    snprintf (temp, sizeof (temp), "     ex)\n");
    len = MIN (remain, strlen (temp));
    strncpy (buf, temp, len);
    buf += len;
    remain -= len;

    if (remain <= 0)
        return (buf - begin);

    for (i=0; i<option_cnt; i++)
    {
        snprintf (temp, sizeof (temp), "     	%s %s %s\n", exec, xberc_property_proc[i].Cmd, xberc_property_proc[i].Options);
        len = MIN (remain, strlen (temp));
        strncpy (buf, temp, len);
        buf += len;
        remain -= len;

        if (remain <= 0)
            return (buf - begin);
    }

    snprintf (temp, sizeof (temp), " options :\n");
    len = MIN (remain, strlen (temp));
    strncpy (buf, temp, len);
    buf += len;
    remain -= len;

    if (remain <= 0)
        return (buf - begin);

    for (i=0; i<option_cnt; i++)
    {
        if (xberc_property_proc[i].Cmd && xberc_property_proc[i].Description)
            snprintf (temp, sizeof (temp), "  %s (%s)\n", xberc_property_proc[i].Cmd, xberc_property_proc[i].Description);
        else
            snprintf (temp, sizeof (temp), "  Cmd(%p) or Descriptiont(%p).\n", xberc_property_proc[i].Cmd, xberc_property_proc[i].Description);
        len = MIN (remain, strlen (temp));
        strncpy (buf, temp, len);
        buf += len;
        remain -= len;

        if (remain <= 0)
            return (buf - begin);

        if (xberc_property_proc[i].DynamicUsage)
        {
            snprintf (temp, sizeof (temp), "     [MODULE:%s]\n", xberc_property_proc[i].DynamicUsage (MODE_NAME_ONLY));
            len = MIN (remain, strlen (temp));
            strncpy (buf, temp, len);
            buf += len;
            remain -= len;

            if (remain <= 0)
                return (buf - begin);
        }

        if (xberc_property_proc[i].DetailedUsage)
            snprintf (temp, sizeof (temp), "     %s\n", xberc_property_proc[i].DetailedUsage);
        else
            snprintf (temp, sizeof (temp), "  DetailedUsage(%p).\n", xberc_property_proc[i].DetailedUsage);
        len = MIN (remain, strlen (temp));
        strncpy (buf, temp, len);
        buf += len;
        remain -= len;

        if (remain <= 0)
            return (buf - begin);
    }

    return (buf - begin);
}

static unsigned int _secXbercInit()
{
    XDBG_DEBUG (MSEC, "%s()\n", __FUNCTION__);

    static Bool g_property_init = FALSE;
    static unsigned int nProperty = sizeof (xberc_property_proc) / sizeof (xberc_property_proc[0]);

    if (g_property_init == FALSE)
    {
        rr_property_atom = MakeAtom (XRRPROPERTY_ATOM, strlen (XRRPROPERTY_ATOM), TRUE);
        g_property_init = TRUE;
    }

    return nProperty;
}

static int _secXbercParseArg (int * argc, char ** argv, RRPropertyValuePtr value)
{
    int i;
    char * data;

    if (argc == NULL || value == NULL || argv == NULL || value->data == NULL)
        return FALSE;

    data = value->data;

    if (value->format != 8)
        return FALSE;

    if (value->size < 3 || data[value->size - 2] != '\0' || data[value->size - 1] != '\0')
        return FALSE;

    for (i=0; *data; i++)
    {
        argv[i] = data;
        data += strlen (data) + 1;
        if (data - (char*)value->data > value->size)
            return FALSE;
    }
    argv[i] = data;
    *argc = i;

    return TRUE;
}

static void _secXbercSetReturnProperty (RRPropertyValuePtr value, const char * f, ...)
{
    int len;
    va_list args;
    char buf[XBERC_BUF_SIZE];

    if (value->data)
    {
        free (value->data);
        value->data = NULL;
    }
    va_start (args, f);
    len = vsnprintf (buf, sizeof(buf), f, args) + 1;
    va_end (args);

    value->data = calloc (1, len);
    value->format = 8;
    value->size = len;

    if (value->data)
        strncpy (value->data, buf, len-1);
}

int
secXbercSetProperty (xf86OutputPtr output, Atom property, RRPropertyValuePtr value)
{
    XDBG_TRACE (MXBRC, "%s\n", __FUNCTION__);

    unsigned int nProperty = _secXbercInit();
    unsigned int p;

    int argc;
    char * argv[1024];
    char buf[XBERC_BUF_SIZE] = {0,};

    if (rr_property_atom != property)
    {
        _secXbercSetReturnProperty (value, "[Xberc]: Unrecognized property name.\n");
        return TRUE;
    }

    if (_secXbercParseArg (&argc, argv, value) == FALSE || argc < 1)
    {
        _secXbercSetReturnProperty (value, "[Xberc]: Parse error.\n");
        return TRUE;
    }

    if (argc < 2)
    {
        _secXbercPrintUsage (buf, sizeof (buf), argv[0]);
        _secXbercSetReturnProperty (value, buf);

        return TRUE;
    }

    for (p=0; p<nProperty; p++)
    {
        if (!strcmp (argv[1], xberc_property_proc[p].Cmd) ||
                (argv[1][0] == '-' && !strcmp (1 + argv[1], xberc_property_proc[p].Cmd)))
        {
            xberc_property_proc[p].set_property (argc, argv, value, output->scrn);
            return TRUE;
        }
    }

    _secXbercPrintUsage (buf, sizeof (buf), argv[0]);
    _secXbercSetReturnProperty (value, buf);

    return TRUE;
}
