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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>

#include <xorgVersion.h>
#include <tbm_bufmgr.h>
#include <xf86Crtc.h>
#include <xf86DDC.h>
#include <xf86cmap.h>
#include <list.h>
#include <X11/Xatom.h>
#include <X11/extensions/dpmsconst.h>
#include <sec.h>

#include "sec_util.h"
#include "sec_crtc.h"
#include "sec_output.h"
#include "sec_prop.h"
#include "sec_xberc.h"
#include "sec_layer.h"
#include "sec_wb.h"
#include "sec_video_virtual.h"

static const int subpixel_conv_table[7] =
{
    0,
    SubPixelUnknown,
    SubPixelHorizontalRGB,
    SubPixelHorizontalBGR,
    SubPixelVerticalRGB,
    SubPixelVerticalBGR,
    SubPixelNone
};

static const char *output_names[] =
{
    "None",
    "VGA",
    "DVI",
    "DVI",
    "DVI",
    "Composite",
    "TV",
    "LVDS",
    "CTV",
    "DIN",
    "DP",
    "HDMI",
    "HDMI",
    "TV",
    "eDP",
    "Virtual",
};

static CARD32
_secOutputResumeWbTimeout (OsTimerPtr timer, CARD32 now, pointer arg)
{
    XDBG_RETURN_VAL_IF_FAIL(arg, 0);

    xf86OutputPtr pOutput = (xf86OutputPtr)arg;
    SECPtr pSec = SECPTR (pOutput->scrn);

    pSec = SECPTR (pOutput->scrn);

    if (pSec->resume_timer)
    {
        TimerFree (pSec->resume_timer);
        pSec->resume_timer = NULL;
    }

    secDisplaySetDispSetMode (pOutput->scrn, pSec->set_mode);
    pSec->set_mode = DISPLAY_SET_MODE_OFF;

    return 0;
}

static void
_secOutputAttachEdid(xf86OutputPtr pOutput)
{
    SECOutputPrivPtr pOutputPriv = pOutput->driver_private;
    drmModeConnectorPtr koutput = pOutputPriv->mode_output;
    SECModePtr pSecMode = pOutputPriv->pSecMode;
    drmModePropertyBlobPtr edid_blob = NULL;
    xf86MonPtr mon = NULL;
    int i;

    /* look for an EDID property */
    for (i = 0; i < koutput->count_props; i++)
    {
        drmModePropertyPtr props;

        props = drmModeGetProperty (pSecMode->fd, koutput->props[i]);
        if (!props)
            continue;

        if (!(props->flags & DRM_MODE_PROP_BLOB))
        {
            drmModeFreeProperty (props);
            continue;
        }

        if (!strcmp (props->name, "EDID"))
        {
            drmModeFreePropertyBlob (edid_blob);
            edid_blob =
                drmModeGetPropertyBlob (pSecMode->fd,
                                        koutput->prop_values[i]);
        }
        drmModeFreeProperty (props);
    }

    if (edid_blob)
    {
        mon = xf86InterpretEDID (pOutput->scrn->scrnIndex,
                                 edid_blob->data);

        if (mon && edid_blob->length > 128)
            mon->flags |= MONITOR_EDID_COMPLETE_RAWDATA;
    }

    xf86OutputSetEDID (pOutput, mon);

    if (edid_blob)
        drmModeFreePropertyBlob (edid_blob);
}

static Bool
_secOutputPropertyIgnore(drmModePropertyPtr prop)
{
    if (!prop)
        return TRUE;

    /* ignore blob prop */
    if (prop->flags & DRM_MODE_PROP_BLOB)
        return TRUE;

    /* ignore standard property */
    if (!strcmp (prop->name, "EDID") ||
        !strcmp (prop->name, "DPMS"))
        return TRUE;

    return FALSE;
}

