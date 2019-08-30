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
   \addtogroup Buffers
   @{
   \file
******************************************************************************/
#pragma once

#include "lib_rtos/types.h"
#include "lib_common/MemDesc.h"
#include "lib_common/FourCC.h"
#include "lib_common/OffsetYC.h"

/*************************************************************************//*!
   \brief Frame buffer stored as IYUV planar format (also called I420)
   old interface. will soon be deprecated.
*****************************************************************************/
typedef struct t_BufferYuv
{
  TMemDesc tMD; /*!< Memory descriptor associated to the buffer */

  int iWidth; /*!< Width in pixel of the frame */
  int iHeight; /*!< Height in pixel of the frame */

  int iPitchY; /*!< offset in bytes between a Luma pixel and the Luma
                     pixel on the next line with same horizontal position*/
  int iPitchC; /*!< offset in bytes between a chroma pixel and the chroma
                     pixel on the next line with same horizontal position*/
  AL_TOffsetYC tOffsetYC; /*< offset for luma and chroma addresses */

  TFourCC tFourCC; /*!< FOURCC identifier */
}TBufferYuv;

/*************************************************************************//*!
   If the framebuffer is stored in raster, the pitch represents the number of bytes
   to go to the next line, so you get one line.
   If the framebuffer is stored in tiles, the pitch represents the number of bytes
   to go to the next line of tile. As a tile height is superior to one line,
   you have to skip multiple lines to go to the next line of tile.
   \param[in] eFrameBufferStorageMode how the framebuffer is stored in memory
   \return Number of lines in the pitch
*****************************************************************************/
int AL_GetNumLinesInPitch(AL_EFbStorageMode eFrameBufferStorageMode);

/****************************************************************************/
/* Useful for traces */
void AL_CleanupMemory(void* pDst, size_t uSize);
extern int AL_CLEAN_BUFFERS;

/*@}*/

