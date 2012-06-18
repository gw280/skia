
/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef GrClipMaskManager_DEFINED
#define GrClipMaskManager_DEFINED

#include "GrClip.h"
#include "GrContext.h"
#include "GrNoncopyable.h"
#include "GrRect.h"
#include "GrStencil.h"
#include "GrTexture.h"

#include "SkDeque.h"
#include "SkPath.h"
#include "SkRefCnt.h"

class GrGpu;
class GrPathRenderer;
class GrPathRendererChain;
class SkPath;
class GrTexture;
class GrDrawState;

/**
 * Scissoring needs special handling during stencil clip mask creation
 * since the creation process re-entrantly invokes setupClipAndFlushState.
 * During this process the call stack is used to keep 
 * track of (and apply to the GPU) the current scissor settings.
 */
struct ScissoringSettings {
    bool    fEnableScissoring;
    GrIRect fScissorRect;

    void setupScissoring(GrGpu* gpu);
};

/**
 * The stencil buffer stores the last clip path - providing a single entry
 * "cache". This class provides similar functionality for AA clip paths
 */
class GrClipMaskCache : public GrNoncopyable {
public:
    GrClipMaskCache() 
    : fContext(NULL)
    , fStack(sizeof(GrClipStackFrame)) {
        // We need an initial frame to capture the clip state prior to 
        // any pushes
        new (fStack.push_back()) GrClipStackFrame();
    }

    ~GrClipMaskCache() {

        while (!fStack.empty()) {
            GrClipStackFrame* temp = (GrClipStackFrame*) fStack.back();
            temp->~GrClipStackFrame();
            fStack.pop_back();
        }
    }

    bool canReuse(const GrClip& clip, int width, int height) {

        if (fStack.empty()) {
            GrAssert(false);
            return false;
        }

        GrClipStackFrame* back = (GrClipStackFrame*) fStack.back();

        if (back->fLastMask.texture() &&
            back->fLastMask.texture()->width() >= width &&
            back->fLastMask.texture()->height() >= height &&
            clip == back->fLastClip) {
            return true;
        }

        return false;
    }

    void reset() {
        if (fStack.empty()) {
//            GrAssert(false);
            return;
        }

        GrClipStackFrame* back = (GrClipStackFrame*) fStack.back();

        back->reset();
    }

    /**
     * After a "push" the clip state is entirely open. Currently, the
     * entire clip stack will be re-rendered into a new clip mask.
     * TODO: can we take advantage of the nested nature of the clips to
     * reduce the mask creation cost?
     */
    void push() {
        new (fStack.push_back()) GrClipStackFrame();
    }

    void pop() {
        //GrAssert(!fStack.empty());

        if (!fStack.empty()) {
            GrClipStackFrame* back = (GrClipStackFrame*) fStack.back();

            back->~GrClipStackFrame();
            fStack.pop_back();
        }
    }

    void getLastClip(GrClip* clip) const {

        if (fStack.empty()) {
            GrAssert(false);
            clip->setEmpty();
            return;
        }

        GrClipStackFrame* back = (GrClipStackFrame*) fStack.back();

        *clip = back->fLastClip;
    }

    GrTexture* getLastMask() {

        if (fStack.empty()) {
            GrAssert(false);
            return NULL;
        }

        GrClipStackFrame* back = (GrClipStackFrame*) fStack.back();

        return back->fLastMask.texture();
    }

    const GrTexture* getLastMask() const {

        if (fStack.empty()) {
            GrAssert(false);
            return NULL;
        }

        GrClipStackFrame* back = (GrClipStackFrame*) fStack.back();

        return back->fLastMask.texture();
    }

    void acquireMask(const GrClip& clip,
                     const GrTextureDesc& desc,
                     const GrIRect& bound) {

        if (fStack.empty()) {
            GrAssert(false);
            return;
        }

        GrClipStackFrame* back = (GrClipStackFrame*) fStack.back();

        back->acquireMask(fContext, clip, desc, bound);
    }

    int getLastMaskWidth() const {

        if (fStack.empty()) {
            GrAssert(false);
            return -1;
        }

        GrClipStackFrame* back = (GrClipStackFrame*) fStack.back();

        if (NULL == back->fLastMask.texture()) {
            return -1;
        }

        return back->fLastMask.texture()->width();
    }

    int getLastMaskHeight() const {

        if (fStack.empty()) {
            GrAssert(false);
            return -1;
        }

        GrClipStackFrame* back = (GrClipStackFrame*) fStack.back();

        if (NULL == back->fLastMask.texture()) {
            return -1;
        }

        return back->fLastMask.texture()->height();
    }

    void getLastBound(GrIRect* bound) const {

        if (fStack.empty()) {
            GrAssert(false);
            bound->setEmpty();
            return;
        }

        GrClipStackFrame* back = (GrClipStackFrame*) fStack.back();

        *bound = back->fLastBound;
    }

    void setContext(GrContext* context) {
        fContext = context;
    }

    GrContext* getContext() {
        return fContext;
    }

    void releaseResources() {

        SkDeque::F2BIter iter(fStack);
        for (GrClipStackFrame* frame = (GrClipStackFrame*) iter.next();
                frame != NULL;
                frame = (GrClipStackFrame*) iter.next()) {
            frame->reset();
        }
    }

protected:
private:
    struct GrClipStackFrame {

