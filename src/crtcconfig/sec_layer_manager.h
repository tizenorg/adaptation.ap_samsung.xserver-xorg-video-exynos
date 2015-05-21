#ifndef SEC_LAYER_MANAGER_H
#define SEC_LAYER_MANAGER_H
#ifdef LAYER_MANAGER
#include <stdint.h>
#include <X11/Xmd.h>
#include <xorg-server.h>
#include <xf86str.h>
#include <sec_layer.h>
/* Private struct */
typedef CARD32 SECLayerMngClientID;
#define LYR_ERROR_ID 0

typedef enum
{
    EVENT_LAYER_SHOW = 0,
    EVENT_LAYER_ANNEX,
    EVENT_LAYER_RELEASE,
    EVENT_LAYER_HIDE,
    EVENT_LAYER_BUFSET,
    EVENT_LAYER_FREE_COUNTER,
    EVENT_LAYER_MAX
} SECLayerMngEventType;

typedef enum
{
    PLANE_LOWER2  = 0,
    PLANE_LOWER1  = 1,
    PLANE_DEFAULT = 2,
    PLANE_UPPER   = 3,
    PLANE_MAX     = 4
} SECLayerMngPlanePos;

typedef union
{
    struct
    {
        void * callback_data;
        uint32_t tv_sec;
        uint32_t tv_usec;
        uint32_t sequence;
    } hardware_callback;
    struct
    {
        SECLayerOutput output;
        SECLayerPos lpos;
        SECLayerMngClientID lyr_client_id;
    } annex_callback;
    struct
    {
        SECLayerOutput output;
        SECLayerPos lpos;
        SECLayerMngClientID lyr_client_id;
    } release_callback;
    struct
    {
        int new_count;
    } counter_callback;
} SECLayerMngEventCallbackDataRec, *SECLayerMngEventCallbackDataPtr;

typedef void (*LYRMNotifyFunc) (void* user_data, SECLayerMngEventCallbackDataPtr callback_data);

SECLayerMngClientID  secLayerMngRegisterClient(ScrnInfoPtr pScrn, char * p_client_name, int priority);
void   secLayerMngUnRegisterClient(SECLayerMngClientID lyr_client_id);
Bool secLayerMngInit (ScrnInfoPtr pScrn);
void secLayerMngDeInit (ScrnInfoPtr pScrn);
void secLayerMngAddEvent (SECLayerMngClientID lyr_client_id, SECLayerMngEventType event_type,
                          LYRMNotifyFunc callback_func, void *callback_func_user_data);
void secLayerMngDelEvent (SECLayerMngClientID lyr_client_id, SECLayerMngEventType event_type,
                          LYRMNotifyFunc callback_func);
Bool secLayerMngSet(SECLayerMngClientID lyr_client_id, int offset_x, int offset_y,
                    xRectangle *src, xRectangle *dst, DrawablePtr pDraw, SECVideoBuf *vbuf,
                    SECLayerOutput output, SECLayerPos lpos,  void * end_func, void * data);
int secLayerMngGetListOfAccessablePos(SECLayerMngClientID lyr_client_id, SECLayerOutput output, SECLayerPos* p_lpos);
void secLayerMngRelease (SECLayerMngClientID lyr_client_id, SECLayerOutput output, SECLayerPos lpos);
Bool secLayerMngReservation(SECLayerMngClientID lyr_client_id, SECLayerOutput output, SECLayerPos lpos);
Bool secLayerMngCheckFreePos(SECLayerOutput output, SECLayerPos lpos);
Bool secLayerMngSwapPos(SECLayerMngClientID lyr_client_id1, SECLayerOutput output1, SECLayerPos lpos1,
                        SECLayerMngClientID lyr_client_id2, SECLayerOutput output2, SECLayerPos lpos2);
int secLayerMngGetListOfOwnedPos(SECLayerMngClientID lyr_client_id, SECLayerOutput output, SECLayerPos *p_lpos);
Bool secLayerMngClearQueue(SECLayerMngClientID lyr_client_id, SECLayerOutput output, SECLayerPos lpos);
/* Temporary solution */
SECLayerPtr secLayerMngTempGetHWLayer(ScrnInfoPtr pScrn, SECLayerOutput output, SECLayerPos lpos);
Bool secLayerMngTempDestroyHWLayer(SECLayerPtr hw_layer);
#endif
#endif // SEC_LAYER_MANAGER_H

