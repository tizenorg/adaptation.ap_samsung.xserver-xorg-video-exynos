/**************************************************************************

xserver-xorg-video-exynos

Copyright 2011 Samsung Electronics co., Ltd. All Rights Reserved.
Copyright 2013 Intel Corporation

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
#include <exynos.h>

#include "exynos_util.h"
#include "exynos_crtc.h"
#include "exynos_output.h"
#include "exynos_prop.h"
#include "exynos_xberc.h"
#include "exynos_layer.h"
#include "exynos_wb.h"
#include "exynos_video_virtual.h"
#include "exynos_video_clone.h"

static const int subpixel_conv_table[7] = {
    0,
    SubPixelUnknown,
    SubPixelHorizontalRGB,
    SubPixelHorizontalBGR,
    SubPixelVerticalRGB,
    SubPixelVerticalBGR,
    SubPixelNone
};

static const char *output_names[] = {
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

#if 0
static CARD32
_exynosOutputResumeWbTimeout(OsTimerPtr timer, CARD32 now, pointer arg)
{
    XDBG_RETURN_VAL_IF_FAIL(arg, 0);

    xf86OutputPtr pOutput = (xf86OutputPtr) arg;
    EXYNOSPtr pExynos = EXYNOSPTR(pOutput->scrn);

    pExynos = EXYNOSPTR(pOutput->scrn);

    if (pExynos->resume_timer) {
        TimerFree(pExynos->resume_timer);
        pExynos->resume_timer = NULL;
    }

    exynosDisplaySetDispSetMode(pOutput->scrn, pExynos->set_mode);
    pExynos->set_mode = DISPLAY_SET_MODE_OFF;

    return 0;
}
#endif
static void
_exynosOutputAttachEdid(xf86OutputPtr pOutput)
{
    EXYNOSOutputPrivPtr pOutputPriv = pOutput->driver_private;

    if (pOutputPriv == NULL) {
        return;
    }
    drmModeConnectorPtr koutput = pOutputPriv->mode_output;
    EXYNOSModePtr pExynosMode = pOutputPriv->pExynosMode;
    drmModePropertyBlobPtr edid_blob = NULL;
    xf86MonPtr mon = NULL;
    int i;

    /* look for an EDID property */
    for (i = 0; i < koutput->count_props; i++) {
        drmModePropertyPtr props;

        props = drmModeGetProperty(pExynosMode->fd, koutput->props[i]);
        if (!props)
            continue;

        if (!(props->flags & DRM_MODE_PROP_BLOB)) {
            drmModeFreeProperty(props);
            continue;
        }

        if (!strcmp(props->name, "EDID")) {
            drmModeFreePropertyBlob(edid_blob);
            edid_blob =
                drmModeGetPropertyBlob(pExynosMode->fd,
                                       koutput->prop_values[i]);
        }
        drmModeFreeProperty(props);
    }

    if (edid_blob) {
        mon = xf86InterpretEDID(pOutput->scrn->scrnIndex, edid_blob->data);

        if (mon && edid_blob->length > 128)
            mon->flags |= MONITOR_EDID_COMPLETE_RAWDATA;
    }

    xf86OutputSetEDID(pOutput, mon);

    if (edid_blob)
        drmModeFreePropertyBlob(edid_blob);
}

static Bool
_exynosOutputPropertyIgnore(drmModePropertyPtr prop)
{
    if (!prop)
        return TRUE;

    /* ignore blob prop */
    if (prop->flags & DRM_MODE_PROP_BLOB)
        return TRUE;

    /* ignore standard property */
    if (!strcmp(prop->name, "EDID") || !strcmp(prop->name, "DPMS"))
        return TRUE;

    return FALSE;
}