static xf86OutputStatus
SECOutputDetect(xf86OutputPtr output)
{
    /* go to the hw and retrieve a new output struct */
    SECOutputPrivPtr pOutputPriv = output->driver_private;
    SECModePtr pSecMode = pOutputPriv->pSecMode;
    xf86OutputStatus status;
//    char *conn_str[] = {"connected", "disconnected", "unknow"};

    /* update output */
    drmModeFreeConnector (pOutputPriv->mode_output);
    pOutputPriv->mode_output =
        drmModeGetConnector (pSecMode->fd, pOutputPriv->output_id);
    XDBG_RETURN_VAL_IF_FAIL (pOutputPriv->mode_output != NULL, XF86OutputStatusUnknown);

    /* update encoder */
    drmModeFreeEncoder (pOutputPriv->mode_encoder);
    pOutputPriv->mode_encoder =
        drmModeGetEncoder (pSecMode->fd, pOutputPriv->mode_output->encoders[0]);
    XDBG_RETURN_VAL_IF_FAIL (pOutputPriv->mode_encoder != NULL, XF86OutputStatusUnknown);

    if (pSecMode->unset_connector_type == pOutputPriv->mode_output->connector_type)
    {
        return XF86OutputStatusDisconnected;
    }
#if 0
    XDBG_INFO (MSEC, "detect : connect(%d, type:%d, status:%s) encoder(%d) crtc(%d).\n",
               pOutputPriv->output_id, pOutputPriv->mode_output->connector_type,
               conn_str[pOutputPriv->mode_output->connection-1],
               pOutputPriv->mode_encoder->encoder_id, pOutputPriv->mode_encoder->crtc_id);
#endif
    switch (pOutputPriv->mode_output->connection)
    {
    case DRM_MODE_CONNECTED:
        status = XF86OutputStatusConnected;
        break;
    case DRM_MODE_DISCONNECTED:
        status = XF86OutputStatusDisconnected;
        /* unset write-back clone */
        secPropUnSetDisplayMode (output);
        break;
    default:
    case DRM_MODE_UNKNOWNCONNECTION:
        status = XF86OutputStatusUnknown;
        break;
    }
    return status;
}

static Bool
SECOutputModeValid(xf86OutputPtr pOutput, DisplayModePtr pModes)
{
    SECOutputPrivPtr pOutputPriv = pOutput->driver_private;
    drmModeConnectorPtr koutput = pOutputPriv->mode_output;
    int i;

    /* driver want to remain available modes which is same as mode
       supported from drmmode */
    if (pOutputPriv->mode_output->connector_type == DRM_MODE_CONNECTOR_LVDS)
    {
        for (i = 0; i < koutput->count_modes; i++)
        {
            if (pModes->HDisplay == koutput->modes[i].hdisplay &&
                pModes->VDisplay == koutput->modes[i].vdisplay)
                return MODE_OK;
        }
        return MODE_ERROR;
    }

    return MODE_OK;
}

static DisplayModePtr
SECOutputGetModes(xf86OutputPtr pOutput)
{
    SECOutputPrivPtr pOutputPriv = pOutput->driver_private;
    drmModeConnectorPtr koutput = pOutputPriv->mode_output;
    DisplayModePtr Modes = NULL;
    int i;
    SECPtr pSec = SECPTR (pOutput->scrn);
    DisplayModePtr Mode;

    /* LVDS1 (main LCD) does not provide edid data */
    if (pOutputPriv->mode_output->connector_type != DRM_MODE_CONNECTOR_LVDS)
        _secOutputAttachEdid(pOutput);

    /* modes should already be available */
    for (i = 0; i < koutput->count_modes; i++)
    {
        Mode = calloc (1, sizeof (DisplayModeRec));
        if (Mode)
        {
            /* generate the fake modes when screen rotation is set */
            if(pSec->fake_root)
                secDisplaySwapModeFromKmode(pOutput->scrn, &koutput->modes[i], Mode);
            else
                secDisplayModeFromKmode(pOutput->scrn, &koutput->modes[i], Mode);
            Modes = xf86ModesAdd(Modes, Mode);
        }
    }

    return Modes;
}