        GrClipStackFrame() {
            reset();
        }

        void acquireMask(GrContext* context,
                         const GrClip& clip, 
                         const GrTextureDesc& desc,
                         const GrIRect& bound) {

            fLastClip = clip;

            fLastMask.set(context, desc);

            fLastBound = bound;
        }

        void reset () {
            fLastClip.setEmpty();

            GrTextureDesc desc;

            fLastMask.set(NULL, desc);
            fLastBound.setEmpty();
        }

        GrClip                  fLastClip;
        // The mask's width & height values are used in setupDrawStateAAClip to 
        // correctly scale the uvs for geometry drawn with this mask
        GrAutoScratchTexture    fLastMask;
        // fLastBound stores the bounding box of the clip mask in canvas 
        // space. The left and top fields are used to offset the uvs for 
        // geometry drawn with this mask (in setupDrawStateAAClip)
        GrIRect                 fLastBound;
    };

    GrContext*   fContext;
    SkDeque      fStack;

    typedef GrNoncopyable INHERITED;
};

/**
 * The clip mask creator handles the generation of the clip mask. If anti 
 * aliasing is requested it will (in the future) generate a single channel 
 * (8bit) mask. If no anti aliasing is requested it will generate a 1-bit 
 * mask in the stencil buffer. In the non anti-aliasing case, if the clip
 * mask can be represented as a rectangle then scissoring is used. In all
 * cases scissoring is used to bound the range of the clip mask.
 */
class GrClipMaskManager : public GrNoncopyable {
public:
    GrClipMaskManager(GrGpu* gpu)
        : fGpu(gpu)
        , fCurrClipMaskType(kNone_ClipMaskType) {
    }

    bool createClipMask(const GrClip& clip, 
                        ScissoringSettings* scissorSettings);

    void releaseResources();

    bool isClipInStencil() const {
        return kStencil_ClipMaskType == fCurrClipMaskType;
    }
    bool isClipInAlpha() const {
        return kAlpha_ClipMaskType == fCurrClipMaskType;
    }

    void invalidateStencilMask() {
        if (kStencil_ClipMaskType == fCurrClipMaskType) {
            fCurrClipMaskType = kNone_ClipMaskType;
        }
    }

    void postClipPush() {
        // TODO: make sure that, if the clip stack is unaltered, the 
        // prior clip mask is reused (i.e., a push w/ no change to the 
        // clip stack)
        fAACache.push();
    }

    void preClipPop() {
        fAACache.pop();
    }

    void setContext(GrContext* context) {
        fAACache.setContext(context);
    }

    GrContext* getContext() {
        return fAACache.getContext();
    }

    /**
     * Informs the helper function adjustStencilParams() about how the stencil
     * buffer clip is being used.
     */
    enum StencilClipMode {
        // Draw to the clip bit of the stencil buffer
        kModifyClip_StencilClipMode,
        // Clip against the existing representation of the clip in the high bit
        // of the stencil buffer.
        kRespectClip_StencilClipMode,
        // Neither writing to nor clipping against the clip bit.
        kIgnoreClip_StencilClipMode,
    };

    /**
     * The stencil func, mask, and reference value are specified by GrGpu's
     * caller but the actual values passed to the API may have to be adjusted
     * due to the stencil buffer simultaneously being used for clipping. This
     * function should be called even when clipping is disabled in order to
     * prevent the clip from being accidentally overwritten.
     */
    GrStencilFunc adjustStencilParams(GrStencilFunc,
                                      StencilClipMode mode,
                                      unsigned int stencilBitCnt,
                                      unsigned int* ref,
                                      unsigned int* mask,
                                      unsigned int* writeMask);

private:
    GrGpu* fGpu;

    /**
     * We may represent the clip as a mask in the stencil buffer or as an alpha
     * texture. It may be neither because the scissor rect suffices or we
     * haven't yet examined the clip.
     */
    enum ClipMaskType {
        kNone_ClipMaskType,
        kStencil_ClipMaskType,
        kAlpha_ClipMaskType,
    } fCurrClipMaskType;
    
    GrClipMaskCache fAACache;       // cache for the AA path

    bool createStencilClipMask(const GrClip& clip, 
                               const GrRect& bounds,
                               ScissoringSettings* scissorSettings);
    bool createAlphaClipMask(const GrClip& clipIn,
                             GrTexture** result,
                             GrIRect *resultBounds);
    bool createSoftwareClipMask(const GrClip& clipIn,
                                GrTexture** result,
                                GrIRect *resultBounds);
    bool clipMaskPreamble(const GrClip& clipIn,
                          GrTexture** result,
                          GrIRect *resultBounds);

    bool useSWOnlyPath(const GrClip& clipIn);

    bool drawClipShape(GrTexture* target,
                       const GrClip& clipIn,
                       int index);

    void drawTexture(GrTexture* target,
                     GrTexture* texture);

    void getTemp(const GrIRect& bounds, GrAutoScratchTexture* temp);

    void setupCache(const GrClip& clip, 
                    const GrIRect& bounds);

    typedef GrNoncopyable INHERITED;
};

#endif // GrClipMaskManager_DEFINED
