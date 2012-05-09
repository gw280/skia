
/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "GrSoftwarePathRenderer.h"
#include "GrPaint.h"
#include "SkPaint.h"
#include "GrRenderTarget.h" 
#include "GrContext.h"
#include "SkDraw.h"
#include "SkRasterClip.h"

////////////////////////////////////////////////////////////////////////////////
bool GrSoftwarePathRenderer::canDrawPath(const SkPath& path,
                                         GrPathFill fill,
                                         const GrDrawTarget* target,
                                         bool antiAlias) const {
    if (!antiAlias || NULL == fContext) {
        // TODO: We could allow the SW path to also handle non-AA paths but
        // this would mean that GrDefaultPathRenderer would never be called
        // (since it appears after the SW renderer in the path renderer
        // chain). Some testing would need to be done r.e. performance 
        // and consistency of the resulting images before removing
        // the "!antiAlias" clause from the above test
        return false;
    }

    return true;
}

namespace {

////////////////////////////////////////////////////////////////////////////////
SkPath::FillType gr_fill_to_sk_fill(GrPathFill fill) {
    switch (fill) {
        case kWinding_PathFill:
            return SkPath::kWinding_FillType;
        case kEvenOdd_PathFill:
            return SkPath::kEvenOdd_FillType;
        case kInverseWinding_PathFill:
            return SkPath::kInverseWinding_FillType;
        case kInverseEvenOdd_PathFill:
            return SkPath::kInverseEvenOdd_FillType;
        default:
            GrCrash("Unexpected fill.");
            return SkPath::kWinding_FillType;
    }
}

////////////////////////////////////////////////////////////////////////////////
// gets device coord bounds of path (not considering the fill) and clip. The
// path bounds will be a subset of the clip bounds. returns false if 
// path bounds would be empty.
bool get_path_and_clip_bounds(const GrDrawTarget* target,
                              const SkPath& path,
                              const GrVec* translate,
                              GrIRect* pathBounds,
                              GrIRect* clipBounds) {
    // compute bounds as intersection of rt size, clip, and path
    const GrRenderTarget* rt = target->getDrawState().getRenderTarget();
    if (NULL == rt) {
        return false;
    }
    *pathBounds = GrIRect::MakeWH(rt->width(), rt->height());
    const GrClip& clip = target->getClip();
    if (clip.hasConservativeBounds()) {
        clip.getConservativeBounds().roundOut(clipBounds);
        if (!pathBounds->intersect(*clipBounds)) {
            return false;
        }
    } else {
        // pathBounds is currently the rt extent, set clip bounds to that rect.
        *clipBounds = *pathBounds;
    }
    GrRect pathSBounds = path.getBounds();
    if (!pathSBounds.isEmpty()) {
        if (NULL != translate) {
            pathSBounds.offset(*translate);
        }
        target->getDrawState().getViewMatrix().mapRect(&pathSBounds,
                                                        pathSBounds);
        GrIRect pathIBounds;
        pathSBounds.roundOut(&pathIBounds);
        if (!pathBounds->intersect(pathIBounds)) {
            return false;
        }
    } else {
        return false;
    }
    return true;
}

////////////////////////////////////////////////////////////////////////////////
/**
 * sw rasterizes path to A8 mask using the context's matrix and uploads to a 
 * scratch texture.
 */

bool sw_draw_path_to_mask_texture(const SkPath& clientPath,
                                  const GrIRect& pathDevBounds,
                                  GrPathFill fill,
                                  GrContext* context,
                                  const GrPoint* translate,
                                  GrAutoScratchTexture* tex,
                                  bool antiAlias) {
    SkPaint paint;
    SkPath tmpPath;
    const SkPath* pathToDraw = &clientPath;
    if (kHairLine_PathFill == fill) {
        paint.setStyle(SkPaint::kStroke_Style);
        paint.setStrokeWidth(SK_Scalar1);
    } else {
        paint.setStyle(SkPaint::kFill_Style);
        SkPath::FillType skfill = gr_fill_to_sk_fill(fill);
        if (skfill != pathToDraw->getFillType()) {
            tmpPath = *pathToDraw;
            tmpPath.setFillType(skfill);
            pathToDraw = &tmpPath;
        }
    }
    paint.setAntiAlias(antiAlias);
    paint.setColor(SK_ColorWHITE);

    GrMatrix matrix = context->getMatrix();
    if (NULL != translate) {
        matrix.postTranslate(translate->fX, translate->fY);
    }

    matrix.postTranslate(-pathDevBounds.fLeft * SK_Scalar1,
                         -pathDevBounds.fTop * SK_Scalar1);
    GrIRect bounds = GrIRect::MakeWH(pathDevBounds.width(),
                                     pathDevBounds.height());

    SkBitmap bm;
    bm.setConfig(SkBitmap::kA8_Config, bounds.fRight, bounds.fBottom);
    if (!bm.allocPixels()) {
        return false;
    }
    sk_bzero(bm.getPixels(), bm.getSafeSize());

    SkDraw  draw;
    sk_bzero(&draw, sizeof(draw));
    SkRasterClip rc(bounds);
    draw.fRC    = &rc;
    draw.fClip  = &rc.bwRgn();
    draw.fMatrix = &matrix;
    draw.fBitmap = &bm;
    draw.drawPath(*pathToDraw, paint);

    const GrTextureDesc desc = {
        kNone_GrTextureFlags,
        bounds.fRight,
        bounds.fBottom,
        kAlpha_8_GrPixelConfig,
        0 // samples
    };

    tex->set(context, desc);
    GrTexture* texture = tex->texture();

    if (NULL == texture) {
        return false;
    }
    SkAutoLockPixels alp(bm);
    texture->writePixels(0, 0, desc.fWidth, desc.fHeight, desc.fConfig,
                         bm.getPixels(), bm.rowBytes());
    return true;
}

////////////////////////////////////////////////////////////////////////////////
void draw_around_inv_path(GrDrawTarget* target,
                          GrDrawState::StageMask stageMask,
                          const GrIRect& clipBounds,
                          const GrIRect& pathBounds) {
    GrDrawTarget::AutoDeviceCoordDraw adcd(target, stageMask);
    GrRect rect;
    if (clipBounds.fTop < pathBounds.fTop) {
        rect.iset(clipBounds.fLeft, clipBounds.fTop, 
                    clipBounds.fRight, pathBounds.fTop);
        target->drawSimpleRect(rect, NULL, stageMask);
    }
    if (clipBounds.fLeft < pathBounds.fLeft) {
        rect.iset(clipBounds.fLeft, pathBounds.fTop, 
                    pathBounds.fLeft, pathBounds.fBottom);
        target->drawSimpleRect(rect, NULL, stageMask);
    }
    if (clipBounds.fRight > pathBounds.fRight) {
        rect.iset(pathBounds.fRight, pathBounds.fTop, 
                    clipBounds.fRight, pathBounds.fBottom);
        target->drawSimpleRect(rect, NULL, stageMask);
    }
    if (clipBounds.fBottom > pathBounds.fBottom) {
        rect.iset(clipBounds.fLeft, pathBounds.fBottom, 
                    clipBounds.fRight, clipBounds.fBottom);
        target->drawSimpleRect(rect, NULL, stageMask);
    }
}

}

