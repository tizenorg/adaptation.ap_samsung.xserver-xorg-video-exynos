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

#include "sec.h"
#include "sec_display.h"
#include "sec_output.h"
#include "sec_crtc.h"
#include "sec_prop.h"
#include "sec_util.h"
#include <exynos_drm.h>
#include <sys/ioctl.h>

#define STR_XRR_DISPLAY_MODE_PROPERTY "XRR_PROPERTY_DISPLAY_MODE"
#define STR_XRR_LVDS_FUNCTION "XRR_PROPERTY_LVDS_FUNCTION"
#define STR_XRR_FB_VISIBLE_PROPERTY "XRR_PROPERTY_FB_VISIBLE"
#define STR_XRR_VIDEO_OFFSET_PROPERTY "XRR_PROPERTY_VIDEO_OFFSET"
#define STR_XRR_PROPERTY_SCREEN_ROTATE "XRR_PROPERTY_SCREEN_ROTATE"

#define PROP_VERIFY_RR_MODE(id, ptr, a)\
    {\
	int rc = dixLookupResourceByType((pointer *)&(ptr), id,\
	                                 RRModeType, serverClient, a);\
	if (rc != Success) {\
	    serverClient->errorValue = id;\
	    return 0;\
	}\
    }


#define FLAG_BITS (RR_HSyncPositive | \
		   RR_HSyncNegative | \
		   RR_VSyncPositive | \
		   RR_VSyncNegative | \
		   RR_Interlace | \
		   RR_DoubleScan | \
		   RR_CSync | \
		   RR_CSyncPositive | \
		   RR_CSyncNegative | \
		   RR_HSkewPresent | \
		   RR_BCast | \
		   RR_PixelMultiplex | \
		   RR_DoubleClock | \
		   RR_ClockDivideBy2)

static Bool g_display_mode_prop_init = FALSE;
static Bool g_lvds_func_prop_init = FALSE;
static Bool g_fb_visible_prop_init = FALSE;
static Bool g_video_offset_prop_init = FALSE;
static Bool g_screen_rotate_prop_init = FALSE;

static Atom xrr_property_display_mode_atom;
static Atom xrr_property_lvds_func_atom;
static Atom xrr_property_fb_visible_atom;
static Atom xrr_property_video_offset_atom;
static Atom xrr_property_screen_rotate_atom;

typedef enum
{
    XRR_OUTPUT_DISPLAY_MODE_NULL,
	XRR_OUTPUT_DISPLAY_MODE_WB_CLONE,
	XRR_OUTPUT_DISPLAY_MODE_VIDEO_ONLY,
} XRROutputPropDisplayMode;

typedef enum
{
    XRR_OUTPUT_LVDS_FUNC_NULL,
	XRR_OUTPUT_LVDS_FUNC_INIT_VIRTUAL,
	XRR_OUTPUT_LVDS_FUNC_HIBERNATION,
	XRR_OUTPUT_LVDS_FUNC_ACCESSIBILITY,
} XRROutputPropLvdsFunc;

/*
 * Convert a RandR mode to a DisplayMode
 */
static void
_RRModeConvertToDisplayMode (ScrnInfoPtr	scrn,
		      RRModePtr		randr_mode,
		      DisplayModePtr	mode)
{
    memset(mode, 0, sizeof(DisplayModeRec));
    mode->status = MODE_OK;

    mode->Clock = randr_mode->mode.dotClock / 1000;

    mode->HDisplay = randr_mode->mode.width;
    mode->HSyncStart = randr_mode->mode.hSyncStart;
    mode->HSyncEnd = randr_mode->mode.hSyncEnd;
    mode->HTotal = randr_mode->mode.hTotal;
    mode->HSkew = randr_mode->mode.hSkew;

    mode->VDisplay = randr_mode->mode.height;
    mode->VSyncStart = randr_mode->mode.vSyncStart;
    mode->VSyncEnd = randr_mode->mode.vSyncEnd;
    mode->VTotal = randr_mode->mode.vTotal;
    mode->VScan = 0;

    mode->Flags = randr_mode->mode.modeFlags & FLAG_BITS;

    xf86SetModeCrtc (mode, scrn->adjustFlags);
}


