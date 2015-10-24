#ifndef EXYNOS_DUMMY_H
#define EXYNOS_DUMMY_H

#include "exynos.h"

#ifndef RR_Rotate_All
#define RR_Rotate_All  (RR_Rotate_0|RR_Rotate_90|RR_Rotate_180|RR_Rotate_270)
#endif                          //RR_Rotate_All
#ifndef RR_Reflect_All
#define RR_Reflect_All (RR_Reflect_X|RR_Reflect_Y)
#endif                          //RR_Reflect_All
#ifdef NO_CRTC_MODE
Bool exynosDummyOutputInit(ScrnInfoPtr pScrn, EXYNOSModePtr pExynosMode,
                           Bool late);
xf86CrtcPtr exynosDummyCrtcInit(ScrnInfoPtr pScrn, EXYNOSModePtr pExynosMode);
#endif
#endif                          // EXYNOS_DUMMY_H
