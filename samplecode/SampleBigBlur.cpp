#include "SampleCode.h"
#include "SkBlurMaskFilter.h"
#include "SkView.h"
#include "SkCanvas.h"

class BigBlurView : public SampleView {
public:
    BigBlurView() {
    }

protected:
    // overrides from SkEventSink
    virtual bool onQuery(SkEvent* evt) {
        if (SampleCode::TitleQ(*evt)) {
            SampleCode::TitleR(evt, "BigBlur");
            return true;
        }
        return this->INHERITED::onQuery(evt);
    }

    virtual void onDrawContent(SkCanvas* canvas) {
        SkPaint paint;
        canvas->save();
        paint.setColor(SK_ColorBLUE);
        SkMaskFilter* mf = SkBlurMaskFilter::Create(
            128,
            SkBlurMaskFilter::kNormal_BlurStyle,
            SkBlurMaskFilter::kHighQuality_BlurFlag);
        paint.setMaskFilter(mf)->unref();
        canvas->translate(200, 200);
        canvas->drawCircle(100, 100, 250, paint);
        canvas->restore();
    }

private:
    typedef SkView INHERITED;
};

//////////////////////////////////////////////////////////////////////////////

static SkView* MyFactory() { return new BigBlurView; }
static SkViewRegister reg(MyFactory);

