
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>

#include "libdrm/drm.h"
#include "xf86drm.h"
#include "xf86drmMode.h"
#include <exynos/exynos_drm.h>
#include "sec_display.h"
#include "sec_util.h"

typedef struct _DRMModeTest
{
    int tc_num;
    int drm_fd;

    drmModeRes *resources;
    drmModePlaneRes *plane_resources;
    drmModeEncoder *encoders[3];
    drmModeConnector *connectors[3];
    drmModeCrtc *crtcs[3];
    drmModeFB *fbs[10];
    drmModePlane *planes[10];

} DRMModeTest;

struct type_name
{
    int type;
    char *name;
};

#define dump_resource(res, reply, len) if (res) reply = dump_##res(reply, len)

static DRMModeTest test;

struct type_name encoder_type_names[] =
{
    { DRM_MODE_ENCODER_NONE, "none" },
    { DRM_MODE_ENCODER_DAC, "DAC" },
    { DRM_MODE_ENCODER_TMDS, "TMDS" },
    { DRM_MODE_ENCODER_LVDS, "LVDS" },
    { DRM_MODE_ENCODER_TVDAC, "TVDAC" },
};

struct type_name connector_status_names[] =
{
    { DRM_MODE_CONNECTED, "connected" },
    { DRM_MODE_DISCONNECTED, "disconnected" },
    { DRM_MODE_UNKNOWNCONNECTION, "unknown" },
};

struct type_name connector_type_names[] =
{
    { DRM_MODE_CONNECTOR_Unknown, "unknown" },
    { DRM_MODE_CONNECTOR_VGA, "VGA" },
    { DRM_MODE_CONNECTOR_DVII, "DVI-I" },
    { DRM_MODE_CONNECTOR_DVID, "DVI-D" },
    { DRM_MODE_CONNECTOR_DVIA, "DVI-A" },
    { DRM_MODE_CONNECTOR_Composite, "composite" },
    { DRM_MODE_CONNECTOR_SVIDEO, "s-video" },
    { DRM_MODE_CONNECTOR_LVDS, "LVDS" },
    { DRM_MODE_CONNECTOR_Component, "component" },
    { DRM_MODE_CONNECTOR_9PinDIN, "9-pin DIN" },
    { DRM_MODE_CONNECTOR_DisplayPort, "displayport" },
    { DRM_MODE_CONNECTOR_HDMIA, "HDMI-A" },
    { DRM_MODE_CONNECTOR_HDMIB, "HDMI-B" },
    { DRM_MODE_CONNECTOR_TV, "TV" },
    { DRM_MODE_CONNECTOR_eDP, "embedded displayport" },
};

extern char* secPlaneDump (char *reply, int *len);
extern char* secUtilDumpVideoBuffer (char *reply, int *len);

static char * encoder_type_str (int type)
{
    int i;
    for (i = 0; i < ARRAY_SIZE(encoder_type_names); i++)
    {
        if (encoder_type_names[i].type == type)
            return encoder_type_names[i].name;
    }
    return "(invalid)";
}


static char * connector_status_str (int type)
{
    int i;
    for (i = 0; i < ARRAY_SIZE(connector_status_names); i++)
    {
        if (connector_status_names[i].type == type)
            return connector_status_names[i].name;
    }
    return "(invalid)";
}

static char * connector_type_str (int type)
{
    int i;
    for (i = 0; i < ARRAY_SIZE(connector_type_names); i++)
    {
        if (connector_type_names[i].type == type)
            return connector_type_names[i].name;
    }
    return "(invalid)";
}

static char* dump_encoders(char *reply, int *len)
{
    drmModeEncoder *encoder;
    drmModeRes *resources = test.resources;
    int i;

    XDBG_REPLY ("Encoders:\n");
    XDBG_REPLY ("id\tcrtc\ttype\tpossible crtcs\tpossible clones\t\n");
    for (i = 0; i < resources->count_encoders; i++)
    {
        encoder = test.encoders[i];;

        if (!encoder)
        {
            XDBG_REPLY ("could not get encoder %i\n", i);
            continue;
        }
        XDBG_REPLY ("%d\t%d\t%s\t0x%08x\t0x%08x\n",
                    encoder->encoder_id,
                    encoder->crtc_id,
                    encoder_type_str(encoder->encoder_type),
                    encoder->possible_crtcs,
                    encoder->possible_clones);
    }

    XDBG_REPLY ("\n");

    return reply;
}

