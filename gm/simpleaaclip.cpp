/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


#include "gm.h"
#include "SkCanvas.h"
#include "SkAAClip.h"

namespace skiagm {

static void paint_rgn(SkCanvas* canvas, const SkAAClip& clip,
                      const SkPaint& paint) {
    SkMask mask;
    SkBitmap bm;

    clip.copyToMask(&mask);

    SkAutoMaskFreeImage amfi(mask.fImage);

    bm.setConfig(SkBitmap::kA8_Config, mask.fBounds.width(),
                 mask.fBounds.height(), mask.fRowBytes);
    bm.setPixels(mask.fImage);

    // need to copy for deferred drawing test to work
    SkBitmap bm2;

    bm.deepCopyTo(&bm2, SkBitmap::kA8_Config);

    canvas->drawBitmap(bm2,
                       SK_Scalar1 * mask.fBounds.fLeft,
                       SK_Scalar1 * mask.fBounds.fTop,
                       &paint);
}

//////////////////////////////////////////////////////////////////////////////
/*
 * This GM tests anti aliased single operation booleans with SkAAClips, 
 * SkRect and SkPaths.
 */
class SimpleClipGM : public GM {
public:
    enum SkGeomTypes {
        kRect_GeomType,
        kPath_GeomType,
        kAAClip_GeomType
    };

    SimpleClipGM(SkGeomTypes geomType)
    : fGeomType(geomType) {

        // offset the rects a bit so we get anti-aliasing in the rect case
        fBase.set(SkFloatToScalar(100.5f), 
                  SkFloatToScalar(100.5f), 
                  SkFloatToScalar(150.5f), 
                  SkFloatToScalar(150.5f));
        fRect = fBase;
        fRect.inset(5, 5);
        fRect.offset(25, 25);

        fBasePath.addRoundRect(fBase, SkIntToScalar(5), SkIntToScalar(5));
        fRectPath.addRoundRect(fRect, SkIntToScalar(5), SkIntToScalar(5));
        INHERITED::setBGColor(0xFFDDDDDD);
    }

protected:
    void buildRgn(SkAAClip* clip, SkRegion::Op op) {
        clip->setPath(fBasePath, NULL, true);

        SkAAClip clip2;
        clip2.setPath(fRectPath, NULL, true);
        clip->op(clip2, op);
    }

    void drawOrig(SkCanvas* canvas) {
        SkPaint     paint;
        
        paint.setStyle(SkPaint::kStroke_Style);
        paint.setColor(SK_ColorBLACK);
        
        canvas->drawRect(fBase, paint);
        canvas->drawRect(fRect, paint);
    }
    
    void drawRgnOped(SkCanvas* canvas, SkRegion::Op op, SkColor color) {

        SkAAClip clip;

        this->buildRgn(&clip, op);
        this->drawOrig(canvas);

        SkPaint paint;
        paint.setColor(color);
        paint_rgn(canvas, clip, paint);
    }

    void drawPathsOped(SkCanvas* canvas, SkRegion::Op op, SkColor color) {

        this->drawOrig(canvas);

        canvas->save();

        // create the clip mask with the supplied boolean op
        if (kPath_GeomType == fGeomType) {
            // path-based case
            canvas->clipPath(fBasePath, SkRegion::kReplace_Op, true);
            canvas->clipPath(fRectPath, op, true);
        } else {
            // rect-based case
            canvas->clipRect(fBase, SkRegion::kReplace_Op, true);
            canvas->clipRect(fRect, op, true);
        }

        // draw a rect that will entirely cover the clip mask area
        SkPaint paint;
        paint.setColor(color);

        SkRect r = SkRect::MakeLTRB(SkIntToScalar(90),  SkIntToScalar(90),
                                    SkIntToScalar(180), SkIntToScalar(180));

        canvas->drawRect(r, paint);

        canvas->restore();
    }

    virtual SkString onShortName() {
        SkString str;
        str.printf("simpleaaclip_%s", 
                    kRect_GeomType == fGeomType ? "rect" :
                    (kPath_GeomType == fGeomType ? "path" : 
                    "aaclip"));
        return str;
    }

    virtual SkISize onISize() {
        return make_isize(640, 480);
    }

    virtual void onDraw(SkCanvas* canvas) {

        static const struct {
            SkColor         fColor;
            const char*     fName;
            SkRegion::Op    fOp;
        } gOps[] = {
            { SK_ColorBLACK,    "Difference", SkRegion::kDifference_Op    },
            { SK_ColorRED,      "Intersect",  SkRegion::kIntersect_Op     },
            { 0xFF008800,       "Union",      SkRegion::kUnion_Op         },
            { SK_ColorBLUE,     "XOR",        SkRegion::kXOR_Op           },
            { SK_ColorGREEN,    "Rev Diff",   SkRegion::kReverseDifference_Op },
            { SK_ColorYELLOW,   "Replace",    SkRegion::kReplace_Op       }
        };

        SkPaint textPaint;
        textPaint.setAntiAlias(true);
        textPaint.setTextSize(SK_Scalar1*24);

        for (size_t op = 0; op < SK_ARRAY_COUNT(gOps); op++) {
            canvas->drawText(gOps[op].fName, strlen(gOps[op].fName),
                             SkIntToScalar(75), SkIntToScalar(50), 
                             textPaint);

            if (kAAClip_GeomType == fGeomType) {
                this->drawRgnOped(canvas, gOps[op].fOp, gOps[op].fColor);
            } else {
                this->drawPathsOped(canvas, gOps[op].fOp, gOps[op].fColor);
            }

            if (op && !(op % 3)) {
                canvas->translate(SkIntToScalar(-600), SkIntToScalar(250));
            } else {
                canvas->translate(SkIntToScalar(200), 0);
            }
        }
    }
private:

    SkGeomTypes fGeomType;

    SkRect fBase;
    SkRect fRect;

    SkPath fBasePath;       // fBase as a round rect
    SkPath fRectPath;       // fRect as a round rect

    typedef GM INHERITED;
};

//////////////////////////////////////////////////////////////////////////////

// rects
static GM* MyFactory(void*) { return new SimpleClipGM(
                                            SimpleClipGM::kRect_GeomType); }
static GMRegistry reg(MyFactory);

// paths
static GM* MyFactory2(void*) { return new SimpleClipGM(
                                            SimpleClipGM::kPath_GeomType); }
static GMRegistry reg2(MyFactory2);

// aa clip
static GM* MyFactory3(void*) { return new SimpleClipGM(
                                            SimpleClipGM::kAAClip_GeomType); }
static GMRegistry reg3(MyFactory3);

}