static void
SECOutputDestory(xf86OutputPtr pOutput)
{
    SECOutputPrivPtr pOutputPriv = pOutput->driver_private;
    SECPtr pSec = SECPTR (pOutput->scrn);
    int i;

    if (pSec->resume_timer)
    {
        TimerFree (pSec->resume_timer);
        pSec->resume_timer = NULL;
    }
    pSec->set_mode = DISPLAY_SET_MODE_OFF;

    for (i = 0; i < pOutputPriv->num_props; i++)
    {
        drmModeFreeProperty (pOutputPriv->props[i].mode_prop);
        free (pOutputPriv->props[i].atoms);
    }
    free (pOutputPriv->props);

    drmModeFreeEncoder (pOutputPriv->mode_encoder);
    drmModeFreeConnector (pOutputPriv->mode_output);
    xorg_list_del (&pOutputPriv->link);
    free (pOutputPriv);

    pOutput->driver_private = NULL;
}

static void
SECOutputDpms(xf86OutputPtr pOutput, int dpms)
{
    SECOutputPrivPtr pOutputPriv = pOutput->driver_private;
    drmModeConnectorPtr koutput = pOutputPriv->mode_output;
    SECModePtr pSecMode = pOutputPriv->pSecMode;
    SECPtr pSec = SECPTR (pOutput->scrn);
    int old_dpms = pOutputPriv->dpms_mode;
    int i;

    if (!strcmp(pOutput->name, "HDMI1") ||
        !strcmp(pOutput->name, "Virtual1"))
            return;

    if (dpms == DPMSModeSuspend)
        return;

    for (i = 0; i < koutput->count_props; i++)
    {
        drmModePropertyPtr props;

        props = drmModeGetProperty (pSecMode->fd, koutput->props[i]);
        if (!props)
            continue;

        if ((old_dpms == DPMSModeStandby && dpms == DPMSModeOn) ||
            (old_dpms == DPMSModeOn && dpms == DPMSModeStandby))
        {
            if (!strcmp (props->name, "panel"))
            {
                int value = (dpms == DPMSModeStandby)? 1 : 0;
                drmModeConnectorSetProperty(pSecMode->fd,
                                            pOutputPriv->output_id,
                                            props->prop_id,
                                            value);
                pOutputPriv->dpms_mode = dpms;
                drmModeFreeProperty (props);
                XDBG_INFO (MDPMS, "panel '%s'\n", (value)?"OFF":"ON");
                return;
            }
        }
        else if (!strcmp (props->name, "DPMS"))
        {
            int _tmp_dpms = dpms;
            switch (dpms)
            {
            case DPMSModeStandby:
            case DPMSModeOn:
                if (pOutputPriv->isLcdOff == FALSE)
                {
                    drmModeFreeProperty (props);
                    return;
                }
                /* lcd on */
                XDBG_INFO (MDPMS, "\t Reqeust DPMS ON (%s)\n", pOutput->name);
                _tmp_dpms = DPMSModeOn;
                pOutputPriv->isLcdOff = FALSE;

                if (!strcmp(pOutput->name, "LVDS1"))
                {
                    pSec->isLcdOff = FALSE;

                    /* if wb need to be started, start wb after timeout. */
                    if (pSec->set_mode == DISPLAY_SET_MODE_CLONE)
                    {
                        pSec->resume_timer = TimerSet (pSec->resume_timer,
                                                       0, 30,
                                                       _secOutputResumeWbTimeout,
                                                       pOutput);
                    }

                    secVideoDpms (pOutput->scrn, TRUE);
                    secVirtualVideoDpms (pOutput->scrn, TRUE);
                }

                /* accessibility */
                SECCrtcPrivPtr pCrtcPriv = pOutput->crtc->driver_private;
                if (pCrtcPriv->screen_rotate_degree > 0)
                    secCrtcEnableScreenRotate (pOutput->crtc, TRUE);
                else
                    secCrtcEnableScreenRotate (pOutput->crtc, FALSE);
                if (pCrtcPriv->bAccessibility || pCrtcPriv->screen_rotate_degree > 0)
                {
                    tbm_bo src_bo = pCrtcPriv->front_bo;
                    tbm_bo dst_bo = pCrtcPriv->accessibility_back_bo;
                    if (!secCrtcExecAccessibility (pOutput->crtc, src_bo, dst_bo))
                    {
                        XDBG_ERROR(MDPMS, "Fail execute accessibility(output name, %s)\n",
                                   pOutput->name);
                    }
                }

                /* set current fb to crtc */
                if(!secCrtcApply(pOutput->crtc))
                {
                    XDBG_ERROR(MDPMS, "Fail crtc apply(output name, %s)\n",
                               pOutput->name);
                }
                break;
            case DPMSModeOff:
                if (pOutputPriv->isLcdOff == TRUE)
                {
                    drmModeFreeProperty (props);
                    return;
                }
                /* lcd off */
                XDBG_INFO (MDPMS, "\t Reqeust DPMS OFF (%s)\n", pOutput->name);
                _tmp_dpms = DPMSModeOff;
                pOutputPriv->isLcdOff = TRUE;

                secCrtcEnableScreenRotate (pOutput->crtc, FALSE);

                if (!strcmp(pOutput->name, "LVDS1"))
                {
                    secVideoDpms (pOutput->scrn, FALSE);
                    secVirtualVideoDpms (pOutput->scrn, FALSE);

                    pSec->isLcdOff = TRUE;

                    if (pSec->resume_timer)
                    {
                        TimerFree (pSec->resume_timer);
                        drmModeFreeProperty (props);
                        pSec->resume_timer = NULL;
                        return;
                    }

                    /* keep previous pSecMode's set_mode. */
                    pSec->set_mode = secDisplayGetDispSetMode (pOutput->scrn);

                    if (pSec->set_mode == DISPLAY_SET_MODE_CLONE)
                    {
                        /* stop wb if wb is working. */
                        secDisplaySetDispSetMode (pOutput->scrn, DISPLAY_SET_MODE_OFF);
                    }
                }
                break;
            default:
                 drmModeFreeProperty (props);
                return;
            }

            drmModeConnectorSetProperty(pSecMode->fd,
                                        pOutputPriv->output_id,
                                        props->prop_id,
                                        _tmp_dpms);

            XDBG_INFO (MDPMS, "\t Success DPMS request (%s)\n", pOutput->name);

            pOutputPriv->dpms_mode = _tmp_dpms;
            drmModeFreeProperty (props);
            return;
        }

        drmModeFreeProperty (props);
    }
}