static char* dump_mode(drmModeModeInfo *mode, char *reply, int *len)
{
    XDBG_REPLY ("  %s %d %d %d %d %d %d %d %d %d\n",
                mode->name,
                mode->vrefresh,
                mode->hdisplay,
                mode->hsync_start,
                mode->hsync_end,
                mode->htotal,
                mode->vdisplay,
                mode->vsync_start,
                mode->vsync_end,
                mode->vtotal);

    return reply;
}

static char*
dump_props(drmModeConnector *connector, char *reply, int *len)
{
    drmModePropertyPtr props;
    int i;

    for (i = 0; i < connector->count_props; i++)
    {
        props = drmModeGetProperty(test.drm_fd, connector->props[i]);
        if (props == NULL)
            continue;
        XDBG_REPLY ("\t%s, flags %d\n", props->name, props->flags);
        drmModeFreeProperty(props);
    }

    return reply;
}

static char* dump_connectors(char *reply, int *len)
{
    drmModeConnector *connector;
    drmModeRes *resources = test.resources;
    int i, j;

    XDBG_REPLY ("Connectors:\n");
    XDBG_REPLY ("id\tencoder\tstatus\t\ttype\tsize (mm)\tmodes\tencoders\n");
    for (i = 0; i < resources->count_connectors; i++)
    {
        connector = test.connectors[i];

        if (!connector)
        {
            XDBG_REPLY ("could not get connector %i\n", i);
            continue;
        }

        XDBG_REPLY ("%d\t%d\t%s\t%s\t%dx%d\t\t%d\t",
                    connector->connector_id,
                    connector->encoder_id,
                    connector_status_str(connector->connection),
                    connector_type_str(connector->connector_type),
                    connector->mmWidth, connector->mmHeight,
                    connector->count_modes);

        for (j = 0; j < connector->count_encoders; j++)
            XDBG_REPLY ("%s%d", j > 0 ? ", " : "", connector->encoders[j]);
        XDBG_REPLY ("\n");

        if (!connector->count_modes)
            continue;

        XDBG_REPLY ("  modes:\n");
        XDBG_REPLY ("  name refresh (Hz) hdisp hss hse htot vdisp "
                    "vss vse vtot)\n");
        for (j = 0; j < connector->count_modes; j++)
            reply = dump_mode(&connector->modes[j], reply, len);

        XDBG_REPLY ("  props:\n");
        reply = dump_props(connector, reply, len);
    }
    XDBG_REPLY ("\n");

    return reply;
}

static char* dump_crtcs(char *reply, int *len)
{
    drmModeCrtc *crtc;
    drmModeRes *resources = test.resources;
    int i;

    XDBG_REPLY ("CRTCs:\n");
    XDBG_REPLY ("id\tfb\tpos\tsize\n");
    for (i = 0; i < resources->count_crtcs; i++)
    {
        crtc = test.crtcs[i];

        if (!crtc)
        {
            XDBG_REPLY ("could not get crtc %i\n", i);
            continue;
        }
        XDBG_REPLY ("%d\t%d\t(%d,%d)\t(%dx%d)\n",
                    crtc->crtc_id,
                    crtc->buffer_id,
                    crtc->x, crtc->y,
                    crtc->width, crtc->height);
        reply = dump_mode(&crtc->mode, reply, len);
    }
    XDBG_REPLY ("\n");

    return reply;
}

