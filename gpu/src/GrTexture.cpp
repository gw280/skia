/*
    Copyright 2011 Google Inc.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

         http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
 */

#include "GrTexture.h"
#include "GrContext.h"

bool GrRenderTarget::readPixels(int left, int top, int width, int height,
                                GrPixelConfig config, void* buffer) {
    // go through context so that all necessary flushing occurs
    GrContext* context = this->getGpu()->getContext();
    GrAssert(NULL != context);
    return context->readRenderTargetPixels(this,
                                           left, top, 
                                           width, height,
                                           config, buffer);
}

GrTexture::~GrTexture() {
    // use this to set a break-point if needed
//    Gr_clz(3);
}

bool GrTexture::readPixels(int left, int top, int width, int height,
                           GrPixelConfig config, void* buffer) {
    // go through context so that all necessary flushing occurs
    GrContext* context = this->getGpu()->getContext();
    GrAssert(NULL != context);
    return context->readTexturePixels(this,
                                        left, top, 
                                        width, height,
                                        config, buffer);
}