static void
SECOutputCreateReaources(xf86OutputPtr pOutput)
{
    SECOutputPrivPtr pOutputPriv = pOutput->driver_private;
    drmModeConnectorPtr mode_output = pOutputPriv->mode_output;
    SECModePtr pSecMode = pOutputPriv->pSecMode;
    int i, j, err;

    pOutputPriv->props = calloc (mode_output->count_props, sizeof (SECPropertyRec));
    if (!pOutputPriv->props)
        return;

    pOutputPriv->num_props = 0;
    for (i = j = 0; i < mode_output->count_props; i++)
    {
        drmModePropertyPtr drmmode_prop;

        drmmode_prop = drmModeGetProperty(pSecMode->fd,
                                          mode_output->props[i]);
        if (_secOutputPropertyIgnore(drmmode_prop))
        {
            drmModeFreeProperty (drmmode_prop);
            continue;
        }

        pOutputPriv->props[j].mode_prop = drmmode_prop;
        pOutputPriv->props[j].value = mode_output->prop_values[i];
        j++;
    }
    pOutputPriv->num_props = j;

    for (i = 0; i < pOutputPriv->num_props; i++)
    {
        SECPropertyPtr p = &pOutputPriv->props[i];
        drmModePropertyPtr drmmode_prop = p->mode_prop;

        if (drmmode_prop->flags & DRM_MODE_PROP_RANGE)
        {
            INT32 range[2];

            p->num_atoms = 1;
            p->atoms = calloc (p->num_atoms, sizeof (Atom));
            if (!p->atoms)
                continue;

            p->atoms[0] = MakeAtom (drmmode_prop->name, strlen (drmmode_prop->name), TRUE);
            range[0] = drmmode_prop->values[0];
            range[1] = drmmode_prop->values[1];
            err = RRConfigureOutputProperty (pOutput->randr_output, p->atoms[0],
                                             FALSE, TRUE,
                                             drmmode_prop->flags & DRM_MODE_PROP_IMMUTABLE ? TRUE : FALSE,
                                             2, range);
            if (err != 0)
            {
                xf86DrvMsg (pOutput->scrn->scrnIndex, X_ERROR,
                            "RRConfigureOutputProperty error, %d\n", err);
            }
            err = RRChangeOutputProperty (pOutput->randr_output, p->atoms[0],
                                          XA_INTEGER, 32, PropModeReplace, 1, &p->value, FALSE, TRUE);
            if (err != 0)
            {
                xf86DrvMsg (pOutput->scrn->scrnIndex, X_ERROR,
                            "RRChangeOutputProperty error, %d\n", err);
            }
        }
        else if (drmmode_prop->flags & DRM_MODE_PROP_ENUM)
        {
            p->num_atoms = drmmode_prop->count_enums + 1;
            p->atoms = calloc (p->num_atoms, sizeof (Atom));
            if (!p->atoms)
                continue;

            p->atoms[0] = MakeAtom (drmmode_prop->name, strlen (drmmode_prop->name), TRUE);
            for (j = 1; j <= drmmode_prop->count_enums; j++)
            {
                struct drm_mode_property_enum *e = &drmmode_prop->enums[j-1];
                p->atoms[j] = MakeAtom (e->name, strlen (e->name), TRUE);
            }

            err = RRConfigureOutputProperty (pOutput->randr_output, p->atoms[0],
                                             FALSE, FALSE,
                                             drmmode_prop->flags & DRM_MODE_PROP_IMMUTABLE ? TRUE : FALSE,
                                             p->num_atoms - 1, (INT32*)&p->atoms[1]);
            if (err != 0)
            {
                xf86DrvMsg (pOutput->scrn->scrnIndex, X_ERROR,
                            "RRConfigureOutputProperty error, %d\n", err);
            }

            for (j = 0; j < drmmode_prop->count_enums; j++)
                if (drmmode_prop->enums[j].value == p->value)
                    break;
            /* there's always a matching value */
            err = RRChangeOutputProperty (pOutput->randr_output, p->atoms[0],
                                          XA_ATOM, 32, PropModeReplace, 1, &p->atoms[j+1], FALSE, TRUE);
            if (err != 0)
            {
                xf86DrvMsg (pOutput->scrn->scrnIndex, X_ERROR,
                            "RRChangeOutputProperty error, %d\n", err);
            }
        }

        if (p->atoms)
            free (p->atoms);
    }
}

