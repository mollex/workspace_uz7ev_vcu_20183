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

#include "omx_module_dec.h"
#include <cmath>
#include <cassert>
#include <unistd.h> // close fd
#include <algorithm>

extern "C"
{
#include <lib_common/BufferSrcMeta.h>
#include <lib_fpga/DmaAllocLinux.h>
#include <lib_common_dec/IpDecFourCC.h>
}

#include "base/omx_mediatype/omx_convert_module_soft.h"
#include "base/omx_mediatype/omx_convert_module_soft_dec.h"
#include "base/omx_utils/round.h"

using namespace std;

DecModule::DecModule(shared_ptr<DecMediatypeInterface> media, shared_ptr<DecDevice> device, shared_ptr<AL_TAllocator> allocator) :
  media(media),
  device(device),
  allocator(allocator)
{
  assert(this->media);
  assert(this->device);
  assert(this->allocator);
  decoder = nullptr;
  isCreated = false;
  ResetRequirements();
}

DecModule::~DecModule() = default;

void DecModule::ResetRequirements()
{
  media->Reset();
}

static int RawAllocationSize(int stride, int sliceHeight, AL_EChromaMode eChromaMode)
{
  auto IP_WIDTH_ALIGNMENT = 64;
  auto IP_HEIGHT_ALIGNMENT = 64;
  assert(stride % IP_WIDTH_ALIGNMENT == 0); // IP requirements
  assert(sliceHeight % IP_HEIGHT_ALIGNMENT == 0); // IP requirements
  auto size = stride * sliceHeight;
  switch(eChromaMode)
  {
  case CHROMA_MONO: return size;
  case CHROMA_4_2_0: return (3 * size) / 2;
  case CHROMA_4_2_2: return 2 * size;
  default: return -1;
  }
}

BufferRequirements DecModule::GetBufferRequirements() const
{
  auto streamSettings = media->settings.tStream;
  BufferRequirements b;
  BufferCounts bufferCounts;
  media->Get(SETTINGS_INDEX_BUFFER_COUNTS, &bufferCounts);
  auto& input = b.input;
  input.min = bufferCounts.input;
  input.size = AL_GetMaxNalSize(media->settings.eCodec, streamSettings.tDim, streamSettings.eChroma, streamSettings.iBitDepth, streamSettings.iLevel, streamSettings.iProfileIdc);
  input.bytesAlignment = device->GetBufferBytesAlignments().input;
  input.contiguous = device->GetBufferContiguities().input;

  auto& output = b.output;
  output.min = bufferCounts.output;
  output.min += 1; // for eos
  output.size = RawAllocationSize(media->stride, media->sliceHeight, streamSettings.eChroma);
  output.bytesAlignment = device->GetBufferBytesAlignments().output;
  output.contiguous = device->GetBufferContiguities().output;

  return b;
}

map<int, string> MapToStringDecodeError =
{
  { AL_ERR_RESOLUTION_CHANGE, "decoder: doesn't support resolution change" },
  { AL_ERR_NO_MEMORY, "decoder: memory allocation failure (firmware or ctrlsw)" },
  { AL_ERR_CHAN_CREATION_NO_CHANNEL_AVAILABLE, "decoder: no channel available on the hardware" },
  { AL_ERR_CHAN_CREATION_RESOURCE_UNAVAILABLE, "decoder: hardware doesn't have enough resources" },
  { AL_ERR_CHAN_CREATION_NOT_ENOUGH_CORES, "decoder: hardware doesn't have enough resources (fragmentation)" },
  { AL_ERR_REQUEST_MALFORMED, "decoder: request to hardware was malformed" },
  { AL_WARN_CONCEAL_DETECT, "decoder, concealment" },
};

string ToStringDecodeError(int error)
{
  string str_error = "";
  try
  {
    str_error = MapToStringDecodeError.at(error);
  }
  catch(out_of_range& e)
  {
    str_error = "unknown error";
  }
  return str_error;
}

