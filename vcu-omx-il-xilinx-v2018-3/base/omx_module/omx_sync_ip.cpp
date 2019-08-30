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

#include "omx_sync_ip.h"
#include <cassert>
#include "DummySyncDriver.h"

extern "C"
{
#include <lib_common/BufferSrcMeta.h>
#include <lib_common/BufferStreamMeta.h>
#include <lib_common/StreamBuffer.h>

#include <lib_common_enc/IpEncFourCC.h>
#include <lib_common_enc/EncBuffers.h>

#include <lib_fpga/DmaAllocLinux.h>
#include <lib_common/HardwareDriver.h>
}

#include "base/omx_checker/omx_checker.h"
#include "base/omx_mediatype/omx_convert_module_soft_enc.h"
#include "base/omx_mediatype/omx_convert_module_soft.h"
#include "base/omx_utils/round.h"
#include "base/omx_module/omx_module_structs.h"

using namespace std;

static char const* syncDevice = "/dev/xvsfsync0";
static constexpr bool usingDummy = false;

AL_TDriver* getDriver()
{
  if(usingDummy)
    return AL_InitDummyDriver(true, 4);

  return AL_GetHardwareDriver();
}

OMXSyncIp::OMXSyncIp(shared_ptr<MediatypeInterface> media, shared_ptr<AL_TAllocator> allocator) : media(media), allocator(allocator), syncIp(getDriver(), syncDevice), sync{&syncIp, syncIp.getFreeChannel()}
{
  assert(media);
  assert(allocator);
}

OMXSyncIp::~OMXSyncIp() = default;

static AL_TMetaData* CreateSourceMeta(shared_ptr<MediatypeInterface> media)
{
  Format format {};
  Resolution resolution {};
  media->Get(SETTINGS_INDEX_RESOLUTION, &resolution);
  media->Get(SETTINGS_INDEX_FORMAT, &format);
  auto picFormat = AL_EncGetSrcPicFormat(ConvertModuleToSoftChroma(format.color), static_cast<uint8_t>(format.bitdepth), AL_FB_RASTER, false);
  auto fourCC = AL_EncGetSrcFourCC(picFormat);
  auto stride = resolution.stride.widthStride;
  auto sliceHeight = resolution.stride.heightStride;
  AL_TPitches const pitches = { stride, stride };
  AL_TOffsetYC const offsetYC = { 0, stride * sliceHeight };
  return (AL_TMetaData*)(AL_SrcMetaData_Create({ resolution.width, resolution.height }, pitches, offsetYC, fourCC));
}

static bool CreateAndAttachSourceMeta(AL_TBuffer* buf, shared_ptr<MediatypeInterface> media)
{
  auto meta = CreateSourceMeta(media);

  if(!meta)
    return false;

  if(!AL_Buffer_AddMetaData(buf, meta))
  {
    meta->MetaDestroy(meta);
    return false;
  }
  return true;
}

AL_TBuffer* CreateBuffer(AL_TLinuxDmaAllocator* allocator, int fd, int size)
{
  if(fd < 0)
    throw invalid_argument("fd");

  auto dmaHandle = AL_LinuxDmaAllocator_ImportFromFd(allocator, fd);

  if(!dmaHandle)
  {
    fprintf(stderr, "SyncIp: Failed to import fd : %i\n", fd);
    return nullptr;
  }

  return AL_Buffer_Create((AL_TAllocator*)allocator, dmaHandle, size, AL_Buffer_Destroy);
}

void OMXSyncIp::addBuffer(BufferHandleInterface* handle)
{
  if(!handle)
  {
    sync.addBuffer(nullptr);
    return;
  }

  int fd = static_cast<int>((intptr_t)handle->data);
  AL_TBuffer* buf = CreateBuffer((AL_TLinuxDmaAllocator*)allocator.get(), fd, handle->payload);

  if(!buf)
    throw runtime_error("SyncIp: Couldn't get the buffer");

  AL_Buffer_Ref(buf);
  bool attached = CreateAndAttachSourceMeta(buf, media);
  assert(attached);
  sync.addBuffer(buf);
  AL_Buffer_Unref(buf);
}

void OMXSyncIp::enable()
{
  sync.enable();
}

