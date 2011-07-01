#include "GrPathRenderer.h"

#include "GrPoint.h"
#include "GrDrawTarget.h"
#include "GrPathUtils.h"
#include "GrTexture.h"

#include "SkString.h"
#include "SkTemplates.h"

#include SK_USER_TRACE_INCLUDE_FILE

GrPathRenderer::GrPathRenderer()
    : fCurveTolerance (GR_Scalar1)
    , fPath(NULL)
    , fTarget(NULL) {
}


void GrPathRenderer::setPath(GrDrawTarget* target,
                             const SkPath* path,
                             GrPathFill fill,
                             const GrPoint* translate) {
    GrAssert(NULL == fPath);
    GrAssert(NULL == fTarget);
    GrAssert(NULL != target);

    fTarget = target;
    fPath = path;
    fFill = fill;
    if (NULL != translate) {
        fTranslate = *translate;
    } else {
        fTranslate.fX = fTranslate.fY = 0;
    }
    this->pathWasSet();
}

void GrPathRenderer::clearPath() {
    this->pathWillClear();
    fTarget->resetVertexSource();
    fTarget = NULL;
    fPath = NULL;
}

////////////////////////////////////////////////////////////////////////////////

GrDefaultPathRenderer::GrDefaultPathRenderer(bool separateStencilSupport,
                                             bool stencilWrapOpsSupport)
    : fSeparateStencil(separateStencilSupport)
    , fStencilWrapOps(stencilWrapOpsSupport)
    , fSubpathCount(0)
    , fSubpathVertCount(0)
    , fPreviousSrcTol(-GR_Scalar1)
    , fPreviousStages(-1) {
    fTarget = NULL;
}

////////////////////////////////////////////////////////////////////////////////
// Stencil rules for paths

////// Even/Odd

static const GrStencilSettings gEOStencilPass = {
    kInvert_StencilOp,           kInvert_StencilOp,
    kKeep_StencilOp,             kKeep_StencilOp,
    kAlwaysIfInClip_StencilFunc, kAlwaysIfInClip_StencilFunc,
    0xffffffff,                  0xffffffff,
    0xffffffff,                  0xffffffff,
    0xffffffff,                  0xffffffff
};

// ok not to check clip b/c stencil pass only wrote inside clip
static const GrStencilSettings gEOColorPass = {
    kZero_StencilOp,          kZero_StencilOp,
    kZero_StencilOp,          kZero_StencilOp,
    kNotEqual_StencilFunc,    kNotEqual_StencilFunc,
    0xffffffff,               0xffffffff,
    0x0,                      0x0,
    0xffffffff,               0xffffffff
};

// have to check clip b/c outside clip will always be zero.
static const GrStencilSettings gInvEOColorPass = {
    kZero_StencilOp,            kZero_StencilOp,
    kZero_StencilOp,            kZero_StencilOp,
    kEqualIfInClip_StencilFunc, kEqualIfInClip_StencilFunc,
    0xffffffff,                 0xffffffff,
    0x0,                        0x0,
    0xffffffff,                 0xffffffff
};

////// Winding

// when we have separate stencil we increment front faces / decrement back faces
// when we don't have wrap incr and decr we use the stencil test to simulate
// them.

static const GrStencilSettings gWindStencilSeparateWithWrap = {
    kIncWrap_StencilOp,             kDecWrap_StencilOp,
    kKeep_StencilOp,                kKeep_StencilOp,
    kAlwaysIfInClip_StencilFunc,    kAlwaysIfInClip_StencilFunc,
    0xffffffff,                     0xffffffff,
    0xffffffff,                     0xffffffff,
    0xffffffff,                     0xffffffff
};

// if inc'ing the max value, invert to make 0
// if dec'ing zero invert to make all ones.
// we can't avoid touching the stencil on both passing and
// failing, so we can't resctrict ourselves to the clip.
static const GrStencilSettings gWindStencilSeparateNoWrap = {
    kInvert_StencilOp,              kInvert_StencilOp,
    kIncClamp_StencilOp,            kDecClamp_StencilOp,
    kEqual_StencilFunc,             kEqual_StencilFunc,
    0xffffffff,                     0xffffffff,
    0xffffffff,                     0x0,
    0xffffffff,                     0xffffffff
};

