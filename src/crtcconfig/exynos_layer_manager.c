/**************************************************************************
xserver-xorg-video-exynos

Copyright 2015 Samsung Electronics co., Ltd. All Rights Reserved.

Contact: SooChan Lim <sc1.lim@samsung.com>
Contact: Andrii Sokolenko <a.sokolenko@samsung.com>

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
#ifdef LAYER_MANAGER
#include "exynos_layer_manager.h"
#include "exynos_util.h"
#include "exynos_plane.h"
#include "exynos_layer.h"
#include "exynos.h"
#include <string.h>

typedef struct {
    EXYNOSLayerMngClientID owner;
    EXYNOSLayerPtr hwlayer;
//    Bool        enabled;
//    EXYNOSLayerOutput output;
//    EXYNOSLayerPos lpos;
    struct xorg_list layer_link;
} exynosLayerMngHWLayerRec, *exynosLayerMngHWLayerPtr;

typedef struct _EXYNOSLayerMng {
    exynosLayerMngHWLayerPtr *array_p_layers;
    int count_of_layers;
    ScrnInfoPtr pScrn;
    struct xorg_list Events[EVENT_LAYER_MAX];
} SecLayerMngRec, *EXYNOSLayerMngPtr;

typedef struct _EXYNOSLayerClient {
    CARD32 stamp;
    int lyr_client_priority;
    char *lyr_client_name;
    EXYNOSLayerMngPtr pLYRM;
    struct xorg_list valid_link;
    /*........... */
} exynosLayerClientRec, *exynosLayerClientPtr;

typedef struct {
    exynosLayerClientPtr p_lyr_client;
    LYRMNotifyFunc callback_func;
    void *callback_user_data;
    struct xorg_list callback_link;
} exynosLayerManagerEventRec, *exynosLayerManagerEventPtr;

typedef struct _exynosLayerManagerEvent {
    const char *event_name;
    int expendable_callback;    /* Did run one time */
} exynosLayerManagerEventConfigRec;

typedef struct {
    EXYNOSLayerPos legacy_pos;
    int hwpos;
    int colorspace_type;        /* 1 - RGB, 2-YUV */
    exynosLayerMngHWLayerPtr layer;
} exynosLayerMngPlaneRec;

#define INVALID_HWPOS -10
#define LPOSCONV(v) (v+2)
#define PPOSCONV(v) (v-2)
static exynosLayerMngPlaneRec layer_table[LAYER_OUTPUT_MAX][PLANE_MAX] = {
    [LAYER_OUTPUT_LCD] = {
                          [PLANE_LOWER2] = {.legacy_pos = -2,.hwpos =
                                            1,.colorspace_type = 1, NULL},
                          [PLANE_LOWER1] = {.legacy_pos = -1,.hwpos =
                                            2,.colorspace_type = 1, NULL},
                          [PLANE_DEFAULT] = {.legacy_pos = 0,.hwpos =
                                             3,.colorspace_type = 1, NULL},
                          [PLANE_UPPER] = {.legacy_pos = 1,.hwpos =
                                           4,.colorspace_type = 1, NULL}
                          },
    [LAYER_OUTPUT_EXT] = {
                          [PLANE_LOWER2] = {.legacy_pos = -2,.hwpos =
                                            INVALID_HWPOS,.colorspace_type =
                                            0, NULL},
                          [PLANE_LOWER1] = {.legacy_pos = -1,.hwpos =
                                            2,.colorspace_type = 2, NULL},
                          [PLANE_DEFAULT] = {.legacy_pos = 0,.hwpos =
                                             0,.colorspace_type = 1, NULL},
                          [PLANE_UPPER] = {.legacy_pos = 1,.hwpos =
                                           1,.colorspace_type = 1, NULL}
                          }
};

static exynosLayerManagerEventConfigRec LyrMngEvents[EVENT_LAYER_MAX] = {
    [EVENT_LAYER_SHOW] = {"Showed", 0},
    [EVENT_LAYER_ANNEX] = {"Annexed", 0},
    [EVENT_LAYER_RELEASE] = {"Release", 0},
    [EVENT_LAYER_HIDE] = {"Hidden", 0},
    [EVENT_LAYER_BUFSET] = {"Set buffer", 1},
    [EVENT_LAYER_FREE_COUNTER] = {"Counter of free layer", 0}
};

static struct xorg_list client_valid_list;

static exynosLayerClientPtr
_findLayerClient(CARD32 stamp)
{
    exynosLayerClientPtr cur = NULL, next = NULL;

    if (stamp == 0 || stamp == LYR_ERROR_ID)
        return NULL;
    xorg_list_for_each_entry_safe(cur, next, &client_valid_list, valid_link) {
        if (stamp == cur->stamp)
            return cur;
    }
    XDBG_DEBUG(MLYRM, "Layer client id %6" PRIXID " is not valid\n", stamp);
    return NULL;
}

static exynosLayerClientPtr
_findLayerClientByName(const char *p_client_name)
{
    exynosLayerClientPtr cur = NULL, next = NULL;

    xorg_list_for_each_entry_safe(cur, next, &client_valid_list, valid_link) {
        if (!strcmp(p_client_name, cur->lyr_client_name)) {
            return cur;
        }
    }
    return NULL;
}

static inline Bool
_clientIsOwner(exynosLayerClientPtr p_lyr_client, EXYNOSLayerOutput output,
               EXYNOSLayerMngPlanePos plane_pos)
{
    Bool ret = FALSE;

    XDBG_RETURN_VAL_IF_FAIL(p_lyr_client != NULL, FALSE);
    exynosLayerMngHWLayerPtr check_layer = layer_table[output][plane_pos].layer;

    if (check_layer) {
        if (check_layer->owner == p_lyr_client->stamp) {
            ret = TRUE;
        }
    }

    XDBG_DEBUG(MLYRM, "Client %s %s own plane on output:%d, ppos:%d\n",
               p_lyr_client->lyr_client_name, ret ? "" : "not", output,
               plane_pos);
    return ret;
}

void
exynosLayerMngDelEvent(EXYNOSLayerMngClientID lyr_client_id,
                       EXYNOSLayerMngEventType event_type,
                       LYRMNotifyFunc callback_func)
{
    exynosLayerClientPtr p_lyr_client = _findLayerClient(lyr_client_id);

    XDBG_RETURN_IF_FAIL(p_lyr_client != NULL);
    XDBG_RETURN_IF_FAIL((event_type >= 0 && event_type < EVENT_LAYER_MAX));
    exynosLayerManagerEventPtr data = NULL, data_next = NULL;

    xorg_list_for_each_entry_safe(data, data_next,
                                  &p_lyr_client->pLYRM->Events[event_type],
                                  callback_link) {
        if (callback_func == NULL && data->p_lyr_client == p_lyr_client) {      /* Remove all callback */
            xorg_list_del(&data->callback_link);
            free(data);
            XDBG_DEBUG(MLYRM, "Successful remove layer client %s event %s\n",
                       p_lyr_client->lyr_client_name,
                       LyrMngEvents[event_type].event_name);
        }
        else if (data->callback_func == callback_func &&
                 data->p_lyr_client == p_lyr_client) {
            xorg_list_del(&data->callback_link);
            free(data);
            XDBG_DEBUG(MLYRM, "Successful remove layer client %s event %s\n",
                       p_lyr_client->lyr_client_name,
                       LyrMngEvents[event_type].event_name);
        }
    }
}

