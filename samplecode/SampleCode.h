
/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef SampleCode_DEFINED
#define SampleCode_DEFINED

#include "SkColor.h"
#include "SkEvent.h"
#include "SkKey.h"
#include "SkView.h"
class SkOSMenu;
class GrContext;

class SampleCode {
public:
    static bool KeyQ(const SkEvent&, SkKey* outKey);
    static bool CharQ(const SkEvent&, SkUnichar* outUni);

    static bool TitleQ(const SkEvent&);
    static void TitleR(SkEvent*, const char title[]);
    
    static bool PrefSizeQ(const SkEvent&);
    static void PrefSizeR(SkEvent*, SkScalar width, SkScalar height);

    static bool FastTextQ(const SkEvent&);

    static SkMSec GetAnimTime();
    static SkMSec GetAnimTimeDelta();
    static SkScalar GetAnimSecondsDelta();
    static SkScalar GetAnimScalar(SkScalar speedPerSec, SkScalar period = 0);

    static GrContext* GetGr();
};

//////////////////////////////////////////////////////////////////////////////

typedef SkView* (*SkViewFactory)();

class SkViewRegister : SkNoncopyable {
public:
    explicit SkViewRegister(SkViewFactory);
    
    static const SkViewRegister* Head() { return gHead; }
    
    SkViewRegister* next() const { return fChain; }
    SkViewFactory   factory() const { return fFact; }
    
private:
    SkViewFactory   fFact;
    SkViewRegister* fChain;
    
    static SkViewRegister* gHead;
};

///////////////////////////////////////////////////////////////////////////////

class SampleView : public SkView {
public:
    SampleView() : fRepeatCount(1), fBGColor(SK_ColorWHITE) {
        fUsePipe = false;
    }

    void setBGColor(SkColor color) { fBGColor = color; }

    static bool IsSampleView(SkView*);
    static bool SetRepeatDraw(SkView*, int count);
    static bool SetUsePipe(SkView*, bool);
    
    //call this to request menu items from a SampleView. A SampleView can 
    //overwrite this method to add new items of various types to the menu and 
    //change its title. The events attached to any new menu items must be 
    //handled in the onEvent method. See SkOSMenu.h for helper functions.
    virtual void requestMenus(SkOSMenu* menu) {}

protected:
    virtual void onDrawBackground(SkCanvas*);
    virtual void onDrawContent(SkCanvas*) = 0;
    
    // overrides
    virtual bool onEvent(const SkEvent& evt);
    virtual bool onQuery(SkEvent* evt);
    virtual void onDraw(SkCanvas*);

private:
    int fRepeatCount;
    SkColor fBGColor;

    bool fUsePipe;

    typedef SkView INHERITED;
};

#endif

