#ifndef SampleCode_DEFINED
#define SampleCode_DEFINED

#include "SkEvent.h"

class SampleCode {
public:
    static bool TitleQ(const SkEvent&);
    static void TitleR(SkEvent*, const char title[]);
    
    static bool PrefSizeQ(const SkEvent&);
    static void PrefSizeR(SkEvent*, SkScalar width, SkScalar height);
    
    static SkMSec GetAnimTime();
    static SkScalar GetAnimScalar(SkScalar speedPerSec, SkScalar period = 0);
};

//////////////////////////////////////////////////////////////////////////////

class SkView;

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

#endif