static int
_exynosLayerMngCountOfFreeLayer(EXYNOSLayerMngPtr pLYRM)
{
    XDBG_RETURN_VAL_IF_FAIL(pLYRM != NULL, -1);
    int i, ret = 0;

    for (i = 0; i < pLYRM->count_of_layers; i++) {
        if (pLYRM->array_p_layers[i]->owner == 0)
            ret++;
    }
    return ret;
}

static int
_exynosLayerMngCountOfAnnexLayer(EXYNOSLayerMngPtr pLYRM, int client_priority)
{
    XDBG_RETURN_VAL_IF_FAIL(pLYRM != NULL, -1);
    int i, ret = 0;

    for (i = 0; i < pLYRM->count_of_layers; i++) {
        if (pLYRM->array_p_layers[i]->owner != 0) {
            exynosLayerClientPtr p_lyr_client =
                _findLayerClient(pLYRM->array_p_layers[i]->owner);
            if (p_lyr_client &&
                p_lyr_client->lyr_client_priority < client_priority)
                ret++;
        }
    }
    return ret;
}

void
exynosLayerMngAddEvent(EXYNOSLayerMngClientID lyr_client_id,
                       EXYNOSLayerMngEventType event_type,
                       LYRMNotifyFunc callback_func,
                       void *callback_func_user_data)
{
    exynosLayerClientPtr p_lyr_client = _findLayerClient(lyr_client_id);

    XDBG_RETURN_IF_FAIL(p_lyr_client != NULL);
    XDBG_RETURN_IF_FAIL((event_type >= 0 && event_type < EVENT_LAYER_MAX));
    XDBG_RETURN_IF_FAIL(callback_func != NULL);
    exynosLayerManagerEventPtr data = NULL, data_next = NULL;

    xorg_list_for_each_entry_safe(data, data_next,
                                  &p_lyr_client->pLYRM->Events[event_type],
                                  callback_link) {
        if (data->callback_func == callback_func &&
            data->callback_user_data == callback_func_user_data &&
            data->p_lyr_client == p_lyr_client) {
            XDBG_WARNING(MLYRM,
                         "Client %s already had Callback on %s event type\n",
                         p_lyr_client->lyr_client_name,
                         LyrMngEvents[event_type].event_name);
            return;
        }
    }
    data = calloc(sizeof(exynosLayerManagerEventRec), 1);
    XDBG_RETURN_IF_FAIL(data != NULL);

    data->callback_func = callback_func;
    data->callback_user_data = callback_func_user_data;
    data->p_lyr_client = p_lyr_client;

    xorg_list_add(&data->callback_link,
                  &p_lyr_client->pLYRM->Events[event_type]);
    XDBG_DEBUG(MLYRM, "Lyr Client:%s registered callback on Event:%s\n",
               p_lyr_client->lyr_client_name,
               LyrMngEvents[event_type].event_name);
}

void
exynosLayerMngEventDispatch(ScrnInfoPtr pScrn,
                            EXYNOSLayerMngEventType event_type,
                            EXYNOSLayerMngEventCallbackDataPtr p_callback_data)
{
    XDBG_RETURN_IF_FAIL(pScrn != NULL);
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);

    XDBG_RETURN_IF_FAIL(pExynos != NULL);
    XDBG_RETURN_IF_FAIL(pExynos->pLYRM != NULL);
    EXYNOSLayerMngPtr pLYRM = (EXYNOSLayerMngPtr) pExynos->pLYRM;

    XDBG_RETURN_IF_FAIL((event_type >= 0 && event_type < EVENT_LAYER_MAX));
    XDBG_RETURN_IF_FAIL(p_callback_data != NULL);
    exynosLayerManagerEventPtr data = NULL, data_next = NULL;

    XDBG_DEBUG(MLYRM, "Dispatch Event:%s\n",
               LyrMngEvents[event_type].event_name);
    switch (event_type) {
    case EVENT_LAYER_ANNEX:
    xorg_list_for_each_entry_safe(data, data_next,
                                      &pLYRM->Events[event_type],
                                      callback_link) {
        if (data->callback_func) {
            /* Victim of annexation */
            if (p_callback_data->annex_callback.lyr_client_id
                == data->p_lyr_client->stamp) {
                XDBG_DEBUG(MLYRM, "Send Event:%s to client:%s\n",
                           LyrMngEvents[event_type].event_name,
                           data->p_lyr_client->lyr_client_name);
                data->callback_func(data->callback_user_data, p_callback_data);
                if (LyrMngEvents[event_type].expendable_callback) {
                    exynosLayerMngDelEvent(data->p_lyr_client->stamp,
                                           event_type, data->callback_func);
                }
            }
        }
    }
        break;
    case EVENT_LAYER_RELEASE:
    {
        xorg_list_for_each_entry_safe(data, data_next,
                                      &pLYRM->Events[event_type],
                                      callback_link) {
            if (data->callback_func) {
                /* Not need send event to initiator */
                if (p_callback_data->release_callback.lyr_client_id !=
                    data->p_lyr_client->stamp) {
                    XDBG_DEBUG(MLYRM, "Send Event:%s to client:%s\n",
                               LyrMngEvents[event_type].event_name,
                               data->p_lyr_client->lyr_client_name);
                    data->callback_func(data->callback_user_data,
                                        p_callback_data);
                    if (LyrMngEvents[event_type].expendable_callback) {
                        exynosLayerMngDelEvent(data->p_lyr_client->stamp,
                                               event_type, data->callback_func);
                    }
                }
            }
        }
        break;
    }
    default:
    xorg_list_for_each_entry_safe(data, data_next,
                                      &pLYRM->Events[event_type],
                                      callback_link) {
        if (data->callback_func) {
            XDBG_DEBUG(MLYRM, "Send Event:%s to client:%s\n",
                       LyrMngEvents[event_type].event_name,
                       data->p_lyr_client->lyr_client_name);
            data->callback_func(data->callback_user_data, p_callback_data);
            if (LyrMngEvents[event_type].expendable_callback) {
                exynosLayerMngDelEvent(data->p_lyr_client->stamp, event_type,
                                       data->callback_func);
            }
        }
    }
        break;
    }
}

