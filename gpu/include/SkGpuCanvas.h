/*
    Copyright 2010 Google Inc.

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


#ifndef SkGpuCanvas_DEFINED
#define SkGpuCanvas_DEFINED

#include "SkCanvas.h"

class GrContext;

/**
 *  Subclass of canvas that creates devices compatible with the GrContext pass
 *  to the canvas' constructor.
 */
class SkGpuCanvas : public SkCanvas {
public:
    /**
     *  The GrContext object is reference counted. When passed to our 
     *  constructor, its reference count is incremented. In our destructor, the 
     *  GrGpu's reference count will be decremented.
     */
    explicit SkGpuCanvas(GrContext*);
    virtual ~SkGpuCanvas();

    /**
     *  Return our GrContext instance
     */
    GrContext* context() const { return fContext; }

    /**
     *  Override from SkCanvas. Returns true, and if not-null, sets size to
     *  be the width/height of our viewport.
     */
    virtual bool getViewport(SkIPoint* size) const;

    /**
     *  Override from SkCanvas. Returns a new device of the correct subclass,
     *  as determined by the GrGpu passed to our constructor.
     */
    virtual SkDevice* createDevice(SkBitmap::Config, int width, int height,
                                   bool isOpaque, bool isLayer);

#if 0
    virtual int saveLayer(const SkRect* bounds, const SkPaint* paint,
                          SaveFlags flags = kARGB_ClipLayer_SaveFlag) {
        return this->save(flags);
    }
#endif
    
private:
    GrContext* fContext;

    typedef SkCanvas INHERITED;
};

#endif


