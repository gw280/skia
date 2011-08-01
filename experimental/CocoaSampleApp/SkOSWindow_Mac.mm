#import <Cocoa/Cocoa.h>
#include "SkOSWindow_Mac.h"
#include "SkOSMenu.h"
#include "SkTypes.h"
#include "SkWindow.h"
#import "SkNSView.h"
#import "SkEventNotifier.h"
#define kINVAL_NSVIEW_EventType "inval-nsview"

SkOSWindow::SkOSWindow(void* hWnd) : fHWND(hWnd) {
    fInvalEventIsPending = false;
    fGLContext = NULL;
    fNotifier = [[SkEventNotifier alloc] init];
}
SkOSWindow::~SkOSWindow() {
    [(SkEventNotifier*)fNotifier release];
}

void SkOSWindow::onHandleInval(const SkIRect& r) {
    if (!fInvalEventIsPending) {
        fInvalEventIsPending = true;
        (new SkEvent(kINVAL_NSVIEW_EventType))->post(this->getSinkID());
    }
}

bool SkOSWindow::onEvent(const SkEvent& evt) {
    if (evt.isType(kINVAL_NSVIEW_EventType)) {
        fInvalEventIsPending = false;
        const SkIRect& r = this->getDirtyBounds();
        [(SkNSView*)fHWND postInvalWithRect:&r];
        [(NSOpenGLContext*)fGLContext update];
        return true;
    }
    if ([(SkNSView*)fHWND onHandleEvent:evt]) {
        return true;
    }
    return this->INHERITED::onEvent(evt);
}

bool SkOSWindow::onDispatchClick(int x, int y, Click::State state, void* owner) {
    return this->INHERITED::onDispatchClick(x, y, state, owner);
}

void SkOSWindow::onSetTitle(const char title[]) {
    [(SkNSView*)fHWND setSkTitle:title];
}

void SkOSWindow::onAddMenu(const SkOSMenu* menu) {
    [(SkNSView*)fHWND onAddMenu:menu];
}

void SkOSWindow::onUpdateMenu(const SkOSMenu* menu) {
    [(SkNSView*)fHWND onUpdateMenu:menu];
}

bool SkOSWindow::attachGL() {
    [(SkNSView*)fHWND attachGL];
}

void SkOSWindow::detachGL() {
    [(SkNSView*)fHWND detachGL];
}

void SkOSWindow::presentGL() {
    [(SkNSView*)fHWND presentGL];
}