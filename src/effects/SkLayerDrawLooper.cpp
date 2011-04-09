#include "SkCanvas.h"
#include "SkLayerDrawLooper.h"
#include "SkPaint.h"

SkLayerDrawLooper::SkLayerDrawLooper() {
    fRecs = NULL;
    fCount = 0;
}

SkLayerDrawLooper::~SkLayerDrawLooper() {
    Rec* rec = fRecs;
    while (rec) {
        Rec* next = rec->fNext;
        SkDELETE(rec);
        rec = next;
    }
}
    
SkPaint* SkLayerDrawLooper::addLayer(SkScalar dx, SkScalar dy, BitFlags bits) {
    fCount += 1;

    Rec* rec = SkNEW(Rec);
    rec->fNext = fRecs;
    rec->fOffset.set(dx, dy);
    rec->fBits = bits;
    fRecs = rec;

    return &rec->fPaint;
}

void SkLayerDrawLooper::init(SkCanvas* canvas) {
    fCurrRec = fRecs;
    canvas->save(SkCanvas::kMatrix_SaveFlag);
}

void SkLayerDrawLooper::ApplyBits(SkPaint* dst, const SkPaint& src,
                                  BitFlags bits) {
    if (0 == bits) {
        return;
    }
    if (kEntirePaint_Bits == bits) {
        *dst = src;
        return;
    }

    SkColor c = dst->getColor();
    if (bits & kAlpha_Bit) {
        c &= 0x00FFFFFF;
        c |= src.getColor() & 0xFF000000;
    }
    if (bits & kColor_Bit) {
        c &= 0xFF000000;
        c |= src.getColor() & 0x00FFFFFF;
    }
    dst->setColor(c);

    if (bits & kStyle_Bit) {
        dst->setStyle(src.getStyle());
        dst->setStrokeWidth(src.getStrokeWidth());
        dst->setStrokeMiter(src.getStrokeMiter());
        dst->setStrokeCap(src.getStrokeCap());
        dst->setStrokeJoin(src.getStrokeJoin());
    }

    if (bits & kTextSkewX_Bit) {
        dst->setTextSkewX(src.getTextSkewX());
    }

    if (bits & kPathEffect_Bit) {
        dst->setPathEffect(src.getPathEffect());
    }
    if (bits & kMaskFilter_Bit) {
        dst->setMaskFilter(src.getMaskFilter());
    }
    if (bits & kShader_Bit) {
        dst->setShader(src.getShader());
    }
    if (bits & kColorFilter_Bit) {
        dst->setColorFilter(src.getColorFilter());
    }
    if (bits & kXfermode_Bit) {
        dst->setXfermode(src.getXfermode());
    }

    // we never copy these
#if 0
    dst->setFlags(src.getFlags());
    dst->setTypeface(src.getTypeface());
    dst->setTextSize(src.getTextSize());
    dst->setTextScaleX(src.getTextScaleX());
    dst->setTextSkewX(src.getTextSkewX());
    dst->setRasterizer(src.getRasterizer());
    dst->setLooper(src.getLooper());
    dst->setTextEncoding(src.getTextEncoding());
    dst->setHinting(src.getHinting());
#endif
}

bool SkLayerDrawLooper::next(SkCanvas* canvas, SkPaint* paint) {
    canvas->restore();
    if (NULL == fCurrRec) {
        return false;
    }

    ApplyBits(paint, fCurrRec->fPaint, fCurrRec->fBits);
    canvas->save(SkCanvas::kMatrix_SaveFlag);
    canvas->translate(fCurrRec->fOffset.fX, fCurrRec->fOffset.fY);
    fCurrRec = fCurrRec->fNext;

    return true;
}

SkLayerDrawLooper::Rec* SkLayerDrawLooper::Rec::Reverse(Rec* head) {
    Rec* rec = head;
    Rec* prev = NULL;
    while (rec) {
        Rec* next = rec->fNext;
        rec->fNext = prev;
        prev = rec;
        rec = next;
    }
    return prev;
}

///////////////////////////////////////////////////////////////////////////////

void SkLayerDrawLooper::flatten(SkFlattenableWriteBuffer& buffer) {
    this->INHERITED::flatten(buffer);

#ifdef SK_DEBUG
    {
        Rec* rec = fRecs;
        int count = 0;
        while (rec) {
            rec = rec->fNext;
            count += 1;
        }
        SkASSERT(count == fCount);
    }
#endif

    buffer.writeInt(fCount);
    
    Rec* rec = fRecs;
    for (int i = 0; i < fCount; i++) {
        buffer.writeScalar(rec->fOffset.fX);
        buffer.writeScalar(rec->fOffset.fY);
        rec->fPaint.flatten(buffer);
        rec = rec->fNext;
    }
}

SkLayerDrawLooper::SkLayerDrawLooper(SkFlattenableReadBuffer& buffer)
        : INHERITED(buffer) {
    fRecs = NULL;
    fCount = 0;
    
    int count = buffer.readInt();

    for (int i = 0; i < count; i++) {
        SkScalar dx = buffer.readScalar();
        SkScalar dy = buffer.readScalar();
        this->addLayer(dx, dy)->unflatten(buffer);
    }
    SkASSERT(count == fCount);

    // we're in reverse order, so fix it now
    fRecs = Rec::Reverse(fRecs);
    
#ifdef SK_DEBUG
    {
        Rec* rec = fRecs;
        int n = 0;
        while (rec) {
            rec = rec->fNext;
            n += 1;
        }
        SkASSERT(count == n);
    }
#endif
}

///////////////////////////////////////////////////////////////////////////////

static SkFlattenable::Registrar gReg("SkLayerDrawLooper",
                                     SkLayerDrawLooper::CreateProc);
