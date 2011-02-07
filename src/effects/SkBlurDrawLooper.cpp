#include "SkBlurDrawLooper.h"
#include "SkBlurMaskFilter.h"
#include "SkCanvas.h"
#include "SkPaint.h"
#include "SkMaskFilter.h"

SkBlurDrawLooper::SkBlurDrawLooper(SkScalar radius, SkScalar dx, SkScalar dy,
                                   SkColor color, uint32_t flags)
    : fDx(dx), fDy(dy), fBlurColor(color), fBlurFlags(flags)
{
    SkASSERT(flags <= kAll_BlurFlag);
    if (radius > 0)
    {
        uint32_t blurFlags = flags & kIgnoreTransform_BlurFlag ?
            SkBlurMaskFilter::kIgnoreTransform_BlurFlag :
            SkBlurMaskFilter::kNone_BlurFlag;

        fBlur = SkBlurMaskFilter::Create(radius,
                                         SkBlurMaskFilter::kNormal_BlurStyle,
                                         blurFlags);
    }
    else
        fBlur = NULL;
}

SkBlurDrawLooper::SkBlurDrawLooper(SkFlattenableReadBuffer& buffer)
{
    fDx = buffer.readScalar();
    fDy = buffer.readScalar();
    fBlurColor = buffer.readU32();
    fBlur = static_cast<SkMaskFilter*>(buffer.readFlattenable());
    fBlurFlags = buffer.readU32() & kAll_BlurFlag;
}

SkBlurDrawLooper::~SkBlurDrawLooper()
{
    SkSafeUnref(fBlur);
}

void SkBlurDrawLooper::flatten(SkFlattenableWriteBuffer& buffer)
{
    buffer.writeScalar(fDx);
    buffer.writeScalar(fDy);
    buffer.write32(fBlurColor);
    buffer.writeFlattenable(fBlur);
    buffer.write32(fBlurFlags);
}

void SkBlurDrawLooper::init(SkCanvas* canvas, SkPaint* paint)
{
    // we do nothing if a maskfilter is already installed
    if (paint->getMaskFilter() != NULL)
        fState = kDone;
    else
    {
        fState = kBeforeEdge;
        fPaint = paint;
        fCanvas = canvas;
        fSaveCount = canvas->getSaveCount();
    }
}

bool SkBlurDrawLooper::next()
{
    switch (fState) {
    case kBeforeEdge:
        fSavedColor = fPaint->getColor();
        fPaint->setColor(fBlurColor);
        fPaint->setMaskFilter(fBlur);
        fCanvas->save(SkCanvas::kMatrix_SaveFlag);
        if (fBlurFlags & kIgnoreTransform_BlurFlag)
        {
            SkMatrix transform(fCanvas->getTotalMatrix());
            transform.postTranslate(fDx, fDy);
            fCanvas->setMatrix(transform);
        }
        else
        {
            fCanvas->translate(fDx, fDy);
        }
        fState = kAfterEdge;
        return true;
    case kAfterEdge:
        fPaint->setColor(fSavedColor);
        fPaint->setMaskFilter(NULL);
        fCanvas->restore(); // to remove the translate we did earlier
        fState = kDone;
        return true;
    default:
        SkASSERT(kDone == fState);
        return false;
    }
}

void SkBlurDrawLooper::restore()
{
    if (kAfterEdge == fState)
    {
        fPaint->setColor(fSavedColor);
        fPaint->setMaskFilter(NULL);
        fCanvas->restore(); // to remove the translate we did earlier
        fState = kDone;
    }
}

///////////////////////////////////////////////////////////////////////////////

static SkFlattenable::Registrar gReg("SkBlurDrawLooper",
                                     SkBlurDrawLooper::CreateProc);