static int
_secPropUnsetCrtc (xf86OutputPtr pOutput)
{
    if (!pOutput->crtc)
        return 1;

    ScrnInfoPtr pScrn = pOutput->scrn;
    SECOutputPrivPtr pOutputPriv = pOutput->driver_private;
    SECModePtr pSecMode =  pOutputPriv->pSecMode;

    pSecMode->unset_connector_type = pOutputPriv->mode_output->connector_type;
    RRGetInfo (pScrn->pScreen, TRUE);
    pSecMode->unset_connector_type = 0;
    RRGetInfo (pScrn->pScreen, TRUE);

    return 1;
}

static int
_secPropSetWbClone (xf86OutputPtr pOutput, int mode_xid)
{
    SECOutputPrivPtr pOutputPriv = pOutput->driver_private;
    SECModePtr pSecMode =  pOutputPriv->pSecMode;
    RRModePtr pRRMode;
    DisplayModeRec mode;

    /* find kmode and set the external default mode */
    PROP_VERIFY_RR_MODE (mode_xid, pRRMode, DixSetAttrAccess);
    _RRModeConvertToDisplayMode (pOutput->scrn, pRRMode, &mode);
    secDisplayModeToKmode (pOutput->scrn, &pSecMode->ext_connector_mode, &mode);

    if (pOutputPriv->mode_output->connector_type == DRM_MODE_CONNECTOR_HDMIA ||
        pOutputPriv->mode_output->connector_type == DRM_MODE_CONNECTOR_HDMIB)
    {
        secDisplaySetDispConnMode (pOutput->scrn, DISPLAY_CONN_MODE_HDMI);
        _secPropUnsetCrtc (pOutput);
    }
    else if (pOutputPriv->mode_output->connector_type == DRM_MODE_CONNECTOR_VIRTUAL)
    {
        secDisplaySetDispConnMode (pOutput->scrn, DISPLAY_CONN_MODE_VIRTUAL);
        _secPropUnsetCrtc (pOutput);
    }
    else
    {
        XDBG_WARNING (MDISP, "(WB_CLONE) Not suuport for this connecotor type\n");
        return 0;
    }

    if(!secDisplaySetDispSetMode (pOutput->scrn, DISPLAY_SET_MODE_CLONE))
    {
        return 0;
    }

    return 1;
}

static void
_secPropUnSetWbClone (xf86OutputPtr pOutput)
{
    secDisplaySetDispSetMode (pOutput->scrn, DISPLAY_SET_MODE_OFF);
    secDisplaySetDispConnMode (pOutput->scrn, DISPLAY_CONN_MODE_NONE);
}

Bool
secPropSetDisplayMode (xf86OutputPtr pOutput, Atom property, RRPropertyValuePtr value)
{
    XDBG_RETURN_VAL_IF_FAIL(value, FALSE);
    XDBG_TRACE (MPROP, "%s\n", __FUNCTION__);

    XRROutputPropDisplayMode disp_mode = XRR_OUTPUT_DISPLAY_MODE_NULL;
    SECOutputPrivPtr pOutputPriv;

    if (g_display_mode_prop_init == FALSE)
    {
        xrr_property_display_mode_atom = MakeAtom (STR_XRR_DISPLAY_MODE_PROPERTY, strlen (STR_XRR_DISPLAY_MODE_PROPERTY), TRUE);
        g_display_mode_prop_init = TRUE;
    }

    if (xrr_property_display_mode_atom != property)
    {
        //ErrorF ("[Display_mode]: Unrecognized property name.\n");
        return FALSE;
    }

    if (!value->data || value->size == 0)
    {
        //ErrorF ("[Display_mode]: Unrecognized property value.\n");
        return TRUE;
    }

    XDBG_DEBUG (MDISP, "output_name=%s, data=%d size=%ld\n", pOutput->name, *(int *)value->data, value->size);

    disp_mode = *(int *)value->data;

    if (disp_mode == XRR_OUTPUT_DISPLAY_MODE_NULL)
        return TRUE;

    XDBG_DEBUG (MDISP, "output_name=%s, disp_mode=%d\n", pOutput->name, disp_mode);

    pOutputPriv = pOutput->driver_private;

    int mode_xid;
    switch (disp_mode)
    {
        case XRR_OUTPUT_DISPLAY_MODE_WB_CLONE:
            mode_xid = *((int *)value->data+1);
            XDBG_INFO (MDISP, "[DISPLAY_MODE]: Set WriteBack Clone\n");
            _secPropSetWbClone (pOutput, mode_xid);
            pOutputPriv->disp_mode = disp_mode;
            break;
        default:
            break;
    }

    return TRUE;
}