static xf86OutputStatus
EXYNOSOutputDetect(xf86OutputPtr pOutput)
{
    /* go to the hw and retrieve a new output struct */
    EXYNOSOutputPrivPtr pOutputPriv = pOutput->driver_private;

    if (pOutputPriv == NULL) {
        return XF86OutputStatusDisconnected;
    }

    EXYNOSModePtr pExynosMode = pOutputPriv->pExynosMode;
    xf86OutputStatus status;

//    char *conn_str[] = {"connected", "disconnected", "unknow"};

    /* update output */
    drmModeFreeConnector(pOutputPriv->mode_output);
    pOutputPriv->mode_output =
        drmModeGetConnector(pExynosMode->fd, pOutputPriv->output_id);
    XDBG_RETURN_VAL_IF_FAIL(pOutputPriv->mode_output != NULL,
                            XF86OutputStatusUnknown);

    /* update encoder */
    drmModeFreeEncoder(pOutputPriv->mode_encoder);
    pOutputPriv->mode_encoder =
        drmModeGetEncoder(pExynosMode->fd,
                          pOutputPriv->mode_output->encoders[0]);
    XDBG_RETURN_VAL_IF_FAIL(pOutputPriv->mode_encoder != NULL,
                            XF86OutputStatusUnknown);

    if (pExynosMode->unset_connector_type ==
        pOutputPriv->mode_output->connector_type) {
        return XF86OutputStatusDisconnected;
    }
#if 0
    XDBG_INFO(MSEC,
              "detect : connect(%d, type:%d, status:%s) encoder(%d) crtc(%d).\n",
              pOutputPriv->output_id, pOutputPriv->mode_output->connector_type,
              conn_str[pOutputPriv->mode_output->connection - 1],
              pOutputPriv->mode_encoder->encoder_id,
              pOutputPriv->mode_encoder->crtc_id);
#endif
    switch (pOutputPriv->mode_output->connection) {
    case DRM_MODE_CONNECTED:
        status = XF86OutputStatusConnected;
        break;
    case DRM_MODE_DISCONNECTED:
        status = XF86OutputStatusDisconnected;
        /* unset write-back clone */
        exynosPropUnSetDisplayMode(pOutput);
        break;
    default:
    case DRM_MODE_UNKNOWNCONNECTION:
        status = XF86OutputStatusUnknown;
        break;
    }
    return status;
}

static Bool
EXYNOSOutputModeValid(xf86OutputPtr pOutput, DisplayModePtr pModes)
{
    EXYNOSOutputPrivPtr pOutputPriv = pOutput->driver_private;

    if (pOutputPriv == NULL) {
#ifdef NO_CRTC_MODE
        if (pModes->type & M_T_DEFAULT)
            return MODE_BAD;
        return MODE_OK;
#else
        return MODE_ERROR;
#endif                          //NO_CRTC_MODE
    }
    drmModeConnectorPtr koutput = pOutputPriv->mode_output;
    int i;

    /* driver want to remain available modes which is same as mode
       supported from drmmode */
#if NO_CRTC_MODE
    if (pOutputPriv->mode_output->connector_type == DRM_MODE_CONNECTOR_LVDS)
#endif
    {
        for (i = 0; i < koutput->count_modes; i++) {
            if (pModes->HDisplay == koutput->modes[i].hdisplay &&
                pModes->VDisplay == koutput->modes[i].vdisplay)
                return MODE_OK;
        }
        return MODE_ERROR;
    }

    return MODE_OK;
}

static DisplayModePtr
EXYNOSOutputGetModes(xf86OutputPtr pOutput)
{
    DisplayModePtr Modes = NULL;
    EXYNOSOutputPrivPtr pOutputPriv = pOutput->driver_private;

    if (pOutputPriv == NULL) {
#ifdef NO_CRTC_MODE
        Modes = xf86ModesAdd(Modes, xf86CVTMode(1024, 768, 60, 0, 0));
#endif
        return Modes;
    }

    drmModeConnectorPtr koutput = pOutputPriv->mode_output;
    int i;
    EXYNOSPtr pExynos = EXYNOSPTR(pOutput->scrn);
    DisplayModePtr Mode;

    /* LVDS1 (main LCD) does not provide edid data */
    if (pOutputPriv->mode_output->connector_type != DRM_MODE_CONNECTOR_LVDS)
        _exynosOutputAttachEdid(pOutput);

    /* modes should already be available */
    for (i = 0; i < koutput->count_modes; i++) {
        Mode = calloc(1, sizeof(DisplayModeRec));
        if (Mode) {
            /* generate the fake modes when screen rotation is set */
            if (pExynos->fake_root)
                exynosDisplaySwapModeFromKmode(pOutput->scrn,
                                               &koutput->modes[i], Mode);
            else
                exynosDisplayModeFromKmode(pOutput->scrn, &koutput->modes[i],
                                           Mode);
            Modes = xf86ModesAdd(Modes, Mode);
        }
    }

    return Modes;
}

static void
EXYNOSOutputDestory(xf86OutputPtr pOutput)
{
    EXYNOSOutputPrivPtr pOutputPriv = pOutput->driver_private;

    if (pOutputPriv == NULL) {
        return;
    }
    EXYNOSPtr pExynos = EXYNOSPTR(pOutput->scrn);
    int i;

    if (pExynos->resume_timer) {
        TimerFree(pExynos->resume_timer);
        pExynos->resume_timer = NULL;
    }
    pExynos->set_mode = DISPLAY_SET_MODE_OFF;

    for (i = 0; i < pOutputPriv->num_props; i++) {
        drmModeFreeProperty(pOutputPriv->props[i].mode_prop);
        free(pOutputPriv->props[i].atoms);
    }
    free(pOutputPriv->props);

    drmModeFreeEncoder(pOutputPriv->mode_encoder);
    drmModeFreeConnector(pOutputPriv->mode_output);
    xorg_list_del(&pOutputPriv->link);
    free(pOutputPriv);

    pOutput->driver_private = NULL;
}