////////////////////////////////////////////////////////////////////////////////
// return true on success; false on failure
bool GrSoftwarePathRenderer::onDrawPath(const SkPath& path,
                                        GrPathFill fill,
                                        const GrVec* translate,
                                        GrDrawTarget* target,
                                        GrDrawState::StageMask stageMask,
                                        bool antiAlias) {

    if (NULL == fContext) {
        return false;
    }

    GrAutoScratchTexture ast;
    GrIRect pathBounds, clipBounds;
    if (!get_path_and_clip_bounds(target, path, translate,
                                  &pathBounds, &clipBounds)) {
        return true;    // path is empty so there is nothing to do
    }
    if (sw_draw_path_to_mask_texture(path, pathBounds,
                                     fill, fContext,
                                     translate, &ast, antiAlias)) {
        GrTexture* texture = ast.texture();
        GrAssert(NULL != texture);
        GrDrawTarget::AutoDeviceCoordDraw adcd(target, stageMask);
        enum {
            // the SW path renderer shares this stage with glyph
            // rendering (kGlyphMaskStage in GrBatchedTextContext)
            kPathMaskStage = GrPaint::kTotalStages,
        };
        GrAssert(NULL == target->drawState()->getTexture(kPathMaskStage));
        target->drawState()->setTexture(kPathMaskStage, texture);
        target->drawState()->sampler(kPathMaskStage)->reset();
        GrScalar w = GrIntToScalar(pathBounds.width());
        GrScalar h = GrIntToScalar(pathBounds.height());
        GrRect maskRect = GrRect::MakeWH(w / texture->width(),
                                         h / texture->height());
        const GrRect* srcRects[GrDrawState::kNumStages] = {NULL};
        srcRects[kPathMaskStage] = &maskRect;
        stageMask |= 1 << kPathMaskStage;
        GrRect dstRect = GrRect::MakeLTRB(
                              SK_Scalar1* pathBounds.fLeft,
                              SK_Scalar1* pathBounds.fTop,
                              SK_Scalar1* pathBounds.fRight,
                              SK_Scalar1* pathBounds.fBottom);
        target->drawRect(dstRect, NULL, stageMask, srcRects, NULL);
        target->drawState()->setTexture(kPathMaskStage, NULL);
        if (GrIsFillInverted(fill)) {
            draw_around_inv_path(target, stageMask,
                                 clipBounds, pathBounds);
        }
        return true;
    }

    return false;
}

