/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkOSWindow_Unix_DEFINED
#define SkOSWindow_Unix_DEFINED

#include <GL/glx.h>
#include <X11/Xlib.h>

#include "SkWindow.h"

class SkEvent;

struct SkUnixWindow {
  Display* fDisplay;
  Window fWin;
  size_t fOSWin;
  GC fGc;
  GLXContext fGLContext;
  bool fGLCreated;
};

class SkOSWindow : public SkWindow {
public:
    SkOSWindow(void*);
    ~SkOSWindow();

    void* getHWND() const { return (void*)fUnixWindow.fWin; }
    void* getDisplay() const { return (void*)fUnixWindow.fDisplay; }
    void* getUnixWindow() const { return (void*)&fUnixWindow; }
    void loop();
    void post_linuxevent();
    bool attachGL();
    void detachGL();
    void presentGL();

    //static bool PostEvent(SkEvent* evt, SkEventSinkID, SkMSec delay);

    //static bool WndProc(SkUnixWindow* w,  XEvent &e);

protected:
    // Overridden from from SkWindow:
    virtual bool onEvent(const SkEvent&) SK_OVERRIDE;
    virtual void onHandleInval(const SkIRect&) SK_OVERRIDE;
    virtual bool onHandleChar(SkUnichar) SK_OVERRIDE;
    virtual bool onHandleKey(SkKey) SK_OVERRIDE;
    virtual bool onHandleKeyUp(SkKey) SK_OVERRIDE;
    virtual void onSetTitle(const char title[]) SK_OVERRIDE;

private:
    void doPaint();
    void mapWindowAndWait();

    SkUnixWindow fUnixWindow;
    bool fGLAttached;

    // Needed for GL
    XVisualInfo* fVi;

    typedef SkWindow INHERITED;
};

#endif