static char* dump_framebuffers(char *reply, int *len)
{
    drmModeFB *fb;
    drmModeRes *resources = test.resources;
    int i;

    XDBG_REPLY ("Frame buffers:\n");
    XDBG_REPLY ("id\tsize\tpitch\n");
    for (i = 0; i < resources->count_fbs; i++)
    {
        fb = test.fbs[i];

        if (!fb)
        {
            XDBG_REPLY ("could not get fb %i\n", i);
            continue;
        }
        XDBG_REPLY ("%u\t(%ux%u)\t%u\n",
                    fb->fb_id,
                    fb->width, fb->height,
                    fb->pitch);
    }
    XDBG_REPLY ("\n");

    return reply;
}

static char* get_resources_all (char *reply, int *len)
{
    int i;

    /* get drm mode resources */
    test.resources = drmModeGetResources (test.drm_fd);
    if (!test.resources)
    {
        XDBG_REPLY ("drmModeGetResources failed: %s\n", strerror(errno));
        return reply;
    }

    /* get drm mode encoder */
    for (i = 0; i < test.resources->count_encoders; i++)
    {
        test.encoders[i] = drmModeGetEncoder (test.drm_fd, test.resources->encoders[i]);
        if (!test.encoders[i])
        {
            XDBG_REPLY ("fail to get encoder %i; %s\n",
                        test.resources->encoders[i], strerror(errno));
            continue;
        }
    }

    /* get drm mode connector */
    for (i = 0; i < test.resources->count_connectors; i++)
    {
        test.connectors[i] = drmModeGetConnector (test.drm_fd, test.resources->connectors[i]);
        if (!test.connectors[i])
        {
            XDBG_REPLY ("fail to get connector %i; %s\n",
                        test.resources->connectors[i], strerror(errno));
            continue;
        }
    }

    /* get drm mode crtc */
    for (i = 0; i < test.resources->count_crtcs; i++)
    {
        test.crtcs[i] = drmModeGetCrtc (test.drm_fd, test.resources->crtcs[i]);
        if (!test.crtcs[i])
        {
            XDBG_REPLY ("fail to get crtc %i; %s\n",
                        test.resources->crtcs[i], strerror(errno));
            continue;
        }
    }

    /* drm mode fb */
    for (i = 0; i < test.resources->count_fbs; i++)
    {
        test.fbs[i] = drmModeGetFB (test.drm_fd, test.resources->fbs[i]);
        if (!test.fbs[i])
        {
            XDBG_REPLY ("fail to get fb %i; %s\n",
                        test.resources->fbs[i], strerror(errno));
            continue;
        }
    }

    return reply;
}

static void free_resources_all ()
{
    int i;

    if (test.resources)
    {
        /* free drm mode fbs */
        for (i = 0; i < test.resources->count_fbs; i++)
            if (test.fbs[i])
            {
                drmModeFreeFB (test.fbs[i]);
                test.fbs[i] = NULL;
            }

        /* free drm mode crtcs */
        for (i = 0; i < test.resources->count_crtcs; i++)
            if (test.crtcs[i])
            {
                drmModeFreeCrtc (test.crtcs[i]);
                test.crtcs[i] = NULL;
            }

        /* free drm mode connectors */
        for (i = 0; i < test.resources->count_connectors; i++)
            if (test.connectors[i])
            {
                drmModeFreeConnector (test.connectors[i]);
                test.connectors[i] = NULL;
            }

        /* free drm mode encoders */
        for (i = 0; i < test.resources->count_encoders; i++)
            if (test.encoders[i])
            {
                drmModeFreeEncoder (test.encoders[i]);
                test.encoders[i] = NULL;
            }

        /* free drm mode resources */
        drmModeFreeResources (test.resources);
        test.resources = NULL;
    }
}

void sec_drmmode_dump (int drm_fd, char *reply, int *len)
{
    int encoders, connectors, crtcs, modes, framebuffers;

    encoders = connectors = crtcs = modes = framebuffers = 1;

    test.drm_fd = drm_fd;

    get_resources_all (reply, len);
    dump_resource(encoders, reply, len);
    dump_resource(connectors, reply, len);
    dump_resource(crtcs, reply, len);
    dump_resource(framebuffers, reply, len);

    reply = secPlaneDump (reply, len);
    reply = secUtilDumpVideoBuffer (reply, len);

    free_resources_all ();

}
