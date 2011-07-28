
/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */



#include "GrGLIndexBuffer.h"
#include "GrGpuGL.h"

#define GPUGL static_cast<GrGpuGL*>(getGpu())

GrGLIndexBuffer::GrGLIndexBuffer(GrGpuGL* gpu,
                                 GrGLuint id,
                                 size_t sizeInBytes,
                                 bool dynamic)
    : INHERITED(gpu, sizeInBytes, dynamic)
    , fBufferID(id)
    , fLockPtr(NULL) {

}

void GrGLIndexBuffer::onRelease() {
    // make sure we've not been abandoned
    if (fBufferID) {
        GPUGL->notifyIndexBufferDelete(this);
        GR_GL(DeleteBuffers(1, &fBufferID));
        fBufferID = 0;
    }
}

void GrGLIndexBuffer::onAbandon() {
    fBufferID = 0;
    fLockPtr = NULL;
}

void GrGLIndexBuffer::bind() const {
    GR_GL(BindBuffer(GR_GL_ELEMENT_ARRAY_BUFFER, fBufferID));
    GPUGL->notifyIndexBufferBind(this);
}

GrGLuint GrGLIndexBuffer::bufferID() const {
    return fBufferID;
}

void* GrGLIndexBuffer::lock() {
    GrAssert(fBufferID);
    GrAssert(!isLocked());
    if (GPUGL->supportsBufferLocking()) {
        this->bind();
        // Let driver know it can discard the old data
        GR_GL(BufferData(GR_GL_ELEMENT_ARRAY_BUFFER, this->sizeInBytes(), NULL,
                         this->dynamic() ? GR_GL_DYNAMIC_DRAW : GR_GL_STATIC_DRAW));
        fLockPtr = GR_GL(MapBuffer(GR_GL_ELEMENT_ARRAY_BUFFER, GR_GL_WRITE_ONLY));

        return fLockPtr;
    }
    return NULL;
}

void* GrGLIndexBuffer::lockPtr() const {
    return fLockPtr;
}

void GrGLIndexBuffer::unlock() {
    GrAssert(fBufferID);
    GrAssert(isLocked());
    GrAssert(GPUGL->supportsBufferLocking());

    this->bind();
    GR_GL(UnmapBuffer(GR_GL_ELEMENT_ARRAY_BUFFER));
    fLockPtr = NULL;
}

bool GrGLIndexBuffer::isLocked() const {
#if GR_DEBUG
    if (this->isValid() && GPUGL->supportsBufferLocking()) {
        this->bind();
        GrGLint mapped;
        GR_GL(GetBufferParameteriv(GR_GL_ELEMENT_ARRAY_BUFFER,
                                   GR_GL_BUFFER_MAPPED, &mapped));
        GrAssert(!!mapped == !!fLockPtr);
    }
#endif
    return NULL != fLockPtr;
}

bool GrGLIndexBuffer::updateData(const void* src, size_t srcSizeInBytes) {
    GrAssert(fBufferID);
    GrAssert(!isLocked());
    if (srcSizeInBytes > this->sizeInBytes()) {
        return false;
    }
    this->bind();
    GrGLenum usage = dynamic() ? GR_GL_DYNAMIC_DRAW : GR_GL_STATIC_DRAW;
    if (this->sizeInBytes() == srcSizeInBytes) {
        GR_GL(BufferData(GR_GL_ELEMENT_ARRAY_BUFFER, srcSizeInBytes, src, usage));
    } else {
#if GR_GL_USE_BUFFER_DATA_NULL_HINT
        GR_GL(BufferData(GR_GL_ELEMENT_ARRAY_BUFFER, this->sizeInBytes(), NULL, usage));
#endif
        GR_GL(BufferSubData(GR_GL_ELEMENT_ARRAY_BUFFER, 0, srcSizeInBytes, src));
    }
    return true;
}

bool GrGLIndexBuffer::updateSubData(const void* src,
                                    size_t srcSizeInBytes,
                                    size_t offset) {
    GrAssert(fBufferID);
    GrAssert(!isLocked());
    if (srcSizeInBytes + offset > this->sizeInBytes()) {
        return false;
    }
    this->bind();
    GR_GL(BufferSubData(GR_GL_ELEMENT_ARRAY_BUFFER, offset, srcSizeInBytes, src));
    return true;
}

