/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkBenchmark.h"
#include "SkCanvas.h"
#include "SkGraphics.h"
#include "SkPaint.h"
#include "SkRandom.h"
#include "SkString.h"

extern bool gSkSuppressFontCachePurgeSpew;

class FontScalerBench : public SkBenchmark {
    SkString fName;
    SkString fText;
public:
    FontScalerBench(void* param) : INHERITED(param) {
        fName.set("fontscaler");
        fText.set("abcdefghijklmnopqrstuvwxyz01234567890");
    }

protected:
    virtual const char* onGetName() { return fName.c_str(); }
    virtual void onDraw(SkCanvas* canvas) {
        SkPaint paint;
        this->setupPaint(&paint);

        bool prev = gSkSuppressFontCachePurgeSpew;
        gSkSuppressFontCachePurgeSpew = true;

        // this is critical - we want to time the creation process, so we
        // explicitly flush our cache before each run
        SkGraphics::SetFontCacheUsed(0);
        for (int ps = 9; ps <= 24; ps += 2) {
            paint.setTextSize(SkIntToScalar(ps));
            canvas->drawText(fText.c_str(), fText.size(),
                             0, SkIntToScalar(20), paint);
        }

        gSkSuppressFontCachePurgeSpew = prev;
    }
private:
    typedef SkBenchmark INHERITED;
};

///////////////////////////////////////////////////////////////////////////////

static SkBenchmark* Fact(void* p) { return SkNEW_ARGS(FontScalerBench, (p)); }

static BenchRegistry gReg(Fact);
