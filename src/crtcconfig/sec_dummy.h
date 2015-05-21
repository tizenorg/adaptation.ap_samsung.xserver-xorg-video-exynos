#ifndef SEC_DUMMY_H
#define SEC_DUMMY_H

#include "sec.h"

#ifndef RR_Rotate_All
#define RR_Rotate_All  (RR_Rotate_0|RR_Rotate_90|RR_Rotate_180|RR_Rotate_270)
#endif //RR_Rotate_All
#ifndef RR_Reflect_All
#define RR_Reflect_All (RR_Reflect_X|RR_Reflect_Y)
#endif //RR_Reflect_All
#ifdef NO_CRTC_MODE
Bool           secDummyOutputInit  (ScrnInfoPtr pScrn, SECModePtr pSecMode, Bool late);
xf86CrtcPtr    secDummyCrtcInit (ScrnInfoPtr pScrn, SECModePtr pSecMode);
#endif
#endif // SEC_DUMMY_H
