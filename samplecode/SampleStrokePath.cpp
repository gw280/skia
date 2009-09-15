#include "SampleCode.h"
#include "SkCanvas.h"
#include "SkParsePath.h"
#include "SkPath.h"
#include "SkRandom.h"
#include "SkView.h"

static void scale_to_width(SkPath* path, SkScalar dstWidth) {
    const SkRect& bounds = path->getBounds();
    SkScalar scale = dstWidth / bounds.width();
    SkMatrix matrix;

    matrix.setScale(scale, scale);
    path->transform(matrix);
}

static const struct {
    SkPaint::Style  fStyle;
    SkPaint::Join   fJoin;
    int             fStrokeWidth;
} gRec[] = {
    { SkPaint::kFill_Style,             SkPaint::kMiter_Join,   0 },
    { SkPaint::kStroke_Style,           SkPaint::kMiter_Join,   0 },
    { SkPaint::kStroke_Style,           SkPaint::kMiter_Join,   10 },
    { SkPaint::kStrokeAndFill_Style,    SkPaint::kMiter_Join,   10 },
};

class StrokePathView : public SkView {
    SkScalar    fWidth;
    SkPath      fPath;
public:
	StrokePathView() {
        fWidth = SkIntToScalar(120);

#if 0
        const char str[] =
            "M 0, 3"
            "C 10, -10, 30, -10, 0, 28"
            "C -30, -10, -10, -10, 0, 3"
            "Z";
        SkParsePath::FromSVGString(str, &fPath);
#else
        fPath.addCircle(0, 0, SkIntToScalar(50), SkPath::kCW_Direction);
        fPath.addCircle(0, SkIntToScalar(-50), SkIntToScalar(30), SkPath::kCW_Direction);
#endif
        
        scale_to_width(&fPath, fWidth);
        const SkRect& bounds = fPath.getBounds();
        fPath.offset(-bounds.fLeft, -bounds.fTop);
    }
    
protected:
    // overrides from SkEventSink
    virtual bool onQuery(SkEvent* evt) {
        if (SampleCode::TitleQ(*evt)) {
            SampleCode::TitleR(evt, "StrokePath");
            return true;
        }
        return this->INHERITED::onQuery(evt);
    }
    
    void drawBG(SkCanvas* canvas) {
        canvas->drawColor(0xFFDDDDDD);
    }
    
    SkRandom rand;
    
    void drawSet(SkCanvas* canvas, SkPaint* paint) {
        SkAutoCanvasRestore acr(canvas, true);

        for (size_t i = 0; i < SK_ARRAY_COUNT(gRec); i++) {
            paint->setStyle(gRec[i].fStyle);
            paint->setStrokeJoin(gRec[i].fJoin);
            paint->setStrokeWidth(SkIntToScalar(gRec[i].fStrokeWidth));
            canvas->drawPath(fPath, *paint);
            canvas->translate(fWidth * 5 / 4, 0);
        }
    }

    virtual void onDraw(SkCanvas* canvas) {
        drawBG(canvas);
        canvas->translate(SkIntToScalar(10), SkIntToScalar(10));

        SkPaint paint;
        paint.setAntiAlias(true);
        paint.setColor(SK_ColorBLUE);

#if 1
        SkPath p;
        float r = rand.nextUScalar1() + 0.5f;
        SkScalar x = 0, y = 0;
        p.moveTo(x, y);
#if 0
        p.cubicTo(x-75*r, y+75*r, x-40*r, y+125*r, x, y+85*r);
        p.cubicTo(x+40*r, y+125*r, x+75*r, y+75*r, x, y);
#else
        p.cubicTo(x+75*r, y+75*r, x+40*r, y+125*r, x, y+85*r);
        p.cubicTo(x-40*r, y+125*r, x-75*r, y+75*r, x, y);
#endif
        p.close();
        fPath = p;
        fPath.offset(100, 0);
#endif
        
        fPath.setFillType(SkPath::kWinding_FillType);
        drawSet(canvas, &paint);
        
        canvas->translate(0, fPath.getBounds().height() * 5 / 4);
        fPath.setFillType(SkPath::kEvenOdd_FillType);
        drawSet(canvas, &paint);
    }

    virtual SkView::Click* onFindClickHandler(SkScalar x, SkScalar y) {
        this->inval(NULL);
        return this->INHERITED::onFindClickHandler(x, y);
    }
private:
    typedef SkView INHERITED;
};

//////////////////////////////////////////////////////////////////////////////

static SkView* MyFactory() { return new StrokePathView; }
static SkViewRegister reg(MyFactory);

