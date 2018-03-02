/*
// Copyright (c) 2016 Amlogic
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
*/

#ifndef __AM_COMPOSER_H
#define __AM_COMPOSER_H

#include <gralloc_priv.h>
#include <framebuffer.h>
#include <HwcLayer.h>

namespace android {
namespace amlogic {

class IDisplayDevice;

class IComposeDevice {
public:
    IComposeDevice(IDisplayDevice& disp);
    virtual ~IComposeDevice();

public:

    virtual bool initialize(framebuffer_info_t* fbInfo);
    virtual void deinitialize();
    virtual int32_t startCompose(Vector< hwc2_layer_t > hwcLayers, int32_t *offset = 0, int32_t frameCount = 0) = 0;
    virtual const char* getName() const = 0;
    // virtual void setCurGlesFbSlot(uint32_t slot) = 0;
    virtual buffer_handle_t getBufHnd() = 0;
    virtual void mergeRetireFence(int32_t slot, int32_t retireFence) = 0;
    virtual void removeRetireFence(int32_t slot) = 0;
    virtual void setVideoOverlayLayerId(hwc2_layer_t layerId) = 0;
    virtual void fillRectangle(hwc_rect_t clipRect, uint32_t color, uint32_t offset, int shared_fd) = 0;

private:
    IDisplayDevice& mDisplayDevice;

    bool mInitialized;
};

} // namespace amlogic
} // namespace android


#endif /* __AM_COMPOSER_H */