static ErrorType ToModuleError(int errorCode)
{
  switch(errorCode)
  {
  case AL_SUCCESS:
    return SUCCESS;
  case AL_ERR_CHAN_CREATION_NO_CHANNEL_AVAILABLE:
    return ERROR_CHAN_CREATION_NO_CHANNEL_AVAILABLE;
  case AL_ERR_CHAN_CREATION_RESOURCE_UNAVAILABLE:
    return ERROR_CHAN_CREATION_RESOURCE_UNAVAILABLE;
  case AL_ERR_CHAN_CREATION_NOT_ENOUGH_CORES:
    return ERROR_CHAN_CREATION_RESOURCE_FRAGMENTED;
  case AL_ERR_REQUEST_MALFORMED: // fallthrough
  case AL_ERR_CMD_NOT_ALLOWED: // fallthrough
  case AL_ERR_INVALID_CMD_VALUE:
    return ERROR_BAD_PARAMETER;
  case AL_ERR_NO_MEMORY:
    return ERROR_NO_MEMORY;
  default:
    return ERROR_UNDEFINED;
  }
}

void DecModule::EndDecoding(AL_TBuffer* decodedFrame)
{
  if(!decodedFrame)
  {
    auto error = AL_Decoder_GetLastError(decoder);

    fprintf(stderr, "/!\\ %s (%d)\n", ToStringDecodeError(error).c_str(), error);

    if(error & AL_ERROR)
      callbacks.event(CALLBACK_EVENT_ERROR, (void*)ToModuleError(error));

    return;
  }

  auto rhandleOut = handlesOut.Get(decodedFrame);
  assert(rhandleOut);

  callbacks.associate(nullptr, rhandleOut);
}

void DecModule::ReleaseBufs(AL_TBuffer* frame)
{
  auto rhandleOut = handlesOut.Pop(frame);
  dpb.Remove(rhandleOut->data);
  callbacks.release(false, rhandleOut);
  AL_Buffer_Unref(frame);
  return;
}

void DecModule::CopyIfRequired(AL_TBuffer* frameToDisplay, int size)
{
  if(shouldBeCopied.Exist(frameToDisplay))
  {
    auto buffer = shouldBeCopied.Get(frameToDisplay);
    copy(AL_Buffer_GetData(frameToDisplay), AL_Buffer_GetData(frameToDisplay) + size, buffer);
  }
}

void DecModule::Display(AL_TBuffer* frameToDisplay, AL_TInfoDecode* info)
{
  auto isRelease = (frameToDisplay && info == nullptr);

  if(isRelease)
    return ReleaseBufs(frameToDisplay);

  auto isEOS = (frameToDisplay == nullptr && info == nullptr);

  if(isEOS)
  {
    auto rhandleOut = handlesOut.Pop(eosHandles.output);
    dpb.Remove(rhandleOut->data);

    auto rhandleIn = handlesIn.Get(eosHandles.input);
    auto eosIsSent = rhandleIn != nullptr;

    if(eosIsSent)
    {
      callbacks.associate(rhandleIn, rhandleOut);
      AL_Buffer_Unref(eosHandles.input);
      eosHandles.input = nullptr;
    }

    currentDisplayPictureType = -1;

    rhandleOut->offset = 0;
    rhandleOut->payload = 0;
    callbacks.filled(rhandleOut, rhandleOut->offset, rhandleOut->payload);

    AL_Buffer_Unref(eosHandles.output);
    eosHandles.output = nullptr;

    return;
  }

  auto size = GetBufferRequirements().output.size;
  CopyIfRequired(frameToDisplay, size);
  currentDisplayPictureType = info->ePicStruct;
  auto rhandleOut = handlesOut.Pop(frameToDisplay);
  rhandleOut->offset = 0;
  rhandleOut->payload = size;
  callbacks.filled(rhandleOut, rhandleOut->offset, rhandleOut->payload);
  currentDisplayPictureType = -1;
}