EXYNOSLayerMngClientID
exynosLayerMngRegisterClient(ScrnInfoPtr pScrn, char *p_client_name,
                             int priority)
{
    XDBG_RETURN_VAL_IF_FAIL(pScrn, LYR_ERROR_ID);
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);

    XDBG_RETURN_VAL_IF_FAIL(pExynos != NULL, LYR_ERROR_ID);
    XDBG_RETURN_VAL_IF_FAIL(pExynos->pLYRM != NULL, LYR_ERROR_ID);
    XDBG_RETURN_VAL_IF_FAIL(p_client_name != NULL, LYR_ERROR_ID);
    EXYNOSLayerMngPtr pLYRM = (EXYNOSLayerMngPtr) pExynos->pLYRM;
    exynosLayerClientPtr p_ret = _findLayerClientByName(p_client_name);

    if (p_ret) {
        XDBG_DEBUG(MLYRM,
                   "Already Registered LYRID:%6" PRIXID
                   " client:%s priority:%d\n", p_ret->stamp,
                   p_ret->lyr_client_name, p_ret->lyr_client_priority);
        return (EXYNOSLayerMngClientID) p_ret->stamp;
    }
    p_ret = calloc(1, sizeof(exynosLayerClientRec));
    if (!p_ret) {
        XDBG_ERROR(MLYRM, "Can't alloc memory\n");
        return LYR_ERROR_ID;
    }
    CARD32 stamp = (CARD32) GetTimeInMillis();

    while (_findLayerClient(stamp))
        stamp++;
    int size = 0;

    while (p_client_name[size++] != '\0');
    p_ret->lyr_client_name = calloc(size, sizeof(char));
    if (!p_ret->lyr_client_name) {
        free(p_ret);
        XDBG_ERROR(MLYRM, "Can't alloc memory\n");
        return LYR_ERROR_ID;
    }
    memcpy(p_ret->lyr_client_name, p_client_name, size * sizeof(char));
    p_ret->lyr_client_priority = priority;
    p_ret->pLYRM = pLYRM;
    p_ret->stamp = stamp;
    xorg_list_add(&p_ret->valid_link, &client_valid_list);
    XDBG_DEBUG(MLYRM, "Registered LYRID:%6" PRIXID " client:%s priority:%d\n",
               stamp, p_ret->lyr_client_name, p_ret->lyr_client_priority);
#if 0
    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
               "Registered LYRID:%6" PRIXID " client:%s priority:%d\n", stamp,
               p_ret->lyr_client_name, p_ret->lyr_client_priority);
#endif
    return (EXYNOSLayerMngClientID) stamp;
}

void
exynosLayerMngUnRegisterClient(EXYNOSLayerMngClientID lyr_client_id)
{
    exynosLayerClientPtr p_lyr_client = _findLayerClient(lyr_client_id);

    XDBG_RETURN_IF_FAIL(p_lyr_client != NULL);
    XDBG_DEBUG(MLYRM, "Unregister LYRID:%6" PRIXID " client:%s\n",
               lyr_client_id, p_lyr_client->lyr_client_name);
    EXYNOSLayerOutput output;
    EXYNOSLayerMngPlanePos plane_pos;
    EXYNOSLayerMngEventType event;

    for (event = EVENT_LAYER_SHOW; event < EVENT_LAYER_MAX; event++) {
        exynosLayerMngDelEvent(lyr_client_id, event, NULL);
    }
    for (output = LAYER_OUTPUT_LCD; output < LAYER_OUTPUT_MAX; output++) {
        for (plane_pos = PLANE_LOWER2; plane_pos < PLANE_MAX; plane_pos++) {
            exynosLayerMngHWLayerPtr search_layer =
                layer_table[output][plane_pos].layer;
            if (search_layer && search_layer->owner == lyr_client_id) {
                exynosLayerMngRelease(lyr_client_id, output,
                                      PPOSCONV(plane_pos));
            }
        }
    }
    XDBG_DEBUG(MLYRM, "Client %s removed from Layer Manager\n",
               p_lyr_client->lyr_client_name);
    xorg_list_del(&p_lyr_client->valid_link);
    free(p_lyr_client->lyr_client_name);
    free(p_lyr_client);
}

Bool
exynosLayerMngInit(ScrnInfoPtr pScrn)
{
    if (!pScrn) {
        EARLY_ERROR_MSG("pScrn is NULL\n");
        return FALSE;
    }
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);

    if (!pExynos) {
        EARLY_ERROR_MSG("pExynos is NULL\n");
        return FALSE;
    }
    int count_of_layers = exynosPlaneGetCount();

    if (count_of_layers == 0) {
        EARLY_ERROR_MSG("Count of hardware layers = 0");
        return FALSE;
    }
    int i;
    EXYNOSLayerMngPtr pLYRM = calloc(1, sizeof(SecLayerMngRec));

    if (!pLYRM) {
        EARLY_ERROR_MSG("Can't alloc memory\n");
        return FALSE;
    }
    xorg_list_init(&client_valid_list);
    for (i = 0; i < EVENT_LAYER_MAX; i++) {
        xorg_list_init(&pLYRM->Events[i]);
    }
    pLYRM->array_p_layers =
        calloc(count_of_layers, sizeof(exynosLayerMngHWLayerPtr));
    pLYRM->count_of_layers = count_of_layers;
    if (!pLYRM->array_p_layers) {
        EARLY_ERROR_MSG("Can't alloc memory\n");
        goto bail;
    }
    for (i = 0; i < count_of_layers; i++) {
        pLYRM->array_p_layers[i] = calloc(1, sizeof(exynosLayerMngHWLayerRec));
        if (!pLYRM->array_p_layers[i]) {
            EARLY_ERROR_MSG("Can't alloc memory\n");
            goto bail;
        }
        pLYRM->array_p_layers[i]->hwlayer =
            exynosLayerCreate(pScrn, LAYER_OUTPUT_LCD, FOR_LAYER_MNG);
        if (!pLYRM->array_p_layers[i]->hwlayer) {
            EARLY_ERROR_MSG("Can't alloc layer\n");
            goto bail;
        }
//        pLYRM->array_p_layers[i]->output = LAYER_OUTPUT_LCD;
//        pLYRM->array_p_layers[i]->lpos = LAYER_NONE;
        exynosLayerSetPos(pLYRM->array_p_layers[i]->hwlayer, LAYER_NONE);
    }
    pLYRM->pScrn = pScrn;
    pExynos->pLYRM = pLYRM;
    return TRUE;
 bail:
    if (pLYRM) {
        if (pLYRM->array_p_layers) {
            for (i = 0; i < count_of_layers; i++) {
                if (pLYRM->array_p_layers[i]) {
                    exynosLayerUnref(pLYRM->array_p_layers[i]->hwlayer);
                    free(pLYRM->array_p_layers[i]);
                }
            }
            free(pLYRM->array_p_layers);
        }
        free(pLYRM);
    }
    return FALSE;
}

void
exynosLayerMngDeInit(ScrnInfoPtr pScrn)
{
    XDBG_RETURN_IF_FAIL(pScrn != NULL);
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);

    XDBG_RETURN_IF_FAIL(pExynos != NULL);
    XDBG_RETURN_IF_FAIL(pExynos->pLYRM != NULL);
    EXYNOSLayerMngPtr pLYRM = (EXYNOSLayerMngPtr) pExynos->pLYRM;
    exynosLayerClientPtr current_entry_client_list =
        NULL, next_entry_client_list = NULL;
    xorg_list_for_each_entry_safe(current_entry_client_list,
                                  next_entry_client_list, &client_valid_list,
                                  valid_link) {
        exynosLayerMngUnRegisterClient((EXYNOSLayerMngClientID)
                                       current_entry_client_list->stamp);
    }

    if (pLYRM->array_p_layers) {
        int i;

        for (i = 0; i < pLYRM->count_of_layers; i++) {
            if (pLYRM->array_p_layers[i]) {
                exynosLayerUnref(pLYRM->array_p_layers[i]->hwlayer);
                free(pLYRM->array_p_layers[i]);
            }
        }
        free(pLYRM->array_p_layers);
    }

    free(pLYRM);
    pExynos->pLYRM = NULL;
}