static void
EXYNOSOutputDpms(xf86OutputPtr pOutput, int dpms)
{
    EXYNOSOutputPrivPtr pOutputPriv = pOutput->driver_private;

    if (pOutputPriv == NULL)
        return;
    drmModeConnectorPtr koutput = pOutputPriv->mode_output;
    EXYNOSModePtr pExynosMode = pOutputPriv->pExynosMode;
    EXYNOSPtr pExynos = EXYNOSPTR(pOutput->scrn);
    int old_dpms = pOutputPriv->dpms_mode;
    int i;

    if (!strcmp(pOutput->name, "Virtual1") ||
        !strcmp(pOutput->name, "HDMI1") || !strcmp(pOutput->name, "DUMMY1"))
        return;

    if (dpms == DPMSModeSuspend)
        return;

    for (i = 0; i < koutput->count_props; i++) {
        drmModePropertyPtr props;

        props = drmModeGetProperty(pExynosMode->fd, koutput->props[i]);
        if (!props)
            continue;

        if ((old_dpms == DPMSModeStandby && dpms == DPMSModeOn) ||
            (old_dpms == DPMSModeOn && dpms == DPMSModeStandby)) {
            if (!strcmp(props->name, "panel")) {
                int value = (dpms == DPMSModeStandby) ? 1 : 0;

                drmModeConnectorSetProperty(pExynosMode->fd,
                                            pOutputPriv->output_id,
                                            props->prop_id, value);
                pOutputPriv->dpms_mode = dpms;
                drmModeFreeProperty(props);
                XDBG_INFO(MDPMS, "panel '%s'\n", (value) ? "OFF" : "ON");
                return;
            }
        }
        else if (!strcmp(props->name, "DPMS")) {
            int _tmp_dpms = dpms;

            switch (dpms) {
            case DPMSModeStandby:
            case DPMSModeOn:
                if (pOutputPriv->isLcdOff == FALSE) {
                    drmModeFreeProperty(props);
                    return;
                }
                /* lcd on */
                XDBG_INFO(MDPMS, "\t Reqeust DPMS ON (%s)\n", pOutput->name);
                _tmp_dpms = DPMSModeOn;
                pOutputPriv->isLcdOff = FALSE;

                if (!strcmp(pOutput->name, "LVDS1")) {
                    pExynos->isLcdOff = FALSE;
#if 0
                    /* if wb need to be started, start wb after timeout. */
                    if (pExynos->set_mode == DISPLAY_SET_MODE_CLONE) {
                        pExynos->resume_timer = TimerSet(pExynos->resume_timer,
                                                         0, 30,
                                                         _exynosOutputResumeWbTimeout,
                                                         pOutput);
                    }

                    exynosVideoDpms(pOutput->scrn, TRUE);
                    exynosVirtualVideoDpms(pOutput->scrn, TRUE);
#endif
                }

                /* accessibility */
                EXYNOSCrtcPrivPtr pCrtcPriv = pOutput->crtc->driver_private;

                if (pCrtcPriv->screen_rotate_degree > 0)
                    exynosCrtcEnableScreenRotate(pOutput->crtc, TRUE);
                else
                    exynosCrtcEnableScreenRotate(pOutput->crtc, FALSE);
                if (pCrtcPriv->bAccessibility ||
                    pCrtcPriv->screen_rotate_degree > 0) {
                    tbm_bo src_bo = pCrtcPriv->front_bo;
                    tbm_bo dst_bo = pCrtcPriv->accessibility_back_bo;

                    if (!exynosCrtcExecAccessibility
                        (pOutput->crtc, src_bo, dst_bo)) {
                        XDBG_ERROR(MDPMS,
                                   "Fail execute accessibility(output name, %s)\n",
                                   pOutput->name);
                    }
                }
#if 0
                /* set current fb to crtc */
                if (!exynosCrtcApply(pOutput->crtc)) {
                    XDBG_ERROR(MDPMS, "Fail crtc apply(output name, %s)\n",
                               pOutput->name);
                }
#endif
                if (!strcmp(pOutput->name, "LVDS1")) {
#ifdef HAVE_HWC_H
                    if (pExynos->use_hwc && pExynos->hwc_active)
                        exynosHwcUpdate(pOutput->scrn);
#endif
                }

                if (pExynos->set_mode == DISPLAY_SET_MODE_MIRROR) {
                    /* start wb if wb is working. */
                    EXYNOSCloneVideoSetDPMS(pOutput->scrn, TRUE);
                }

                break;
            case DPMSModeOff:
                if (pOutputPriv->isLcdOff == TRUE) {
                    drmModeFreeProperty(props);
                    return;
                }
                /* lcd off */
                XDBG_INFO(MDPMS, "\t Reqeust DPMS OFF (%s)\n", pOutput->name);
                _tmp_dpms = DPMSModeOff;
                pOutputPriv->isLcdOff = TRUE;
                exynosCrtcEnableScreenRotate(pOutput->crtc, FALSE);

                if (!strcmp(pOutput->name, "LVDS1")) {
                    pExynos->isLcdOff = TRUE;
#if 0
                    exynosVideoDpms(pOutput->scrn, FALSE);
//#ifdef HAVE_HWC_H
//                    if (pExynos->use_hwc)
//                        EXYNOSHwcSetDrawables (pOutput->scrn->pScreen, NULL, NULL, NULL, 0);
//#endif
                    exynosVirtualVideoDpms(pOutput->scrn, FALSE);

                    if (pExynos->resume_timer) {
                        TimerFree(pExynos->resume_timer);
                        drmModeFreeProperty(props);
                        pExynos->resume_timer = NULL;
                        return;
                    }

                    /* keep previous pExynosMode's set_mode. */
                    pExynos->set_mode =
                        exynosDisplayGetDispSetMode(pOutput->scrn);

                    if (pExynos->set_mode == DISPLAY_SET_MODE_CLONE ||
                        pExynos->set_mode == DISPLAY_SET_MODE_EXT) {
                        /* stop wb if wb is working. */
                        exynosDisplaySetDispSetMode(pOutput->scrn,
                                                    DISPLAY_SET_MODE_OFF);
                    }
#endif
                    if (pExynos->set_mode == DISPLAY_SET_MODE_MIRROR) {
                        /* stop wb if wb is working. */
                        EXYNOSCloneVideoSetDPMS(pOutput->scrn, FALSE);
                    }
                }

                break;
            default:
                drmModeFreeProperty(props);
                return;
            }

            drmModeConnectorSetProperty(pExynosMode->fd,
                                        pOutputPriv->output_id,
                                        props->prop_id, _tmp_dpms);

            XDBG_INFO(MDPMS, "\t Success DPMS request (%s)\n", pOutput->name);

            pOutputPriv->dpms_mode = _tmp_dpms;
            drmModeFreeProperty(props);
            return;
        }

        drmModeFreeProperty(props);
    }
}