void DecModule::ResolutionFound(int bufferNumber, int bufferSize, AL_TStreamSettings const& settings, AL_TCropInfo const& crop)
{
  (void)bufferNumber, (void)bufferSize, (void)crop;

  media->settings.tStream = settings;

  Stride strideAlignment;
  media->Get(SETTINGS_INDEX_STRIDE_ALIGNMENT, &strideAlignment);

  media->stride = (int)RoundUp(AL_Decoder_GetMinPitch(settings.tDim.iWidth, settings.iBitDepth, media->settings.eFBStorageMode), strideAlignment.widthStride);
  media->sliceHeight = (int)RoundUp(AL_Decoder_GetMinStrideHeight(settings.tDim.iHeight), strideAlignment.heightStride);

  callbacks.event(CALLBACK_EVENT_RESOLUTION_CHANGE, nullptr);
}

ErrorType DecModule::CreateDecoder(bool shouldPrealloc)
{
  if(decoder)
  {
    fprintf(stderr, "Decoder is ALREADY created\n");
    return ERROR_UNDEFINED;
  }

  channel = device->Init(*allocator.get());
  AL_TDecCallBacks decCallbacks {};
  decCallbacks.endDecodingCB = { RedirectionEndDecoding, this };
  decCallbacks.displayCB = { RedirectionDisplay, this };
  decCallbacks.resolutionFoundCB = { RedirectionResolutionFound, this };

  auto errorCode = AL_Decoder_Create(&decoder, channel, allocator.get(), &media->settings, &decCallbacks);

  if(errorCode != AL_SUCCESS)
  {
    fprintf(stderr, "Failed to create Decoder: %d\n", errorCode);
    return ToModuleError(errorCode);
  }

  if(shouldPrealloc)
  {
    if(!AL_Decoder_PreallocateBuffers(decoder))
    {
      auto errorCode = AL_Decoder_GetLastError(decoder);
      DestroyDecoder();
      return ToModuleError(errorCode);
    }
  }

  return SUCCESS;
}

bool DecModule::DestroyDecoder()
{
  if(!decoder)
  {
    fprintf(stderr, "Decoder isn't created\n");
    return false;
  }

  AL_Decoder_Destroy(decoder);
  device->Deinit();
  decoder = nullptr;
  channel = nullptr;

  return true;
}

bool DecModule::CheckParam()
{
  // TODO check some settings ?
  return true;
}

bool DecModule::Create()
{
  if(decoder)
  {
    fprintf(stderr, "Decoder should NOT be created\n");
    return false;
  }
  isCreated = true;

  return true;
}

void DecModule::Destroy()
{
  assert(!decoder && "Decoder should ALREADY be destroyed");
  isCreated = false;
}

void DecModule::Free(void* buffer)
{
  if(!buffer)
    return;

  if(dpb.Exist((char*)buffer))
  {
    auto handle = dpb.Pop((char*)buffer);
    assert(!handlesOut.Exist(handle));
    AL_Buffer_Unref(handle);
  }

  if(allocated.Exist(buffer))
  {
    auto handle = allocated.Pop(buffer);
    AL_Allocator_Free(allocator.get(), handle);
  }
}

void DecModule::FreeDMA(int fd)
{
  if(fd < 0)
    return;

  auto buffer = (char*)((intptr_t)fd);

  if(dpb.Exist(buffer))
  {
    auto handle = dpb.Pop(buffer);
    assert(!handlesOut.Exist(handle));
    AL_Buffer_Unref(handle);
  }

  if(allocatedDMA.Exist(fd))
  {
    auto handle = allocatedDMA.Pop(fd);
    AL_Allocator_Free(allocator.get(), handle);
    close(fd);
  }
}

void* DecModule::Allocate(size_t size)
{
  auto handle = AL_Allocator_Alloc(allocator.get(), size);

  if(!handle)
  {
    fprintf(stderr, "No more memory\n");
    return nullptr;
  }

  auto addr = AL_Allocator_GetVirtualAddr(allocator.get(), handle);
  assert(addr);
  allocated.Add(addr, handle);
  return addr;
}

int DecModule::AllocateDMA(int size)
{
  auto handle = AL_Allocator_Alloc(allocator.get(), size);

  if(!handle)
  {
    fprintf(stderr, "No more memory\n");
    return -1;
  }

  auto fd = AL_LinuxDmaAllocator_GetFd((AL_TLinuxDmaAllocator*)allocator.get(), handle);
  allocatedDMA.Add(fd, handle);
  return fd;
}