/* TODO: Avoid LEGACY Layer function */
static exynosLayerMngHWLayerPtr
_exynosLayerMngFindUnusedLayer(EXYNOSLayerMngPtr pLYRM)
{
    int i;

    for (i = 0; i < pLYRM->count_of_layers; i++) {
        if (pLYRM->array_p_layers[i]->owner == 0) {
            XDBG_DEBUG(MLYRM, "Find unused plane %p\n",
                       pLYRM->array_p_layers[i]);
            return pLYRM->array_p_layers[i];
        }
    }
    return NULL;
}

static Bool
_exynosLayerMngTestAnnexLayer(exynosLayerClientPtr p_lyr_client,
                              EXYNOSLayerOutput output,
                              EXYNOSLayerMngPlanePos plane_pos)
{
    XDBG_RETURN_VAL_IF_FAIL(output >= LAYER_OUTPUT_LCD &&
                            output < LAYER_OUTPUT_MAX, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(plane_pos >= PLANE_LOWER2 &&
                            plane_pos < PLANE_MAX, FALSE);
    exynosLayerMngHWLayerPtr search_layer =
        layer_table[output][plane_pos].layer;

    XDBG_RETURN_VAL_IF_FAIL(search_layer != NULL, FALSE);
    exynosLayerClientPtr current_layer_owner =
        _findLayerClient(search_layer->owner);
    Bool ret_data = FALSE;

    if (!current_layer_owner) {
        XDBG_NEVER_GET_HERE(MLYRM);
        ret_data = TRUE;
    }
    else if (current_layer_owner == p_lyr_client) {
        XDBG_NEVER_GET_HERE(MLYRM);
        ret_data = TRUE;
    }
    else if (current_layer_owner->lyr_client_priority <
             p_lyr_client->lyr_client_priority) {
        ret_data = TRUE;
    }
    else {
        ret_data = FALSE;
    }
    XDBG_DEBUG(MLYRM, "Client:%s %s annex, output:%d, ppos:%d, at owner:%s\n",
               p_lyr_client->lyr_client_name, ret_data ? "Can" : "Can't",
               output, plane_pos,
               current_layer_owner ? current_layer_owner->lyr_client_name :
               "not valid");
    return ret_data;
}

/* Temp func
 * TODO: save ppos and output on hw_layer struct
*/
static Bool
_exynosLayerMngFindPlaneByLayer(exynosLayerMngHWLayerPtr hw_layer,
                                EXYNOSLayerOutput * p_ret_output,
                                EXYNOSLayerMngPlanePos * p_ret_plane_pos)
{
    EXYNOSLayerOutput output;
    EXYNOSLayerMngPlanePos plane_pos;
    Bool ret = FALSE;

    for (output = LAYER_OUTPUT_LCD; output < LAYER_OUTPUT_MAX; output++) {
        for (plane_pos = PLANE_LOWER2; plane_pos < PLANE_MAX; plane_pos++) {
            exynosLayerMngHWLayerPtr search_layer =
                layer_table[output][plane_pos].layer;
            if (search_layer && search_layer == hw_layer) {
                ret = TRUE;
                if (p_ret_output)
                    *p_ret_output = output;
                if (p_ret_plane_pos)
                    *p_ret_plane_pos = plane_pos;
                break;
            }
        }
        if (ret)
            break;
    }
    return ret;
}

static exynosLayerMngHWLayerPtr
_exynosLayerMngAnnexLayer(exynosLayerClientPtr p_lyr_client,
                          EXYNOSLayerOutput desired_output,
                          EXYNOSLayerMngPlanePos desired_plane_pos)
{
    exynosLayerMngHWLayerPtr return_layer = NULL;
    EXYNOSLayerMngPtr pLYRM = p_lyr_client->pLYRM;
    ScrnInfoPtr pScrn = pLYRM->pScrn;
    exynosLayerClientPtr current_layer_owner = NULL;

    /* Need any layer for annexing */
    if (desired_output == LAYER_OUTPUT_MAX && desired_plane_pos == PLANE_MAX) {
        int i;

        for (i = 0; i < pLYRM->count_of_layers; i++) {
            if (pLYRM->array_p_layers[i]->owner != p_lyr_client->stamp) {
                current_layer_owner =
                    _findLayerClient(pLYRM->array_p_layers[i]->owner);
                if (!current_layer_owner) {
                    XDBG_NEVER_GET_HERE(MLYRM);
                    return_layer = pLYRM->array_p_layers[i];
                    break;
                }
                else if (current_layer_owner->lyr_client_priority <
                         p_lyr_client->lyr_client_priority) {
                    EXYNOSLayerOutput temp_output;
                    EXYNOSLayerMngPlanePos temp_plane_pos;

                    XDBG_RETURN_VAL_IF_FAIL(_exynosLayerMngFindPlaneByLayer
                                            (pLYRM->array_p_layers[i],
                                             &temp_output, &temp_plane_pos),
                                            NULL);
                    XDBG_DEBUG(MLYRM,
                               "Used other plane %p of layer client %s will be annex\n",
                               pLYRM->array_p_layers[i],
                               current_layer_owner->lyr_client_name);
                    pLYRM->array_p_layers[i]->owner = p_lyr_client->stamp;      /* For callback */
                    EXYNOSLayerMngEventCallbackDataRec callback_data =
                        {.annex_callback = {.lpos =
                                            PPOSCONV(temp_plane_pos),.output =
                                            temp_output,
                                            .lyr_client_id =
                                            current_layer_owner->stamp}
                    };
                    exynosLayerMngEventDispatch(pScrn, EVENT_LAYER_ANNEX,
                                                &callback_data);
                    exynosLayerHide(pLYRM->array_p_layers[i]->hwlayer);
                    layer_table[temp_output][temp_plane_pos].layer = NULL;
                    pLYRM->array_p_layers[i]->owner = 0;
                    return_layer = pLYRM->array_p_layers[i];
                    break;
                }
            }
        }
    }
    else {
        XDBG_RETURN_VAL_IF_FAIL(desired_output >= LAYER_OUTPUT_LCD
                                && desired_output < LAYER_OUTPUT_MAX, NULL);
        XDBG_RETURN_VAL_IF_FAIL(desired_plane_pos >= PLANE_LOWER2
                                && desired_plane_pos < PLANE_MAX, NULL);
        exynosLayerMngHWLayerPtr desired_layer =
            layer_table[desired_output][desired_plane_pos].layer;
        XDBG_RETURN_VAL_IF_FAIL(desired_layer != NULL, NULL);
        current_layer_owner = _findLayerClient(desired_layer->owner);
        if (!current_layer_owner) {
            XDBG_NEVER_GET_HERE(MLYRM);
            return_layer = desired_layer;
        }
        else if (current_layer_owner == p_lyr_client) {
            XDBG_NEVER_GET_HERE(MLYRM);
            return_layer = desired_layer;
        }
        else if (current_layer_owner->lyr_client_priority <
                 p_lyr_client->lyr_client_priority) {
            XDBG_DEBUG(MLYRM,
                       "Used plane %p of layer client %s will be annex\n",
                       desired_layer, current_layer_owner->lyr_client_name);
            desired_layer->owner = p_lyr_client->stamp; /* For callback */
            EXYNOSLayerMngEventCallbackDataRec callback_data =
                {.annex_callback = {.lpos =
                                    PPOSCONV(desired_plane_pos),.output =
                                    desired_output,
                                    .lyr_client_id = current_layer_owner->stamp}
            };
            exynosLayerMngEventDispatch(pScrn, EVENT_LAYER_ANNEX,
                                        &callback_data);
            exynosLayerHide(desired_layer->hwlayer);
            layer_table[desired_output][desired_plane_pos].layer = NULL;
            desired_layer->owner = 0;
            return_layer = desired_layer;
        }
    }

    XDBG_DEBUG(MLYRM, "Annex plane %p\n", return_layer);

    return return_layer;
}

Bool
exynosLayerMngCheckFreePos(EXYNOSLayerOutput output, EXYNOSLayerPos lpos)
{
    Bool ret = FALSE;

    XDBG_RETURN_VAL_IF_FAIL(output >= LAYER_OUTPUT_LCD &&
                            output < LAYER_OUTPUT_MAX, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(LPOSCONV(lpos) >= PLANE_LOWER2 &&
                            LPOSCONV(lpos) < PLANE_MAX, FALSE);
    if (!layer_table[output][LPOSCONV(lpos)].layer &&
        layer_table[output][LPOSCONV(lpos)].hwpos != INVALID_HWPOS) {
        ret = TRUE;
    }
    return ret;
}

int
exynosLayerMngGetListOfOwnedPos(EXYNOSLayerMngClientID lyr_client_id,
                                EXYNOSLayerOutput output,
                                EXYNOSLayerPos * p_lpos)
{
    int ret = 0;
    exynosLayerClientPtr p_lyr_client = _findLayerClient(lyr_client_id);

    XDBG_RETURN_VAL_IF_FAIL(p_lyr_client != NULL, 0);
    EXYNOSLayerMngPlanePos plane_pos;

    for (plane_pos = PLANE_LOWER2; plane_pos < PLANE_MAX; plane_pos++) {
        if (layer_table[output][plane_pos].layer) {
            if (layer_table[output][plane_pos].layer->owner == lyr_client_id) {
                if (p_lpos)
                    p_lpos[ret] = PPOSCONV(plane_pos);
                ret++;
            }
        }
    }
    XDBG_DEBUG(MLYRM, "Client:%s used %d planes\n",
               p_lyr_client->lyr_client_name, ret);
    return ret;
}

int
exynosLayerMngGetListOfAccessablePos(EXYNOSLayerMngClientID lyr_client_id,
                                     EXYNOSLayerOutput output,
                                     EXYNOSLayerPos * p_lpos)
{

    int ret = 0;
    exynosLayerClientPtr p_lyr_client = _findLayerClient(lyr_client_id);

    XDBG_RETURN_VAL_IF_FAIL(p_lyr_client != NULL, 0);
    EXYNOSLayerMngPtr pLYRM = p_lyr_client->pLYRM;

    XDBG_RETURN_VAL_IF_FAIL(pLYRM != NULL, 0);
    int count_of_free_plane = _exynosLayerMngCountOfFreeLayer(pLYRM);
    int count_of_annex_plane =
        _exynosLayerMngCountOfAnnexLayer(pLYRM,
                                         p_lyr_client->lyr_client_priority);
    XDBG_RETURN_VAL_IF_FAIL(count_of_free_plane != -1, 0);
    XDBG_RETURN_VAL_IF_FAIL(count_of_annex_plane != -1, 0);
    EXYNOSLayerMngPlanePos plane_pos;

    for (plane_pos = PLANE_LOWER2; plane_pos < PLANE_MAX; plane_pos++) {
        if (layer_table[output][plane_pos].hwpos == INVALID_HWPOS)
            continue;
        if (layer_table[output][plane_pos].layer) {
            if (layer_table[output][plane_pos].layer->owner == lyr_client_id) {
                if (p_lpos)
                    p_lpos[ret] = PPOSCONV(plane_pos);
                ret++;
            }
            else if (count_of_annex_plane > 0
                     && _exynosLayerMngTestAnnexLayer(p_lyr_client, output,
                                                      plane_pos)) {
                if (p_lpos)
                    p_lpos[ret] = PPOSCONV(plane_pos);
                ret++;
                count_of_annex_plane--;
            }
        }
        else {
            if (count_of_free_plane != 0) {
                if (p_lpos)
                    p_lpos[ret] = PPOSCONV(plane_pos);
                ret++;
                count_of_free_plane--;
            }
            else if (count_of_annex_plane != 0) {
                if (p_lpos)
                    p_lpos[ret] = PPOSCONV(plane_pos);
                ret++;
                count_of_annex_plane--;
            }
        }
    }

    XDBG_DEBUG(MLYRM, "Client:%s can set %d planes\n",
               p_lyr_client->lyr_client_name, ret);
    return ret;
}

static exynosLayerMngHWLayerPtr
_exynosLayerMngNeedPlane(exynosLayerClientPtr p_lyr_client,
                         EXYNOSLayerOutput output,
                         EXYNOSLayerMngPlanePos plane_pos)
{
    XDBG_RETURN_VAL_IF_FAIL(output >= LAYER_OUTPUT_LCD &&
                            output < LAYER_OUTPUT_MAX, NULL);
    XDBG_RETURN_VAL_IF_FAIL(plane_pos >= PLANE_LOWER2 &&
                            plane_pos < PLANE_MAX, NULL);
    exynosLayerMngHWLayerPtr search_layer = NULL, ret_layer = NULL;

    search_layer = layer_table[output][plane_pos].layer;
    if (search_layer) {
        if (search_layer->owner == (EXYNOSLayerMngClientID) p_lyr_client->stamp) {
            XDBG_DEBUG(MLYRM,
                       "Found usable plane %p on output %d, pos %d, owner %s\n",
                       search_layer, output, plane_pos,
                       p_lyr_client->lyr_client_name);
            ret_layer = search_layer;
        }
        else {
            search_layer =
                _exynosLayerMngAnnexLayer(p_lyr_client, output, plane_pos);
            if (search_layer) {
                XDBG_DEBUG(MLYRM,
                           "Annex usable plane %p on output %d, pos %d, for owner %s\n",
                           search_layer, output, plane_pos,
                           p_lyr_client->lyr_client_name);
                ret_layer = search_layer;
            }
        }
    }
    else {
        EXYNOSLayerMngPtr pLYRM = p_lyr_client->pLYRM;

        XDBG_RETURN_VAL_IF_FAIL(pLYRM != NULL, NULL);
        search_layer = _exynosLayerMngFindUnusedLayer(pLYRM);
        if (search_layer) {
            XDBG_DEBUG(MLYRM,
                       "Found free plane %p on output %d, pos %d, owner %s\n",
                       search_layer, output, plane_pos,
                       p_lyr_client->lyr_client_name);
            ret_layer = search_layer;
            exynosLayerSetOutput(search_layer->hwlayer, output);
        }
        else {
            search_layer =
                _exynosLayerMngAnnexLayer(p_lyr_client, LAYER_OUTPUT_MAX,
                                          PLANE_MAX);
            if (search_layer) {
                XDBG_DEBUG(MLYRM, "Annex usable plane %p, for owner %s\n",
                           search_layer, p_lyr_client->lyr_client_name);
                ret_layer = search_layer;
                exynosLayerSetOutput(search_layer->hwlayer, output);
            }
        }
    }

    if (!ret_layer) {
        XDBG_DEBUG(MLYRM,
                   "Didn't found free plane on output %d, pos %d, for owner %s\n",
                   output, plane_pos, p_lyr_client->lyr_client_name);
    }
    return ret_layer;
}

Bool
exynosLayerMngReservation(EXYNOSLayerMngClientID lyr_client_id,
                          EXYNOSLayerOutput output, EXYNOSLayerPos lpos)
{
    exynosLayerClientPtr p_lyr_client = _findLayerClient(lyr_client_id);

    XDBG_RETURN_VAL_IF_FAIL(p_lyr_client != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(output >= LAYER_OUTPUT_LCD &&
                            output < LAYER_OUTPUT_MAX, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(LPOSCONV(lpos) >= PLANE_LOWER2 &&
                            LPOSCONV(lpos) < PLANE_MAX, FALSE);
    if (layer_table[output][LPOSCONV(lpos)].hwpos == INVALID_HWPOS) {
        XDBG_ERROR(MLYRM, "Can't use lpos:%d for output:%d\n", lpos, output);
        return FALSE;
    }
    exynosLayerMngHWLayerPtr tunable_layer =
        _exynosLayerMngNeedPlane(p_lyr_client, output, LPOSCONV(lpos));
    if (tunable_layer) {
        tunable_layer->owner = lyr_client_id;
        layer_table[output][LPOSCONV(lpos)].layer = tunable_layer;
        XDBG_DEBUG(MLYRM,
                   "Successful to reserve plane:%p, client:%s, output:%d, ppos:%d\n",
                   tunable_layer, p_lyr_client->lyr_client_name, output,
                   LPOSCONV(lpos));
        return TRUE;
    }
    XDBG_DEBUG(MLYRM,
               "Failure to reserve plane:%p, client:%s, output:%d, ppos:%d\n",
               tunable_layer, p_lyr_client->lyr_client_name, output,
               LPOSCONV(lpos));
    return FALSE;
}

Bool
exynosLayerMngSet(EXYNOSLayerMngClientID lyr_client_id, int offset_x,
                  int offset_y, xRectangle *src, xRectangle *dst,
                  DrawablePtr pDraw, EXYNOSVideoBuf * vbuf,
                  EXYNOSLayerOutput output, EXYNOSLayerPos lpos, void *end_func,
                  void *data)
{
    /* TODO auto crop and scale mechanism */
    /* TODO: Rework to set new size of layer */
    exynosLayerClientPtr p_lyr_client = _findLayerClient(lyr_client_id);

    XDBG_RETURN_VAL_IF_FAIL(p_lyr_client != NULL, FALSE);
    EXYNOSLayerMngPtr pLYRM = p_lyr_client->pLYRM;

    XDBG_RETURN_VAL_IF_FAIL(pLYRM != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(!(vbuf != NULL && pDraw != NULL), FALSE);
    XDBG_RETURN_VAL_IF_FAIL(output >= LAYER_OUTPUT_LCD &&
                            output < LAYER_OUTPUT_MAX, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(LPOSCONV(lpos) >= PLANE_LOWER2 &&
                            LPOSCONV(lpos) < PLANE_MAX, FALSE);
    if (layer_table[output][LPOSCONV(lpos)].hwpos == INVALID_HWPOS) {
        XDBG_ERROR(MLYRM, "Can't use lpos:%d for output:%d\n", lpos, output);
        return FALSE;
    }
    exynosLayerMngHWLayerPtr tunable_layer =
        _exynosLayerMngNeedPlane(p_lyr_client, output, LPOSCONV(lpos));
    if (tunable_layer) {
        exynosLayerEnableVBlank(tunable_layer->hwlayer, TRUE);
        if (src && dst) {
            exynosLayerFreezeUpdate(tunable_layer->hwlayer, TRUE);
            XDBG_GOTO_IF_FAIL(exynosLayerSetOffset
                              (tunable_layer->hwlayer, offset_x, offset_y),
                              bail);
            XDBG_GOTO_IF_FAIL(exynosLayerSetOutput
                              (tunable_layer->hwlayer, output), bail);
            XDBG_GOTO_IF_FAIL(exynosLayerSetPos
                              (tunable_layer->hwlayer,
                               layer_table[output][LPOSCONV(lpos)].legacy_pos),
                              bail);
            XDBG_GOTO_IF_FAIL(exynosLayerSetRect
                              (tunable_layer->hwlayer, src, dst), bail);
            exynosLayerFreezeUpdate(tunable_layer->hwlayer, FALSE);
        }

        if (vbuf) {
            XDBG_GOTO_IF_FAIL(exynosLayerSetBuffer
                              (tunable_layer->hwlayer, vbuf), bail);
        }
        else if (pDraw) {
            XDBG_GOTO_IF_FAIL(exynosLayerSetDraw(tunable_layer->hwlayer, pDraw),
                              bail);
        }

//        vbuf->vblank_handler = end_func;
//        vbuf->vblank_user_data = data;

        tunable_layer->owner = lyr_client_id;
        layer_table[output][LPOSCONV(lpos)].layer = tunable_layer;
        if (!exynosLayerIsVisible(tunable_layer->hwlayer)) {
            exynosLayerShow(tunable_layer->hwlayer);
        }
        else if (exynosLayerIsNeedUpdate(tunable_layer->hwlayer)) {
            exynosLayerUpdate(tunable_layer->hwlayer);
        }
    }
    XDBG_DEBUG(MLYRM,
               "Successful setup plane:%p, client:%s, output:%d, ppos:%d\n",
               tunable_layer, p_lyr_client->lyr_client_name, output,
               LPOSCONV(lpos));
    return TRUE;
 bail:
    XDBG_DEBUG(MLYRM, "Fail setup plane, client:%s, output:%d, ppos:%d\n",
               p_lyr_client->lyr_client_name, output, LPOSCONV(lpos));
    return FALSE;
}

void
exynosLayerMngRelease(EXYNOSLayerMngClientID lyr_client_id,
                      EXYNOSLayerOutput output, EXYNOSLayerPos lpos)
{
    exynosLayerClientPtr p_lyr_client = _findLayerClient(lyr_client_id);

    XDBG_RETURN_IF_FAIL(p_lyr_client != NULL);
    XDBG_RETURN_IF_FAIL(output >= LAYER_OUTPUT_LCD &&
                        output < LAYER_OUTPUT_MAX);
    XDBG_RETURN_IF_FAIL(LPOSCONV(lpos) >= PLANE_LOWER2 &&
                        LPOSCONV(lpos) < PLANE_MAX);
    EXYNOSLayerMngPtr pLYRM = p_lyr_client->pLYRM;

    XDBG_RETURN_IF_FAIL(pLYRM != NULL);
    ScrnInfoPtr pScrn = pLYRM->pScrn;
    exynosLayerMngHWLayerPtr tunable_layer =
        layer_table[output][LPOSCONV(lpos)].layer;
    if (tunable_layer) {
        if (tunable_layer->owner != lyr_client_id) {
            XDBG_ERROR(MLYRM,
                       "Can't release plane. Client %s not own this plane %p\n",
                       p_lyr_client->lyr_client_name, tunable_layer);
            exynosLayerClientPtr real_lyr_client =
                _findLayerClient(tunable_layer->owner);
            XDBG_ERROR(MLYRM, "Owner of plane %p is %s\n", tunable_layer,
                       real_lyr_client ? real_lyr_client->lyr_client_name :
                       "NONE");
        }
        else {
            XDBG_DEBUG(MLYRM, "Client:%s release plane:%p. lpos:%d output:%d\n",
                       p_lyr_client->lyr_client_name, tunable_layer, lpos,
                       output);
            exynosLayerHide(tunable_layer->hwlayer);
            exynosLayerDestroy(tunable_layer->hwlayer);
            tunable_layer->hwlayer =
                exynosLayerCreate(pScrn, LAYER_OUTPUT_LCD, FOR_LAYER_MNG);
            exynosLayerSetPos(tunable_layer->hwlayer, LAYER_NONE);
            tunable_layer->owner = 0;
            layer_table[output][LPOSCONV(lpos)].layer = NULL;
            EXYNOSLayerMngEventCallbackDataRec release_callback_data =
                {.release_callback = {.output = output,.lpos =
                                      lpos,.lyr_client_id = lyr_client_id}
            };
            int old_count_of_free_layer =
                _exynosLayerMngCountOfFreeLayer(pLYRM) - 1;
            exynosLayerMngEventDispatch(pScrn, EVENT_LAYER_RELEASE,
                                        &release_callback_data);
            int new_count_of_free_layer =
                _exynosLayerMngCountOfFreeLayer(pLYRM);

            if (new_count_of_free_layer != old_count_of_free_layer) {
                EXYNOSLayerMngEventCallbackDataRec counter_callback_data =
                    {.counter_callback = {.new_count = new_count_of_free_layer}
                };
                exynosLayerMngEventDispatch(pScrn, EVENT_LAYER_FREE_COUNTER,
                                            &counter_callback_data);
            }
        }
    }
    else {
        XDBG_DEBUG(MLYRM, "Not found any plane on output %d, lpos %d\n", output,
                   lpos);
    }
}

Bool
exynosLayerMngClearQueue(EXYNOSLayerMngClientID lyr_client_id,
                         EXYNOSLayerOutput output, EXYNOSLayerPos lpos)
{
    XDBG_RETURN_VAL_IF_FAIL(output >= LAYER_OUTPUT_LCD &&
                            output < LAYER_OUTPUT_MAX, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(LPOSCONV(lpos) >= PLANE_LOWER2 &&
                            LPOSCONV(lpos) < PLANE_MAX, FALSE);
    exynosLayerClientPtr p_lyr_client = _findLayerClient(lyr_client_id);

    XDBG_RETURN_VAL_IF_FAIL(p_lyr_client != NULL, FALSE);
    exynosLayerMngHWLayerPtr tunable_layer =
        layer_table[output][LPOSCONV(lpos)].layer;
    if (tunable_layer) {
        if (tunable_layer->owner != lyr_client_id) {
            XDBG_ERROR(MLYRM,
                       "Can't clear vbuf queue. Client %s not own this plane %p\n",
                       p_lyr_client->lyr_client_name, tunable_layer);
            exynosLayerClientPtr real_lyr_client =
                _findLayerClient(tunable_layer->owner);
            XDBG_ERROR(MLYRM, "Owner of plane %p is %s\n", tunable_layer,
                       real_lyr_client ? real_lyr_client->lyr_client_name :
                       "NONE");
        }
        else {
            XDBG_DEBUG(MLYRM,
                       "Client:%s clear vbuf queue plane:%p. lpos:%d output:%d\n",
                       p_lyr_client->lyr_client_name, tunable_layer, lpos,
                       output);
            exynosLayerClearQueue(tunable_layer->hwlayer);
        }
    }
    else {
        XDBG_DEBUG(MLYRM, "Not found any plane on output %d, lpos %d\n", output,
                   lpos);
    }
    return TRUE;
}

Bool
exynosLayerMngSwapPos(EXYNOSLayerMngClientID lyr_client_id1,
                      EXYNOSLayerOutput output1, EXYNOSLayerPos lpos1,
                      EXYNOSLayerMngClientID lyr_client_id2,
                      EXYNOSLayerOutput output2, EXYNOSLayerPos lpos2)
{
/* TODO: swap between output */
    if (output1 != output2) {
        XDBG_ERROR(MLYRM, "Not implemented yet output:%d != output:%d\n",
                   output1, output2);
        return FALSE;
    }
    XDBG_RETURN_VAL_IF_FAIL(output1 >= LAYER_OUTPUT_LCD &&
                            output1 < LAYER_OUTPUT_MAX, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(LPOSCONV(lpos1) >= PLANE_LOWER2 &&
                            LPOSCONV(lpos1) < PLANE_MAX, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(output2 >= LAYER_OUTPUT_LCD &&
                            output2 < LAYER_OUTPUT_MAX, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(LPOSCONV(lpos2) >= PLANE_LOWER2 &&
                            LPOSCONV(lpos2) < PLANE_MAX, FALSE);
    exynosLayerClientPtr p_lyr_client1 = _findLayerClient(lyr_client_id1);
    exynosLayerClientPtr p_lyr_client2 = _findLayerClient(lyr_client_id2);

    XDBG_RETURN_VAL_IF_FAIL(p_lyr_client2 != NULL ||
                            p_lyr_client1 != NULL, FALSE);

    if (!layer_table[output1][LPOSCONV(lpos1)].layer &&
        !layer_table[output2][LPOSCONV(lpos2)].layer) {
        XDBG_DEBUG(MLYRM,
                   "Can't swap. output:%d, lpos:%d and output:%d lpos:%d not set\n",
                   output1, lpos1, output2, lpos2);
        return TRUE;
    }
    if (!layer_table[output2][LPOSCONV(lpos2)].layer && !p_lyr_client2) {
        XDBG_RETURN_VAL_IF_FAIL(_clientIsOwner
                                (p_lyr_client1, output1, LPOSCONV(lpos1)),
                                FALSE);
        exynosLayerFreezeUpdate(layer_table[output1][LPOSCONV(lpos1)].layer->
                                hwlayer, TRUE);
        exynosLayerSetPos(layer_table[output1][LPOSCONV(lpos1)].layer->hwlayer,
                          lpos2);
        exynosLayerFreezeUpdate(layer_table[output1][LPOSCONV(lpos1)].layer->
                                hwlayer, FALSE);
        layer_table[output2][LPOSCONV(lpos2)].layer =
            layer_table[output1][LPOSCONV(lpos1)].layer;
        layer_table[output1][LPOSCONV(lpos1)].layer = NULL;
        EXYNOSLayerMngEventCallbackDataRec callback_data =
            {.release_callback = {.output = output1,.lpos =
                                  lpos1,.lyr_client_id = lyr_client_id1}
        };
        ScrnInfoPtr pScrn = p_lyr_client1->pLYRM->pScrn;

        exynosLayerMngEventDispatch(pScrn, EVENT_LAYER_RELEASE, &callback_data);
    }
    else if (!layer_table[output1][LPOSCONV(lpos1)].layer && !p_lyr_client1) {
        XDBG_RETURN_VAL_IF_FAIL(_clientIsOwner
                                (p_lyr_client2, output2, LPOSCONV(lpos2)),
                                FALSE);
        exynosLayerFreezeUpdate(layer_table[output2][LPOSCONV(lpos2)].layer->
                                hwlayer, TRUE);
        exynosLayerSetPos(layer_table[output2][LPOSCONV(lpos2)].layer->hwlayer,
                          lpos1);
        exynosLayerFreezeUpdate(layer_table[output2][LPOSCONV(lpos2)].layer->
                                hwlayer, FALSE);
        layer_table[output1][LPOSCONV(lpos1)].layer =
            layer_table[output2][LPOSCONV(lpos2)].layer;
        layer_table[output2][LPOSCONV(lpos2)].layer = NULL;
        ScrnInfoPtr pScrn = p_lyr_client2->pLYRM->pScrn;
        EXYNOSLayerMngEventCallbackDataRec callback_data =
            {.release_callback = {.output = output2,.lpos =
                                  lpos2,.lyr_client_id = lyr_client_id2}
        };
        exynosLayerMngEventDispatch(pScrn, EVENT_LAYER_RELEASE, &callback_data);
    }
    else {
        XDBG_RETURN_VAL_IF_FAIL(_clientIsOwner
                                (p_lyr_client2, output2, LPOSCONV(lpos2)),
                                FALSE);
        XDBG_RETURN_VAL_IF_FAIL(_clientIsOwner
                                (p_lyr_client1, output1, LPOSCONV(lpos1)),
                                FALSE);
        if (p_lyr_client2->lyr_client_priority !=
            p_lyr_client1->lyr_client_priority) {
            XDBG_ERROR(MLYRM,
                       "Can't swap Client1:%s:%d and Client2:%s:%d different priority\n",
                       p_lyr_client1->lyr_client_name,
                       p_lyr_client2->lyr_client_name,
                       p_lyr_client1->lyr_client_priority,
                       p_lyr_client2->lyr_client_priority);
            return FALSE;
        }
        exynosLayerSetPos(layer_table[output2][LPOSCONV(lpos2)].layer->hwlayer,
                          LAYER_NONE);
        exynosLayerSetPos(layer_table[output1][LPOSCONV(lpos1)].layer->hwlayer,
                          LAYER_NONE);
        exynosLayerFreezeUpdate(layer_table[output1][LPOSCONV(lpos1)].layer->
                                hwlayer, TRUE);
        exynosLayerFreezeUpdate(layer_table[output2][LPOSCONV(lpos2)].layer->
                                hwlayer, TRUE);
        exynosLayerSetPos(layer_table[output1][LPOSCONV(lpos1)].layer->hwlayer,
                          lpos2);
        exynosLayerSetPos(layer_table[output2][LPOSCONV(lpos2)].layer->hwlayer,
                          lpos1);
        exynosLayerFreezeUpdate(layer_table[output1][LPOSCONV(lpos1)].layer->
                                hwlayer, FALSE);
        exynosLayerFreezeUpdate(layer_table[output2][LPOSCONV(lpos2)].layer->
                                hwlayer, FALSE);
        exynosLayerMngHWLayerPtr temp =
            layer_table[output1][LPOSCONV(lpos1)].layer;
        layer_table[output1][LPOSCONV(lpos1)].layer =
            layer_table[output2][LPOSCONV(lpos2)].layer;
        layer_table[output2][LPOSCONV(lpos2)].layer = temp;
    }

    XDBG_DEBUG(MLYRM, "Swap Client1:%s <-> Client2:%s\n",
               p_lyr_client1 ? p_lyr_client1->lyr_client_name : "NULL",
               p_lyr_client2 ? p_lyr_client2->lyr_client_name : "NULL");
    XDBG_DEBUG(MLYRM, "output1:%d <-> output2:%d\n", output1, output2);
    XDBG_DEBUG(MLYRM, "lpos1:%d <-> lpos2:%d\n", lpos1, lpos2);
    return TRUE;
}

/* Temporary solution */
EXYNOSLayerPtr
exynosLayerMngTempGetHWLayer(ScrnInfoPtr pScrn, EXYNOSLayerOutput output,
                             EXYNOSLayerPos lpos)
{
    XDBG_RETURN_VAL_IF_FAIL(output >= LAYER_OUTPUT_LCD &&
                            output < LAYER_OUTPUT_MAX, NULL);
    XDBG_RETURN_VAL_IF_FAIL(LPOSCONV(lpos) >= PLANE_LOWER2 &&
                            LPOSCONV(lpos) < PLANE_MAX, NULL);
    XDBG_RETURN_VAL_IF_FAIL(pScrn != NULL, NULL);
    EXYNOSLayerMngClientID temp_id =
        exynosLayerMngRegisterClient(pScrn, "temp_client", 2);
    exynosLayerClientPtr p_lyr_client = _findLayerClient(temp_id);

    XDBG_RETURN_VAL_IF_FAIL(p_lyr_client != NULL, NULL);
    exynosLayerMngHWLayerPtr search_layer =
        _exynosLayerMngNeedPlane(p_lyr_client, output, LPOSCONV(lpos));
    if (search_layer) {
        search_layer->owner = temp_id;
        layer_table[output][LPOSCONV(lpos)].layer = search_layer;
        exynosLayerFreezeUpdate(search_layer->hwlayer, TRUE);
        exynosLayerSetPos(search_layer->hwlayer, lpos);
        exynosLayerFreezeUpdate(search_layer->hwlayer, FALSE);
        XDBG_DEBUG(MLYRM,
                   "Allocate plane:%p for client:%s, output:%d, pos:%d\n",
                   search_layer, p_lyr_client->lyr_client_name, output,
                   LPOSCONV(lpos));
        return search_layer->hwlayer;
    }
    XDBG_DEBUG(MLYRM, "Can't find free layer output:%d lpos:%d\n", output,
               lpos);
    return NULL;
}

/* Temporary solution */
Bool
exynosLayerMngTempDestroyHWLayer(EXYNOSLayerPtr hw_layer)
{
    exynosLayerClientPtr current_entry_client_list =
        NULL, next_entry_client_list = NULL;
    int found_client = 0, break_flag = 0;

    xorg_list_for_each_entry_safe(current_entry_client_list, next_entry_client_list, &client_valid_list, valid_link) {  /* Search any client */
        found_client++;
    }
    if (found_client == 0)
        return FALSE;

    EXYNOSLayerOutput output;
    EXYNOSLayerMngPlanePos plane_pos;

    for (output = LAYER_OUTPUT_LCD; output < LAYER_OUTPUT_MAX; output++) {
        for (plane_pos = PLANE_LOWER2; plane_pos < PLANE_MAX; plane_pos++) {
            exynosLayerMngHWLayerPtr search_layer =
                layer_table[output][plane_pos].layer;
            if (search_layer && search_layer->hwlayer == hw_layer) {
                exynosLayerMngRelease(search_layer->owner, output,
                                      PPOSCONV(plane_pos));
                break_flag = 1;
                break;
            }
        }
        if (break_flag)
            break;
    }

    XDBG_DEBUG(MLYRM, "Release Layer %p \n", hw_layer);
    return TRUE;
}

/* Temporary solution */

#endif