static Bool
SECOutputSetProperty(xf86OutputPtr output, Atom property,
                     RRPropertyValuePtr value)
{
    SECOutputPrivPtr pOutputPriv = output->driver_private;
    SECModePtr pSecMode = pOutputPriv->pSecMode;
    int i;

    //SECOutputDpms(output, DPMSModeStandby);

    for (i = 0; i < pOutputPriv->num_props; i++)
    {
        SECPropertyPtr p = &pOutputPriv->props[i];

        if (p->atoms[0] != property)
            continue;

        if (p->mode_prop->flags & DRM_MODE_PROP_RANGE)
        {
            uint32_t val;

            if (value->type != XA_INTEGER || value->format != 32 ||
                    value->size != 1)
                return FALSE;
            val = *(uint32_t*)value->data;

            drmModeConnectorSetProperty (pSecMode->fd, pOutputPriv->output_id,
                                         p->mode_prop->prop_id, (uint64_t) val);
            return TRUE;
        }
        else if (p->mode_prop->flags & DRM_MODE_PROP_ENUM)
        {
            Atom	atom;
            const char	*name;
            int		j;

            if (value->type != XA_ATOM || value->format != 32 || value->size != 1)
                return FALSE;
            memcpy (&atom, value->data, 4);
            name = NameForAtom (atom);
            XDBG_RETURN_VAL_IF_FAIL ((name != NULL), FALSE);

            /* search for matching name string, then set its value down */
            for (j = 0; j < p->mode_prop->count_enums; j++)
            {
                if (!strcmp (p->mode_prop->enums[j].name, name))
                {
                    drmModeConnectorSetProperty (pSecMode->fd, pOutputPriv->output_id,
                                                 p->mode_prop->prop_id, p->mode_prop->enums[j].value);
                    return TRUE;
                }
            }
            return FALSE;
        }
    }

    /* We didn't recognise this property, just report success in order
     * to allow the set to continue, otherwise we break setting of
     * common properties like EDID.
     */
    /* set the hidden properties : features for sec debugging*/
    /* TODO : xberc can works on only LVDS????? */
    if (pOutputPriv->mode_output->connector_type == DRM_MODE_CONNECTOR_LVDS)
    {
        if (secPropSetLvdsFunc (output, property, value))
            return TRUE;

        if (secPropSetFbVisible (output, property, value))
            return TRUE;

        if (secPropSetVideoOffset (output, property, value))
            return TRUE;

        if (secPropSetScreenRotate (output, property, value))
            return TRUE;

        if (secXbercSetProperty (output, property, value))
            return TRUE;
    }
    /* set the hidden properties : features for driver specific funtions */
    if (pOutputPriv->mode_output->connector_type == DRM_MODE_CONNECTOR_HDMIA ||
        pOutputPriv->mode_output->connector_type == DRM_MODE_CONNECTOR_HDMIB ||
        pOutputPriv->mode_output->connector_type == DRM_MODE_CONNECTOR_VIRTUAL)
    {
        /* set the property for the display mode */
        if (secPropSetDisplayMode(output, property, value))
            return TRUE;
    }

    return TRUE;
}

