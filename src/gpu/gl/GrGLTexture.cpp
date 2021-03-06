/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "GrGLTexture.h"
#include "GrGpuGL.h"

SK_DEFINE_INST_COUNT(GrGLTexID)

#define GPUGL static_cast<GrGpuGL*>(getGpu())

#define GL_CALL(X) GR_GL_CALL(GPUGL->glInterface(), X)

void GrGLTexture::init(GrGpuGL* gpu,
                       const Desc& textureDesc,
                       const GrGLRenderTarget::Desc* rtDesc) {

    GrAssert(0 != textureDesc.fTextureID);

    fTexParams.invalidate();
    fTexParamsTimestamp = GrGpu::kExpiredTimestamp;
    fTexIDObj           = SkNEW_ARGS(GrGLTexID,
                                     (GPUGL->glInterface(),
                                      textureDesc.fTextureID,
                                      textureDesc.fOwnsID));
    fOrientation        = textureDesc.fOrientation;

    if (NULL != rtDesc) {
        // we render to the top left
        GrGLIRect vp;
        vp.fLeft   = 0;
        vp.fWidth  = textureDesc.fWidth;
        vp.fBottom = 0;
        vp.fHeight = textureDesc.fHeight;

        fRenderTarget = SkNEW_ARGS(GrGLRenderTarget,
                                   (gpu, *rtDesc, vp, fTexIDObj, this));
    }
}

GrGLTexture::GrGLTexture(GrGpuGL* gpu,
                         const Desc& textureDesc)
    : INHERITED(gpu, textureDesc) {
    this->init(gpu, textureDesc, NULL);
}

GrGLTexture::GrGLTexture(GrGpuGL* gpu,
                         const Desc& textureDesc,
                         const GrGLRenderTarget::Desc& rtDesc)
    : INHERITED(gpu, textureDesc) {
    this->init(gpu, textureDesc, &rtDesc);
}

void GrGLTexture::onRelease() {
    INHERITED::onRelease();
    GPUGL->notifyTextureDelete(this);
    if (NULL != fTexIDObj) {
        fTexIDObj->unref();
        fTexIDObj = NULL;
    }
}

void GrGLTexture::onAbandon() {
    INHERITED::onAbandon();
    if (NULL != fTexIDObj) {
        fTexIDObj->abandon();
    }
}

intptr_t GrGLTexture::getTextureHandle() const {
    return fTexIDObj->id();
}