// When there are no separate faces we do two passes to setup the winding rule
// stencil. First we draw the front faces and inc, then we draw the back faces
// and dec. These are same as the above two split into the incrementing and
// decrementing passes.
static const GrStencilSettings gWindSingleStencilWithWrapInc = {
    kIncWrap_StencilOp,             kIncWrap_StencilOp,
    kKeep_StencilOp,                kKeep_StencilOp,
    kAlwaysIfInClip_StencilFunc,    kAlwaysIfInClip_StencilFunc,
    0xffffffff,                     0xffffffff,
    0xffffffff,                     0xffffffff,
    0xffffffff,                     0xffffffff
};
static const GrStencilSettings gWindSingleStencilWithWrapDec = {
    kDecWrap_StencilOp,             kDecWrap_StencilOp,
    kKeep_StencilOp,                kKeep_StencilOp,
    kAlwaysIfInClip_StencilFunc,    kAlwaysIfInClip_StencilFunc,
    0xffffffff,                     0xffffffff,
    0xffffffff,                     0xffffffff,
    0xffffffff,                     0xffffffff
};
static const GrStencilSettings gWindSingleStencilNoWrapInc = {
    kInvert_StencilOp,              kInvert_StencilOp,
    kIncClamp_StencilOp,            kIncClamp_StencilOp,
    kEqual_StencilFunc,             kEqual_StencilFunc,
    0xffffffff,                     0xffffffff,
    0xffffffff,                     0xffffffff,
    0xffffffff,                     0xffffffff
};
static const GrStencilSettings gWindSingleStencilNoWrapDec = {
    kInvert_StencilOp,              kInvert_StencilOp,
    kDecClamp_StencilOp,            kDecClamp_StencilOp,
    kEqual_StencilFunc,             kEqual_StencilFunc,
    0xffffffff,                     0xffffffff,
    0x0,                            0x0,
    0xffffffff,                     0xffffffff
};

static const GrStencilSettings gWindColorPass = {
    kZero_StencilOp,                kZero_StencilOp,
    kZero_StencilOp,                kZero_StencilOp,
    kNonZeroIfInClip_StencilFunc,   kNonZeroIfInClip_StencilFunc,
    0xffffffff,                     0xffffffff,
    0x0,                            0x0,
    0xffffffff,                     0xffffffff
};

static const GrStencilSettings gInvWindColorPass = {
    kZero_StencilOp,                kZero_StencilOp,
    kZero_StencilOp,                kZero_StencilOp,
    kEqualIfInClip_StencilFunc,     kEqualIfInClip_StencilFunc,
    0xffffffff,                     0xffffffff,
    0x0,                            0x0,
    0xffffffff,                     0xffffffff
};

////// Normal render to stencil

// Sometimes the default path renderer can draw a path directly to the stencil
// buffer without having to first resolve the interior / exterior.
static const GrStencilSettings gDirectToStencil = {
    kZero_StencilOp,                kZero_StencilOp,
    kIncClamp_StencilOp,            kIncClamp_StencilOp,
    kAlwaysIfInClip_StencilFunc,    kAlwaysIfInClip_StencilFunc,
    0xffffffff,                     0xffffffff,
    0x0,                            0x0,
    0xffffffff,                     0xffffffff
};

////////////////////////////////////////////////////////////////////////////////
// Helpers for drawPath

static GrConvexHint getConvexHint(const SkPath& path) {
    return path.isConvex() ? kConvex_ConvexHint : kConcave_ConvexHint;
}

#define STENCIL_OFF     0   // Always disable stencil (even when needed)

static inline bool single_pass_path(const GrDrawTarget& target,
                                    const GrPath& path,
                                    GrPathFill fill) {
#if STENCIL_OFF
    return true;
#else
    if (kEvenOdd_PathFill == fill) {
        GrConvexHint hint = getConvexHint(path);
        return hint == kConvex_ConvexHint ||
               hint == kNonOverlappingConvexPieces_ConvexHint;
    } else if (kWinding_PathFill == fill) {
        GrConvexHint hint = getConvexHint(path);
        return hint == kConvex_ConvexHint ||
               hint == kNonOverlappingConvexPieces_ConvexHint ||
               (hint == kSameWindingConvexPieces_ConvexHint &&
                target.canDisableBlend() && !target.isDitherState());

    }
    return false;
#endif
}

bool GrDefaultPathRenderer::requiresStencilPass(const GrDrawTarget* target,
                                                const GrPath& path, 
                                                GrPathFill fill) const {
    return !single_pass_path(*target, path, fill);
}

void GrDefaultPathRenderer::pathWillClear() {
    fSubpathVertCount.realloc(0);
    fTarget->resetVertexSource();
    fPreviousSrcTol = -GR_Scalar1;
    fPreviousStages = -1;
}