static void
EXYNOSOutputCreateReaources(xf86OutputPtr pOutput)
{
    EXYNOSOutputPrivPtr pOutputPriv = pOutput->driver_private;

    if (pOutputPriv == NULL)
        return;
    drmModeConnectorPtr mode_output = pOutputPriv->mode_output;
    EXYNOSModePtr pExynosMode = pOutputPriv->pExynosMode;
    int i, j, err;

    pOutputPriv->props =
        calloc(mode_output->count_props, sizeof(EXYNOSPropertyRec));
    if (!pOutputPriv->props)
        return;

    pOutputPriv->num_props = 0;
    for (i = j = 0; i < mode_output->count_props; i++) {
        drmModePropertyPtr drmmode_prop;

        drmmode_prop =
            drmModeGetProperty(pExynosMode->fd, mode_output->props[i]);
        if (_exynosOutputPropertyIgnore(drmmode_prop)) {
            drmModeFreeProperty(drmmode_prop);
            continue;
        }

        pOutputPriv->props[j].mode_prop = drmmode_prop;
        pOutputPriv->props[j].value = mode_output->prop_values[i];
        j++;
    }
    pOutputPriv->num_props = j;

    for (i = 0; i < pOutputPriv->num_props; i++) {
        EXYNOSPropertyPtr p = &pOutputPriv->props[i];
        drmModePropertyPtr drmmode_prop = p->mode_prop;

        if (drmmode_prop->flags & DRM_MODE_PROP_RANGE) {
            INT32 range[2];

            p->num_atoms = 1;
            p->atoms = calloc(p->num_atoms, sizeof(Atom));
            if (!p->atoms)
                continue;

            p->atoms[0] =
                MakeAtom(drmmode_prop->name, strlen(drmmode_prop->name), TRUE);
            range[0] = drmmode_prop->values[0];
            range[1] = drmmode_prop->values[1];
            err = RRConfigureOutputProperty(pOutput->randr_output, p->atoms[0],
                                            FALSE, TRUE,
                                            drmmode_prop->flags &
                                            DRM_MODE_PROP_IMMUTABLE ? TRUE :
                                            FALSE, 2, range);
            if (err != 0) {
                xf86DrvMsg(pOutput->scrn->scrnIndex, X_ERROR,
                           "RRConfigureOutputProperty error, %d\n", err);
            }
            err = RRChangeOutputProperty(pOutput->randr_output, p->atoms[0],
                                         XA_INTEGER, 32, PropModeReplace, 1,
                                         &p->value, FALSE, TRUE);
            if (err != 0) {
                xf86DrvMsg(pOutput->scrn->scrnIndex, X_ERROR,
                           "RRChangeOutputProperty error, %d\n", err);
            }
        }
        else if (drmmode_prop->flags & DRM_MODE_PROP_ENUM) {
            p->num_atoms = drmmode_prop->count_enums + 1;
            p->atoms = calloc(p->num_atoms, sizeof(Atom));
            if (!p->atoms)
                continue;

            p->atoms[0] =
                MakeAtom(drmmode_prop->name, strlen(drmmode_prop->name), TRUE);
            for (j = 1; j <= drmmode_prop->count_enums; j++) {
                struct drm_mode_property_enum *e = &drmmode_prop->enums[j - 1];

                p->atoms[j] = MakeAtom(e->name, strlen(e->name), TRUE);
            }

            err = RRConfigureOutputProperty(pOutput->randr_output, p->atoms[0],
                                            FALSE, FALSE,
                                            drmmode_prop->flags &
                                            DRM_MODE_PROP_IMMUTABLE ? TRUE :
                                            FALSE, p->num_atoms - 1,
                                            (INT32 *) &p->atoms[1]);
            if (err != 0) {
                xf86DrvMsg(pOutput->scrn->scrnIndex, X_ERROR,
                           "RRConfigureOutputProperty error, %d\n", err);
            }

            for (j = 0; j < drmmode_prop->count_enums; j++)
                if (drmmode_prop->enums[j].value == p->value)
                    break;
            /* there's always a matching value */
            err = RRChangeOutputProperty(pOutput->randr_output, p->atoms[0],
                                         XA_ATOM, 32, PropModeReplace, 1,
                                         &p->atoms[j + 1], FALSE, TRUE);
            if (err != 0) {
                xf86DrvMsg(pOutput->scrn->scrnIndex, X_ERROR,
                           "RRChangeOutputProperty error, %d\n", err);
            }
        }

        if (p->atoms)
            free(p->atoms);
    }
}