static void StubCallbackEvent(CallbackEventType, void*)
{
}

bool DecModule::SetCallbacks(Callbacks callbacks)
{
  if(!callbacks.emptied || !callbacks.associate || !callbacks.filled || !callbacks.release)
    return false;

  if(!callbacks.event)
    callbacks.event = &StubCallbackEvent;

  this->callbacks = callbacks;

  return true;
}

void DecModule::InputBufferDestroy(AL_TBuffer* input)
{
  auto rhandleIn = handlesIn.Pop(input);

  AL_Buffer_Destroy(input);

  rhandleIn->offset = 0;
  rhandleIn->payload = 0;
  callbacks.emptied(rhandleIn);
}

AL_TBuffer* DecModule::CreateInputBuffer(char* buffer, int size)
{
  AL_TBuffer* input = nullptr;

  BufferHandles bufferHandles {};
  media->Get(SETTINGS_INDEX_BUFFER_HANDLES, &bufferHandles);

  if(bufferHandles.input == BufferHandleType::BUFFER_HANDLE_FD)
  {
    auto fd = static_cast<int>((intptr_t)buffer);

    if(fd < 0)
      throw invalid_argument("fd");

    auto dmaHandle = AL_LinuxDmaAllocator_ImportFromFd((AL_TLinuxDmaAllocator*)allocator.get(), fd);

    if(!dmaHandle)
    {
      fprintf(stderr, "Failed to import fd : %i\n", fd);
      return nullptr;
    }

    input = AL_Buffer_Create(allocator.get(), dmaHandle, size, RedirectionInputBufferDestroy);
  }
  else
  {
    if(allocated.Exist(buffer))
      input = AL_Buffer_Create(allocator.get(), allocated.Get(buffer), size, RedirectionInputBufferDestroy);
    else
      input = AL_Buffer_WrapData((uint8_t*)buffer, size, RedirectionInputBufferDestroy);
  }

  if(input == nullptr)
    return nullptr;

  AL_Buffer_SetUserData(input, this);
  AL_Buffer_Ref(input);

  return input;
}

bool DecModule::Empty(BufferHandleInterface* handle)
{
  if(!decoder)
    return false;

  auto buffer = handle->data;
  AL_TBuffer* input = CreateInputBuffer(buffer, handle->payload);

  if(!input)
    return false;

  handlesIn.Add(input, handle);

  auto eos = (handle->payload == 0);

  if(eos)
  {
    eosHandles.input = input;
    AL_Decoder_Flush(decoder);
    return true;
  }

  auto pushed = AL_Decoder_PushBuffer(decoder, input, handle->payload);
  AL_Buffer_Unref(input);

  return pushed;
}

static AL_TMetaData* CreateSourceMeta(AL_TStreamSettings const& streamSettings, Resolution resolution)
{
  auto picFormat = AL_GetDecPicFormat(streamSettings.eChroma, static_cast<uint8_t>(streamSettings.iBitDepth), AL_FB_RASTER, false);
  auto fourCC = AL_GetDecFourCC(picFormat);
  auto stride = resolution.stride.widthStride;
  auto sliceHeight = resolution.stride.heightStride;
  AL_TPitches const pitches = { stride, stride };
  AL_TOffsetYC const offsetYC = { 0, stride * sliceHeight };
  return (AL_TMetaData*)(AL_SrcMetaData_Create({ resolution.width, resolution.height }, pitches, offsetYC, fourCC));
}

void DecModule::OutputBufferDestroy(AL_TBuffer* output)
{
  output->hBuf = NULL;
  AL_Buffer_Destroy(output);
}

void DecModule::OutputDmaBufferDestroy(AL_TBuffer* output)
{
  AL_Buffer_Destroy(output);
}

void DecModule::OutputBufferDestroyAndFree(AL_TBuffer* output)
{
  shouldBeCopied.Pop(output);

  AL_Buffer_Destroy(output);
}

