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

#if AL_ENABLE_TWOPASS
#include "lib_common/BufferLookAheadMeta.h"
#include "lib_rtos/lib_rtos.h"
#include <assert.h>

static bool LookAheadMeta_Destroy(AL_TMetaData* pMeta)
{
  AL_TLookAheadMetaData* pLookAheadMeta = (AL_TLookAheadMetaData*)pMeta;
  Rtos_Free(pLookAheadMeta);
  return true;
}

AL_TLookAheadMetaData* AL_LookAheadMetaData_Create()
{
  AL_TLookAheadMetaData* pMeta;

  pMeta = Rtos_Malloc(sizeof(*pMeta));

  if(!pMeta)
    return NULL;

  pMeta->tMeta.eType = AL_META_TYPE_LOOKAHEAD;
  pMeta->tMeta.MetaDestroy = LookAheadMeta_Destroy;

  AL_LookAheadMetaData_Reset(pMeta);

  return pMeta;
}

AL_TLookAheadMetaData* AL_LookAheadMetaData_Clone(AL_TLookAheadMetaData* pMeta)
{
  if(!pMeta)
    return NULL;

  AL_TLookAheadMetaData* pLookAheadMeta = AL_LookAheadMetaData_Create();

  if(!pLookAheadMeta)
    return NULL;
  AL_LookAheadMetaData_Copy(pMeta, pLookAheadMeta);
  return pLookAheadMeta;
}

void AL_LookAheadMetaData_Copy(AL_TLookAheadMetaData* pMetaSrc, AL_TLookAheadMetaData* pMetaDest)
{
  if(!pMetaSrc || !pMetaDest)
    return;

  pMetaDest->iPictureSize = pMetaSrc->iPictureSize;
  pMetaDest->iPercentIntra = pMetaSrc->iPercentIntra;
  pMetaDest->bNextSceneChange = pMetaSrc->bNextSceneChange;
  pMetaDest->iIPRatio = pMetaSrc->iIPRatio;
  pMetaDest->iPercentSkip = pMetaSrc->iPercentSkip;
  pMetaDest->iComplexity = pMetaSrc->iComplexity;
}

void AL_LookAheadMetaData_Reset(AL_TLookAheadMetaData* pMeta)
{
  pMeta->iPictureSize = -1;
  pMeta->iPercentIntra = -1;
  pMeta->bNextSceneChange = false;
  pMeta->iIPRatio = 1000;
  pMeta->iPercentSkip = -1;
  pMeta->iComplexity = 1000;
}

#endif