void
secPropUnSetDisplayMode (xf86OutputPtr pOutput)
{
    XRROutputPropDisplayMode disp_mode;
    SECOutputPrivPtr pOutputPriv;

    pOutputPriv = pOutput->driver_private;
    disp_mode = pOutputPriv->disp_mode;

    if (disp_mode == XRR_OUTPUT_DISPLAY_MODE_NULL)
        return;

    /* check the private and unset the diplaymode */
    switch (disp_mode)
    {
        case XRR_OUTPUT_DISPLAY_MODE_WB_CLONE:
            XDBG_INFO (MDISP, "[DISPLAY_MODE]: UnSet WriteBack Clone\n");
            _secPropUnSetWbClone (pOutput);
            break;
        default:
            break;
    }

    pOutputPriv->disp_mode = XRR_OUTPUT_DISPLAY_MODE_NULL;
}

static const char fake_edid_info[] = {
    /* fill the edid information */
};

static void
_secPropSetVirtual (xf86OutputPtr pOutput, int sc_conn)
{
    SECPtr pSec = SECPTR (pOutput->scrn);
    int fd = pSec->drm_fd;

    struct drm_exynos_vidi_connection vidi;

    if (sc_conn == 1)
    {
        vidi.connection = 1;
        vidi.extensions = 1;
        vidi.edid = (uint64_t *)fake_edid_info;
    }
    else if (sc_conn == 2)
    {
        vidi.connection = 0;
    }
    else
    {
        XDBG_WARNING (MDISP, "Warning : wrong virtual connection command\n");
        return;
    }

    ioctl (fd, DRM_IOCTL_EXYNOS_VIDI_CONNECTION, &vidi);
}

Bool
secPropSetLvdsFunc (xf86OutputPtr pOutput, Atom property, RRPropertyValuePtr value)
{
    XDBG_TRACE (MPROP, "%s\n", __FUNCTION__);
    XDBG_RETURN_VAL_IF_FAIL(value, FALSE);

    XRROutputPropLvdsFunc lvds_func = XRR_OUTPUT_LVDS_FUNC_NULL;

    if (g_lvds_func_prop_init == FALSE)
    {
        xrr_property_lvds_func_atom = MakeAtom (STR_XRR_LVDS_FUNCTION, strlen (STR_XRR_LVDS_FUNCTION), TRUE);
        g_lvds_func_prop_init = TRUE;
    }

    if (xrr_property_lvds_func_atom != property)
    {
        return FALSE;
    }

    if (!value->data || value->size == 0)
    {
        //ErrorF ("[Display_mode]: Unrecognized property value.\n");
        return TRUE;
    }

    XDBG_DEBUG (MDISP, "output_name=%s, data=%d size=%ld\n", pOutput->name, *(int *)value->data, value->size);

    lvds_func = *(int *)value->data;

    if (lvds_func == XRR_OUTPUT_LVDS_FUNC_NULL)
        return TRUE;

    XDBG_DEBUG (MDISP, "output_name=%s, lvds_func=%d\n", pOutput->name, lvds_func);

    int sc_conn;
    switch (lvds_func)
    {
        case XRR_OUTPUT_LVDS_FUNC_INIT_VIRTUAL:
            sc_conn = *((int *)value->data+1);
            XDBG_INFO (MDISP, "[LVDS_FUNC]: set virtual output (%d)\n", sc_conn);
            _secPropSetVirtual (pOutput, sc_conn);
            break;
        case XRR_OUTPUT_LVDS_FUNC_HIBERNATION:
            XDBG_INFO (MDISP, "[LVDS_FUNC]: set hibernationn\n");
            break;
        case XRR_OUTPUT_LVDS_FUNC_ACCESSIBILITY:
            XDBG_INFO (MDISP, "[LVDS_FUNC]: set accessibility\n");
            break;
        default:
            break;
    }

    return TRUE;
}