AL_TBuffer* DecModule::CreateOutputBuffer(char* buffer, int size)
{
  Resolution resolution {};
  media->Get(SETTINGS_INDEX_RESOLUTION, &resolution);
  auto sourceMeta = CreateSourceMeta(media->settings.tStream, resolution);

  if(!sourceMeta)
    return nullptr;

  AL_TBuffer* output = nullptr;

  BufferHandles bufferHandles {};
  media->Get(SETTINGS_INDEX_BUFFER_HANDLES, &bufferHandles);

  if(bufferHandles.output == BufferHandleType::BUFFER_HANDLE_FD)
  {
    auto fd = static_cast<int>((intptr_t)buffer);

    if(fd < 0)
      throw invalid_argument("fd");

    auto dmaHandle = AL_LinuxDmaAllocator_ImportFromFd((AL_TLinuxDmaAllocator*)allocator.get(), fd);

    if(!dmaHandle)
    {
      fprintf(stderr, "Failed to import fd : %i\n", fd);
      ((AL_TMetaData*)sourceMeta)->MetaDestroy((AL_TMetaData*)sourceMeta);
      return nullptr;
    }

    output = AL_Buffer_Create(allocator.get(), dmaHandle, size, RedirectionOutputDmaBufferDestroy);
  }
  else
  {
    if(allocated.Exist(buffer))
      output = AL_Buffer_Create(allocator.get(), allocated.Get(buffer), size, RedirectionOutputBufferDestroy);
    else
    {
      output = AL_Buffer_Create_And_Allocate(allocator.get(), size, RedirectionOutputBufferDestroyAndFree);

      if(output)
        shouldBeCopied.Add(output, buffer);
    }
  }

  if(output == nullptr)
  {
    ((AL_TMetaData*)sourceMeta)->MetaDestroy((AL_TMetaData*)sourceMeta);
    return nullptr;
  }

  auto attachedSourceMeta = AL_Buffer_AddMetaData(output, (AL_TMetaData*)sourceMeta);
  assert(attachedSourceMeta);

  AL_Buffer_SetUserData(output, this);
  dpb.Add(buffer, output);
  AL_Buffer_Ref(output);

  return output;
}

bool DecModule::Fill(BufferHandleInterface* handle)
{
  if(!decoder)
    return false;

  auto buffer = handle->data;

  auto output = dpb.Exist(buffer) ? dpb.Get(buffer) : CreateOutputBuffer(buffer, handle->size);

  if(!output)
    return false;

  handlesOut.Add(output, handle);

  if(!eosHandles.output)
  {
    eosHandles.output = output;
    return true;
  }

  AL_Decoder_PutDisplayPicture(decoder, output);
  return true;
}

ErrorType DecModule::Run(bool shouldPrealloc)
{
  if(decoder)
  {
    fprintf(stderr, "You can't call Run twice\n");
    return ERROR_UNDEFINED;
  }

  if(!isCreated)
  {
    fprintf(stderr, "You should call Create before Run\n");
    return ERROR_UNDEFINED;
  }

  eosHandles.input = nullptr;
  eosHandles.output = nullptr;

  return CreateDecoder(shouldPrealloc);
}

bool DecModule::Flush()
{
  if(!decoder)
  {
    FlushEosHandles();
    return false;
  }

  Stop();
  return Run(true) == SUCCESS;
}

void DecModule::FlushEosHandles()
{
  auto rhandleOut = handlesOut.Get(eosHandles.output);

  if(rhandleOut)
  {
    dpb.Remove(rhandleOut->data);
    handlesOut.Remove(eosHandles.output);

    callbacks.release(false, rhandleOut);

    AL_Buffer_Unref(eosHandles.output);
    eosHandles.output = nullptr;
  }
}

void DecModule::ReleaseAllBuffers()
{
  DestroyDecoder();
  FlushEosHandles();
}

void DecModule::Stop()
{
  if(!decoder)
    return;

  ReleaseAllBuffers();
}

ErrorType DecModule::SetDynamic(std::string index, void const* param)
{
  (void)index, (void)param;
  return ERROR_NOT_IMPLEMENTED;
}

ErrorType DecModule::GetDynamic(std::string index, void* param)
{
  (void)index, (void)param;
  return ERROR_NOT_IMPLEMENTED;
}

int DecModule::GetDisplayPictureType() const
{
  return currentDisplayPictureType;
}

