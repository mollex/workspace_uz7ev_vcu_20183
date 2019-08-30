/******************************************************************************
*
* Copyright (C) 2018 Allegro DVT2.  All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* Use of the Software is limited solely to applications:
* (a) running on a Xilinx device, or
* (b) that interact with a Xilinx device through a bus or interconnect.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
* XILINX OR ALLEGRO DVT2 BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
* WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
* OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*
* Except as contained in this notice, the name of  Xilinx shall not be used
* in advertising or otherwise to promote the sale, use or other dealings in
* this Software without prior written authorization from Xilinx.
*
*
* Except as contained in this notice, the name of Allegro DVT2 shall not be used
* in advertising or otherwise to promote the sale, use or other dealings in
* this Software without prior written authorization from Allegro DVT2.
*
******************************************************************************/

/**************************************************************************//*!
   \addtogroup lib_decode_hls
   @{
   \file
 *****************************************************************************/
#pragma once

#include "lib_rtos/types.h"

#include "lib_common_dec/DecBuffers.h"
#include "lib_common_dec/DecInfo.h"
#include "InternalError.h"

typedef struct AL_s_TDecoder AL_TDecoder;

typedef struct
{
  void (* pfnDecoderDestroy)(AL_TDecoder* pDec);
  void (* pfnSetParam)(AL_TDecoder* pDec, bool bConceal, bool bUseBoard, int iFrmID, int iNumFrm, bool bForceCleanBuffer);
  bool (* pfnPushBuffer)(AL_TDecoder* pDec, AL_TBuffer* pBuf, size_t uSize);
  void (* pfnFlush)(AL_TDecoder* pDec);
  void (* pfnPutDisplayPicture)(AL_TDecoder* pDec, AL_TBuffer* pDisplay);
  int (* pfnGetMaxBD)(AL_TDecoder* pDec);
  AL_ERR (* pfnGetLastError)(AL_TDecoder* pDec);
  AL_ERR (* pfnGetFrameError)(AL_TDecoder* pDec, AL_TBuffer* pBuf);
  bool (* pfnPreallocateBuffers)(AL_TDecoder* pDec);

  // only for the feeders
  UNIT_ERROR (* pfnTryDecodeOneUnit)(AL_TDecoder* pDec, TCircBuffer* pBufStream);
  void (* pfnInternalFlush)(AL_TDecoder* pDec);
  int (* pfnGetStrOffset)(AL_TDecoder* pDec);
  void (* pfnFlushInput)(AL_TDecoder* pDec);
}AL_TDecoderVtable;

typedef struct AL_s_TDecoder
{
  const AL_TDecoderVtable* vtable;
}AL_TDecoder;

/*@}*/