void
secPropUnSetLvdsFunc (xf86OutputPtr pOutput)
{

}

static void
_secPropReturnProperty (RRPropertyValuePtr value, const char * f, ...)
{
    int len;
    va_list args;
    char buf[1024];

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

Bool secPropFbVisible (char *cmd, Bool always, RRPropertyValuePtr value, ScrnInfoPtr scrn)
{
    int output = 0;
    int pos = 0;
    Bool onoff = FALSE;
    char str[128];
    char *p;
    SECLayer *layer;
    SECLayerPos lpos;

    XDBG_RETURN_VAL_IF_FAIL (cmd != NULL, FALSE);

    snprintf (str, sizeof(str), "%s", cmd);

    p = strtok (str, ":");
    XDBG_RETURN_VAL_IF_FAIL (p != NULL, FALSE);
    output = atoi (p);

    p = strtok (NULL, ":");
    XDBG_RETURN_VAL_IF_FAIL (p != NULL, FALSE);
    pos = atoi (p);

    if (output == LAYER_OUTPUT_LCD)
        lpos = pos - 3;
    else
        lpos = pos - 1;

    p = strtok (NULL, ":");
    if (!p)
    {
        _secPropReturnProperty (value, "%d", 0);

        if (lpos != 0)
        {
            layer = secLayerFind ((SECLayerOutput)output, lpos);
            if (layer)
                _secPropReturnProperty (value, "%d", secLayerTurnStatus (layer));
        }
        else
        {
            xf86CrtcConfigPtr pCrtcConfig;
            int i;

            pCrtcConfig = XF86_CRTC_CONFIG_PTR (scrn);
            if (!pCrtcConfig)
                return FALSE;

            for (i = 0; i < pCrtcConfig->num_output; i++)
            {
                xf86OutputPtr pOutput = pCrtcConfig->output[i];
                SECOutputPrivPtr pOutputPriv = pOutput->driver_private;

                if ((output == LAYER_OUTPUT_LCD &&
                    (pOutputPriv->mode_output->connector_type == DRM_MODE_CONNECTOR_LVDS ||
                     pOutputPriv->mode_output->connector_type == DRM_MODE_CONNECTOR_Unknown)) ||
                    (output == LAYER_OUTPUT_EXT &&
                    (pOutputPriv->mode_output->connector_type == DRM_MODE_CONNECTOR_HDMIA ||
                     pOutputPriv->mode_output->connector_type == DRM_MODE_CONNECTOR_HDMIB ||
                     pOutputPriv->mode_output->connector_type == DRM_MODE_CONNECTOR_VIRTUAL)))
                {
                    if (pOutput->crtc)
                    {
                        SECCrtcPrivPtr pCrtcPriv = pOutput->crtc->driver_private;
                        _secPropReturnProperty (value, "%d", pCrtcPriv->onoff);
                    }
                    break;
                }
            }
        }

        return TRUE;
    }
    onoff = atoi (p);

    if (lpos != 0)
    {
        if (!always)
            if (output == LAYER_OUTPUT_LCD && lpos == LAYER_UPPER)
            {
                xf86CrtcPtr pCrtc = xf86CompatCrtc (scrn);
                SECCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

                secCrtcOverlayNeedOff (pCrtc, !onoff);

                if (pCrtcPriv->cursor_show && !onoff)
                {
                    XDBG_TRACE (MCRS, "can't turn upper off.\n");
                    return FALSE;
                }
            }

        layer = secLayerFind ((SECLayerOutput)output, lpos);
        if (!layer)
            return FALSE;

        if (onoff)
            secLayerTurn (layer, TRUE, TRUE);
        else
            secLayerTurn (layer, FALSE, TRUE);
    }
    else
    {
        xf86CrtcConfigPtr pCrtcConfig;
        int i;

        pCrtcConfig = XF86_CRTC_CONFIG_PTR (scrn);
        if (!pCrtcConfig)
            return FALSE;

        for (i = 0; i < pCrtcConfig->num_output; i++)
        {
            xf86OutputPtr pOutput = pCrtcConfig->output[i];
            SECOutputPrivPtr pOutputPriv = pOutput->driver_private;

            if ((output == LAYER_OUTPUT_LCD &&
                (pOutputPriv->mode_output->connector_type == DRM_MODE_CONNECTOR_LVDS ||
                 pOutputPriv->mode_output->connector_type == DRM_MODE_CONNECTOR_Unknown)) ||
                (output == LAYER_OUTPUT_EXT &&
                (pOutputPriv->mode_output->connector_type == DRM_MODE_CONNECTOR_HDMIA ||
                 pOutputPriv->mode_output->connector_type == DRM_MODE_CONNECTOR_HDMIB ||
                 pOutputPriv->mode_output->connector_type == DRM_MODE_CONNECTOR_VIRTUAL)))
            {
                SECCrtcPrivPtr pCrtcPriv;

                if (!pOutput->crtc)
                    break;

                pCrtcPriv = pOutput->crtc->driver_private;
                if (pCrtcPriv->bAccessibility)
                {
                    _secPropReturnProperty (value, "[Xorg] crtc(%d) accessibility ON. \n",
                                            secCrtcID(pCrtcPriv));
                    return TRUE;
                }

                if (pOutput->crtc && !secCrtcTurn (pOutput->crtc, onoff, always, TRUE))
                {
                    _secPropReturnProperty (value, "[Xorg] crtc(%d) now %s%s\n",
                                                secCrtcID(pCrtcPriv),
                                                (pCrtcPriv->onoff)?"ON":"OFF",
                                                (pCrtcPriv->onoff_always)?"(always).":".");
                    return TRUE;
                }
                break;
            }
        }
    }

    _secPropReturnProperty (value, "[Xorg] output(%d), zpos(%d) layer %s%s\n",
                                output, pos, (onoff)?"ON":"OFF", (always)?"(always).":".");

    return TRUE;
}

Bool secPropVideoOffset (char *cmd, RRPropertyValuePtr value, ScrnInfoPtr scrn)
{
    SECPtr pSec = SECPTR (scrn);
    int x, y;
    char str[128];
    char *p;
    SECLayer *layer;

    snprintf (str, sizeof(str), "%s", cmd);

    p = strtok (str, ",");
    XDBG_RETURN_VAL_IF_FAIL (p != NULL, FALSE);
    x = atoi (p);

    p = strtok (NULL, ",");
    XDBG_RETURN_VAL_IF_FAIL (p != NULL, FALSE);
    y = atoi (p);

#if 0
    PropertyPtr rotate_prop;
    int rotate = 0;
    rotate_prop = secUtilGetWindowProperty (scrn->pScreen->root, "_E_ILLUME_ROTATE_ROOT_ANGLE");
    if (rotate_prop)
        rotate = *(int*)rotate_prop->data;
#endif

    pSec->pVideoPriv->video_offset_x = x;
    pSec->pVideoPriv->video_offset_y = y;

    layer = secLayerFind (LAYER_OUTPUT_LCD, LAYER_LOWER1);
    if (layer)
        secLayerSetOffset (layer, x, y);

    layer = secLayerFind (LAYER_OUTPUT_LCD, LAYER_LOWER2);
    if (layer)
        secLayerSetOffset (layer, x, y);

    _secPropReturnProperty (value, "[Xorg] video_offset : %d,%d.\n",
                            pSec->pVideoPriv->video_offset_x,
                            pSec->pVideoPriv->video_offset_y);

    return TRUE;
}

Bool
secPropSetFbVisible (xf86OutputPtr pOutput, Atom property, RRPropertyValuePtr value)
{
    if (g_fb_visible_prop_init == FALSE)
    {
        xrr_property_fb_visible_atom = MakeAtom (STR_XRR_FB_VISIBLE_PROPERTY,
                                                 strlen (STR_XRR_FB_VISIBLE_PROPERTY), TRUE);
        g_fb_visible_prop_init = TRUE;
    }

    if (xrr_property_fb_visible_atom != property)
        return FALSE;

    if (!value || !value->data || value->size == 0)
        return TRUE;

    if (value->format != 8)
        return TRUE;

    XDBG_TRACE (MPROP, "%s \n", value->data);

    secPropFbVisible (value->data, FALSE, value, pOutput->scrn);

    return TRUE;
}

Bool
secPropSetVideoOffset (xf86OutputPtr pOutput, Atom property, RRPropertyValuePtr value)
{
    if (g_video_offset_prop_init == FALSE)
    {
        xrr_property_video_offset_atom = MakeAtom (STR_XRR_VIDEO_OFFSET_PROPERTY,
                                                 strlen (STR_XRR_VIDEO_OFFSET_PROPERTY), TRUE);
        g_video_offset_prop_init = TRUE;
    }

    if (xrr_property_video_offset_atom != property)
        return FALSE;

    if (!value || !value->data || value->size == 0)
        return TRUE;

    if (value->format != 8)
        return TRUE;

    XDBG_TRACE (MPROP, "%s \n", value->data);

    secPropVideoOffset (value->data, value, pOutput->scrn);

    return TRUE;
}

Bool secPropScreenRotate (char *cmd, RRPropertyValuePtr value, ScrnInfoPtr scrn)
{
    xf86CrtcPtr crtc = xf86CompatCrtc (scrn);
    int degree;

    if (!crtc)
        return TRUE;

    if (!strcmp (cmd, "normal"))
        degree = 0;
    else if (!strcmp (cmd, "right"))
        degree = 90;
    else if (!strcmp (cmd, "inverted"))
        degree = 180;
    else if (!strcmp (cmd, "left"))
        degree = 270;
    else if (!strcmp (cmd, "0"))
        degree = 0;
    else if (!strcmp (cmd, "1"))
        degree = 270;
    else if (!strcmp (cmd, "2"))
        degree = 180;
    else if (!strcmp (cmd, "3"))
        degree = 90;
    else
    {
        _secPropReturnProperty (value, "[Xorg] unknown value: %s\n", cmd);
        return TRUE;
    }

    if (secCrtcScreenRotate (crtc, degree))
        _secPropReturnProperty (value, "[Xorg] screen rotated %d.\n", degree);
    else
    {
        _secPropReturnProperty (value, "[Xorg] Fail screen rotate %d.\n", degree);
        return FALSE;
    }

    return TRUE;
}

Bool
secPropSetScreenRotate (xf86OutputPtr pOutput, Atom property, RRPropertyValuePtr value)
{
    if (g_screen_rotate_prop_init == FALSE)
    {
        xrr_property_screen_rotate_atom = MakeAtom (STR_XRR_PROPERTY_SCREEN_ROTATE,
                                                 strlen (STR_XRR_PROPERTY_SCREEN_ROTATE), TRUE);
        g_screen_rotate_prop_init = TRUE;
    }

    if (xrr_property_screen_rotate_atom != property)
        return FALSE;

    if (!value || !value->data || value->size == 0)
        return TRUE;

    if (value->format != 8)
        return TRUE;

    XDBG_TRACE (MPROP, "%s \n", value->data);

    secPropScreenRotate (value->data, value, pOutput->scrn);

    return TRUE;
}