static Bool
SECOutputGetProperty(xf86OutputPtr pOutput, Atom property)
{
    return FALSE;
}

static const xf86OutputFuncsRec sec_output_funcs =
{
    .create_resources = SECOutputCreateReaources,
#ifdef RANDR_12_INTERFACE
    .set_property = SECOutputSetProperty,
    .get_property = SECOutputGetProperty,
#endif
    .dpms = SECOutputDpms,
#if 0
    .save = drmmode_crt_save,
    .restore = drmmode_crt_restore,
    .mode_fixup = drmmode_crt_mode_fixup,
    .prepare = sec_output_prepare,
    .mode_set = drmmode_crt_mode_set,
    .commit = sec_output_commit,
#endif
    .detect = SECOutputDetect,
    .mode_valid = SECOutputModeValid,

    .get_modes = SECOutputGetModes,
    .destroy = SECOutputDestory
};

Bool
secOutputDrmUpdate (ScrnInfoPtr pScrn)
{
    SECModePtr pSecMode = (SECModePtr) SECPTR (pScrn)->pSecMode;
    Bool ret = TRUE;
    int i;

    for (i = 0; i < pSecMode->mode_res->count_connectors; i++)
    {
        SECOutputPrivPtr pCur = NULL, pNext = NULL, pOutputPriv = NULL;
        drmModeConnectorPtr koutput;
        drmModeEncoderPtr kencoder;
        char *conn_str[] = {"connected", "disconnected", "unknow"};

        xorg_list_for_each_entry_safe (pCur, pNext, &pSecMode->outputs, link)
        {
            if (pCur->output_id == pSecMode->mode_res->connectors[i])
            {
                pOutputPriv = pCur;
                break;
            }
        }

        if (!pOutputPriv)
        {
            ret = FALSE;
            break;
        }

        koutput = drmModeGetConnector (pSecMode->fd,
                                       pSecMode->mode_res->connectors[i]);
        if (!koutput)
        {
            ret = FALSE;
            break;
        }

        kencoder = drmModeGetEncoder (pSecMode->fd, koutput->encoders[0]);
        if (!kencoder)
        {
            drmModeFreeConnector (koutput);
            ret = FALSE;
            break;
        }

        if (pOutputPriv->mode_output)
        {
            drmModeFreeConnector (pOutputPriv->mode_output);
            pOutputPriv->mode_output = NULL;
        }
        pOutputPriv->mode_output = koutput;

        if (pOutputPriv->mode_encoder)
        {
            drmModeFreeEncoder (pOutputPriv->mode_encoder);
            pOutputPriv->mode_encoder = NULL;
        }
        pOutputPriv->mode_encoder = kencoder;

        XDBG_INFO (MSEC, "drm update : connect(%d, type:%d, status:%s) encoder(%d) crtc(%d).\n",
                   pSecMode->mode_res->connectors[i], koutput->connector_type,
                   conn_str[pOutputPriv->mode_output->connection-1],
                   kencoder->encoder_id, kencoder->crtc_id);
#if 0
        /* Does these need to update? */
        pOutput->mm_width = koutput->mmWidth;
        pOutput->mm_height = koutput->mmHeight;

        pOutput->possible_crtcs = kencoder->possible_crtcs;
        pOutput->possible_clones = kencoder->possible_clones;
#endif
    }

    if (!ret)
        xf86DrvMsg (pScrn->scrnIndex, X_ERROR, "drm(output) update error. (%s)\n", strerror (errno));

    return ret;
}