void GrDefaultPathRenderer::createGeom(GrScalar srcSpaceTol, 
                                       GrDrawTarget::StageBitfield stages) {
    {
    SK_TRACE_EVENT0("GrDefaultPathRenderer::createGeom");

    fPreviousSrcTol = srcSpaceTol;
    fPreviousStages = stages;

    GrScalar srcSpaceTolSqd = GrMul(srcSpaceTol, srcSpaceTol);
    int maxPts = GrPathUtils::worstCasePointCount(*fPath, &fSubpathCount,
                                                  srcSpaceTol);

    GrVertexLayout layout = 0;
    for (int s = 0; s < GrDrawTarget::kNumStages; ++s) {
        if ((1 << s) & stages) {
            layout |= GrDrawTarget::StagePosAsTexCoordVertexLayoutBit(s);
        }
    }

    // add 4 to hold the bounding rect
    GrPoint* base;
    fTarget->reserveVertexSpace(layout, maxPts + 4, (void**)&base);

    GrPoint* vert = base;
    GrPoint* subpathBase = base;

    fSubpathVertCount.realloc(fSubpathCount);

    GrPoint pts[4];

    bool first = true;
    int subpath = 0;

    SkPath::Iter iter(*fPath, false);

    for (;;) {
        GrPathCmd cmd = (GrPathCmd)iter.next(pts);
        switch (cmd) {
            case kMove_PathCmd:
                if (!first) {
                    fSubpathVertCount[subpath] = vert-subpathBase;
                    subpathBase = vert;
                    ++subpath;
                }
                *vert = pts[0];
                vert++;
                break;
            case kLine_PathCmd:
                *vert = pts[1];
                vert++;
                break;
            case kQuadratic_PathCmd: {
                GrPathUtils::generateQuadraticPoints(pts[0], pts[1], pts[2],
                                                     srcSpaceTolSqd, &vert,
                                                     GrPathUtils::quadraticPointCount(pts, srcSpaceTol));
                break;
            }
            case kCubic_PathCmd: {
                GrPathUtils::generateCubicPoints(pts[0], pts[1], pts[2], pts[3],
                                                 srcSpaceTolSqd, &vert,
                                                 GrPathUtils::cubicPointCount(pts, srcSpaceTol));
                break;
            }
            case kClose_PathCmd:
                break;
            case kEnd_PathCmd:
                fSubpathVertCount[subpath] = vert-subpathBase;
                ++subpath; // this could be only in debug
                goto FINISHED;
        }
        first = false;
    }
FINISHED:
    GrAssert(subpath == fSubpathCount);
    GrAssert((vert - base) <= maxPts);

    if (fTranslate.fX || fTranslate.fY) {
        int count = vert - base;
        for (int i = 0; i < count; i++) {
            base[i].offset(fTranslate.fX, fTranslate.fY);
        }
    }
    }
}