static Bool
EXYNOSOutputSetProperty(xf86OutputPtr output, Atom property,
                        RRPropertyValuePtr value)
{
    EXYNOSOutputPrivPtr pOutputPriv = output->driver_private;

    if (pOutputPriv == NULL)
        return TRUE;
    EXYNOSModePtr pExynosMode = pOutputPriv->pExynosMode;
    int i;

    //EXYNOSOutputDpms(output, DPMSModeStandby);

    for (i = 0; i < pOutputPriv->num_props; i++) {
        EXYNOSPropertyPtr p = &pOutputPriv->props[i];

        if (p->atoms[0] != property)
            continue;

        if (p->mode_prop->flags & DRM_MODE_PROP_RANGE) {
            uint32_t val;

            if (value->type != XA_INTEGER || value->format != 32 ||
                value->size != 1)
                return FALSE;
            val = *(uint32_t *) value->data;

            drmModeConnectorSetProperty(pExynosMode->fd, pOutputPriv->output_id,
                                        p->mode_prop->prop_id, (uint64_t) val);
            return TRUE;
        }
        else if (p->mode_prop->flags & DRM_MODE_PROP_ENUM) {
            Atom atom;
            const char *name;
            int j;

            if (value->type != XA_ATOM || value->format != 32 ||
                value->size != 1)
                return FALSE;
            memcpy(&atom, value->data, 4);
            name = NameForAtom(atom);
            XDBG_RETURN_VAL_IF_FAIL((name != NULL), FALSE);

            /* search for matching name string, then set its value down */
            for (j = 0; j < p->mode_prop->count_enums; j++) {
                if (!strcmp(p->mode_prop->enums[j].name, name)) {
                    drmModeConnectorSetProperty(pExynosMode->fd,
                                                pOutputPriv->output_id,
                                                p->mode_prop->prop_id,
                                                p->mode_prop->enums[j].value);
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
    /* set the hidden properties : features for exynos debugging */
    /* TODO : xberc can works on only LVDS????? */
#ifdef NO_CRTC_MODE
    if ((pOutputPriv->mode_output->connector_type == DRM_MODE_CONNECTOR_HDMIA)
        || (pOutputPriv->mode_output->connector_type ==
            DRM_MODE_CONNECTOR_VIRTUAL) ||
        (pOutputPriv->mode_output->connector_type == DRM_MODE_CONNECTOR_LVDS))
#else
    if (pOutputPriv->mode_output->connector_type == DRM_MODE_CONNECTOR_LVDS)
#endif
    {
        if (exynosPropSetLvdsFunc(output, property, value))
            return TRUE;

        if (exynosPropSetFbVisible(output, property, value))
            return TRUE;

        if (exynosPropSetVideoOffset(output, property, value))
            return TRUE;

        if (exynosPropSetScreenRotate(output, property, value))
            return TRUE;

        /* set the property for the display mode */
        if (exynosPropSetDisplayMode(output, property, value))
            return TRUE;

        if (exynosXbercSetProperty(output, property, value))
            return TRUE;
    }

    return TRUE;
}

static Bool
EXYNOSOutputGetProperty(xf86OutputPtr pOutput, Atom property)
{
    return FALSE;
}

static const xf86OutputFuncsRec exynos_output_funcs = {
    .create_resources = EXYNOSOutputCreateReaources,
#ifdef RANDR_12_INTERFACE
    .set_property = EXYNOSOutputSetProperty,
    .get_property = EXYNOSOutputGetProperty,
#endif
    .dpms = EXYNOSOutputDpms,
#if 0
    .save = drmmode_crt_save,
    .restore = drmmode_crt_restore,
    .mode_fixup = drmmode_crt_mode_fixup,
    .prepare = exynos_output_prepare,
    .mode_set = drmmode_crt_mode_set,
    .commit = exynos_output_commit,
#endif
    .detect = EXYNOSOutputDetect,
    .mode_valid = EXYNOSOutputModeValid,

    .get_modes = EXYNOSOutputGetModes,
    .destroy = EXYNOSOutputDestory
};

Bool
exynosOutputDrmUpdate(ScrnInfoPtr pScrn)
{
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    EXYNOSModePtr pExynosMode = pExynos->pExynosMode;
    Bool ret = TRUE;
    int i;

    for (i = 0; i < pExynosMode->mode_res->count_connectors; i++) {
#ifdef NO_CRTC_MODE
        ret = TRUE;
#endif
        EXYNOSOutputPrivPtr pCur = NULL, pNext = NULL, pOutputPriv = NULL;
        drmModeConnectorPtr koutput;
        drmModeEncoderPtr kencoder;
        char *conn_str[] = { "connected", "disconnected", "unknow" };

        xorg_list_for_each_entry_safe(pCur, pNext, &pExynosMode->outputs, link) {
            if (pCur->output_id == pExynosMode->mode_res->connectors[i]) {
                pOutputPriv = pCur;
                break;
            }
        }

        if (!pOutputPriv) {
#ifdef NO_CRTC_MODE
            continue;
#else
            ret = FALSE;
            break;
#endif
        }
#ifdef NO_CRTC_MODE
        if (pOutputPriv->is_dummy == TRUE) {
            continue;
        }
        pOutputPriv->pOutput->crtc = NULL;
#endif
        koutput = drmModeGetConnector(pExynosMode->fd,
                                      pExynosMode->mode_res->connectors[i]);
        if (!koutput) {
#ifdef NO_CRTC_MODE
            continue;
#else
            ret = FALSE;
            break;
#endif
        }

        kencoder = drmModeGetEncoder(pExynosMode->fd, koutput->encoders[0]);
        if (!kencoder) {
            drmModeFreeConnector(koutput);
#ifdef NO_CRTC_MODE
            continue;
#else
            ret = FALSE;
            break;
#endif
        }

        if (pOutputPriv->mode_output) {
            drmModeFreeConnector(pOutputPriv->mode_output);
            pOutputPriv->mode_output = NULL;
        }
        pOutputPriv->mode_output = koutput;

        if (pOutputPriv->mode_encoder) {
            drmModeFreeEncoder(pOutputPriv->mode_encoder);
            pOutputPriv->mode_encoder = NULL;
        }
        pOutputPriv->mode_encoder = kencoder;
#ifdef NO_CRTC_MODE
        EXYNOSCrtcPrivPtr crtc_ref = NULL, crtc_next = NULL;

        xorg_list_for_each_entry_safe(crtc_ref, crtc_next, &pExynosMode->crtcs,
                                      link) {
            if (pOutputPriv->mode_encoder->crtc_id ==
                crtc_ref->mode_crtc->crtc_id) {
                if (pOutputPriv->mode_output->connection == DRM_MODE_CONNECTED)
                    pOutputPriv->pOutput->crtc = crtc_ref->pCrtc;
                else {
                    exynosOutputDpmsSet(pOutputPriv->pOutput, DPMSModeOff);
                    drmModeSetCrtc(pExynosMode->fd,
                                   pOutputPriv->mode_encoder->crtc_id, 0, 0, 0,
                                   NULL, 0, NULL);
                    pOutputPriv->pOutput->crtc = NULL;
                }
            }
        }
#endif
        XDBG_INFO(MSEC,
                  "drm update : connect(%d, type:%d, status:%s) encoder(%d) crtc(%d).\n",
                  pExynosMode->mode_res->connectors[i], koutput->connector_type,
                  conn_str[pOutputPriv->mode_output->connection - 1],
                  kencoder->encoder_id, kencoder->crtc_id);
#ifdef NO_CRTC_MODE
        /* Does these need to update? */
        pOutputPriv->pOutput->mm_width = koutput->mmWidth;
        pOutputPriv->pOutput->mm_height = koutput->mmHeight;
        pOutputPriv->pOutput->possible_crtcs = kencoder->possible_crtcs;
        pOutputPriv->pOutput->possible_clones = kencoder->possible_clones;
#endif
    }
#ifdef NO_CRTC_MODE
    EXYNOSCrtcPrivPtr crtc_ref = NULL, crtc_next = NULL;

    xorg_list_for_each_entry_safe(crtc_ref, crtc_next, &pExynosMode->crtcs,
                                  link) {
        crtc_ref->pCrtc->enabled = xf86CrtcInUse(crtc_ref->pCrtc);
    }
    xf86DisableUnusedFunctions(pScrn);
    pExynos->isCrtcOn = exynosCrtcCheckInUseAll(pScrn);
#else
    if (!ret)
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "drm(output) update error. (%s)\n", strerror(errno));
#endif
    return ret;
}

#if 0
Bool
exynosOutputDummyInit(ScrnInfoPtr pScrn, EXYNOSModePtr pExynosMode, Bool late)
{
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    xf86OutputPtr pOutput;
    xf86CrtcPtr pCrtc;
    RROutputPtr clones[32];
    RRCrtcPtr crtcs[32];
    char buf[80];
    int i, len;

    if (pExynosMode->num_dummy_output >= 32)
        return FALSE;

    XDBG_DEBUG(MDISP, "(late=%d, num_dummy=%d)\n", late,
               pExynosMode->num_dummy_output + 1);

    len = sprintf(buf, "DUMMY%d", pExynosMode->num_dummy_output + 1);
    pOutput = xf86OutputCreate(pScrn, &exynos_output_funcs, buf);
    if (!pOutput) {
        return FALSE;
    }

    pCrtc = exynosCrtcDummyInit(pScrn);

    if (pCrtc == NULL) {
        xf86OutputDestroy(pOutput);
        return FALSE;
    }

    pOutput->mm_width = 0;
    pOutput->mm_height = 0;
    pOutput->interlaceAllowed = FALSE;
    pOutput->subpixel_order = SubPixelNone;

    pOutput->possible_crtcs = ~((1 << pExynosMode->num_real_crtc) - 1);
    pOutput->possible_clones = ~((1 << pExynosMode->num_real_output) - 1);

    if (late) {
        ScreenPtr pScreen = xf86ScrnToScreen(pScrn);

        pCrtc->randr_crtc = RRCrtcCreate(pScreen, pCrtc);
        pOutput->randr_output = RROutputCreate(pScreen, buf, len, pOutput);
        if (pCrtc->randr_crtc == NULL || pOutput->randr_output == NULL) {
            xf86OutputDestroy(pOutput);
            xf86CrtcDestroy(pCrtc);
            return FALSE;
        }

        RRPostPendingProperties(pOutput->randr_output);

        for (i = pExynosMode->num_real_output; i < xf86_config->num_output; i++)
            clones[i - pExynosMode->num_real_output] =
                xf86_config->output[i]->randr_output;
        XDBG_RETURN_VAL_IF_FAIL(i - pExynosMode->num_real_output ==
                                pExynosMode->num_dummy_output + 1, FALSE);

        for (i = pExynosMode->num_real_crtc; i < xf86_config->num_crtc; i++)
            crtcs[i - pExynosMode->num_real_crtc] =
                xf86_config->crtc[i]->randr_crtc;
        XDBG_RETURN_VAL_IF_FAIL(i - pExynosMode->num_real_crtc ==
                                pExynosMode->num_dummy_output + 1, FALSE);

        for (i = pExynosMode->num_real_output; i < xf86_config->num_output; i++) {
            RROutputPtr rr_output = xf86_config->output[i]->randr_output;

            if (!RROutputSetCrtcs
                (rr_output, crtcs, pExynosMode->num_dummy_output + 1) ||
                !RROutputSetClones(rr_output, clones,
                                   pExynosMode->num_dummy_output + 1))
                goto err;
        }

        RRCrtcSetRotations(pCrtc->randr_crtc, RR_Rotate_All | RR_Reflect_All);
    }

    pExynosMode->num_dummy_output++;
    return TRUE;

 err:
    for (i = 0; i < xf86_config->num_output; i++) {
        pOutput = xf86_config->output[i];
        if (pOutput->driver_private)
            continue;

        xf86OutputDestroy(pOutput);
    }

    for (i = 0; i < xf86_config->num_crtc; i++) {
        pCrtc = xf86_config->crtc[i];
        if (pCrtc->driver_private)
            continue;
        xf86CrtcDestroy(pCrtc);
    }
    pExynosMode->num_dummy_output = -1;
    return FALSE;
}
#endif                          //NO_CRTC_MODE
void
exynosOutputInit(ScrnInfoPtr pScrn, EXYNOSModePtr pExynosMode, int num)
{
    xf86OutputPtr pOutput;
    drmModeConnectorPtr koutput;
    drmModeEncoderPtr kencoder;
    EXYNOSOutputPrivPtr pOutputPriv;
    const char *output_name;
    char name[32];

    koutput = drmModeGetConnector(pExynosMode->fd,
                                  pExynosMode->mode_res->connectors[num]);
    if (!koutput)
        return;

    kencoder = drmModeGetEncoder(pExynosMode->fd, koutput->encoders[0]);
    if (!kencoder) {
        drmModeFreeConnector(koutput);
        return;
    }

    if (koutput->connector_type < ARRAY_SIZE(output_names))
        output_name = output_names[koutput->connector_type];
    else
        output_name = "UNKNOWN";
    snprintf(name, 32, "%s%d", output_name, koutput->connector_type_id);

    pOutput = xf86OutputCreate(pScrn, &exynos_output_funcs, name);
    if (!pOutput) {
        drmModeFreeEncoder(kencoder);
        drmModeFreeConnector(koutput);
        return;
    }

    pOutputPriv = calloc(sizeof(EXYNOSOutputPrivRec), 1);
    if (!pOutputPriv) {
        xf86OutputDestroy(pOutput);
        drmModeFreeConnector(koutput);
        drmModeFreeEncoder(kencoder);
        return;
    }

    pOutputPriv->output_id = pExynosMode->mode_res->connectors[num];
    pOutputPriv->mode_output = koutput;
    pOutputPriv->mode_encoder = kencoder;
    pOutputPriv->pExynosMode = pExynosMode;

    pOutput->mm_width = koutput->mmWidth;
    pOutput->mm_height = koutput->mmHeight;

    pOutput->subpixel_order = subpixel_conv_table[koutput->subpixel];
    pOutput->driver_private = pOutputPriv;

    pOutput->possible_crtcs = kencoder->possible_crtcs;
    pOutput->possible_clones = kencoder->possible_clones;
    pOutput->interlaceAllowed = TRUE;

    pOutputPriv->pOutput = pOutput;
    /* TODO : soolim : management crtc privates */
    xorg_list_add(&pOutputPriv->link, &pExynosMode->outputs);
#ifdef NO_CRTC_MODE
    ++(pExynosMode->num_real_output);
#endif
}

int
exynosOutputDpmsStatus(xf86OutputPtr pOutput)
{
    EXYNOSOutputPrivPtr pOutputPriv = pOutput->driver_private;

    if (pOutputPriv == NULL)
        return 0;
    return pOutputPriv->dpms_mode;
}

void
exynosOutputDpmsSet(xf86OutputPtr pOutput, int mode)
{
    EXYNOSOutputDpms(pOutput, mode);
}

EXYNOSOutputPrivPtr
exynosOutputGetPrivateForConnType(ScrnInfoPtr pScrn, int connect_type)
{
    EXYNOSModePtr pExynosMode = (EXYNOSModePtr) EXYNOSPTR(pScrn)->pExynosMode;
    int i;

    for (i = 0; i < pExynosMode->mode_res->count_connectors; i++) {
        EXYNOSOutputPrivPtr pCur = NULL, pNext = NULL;

        xorg_list_for_each_entry_safe(pCur, pNext, &pExynosMode->outputs, link) {
            drmModeConnectorPtr koutput = pCur->mode_output;

            if (koutput && koutput->connector_type == connect_type)
                return pCur;
        }
    }
#ifndef NO_CRTC_MODE
    XDBG_ERROR(MSEC, "no output for connect_type(%d) \n", connect_type);
#endif
    return NULL;
}