void
secOutputInit (ScrnInfoPtr pScrn, SECModePtr pSecMode, int num)
{
    xf86OutputPtr pOutput;
    drmModeConnectorPtr koutput;
    drmModeEncoderPtr kencoder;
    SECOutputPrivPtr pOutputPriv;
    const char *output_name;
    char name[32];

    koutput = drmModeGetConnector (pSecMode->fd,
                                   pSecMode->mode_res->connectors[num]);
    if (!koutput)
        return;

    kencoder = drmModeGetEncoder (pSecMode->fd, koutput->encoders[0]);
    if (!kencoder)
    {
        drmModeFreeConnector (koutput);
        return;
    }

    if (koutput->connector_type < ARRAY_SIZE (output_names))
        output_name = output_names[koutput->connector_type];
    else
        output_name = "UNKNOWN";
    snprintf (name, 32, "%s%d", output_name, koutput->connector_type_id);

    pOutput = xf86OutputCreate (pScrn, &sec_output_funcs, name);
    if (!pOutput)
    {
        drmModeFreeEncoder (kencoder);
        drmModeFreeConnector (koutput);
        return;
    }

    pOutputPriv = calloc (sizeof (SECOutputPrivRec), 1);
    if (!pOutputPriv)
    {
        xf86OutputDestroy (pOutput);
        drmModeFreeConnector (koutput);
        drmModeFreeEncoder (kencoder);
        return;
    }

    pOutputPriv->output_id = pSecMode->mode_res->connectors[num];
    pOutputPriv->mode_output = koutput;
    pOutputPriv->mode_encoder = kencoder;
    pOutputPriv->pSecMode = pSecMode;

    pOutput->mm_width = koutput->mmWidth;
    pOutput->mm_height = koutput->mmHeight;

    pOutput->subpixel_order = subpixel_conv_table[koutput->subpixel];
    pOutput->driver_private = pOutputPriv;

    pOutput->possible_crtcs = kencoder->possible_crtcs;
    pOutput->possible_clones = kencoder->possible_clones;
    pOutput->interlaceAllowed = TRUE;

    pOutputPriv->pOutput = pOutput;
    /* TODO : soolim : management crtc privates */
    xorg_list_add(&pOutputPriv->link, &pSecMode->outputs);
}

int
secOutputDpmsStatus(xf86OutputPtr pOutput)
{
    SECOutputPrivPtr pOutputPriv = pOutput->driver_private;
    return pOutputPriv->dpms_mode;
}

void
secOutputDpmsSet(xf86OutputPtr pOutput, int mode)
{
    SECOutputDpms(pOutput, mode);
}

SECOutputPrivPtr
secOutputGetPrivateForConnType (ScrnInfoPtr pScrn, int connect_type)
{
    SECModePtr pSecMode = (SECModePtr) SECPTR (pScrn)->pSecMode;
    int i;

    for (i = 0; i < pSecMode->mode_res->count_connectors; i++)
    {
        SECOutputPrivPtr pCur = NULL, pNext = NULL;

        xorg_list_for_each_entry_safe (pCur, pNext, &pSecMode->outputs, link)
        {
            drmModeConnectorPtr koutput = pCur->mode_output;

            if (koutput && koutput->connector_type == connect_type)
                return pCur;
        }
    }

    XDBG_ERROR (MSEC, "no output for connect_type(%d) \n", connect_type);

    return NULL;
}
