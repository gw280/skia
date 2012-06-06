/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "Simplify.h"

namespace SimplifyNewTest {

#include "Simplify.cpp"

} // end of SimplifyNewTest namespace

#include "EdgeWalker_Test.h"
#include "Intersection_Tests.h"

static SkBitmap bitmap;

static bool testSimplifyx(const SkPath& path, SkPath& out, SkBitmap& bitmap) {
    if (false) {
        showPath(path);
    }
    simplifyx(path, out);
    if (false) {
        return true;
    }
    return comparePaths(path, out, bitmap, 0) == 0;
}

static void testLine1() {
    SkPath path, simple;
    path.moveTo(2,0);
    path.lineTo(1,1);
    path.lineTo(0,0);
    path.close();
    testSimplifyx(path, simple, bitmap);
}

static void addInnerCWTriangle(SkPath& path) {
    path.moveTo(3,0);
    path.lineTo(4,1);
    path.lineTo(2,1);
    path.close();
}

static void addInnerCCWTriangle(SkPath& path) {
    path.moveTo(3,0);
    path.lineTo(2,1);
    path.lineTo(4,1);
    path.close();
}

static void addOuterCWTriangle(SkPath& path) {
    path.moveTo(3,0);
    path.lineTo(6,2);
    path.lineTo(0,2);
    path.close();
}

static void addOuterCCWTriangle(SkPath& path) {
    path.moveTo(3,0);
    path.lineTo(0,2);
    path.lineTo(6,2);
    path.close();
}

static void testLine2() {
    SkPath path, simple;
    addInnerCWTriangle(path);
    addOuterCWTriangle(path);
    testSimplifyx(path, simple, bitmap);
}

static void testLine3() {
    SkPath path, simple;
    addInnerCCWTriangle(path);
    addOuterCWTriangle(path);
    testSimplifyx(path, simple, bitmap);
}

static void testLine4() {
    SkPath path, simple;
    addOuterCCWTriangle(path);
    addOuterCWTriangle(path);
    testSimplifyx(path, simple, bitmap);
}

static void testLine5() {
    SkPath path, simple;
    addOuterCWTriangle(path);
    addOuterCWTriangle(path);
    testSimplifyx(path, simple, bitmap);
}

static void (*tests[])() = {
    testLine1,
    testLine2,
    testLine3,
    testLine4,
    testLine5
};

static const size_t testCount = sizeof(tests) / sizeof(tests[0]);

static void (*firstTest)() = testLine5;
static bool skipAll = false;

void SimplifyNew_Test() {
    if (skipAll) {
        return;
    }
    size_t index = 0;
    if (firstTest) {
        while (index < testCount && tests[index] != firstTest) {
            ++index;
        }
    }
    bool firstTestComplete = false;
    for ( ; index < testCount; ++index) {
        SkDebugf("%s [%d]\n", __FUNCTION__, index + 1);
        (*tests[index])();
        firstTestComplete = true;
    }
}