void GrDefaultPathRenderer::onDrawPath(GrDrawTarget::StageBitfield stages,
                                       bool stencilOnly) {

    SK_TRACE_EVENT1("GrDefaultPathRenderer::onDrawPath",
                    "points", SkStringPrintf("%i", path.countPoints()).c_str());

    GrMatrix viewM = fTarget->getViewMatrix();
    // In order to tesselate the path we get a bound on how much the matrix can
    // stretch when mapping to screen coordinates.
    GrScalar stretch = viewM.getMaxStretch();
    bool useStretch = stretch > 0;
    GrScalar tol = fCurveTolerance;

    if (!useStretch) {
        // TODO: deal with perspective in some better way.
        tol /= 10;
    } else {
        tol = GrScalarDiv(tol, stretch);
    }
    // FIXME: It's really dumb that we recreate the verts for a new vertex
    // layout. We only do that because the GrDrawTarget API doesn't allow
    // us to change the vertex layout after reserveVertexSpace(). We won't
    // actually change the vertex data when the layout changes since all the
    // stages reference the positions (rather than having separate tex coords)
    // and we don't ever have per-vert colors. In practice our call sites
    // won't change the stages in use inside a setPath / removePath pair. But
    // it is a silly limitation of the GrDrawTarget design that should be fixed.
    if (tol != fPreviousSrcTol ||
        stages != fPreviousStages) {
        this->createGeom(tol, stages);
    }

    GrAssert(NULL != fTarget);
    GrDrawTarget::AutoStateRestore asr(fTarget);
    bool colorWritesWereDisabled = fTarget->isColorWriteDisabled();
    // face culling doesn't make sense here
    GrAssert(GrDrawTarget::kBoth_DrawFace == fTarget->getDrawFace());

    GrPrimitiveType             type;
    int                         passCount = 0;
    const GrStencilSettings*    passes[3];
    GrDrawTarget::DrawFace      drawFace[3];
    bool                        reverse = false;
    bool                        lastPassIsBounds;

    if (kHairLine_PathFill == fFill) {
        type = kLineStrip_PrimitiveType;
        passCount = 1;
        if (stencilOnly) {
            passes[0] = &gDirectToStencil;
        } else {
            passes[0] = NULL;
        }
        lastPassIsBounds = false;
        drawFace[0] = GrDrawTarget::kBoth_DrawFace;
    } else {
        type = kTriangleFan_PrimitiveType;
        if (single_pass_path(*fTarget, *fPath, fFill)) {
            passCount = 1;
            if (stencilOnly) {
                passes[0] = &gDirectToStencil;
            } else {
                passes[0] = NULL;
            }
            drawFace[0] = GrDrawTarget::kBoth_DrawFace;
            lastPassIsBounds = false;
        } else {
            switch (fFill) {
                case kInverseEvenOdd_PathFill:
                    reverse = true;
                    // fallthrough
                case kEvenOdd_PathFill:
                    passes[0] = &gEOStencilPass;
                    if (stencilOnly) {
                        passCount = 1;
                        lastPassIsBounds = false;
                    } else {
                        passCount = 2;
                        lastPassIsBounds = true;
                        if (reverse) {
                            passes[1] = &gInvEOColorPass;
                        } else {
                            passes[1] = &gEOColorPass;
                        }
                    }
                    drawFace[0] = drawFace[1] = GrDrawTarget::kBoth_DrawFace;
                    break;

                case kInverseWinding_PathFill:
                    reverse = true;
                    // fallthrough
                case kWinding_PathFill:
                    if (fSeparateStencil) {
                        if (fStencilWrapOps) {
                            passes[0] = &gWindStencilSeparateWithWrap;
                        } else {
                            passes[0] = &gWindStencilSeparateNoWrap;
                        }
                        passCount = 2;
                        drawFace[0] = GrDrawTarget::kBoth_DrawFace;
                    } else {
                        if (fStencilWrapOps) {
                            passes[0] = &gWindSingleStencilWithWrapInc;
                            passes[1] = &gWindSingleStencilWithWrapDec;
                        } else {
                            passes[0] = &gWindSingleStencilNoWrapInc;
                            passes[1] = &gWindSingleStencilNoWrapDec;
                        }
                        // which is cw and which is ccw is arbitrary.
                        drawFace[0] = GrDrawTarget::kCW_DrawFace;
                        drawFace[1] = GrDrawTarget::kCCW_DrawFace;
                        passCount = 3;
                    }
                    if (stencilOnly) {
                        lastPassIsBounds = false;
                        --passCount;
                    } else {
                        lastPassIsBounds = true;
                        drawFace[passCount-1] = GrDrawTarget::kBoth_DrawFace;
                        if (reverse) {
                            passes[passCount-1] = &gInvWindColorPass;
                        } else {
                            passes[passCount-1] = &gWindColorPass;
                        }
                    }
                    break;
                default:
                    GrAssert(!"Unknown path fFill!");
                    return;
            }
        }
    }

    {
    SK_TRACE_EVENT1("GrDefaultPathRenderer::onDrawPath::renderPasses",
                    "verts", SkStringPrintf("%i", vert - base).c_str());
    for (int p = 0; p < passCount; ++p) {
        fTarget->setDrawFace(drawFace[p]);
        if (NULL != passes[p]) {
            fTarget->setStencil(*passes[p]);
        }

        if (lastPassIsBounds && (p == passCount-1)) {
            if (!colorWritesWereDisabled) {
                fTarget->disableState(GrDrawTarget::kNoColorWrites_StateBit);
            }
            GrRect bounds;
            if (reverse) {
                GrAssert(NULL != fTarget->getRenderTarget());
                // draw over the whole world.
                bounds.setLTRB(0, 0,
                               GrIntToScalar(fTarget->getRenderTarget()->width()),
                               GrIntToScalar(fTarget->getRenderTarget()->height()));
                GrMatrix vmi;
                if (fTarget->getViewInverse(&vmi)) {
                    vmi.mapRect(&bounds);
                }
            } else {
                bounds = fPath->getBounds();
            }
            GrDrawTarget::AutoGeometryPush agp(fTarget);
            fTarget->drawSimpleRect(bounds, NULL, stages);
        } else {
            if (passCount > 1) {
                fTarget->enableState(GrDrawTarget::kNoColorWrites_StateBit);
            }
            int baseVertex = 0;
            for (int sp = 0; sp < fSubpathCount; ++sp) {
                fTarget->drawNonIndexed(type, baseVertex, 
                                        fSubpathVertCount[sp]);
                baseVertex += fSubpathVertCount[sp];
            }
        }
    }
    }
}

void GrDefaultPathRenderer::drawPath(GrDrawTarget::StageBitfield stages) {
    this->onDrawPath(stages, false);
}

void GrDefaultPathRenderer::drawPathToStencil() {
    GrAssert(kInverseEvenOdd_PathFill != fFill);
    GrAssert(kInverseWinding_PathFill != fFill);
    this->onDrawPath(0, true);
}
