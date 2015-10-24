/**************************************************************************

xserver-xorg-video-exynos

Copyright 2010 - 2011 Samsung Electronics co., Ltd. All Rights Reserved.

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
    
#include "exynos.h"
#include <malloc.h>
    
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <malloc.h>
    
#include "exynos.h"
#include "xace.h"
#include "xacestr.h"
    
#include <X11/extensions/dpmsconst.h>
#include "exynos_util.h"
#include "exynos_dpms.h"
#include "exynos_memory_flush.h"
extern CallbackListPtr DPMSCallback;
 static void 
_exynosMemoryDPMS(CallbackListPtr *list, pointer closure, pointer calldata) 
{
    SecDPMSPtr pDPMSInfo = (SecDPMSPtr) calldata;
    ScrnInfoPtr pScrn;

    
        //int mode = pDPMSInfo->mode;
        //int flags = pDPMSInfo->flags;
        if (!pDPMSInfo || !(pScrn = pDPMSInfo->pScrn))
         {
        XDBG_ERROR(MMEM, "[X11][%s] DPMS info or screen info is invalid !\n",
                    __FUNCTION__);
        return;
        }
     EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
     switch (DPMSPowerLevel)
         {
    case DPMSModeOn:
    case DPMSModeSuspend:
        break;
     case DPMSModeStandby:   //LCD on
        if (pExynos->isLcdOff == FALSE)
            break;
        break;
     case DPMSModeOff:       //LCD off
        if (pExynos->isLcdOff == TRUE)
            break;
         
            //stack and heap memory trim
            exynosMemoryStackTrim();
        malloc_trim(0);
         XDBG_DEBUG(MMEM, "[X11][%s] Memory flush done !\n", __FUNCTION__);
        break;
     default:
        return;
        }
}

 static void 
_exynosMemoryClientStateEvent(CallbackListPtr *list, pointer closure,
                              pointer calldata) 
{
    static int chance = 0;

    NewClientInfoRec * clientinfo = (NewClientInfoRec *) calldata;
    ClientPtr pClient = clientinfo->client;

     if (ClientStateGone == pClient->clientState)
         {
        int flush;

         
            //memory flush will be done for every third client gone
            chance++;
        flush = !(chance = chance % 3);
         if (flush)
             {
            
                //stack and heap memory trim
                exynosMemoryStackTrim();
            malloc_trim(0);
             XDBG_DEBUG(MMEM, "[X11][%s] Memory flush done !\n",
                          __FUNCTION__);
            }
        }
}

 Bool 
exynosMemoryInstallHooks(void) 
{
    int ret = TRUE;

    ret &=
        AddCallback(&ClientStateCallback, _exynosMemoryClientStateEvent, NULL);
    ret &= AddCallback(&DPMSCallback, _exynosMemoryDPMS, NULL);
     if (!ret)
         {
        XDBG_ERROR(MMEM,
                    "exynosMemoryInstallHooks: Failed to register one or more callbacks\n");
        return BadAlloc;
        }
     return Success;
}

  Bool 
exynosMemoryUninstallHooks(void) 
{
    DeleteCallback(&ClientStateCallback, _exynosMemoryClientStateEvent, NULL);
    DeleteCallback(&DPMSCallback, _exynosMemoryDPMS, NULL);
     return Success;
}


