#ifndef EXYNOS_LAYER_MANAGER_H
#define EXYNOS_LAYER_MANAGER_H
#ifdef LAYER_MANAGER
#include <stdint.h>
#include <X11/Xmd.h>
#include <xorg-server.h>
#include <xf86str.h>
#include <exynos_layer.h>
/* Private struct */
typedef CARD32 EXYNOSLayerMngClientID;

#define LYR_ERROR_ID 0

typedef enum {
    EVENT_LAYER_SHOW = 0,
    EVENT_LAYER_ANNEX,
    EVENT_LAYER_RELEASE,
    EVENT_LAYER_HIDE,
    EVENT_LAYER_BUFSET,
    EVENT_LAYER_FREE_COUNTER,
    EVENT_LAYER_MAX
} EXYNOSLayerMngEventType;

typedef enum {
    PLANE_LOWER2 = 0,
    PLANE_LOWER1 = 1,
    PLANE_DEFAULT = 2,
    PLANE_UPPER = 3,
    PLANE_MAX = 4
} EXYNOSLayerMngPlanePos;

typedef union {
    struct {
        void *callback_data;
        uint32_t tv_sec;
        uint32_t tv_usec;
        uint32_t sequence;
    } hardware_callback;
    struct {
        EXYNOSLayerOutput output;
        EXYNOSLayerPos lpos;
        EXYNOSLayerMngClientID lyr_client_id;
    } annex_callback;
    struct {
        EXYNOSLayerOutput output;
        EXYNOSLayerPos lpos;
        EXYNOSLayerMngClientID lyr_client_id;
    } release_callback;
    struct {
        int new_count;
    } counter_callback;
} EXYNOSLayerMngEventCallbackDataRec, *EXYNOSLayerMngEventCallbackDataPtr;

typedef void (*LYRMNotifyFunc) (void *user_data,
                                EXYNOSLayerMngEventCallbackDataPtr
                                callback_data);

EXYNOSLayerMngClientID exynosLayerMngRegisterClient(ScrnInfoPtr pScrn,
                                                    char *p_client_name,
                                                    int priority);
void exynosLayerMngUnRegisterClient(EXYNOSLayerMngClientID lyr_client_id);
Bool exynosLayerMngInit(ScrnInfoPtr pScrn);
void exynosLayerMngDeInit(ScrnInfoPtr pScrn);
void exynosLayerMngAddEvent(EXYNOSLayerMngClientID lyr_client_id,
                            EXYNOSLayerMngEventType event_type,
                            LYRMNotifyFunc callback_func,
                            void *callback_func_user_data);
void exynosLayerMngDelEvent(EXYNOSLayerMngClientID lyr_client_id,
                            EXYNOSLayerMngEventType event_type,
                            LYRMNotifyFunc callback_func);
Bool exynosLayerMngSet(EXYNOSLayerMngClientID lyr_client_id, int offset_x,
                       int offset_y, xRectangle *src, xRectangle *dst,
                       DrawablePtr pDraw, EXYNOSVideoBuf * vbuf,
                       EXYNOSLayerOutput output, EXYNOSLayerPos lpos,
                       void *end_func, void *data);
int exynosLayerMngGetListOfAccessablePos(EXYNOSLayerMngClientID lyr_client_id,
                                         EXYNOSLayerOutput output,
                                         EXYNOSLayerPos * p_lpos);
void exynosLayerMngRelease(EXYNOSLayerMngClientID lyr_client_id,
                           EXYNOSLayerOutput output, EXYNOSLayerPos lpos);
Bool exynosLayerMngReservation(EXYNOSLayerMngClientID lyr_client_id,
                               EXYNOSLayerOutput output, EXYNOSLayerPos lpos);
Bool exynosLayerMngCheckFreePos(EXYNOSLayerOutput output, EXYNOSLayerPos lpos);
Bool exynosLayerMngSwapPos(EXYNOSLayerMngClientID lyr_client_id1,
                           EXYNOSLayerOutput output1, EXYNOSLayerPos lpos1,
                           EXYNOSLayerMngClientID lyr_client_id2,
                           EXYNOSLayerOutput output2, EXYNOSLayerPos lpos2);
int exynosLayerMngGetListOfOwnedPos(EXYNOSLayerMngClientID lyr_client_id,
                                    EXYNOSLayerOutput output,
                                    EXYNOSLayerPos * p_lpos);
Bool exynosLayerMngClearQueue(EXYNOSLayerMngClientID lyr_client_id,
                              EXYNOSLayerOutput output, EXYNOSLayerPos lpos);
/* Temporary solution */
EXYNOSLayerPtr exynosLayerMngTempGetHWLayer(ScrnInfoPtr pScrn,
                                            EXYNOSLayerOutput output,
                                            EXYNOSLayerPos lpos);
Bool exynosLayerMngTempDestroyHWLayer(EXYNOSLayerPtr hw_layer);
#endif
#endif                          // EXYNOS_LAYER_MANAGER_H
