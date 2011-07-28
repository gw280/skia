
/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "Test.h"
#include "SkPath.h"
#include "SkParse.h"
#include "SkSize.h"

static void check_close(skiatest::Reporter* reporter, const SkPath& path) {
    for (int i = 0; i < 2; ++i) {
        SkPath::Iter iter(path, (bool)i);
        SkPoint mv;
        SkPoint pts[4];
        SkPath::Verb v;
        int nMT = 0;
        int nCL = 0;
        mv.set(0, 0);
        while (SkPath::kDone_Verb != (v = iter.next(pts))) {
            switch (v) {
                case SkPath::kMove_Verb:
                    mv = pts[0];
                    ++nMT;
                    break;
                case SkPath::kClose_Verb:
                    REPORTER_ASSERT(reporter, mv == pts[0]);
                    ++nCL;
                    break;
                default:
                    break;
            }
        }
        // if we force a close on the interator we should have a close
        // for every moveTo
        REPORTER_ASSERT(reporter, !i || nMT == nCL);
    }
}

static void test_close(skiatest::Reporter* reporter) {
    SkPath closePt;
    closePt.moveTo(0, 0);
    closePt.close();
    check_close(reporter, closePt);

    SkPath openPt;
    openPt.moveTo(0, 0);
    check_close(reporter, openPt);

    SkPath empty;
    check_close(reporter, empty);
    empty.close();
    check_close(reporter, empty);

    SkPath rect;
    rect.addRect(SK_Scalar1, SK_Scalar1, 10 * SK_Scalar1, 10*SK_Scalar1);
    check_close(reporter, rect);
    rect.close();
    check_close(reporter, rect);

    SkPath quad;
    quad.quadTo(SK_Scalar1, SK_Scalar1, 10 * SK_Scalar1, 10*SK_Scalar1);
    check_close(reporter, quad);
    quad.close();
    check_close(reporter, quad);

    SkPath cubic;
    quad.cubicTo(SK_Scalar1, SK_Scalar1, 10 * SK_Scalar1, 
                 10*SK_Scalar1, 20 * SK_Scalar1, 20*SK_Scalar1);
    check_close(reporter, cubic);
    cubic.close();
    check_close(reporter, cubic);

    SkPath line;
    line.moveTo(SK_Scalar1, SK_Scalar1);
    line.lineTo(10 * SK_Scalar1, 10*SK_Scalar1);
    check_close(reporter, line);
    line.close();
    check_close(reporter, line);

    SkPath rect2;
    rect2.addRect(SK_Scalar1, SK_Scalar1, 10 * SK_Scalar1, 10*SK_Scalar1);
    rect2.close();
    rect2.addRect(SK_Scalar1, SK_Scalar1, 10 * SK_Scalar1, 10*SK_Scalar1);
    check_close(reporter, rect2);
    rect2.close();
    check_close(reporter, rect2);

    SkPath oval3;
    oval3.addOval(SkRect::MakeWH(SK_Scalar1*100,SK_Scalar1*100));
    oval3.close();
    oval3.addOval(SkRect::MakeWH(SK_Scalar1*200,SK_Scalar1*200));
    check_close(reporter, oval3);
    oval3.close();
    check_close(reporter, oval3);

    SkPath moves;
    moves.moveTo(SK_Scalar1, SK_Scalar1);
    moves.moveTo(5 * SK_Scalar1, SK_Scalar1);
    moves.moveTo(SK_Scalar1, 10 * SK_Scalar1);
    moves.moveTo(10 *SK_Scalar1, SK_Scalar1);
    check_close(reporter, moves);
}

static void check_convexity(skiatest::Reporter* reporter, const SkPath& path,
                            SkPath::Convexity expected) {
    SkPath::Convexity c = SkPath::ComputeConvexity(path);
    REPORTER_ASSERT(reporter, c == expected);
}

static void test_convexity2(skiatest::Reporter* reporter) {
    SkPath pt;
    pt.moveTo(0, 0);
    pt.close();
    check_convexity(reporter, pt, SkPath::kConvex_Convexity);
    
    SkPath line;
    line.moveTo(12, 20);
    line.lineTo(-12, -20);
    line.close();
    check_convexity(reporter, pt, SkPath::kConvex_Convexity);
    
    SkPath triLeft;
    triLeft.moveTo(0, 0);
    triLeft.lineTo(1, 0);
    triLeft.lineTo(1, 1);
    triLeft.close();
    check_convexity(reporter, triLeft, SkPath::kConvex_Convexity);
    
    SkPath triRight;
    triRight.moveTo(0, 0);
    triRight.lineTo(-1, 0);
    triRight.lineTo(1, 1);
    triRight.close();
    check_convexity(reporter, triRight, SkPath::kConvex_Convexity);
    
    SkPath square;
    square.moveTo(0, 0);
    square.lineTo(1, 0);
    square.lineTo(1, 1);
    square.lineTo(0, 1);
    square.close();
    check_convexity(reporter, square, SkPath::kConvex_Convexity);
    
    SkPath redundantSquare;
    redundantSquare.moveTo(0, 0);
    redundantSquare.lineTo(0, 0);
    redundantSquare.lineTo(0, 0);
    redundantSquare.lineTo(1, 0);
    redundantSquare.lineTo(1, 0);
    redundantSquare.lineTo(1, 0);
    redundantSquare.lineTo(1, 1);
    redundantSquare.lineTo(1, 1);
    redundantSquare.lineTo(1, 1);
    redundantSquare.lineTo(0, 1);
    redundantSquare.lineTo(0, 1);
    redundantSquare.lineTo(0, 1);
    redundantSquare.close();
    check_convexity(reporter, redundantSquare, SkPath::kConvex_Convexity);
    
    SkPath bowTie;
    bowTie.moveTo(0, 0);
    bowTie.lineTo(0, 0);
    bowTie.lineTo(0, 0);
    bowTie.lineTo(1, 1);
    bowTie.lineTo(1, 1);
    bowTie.lineTo(1, 1);
    bowTie.lineTo(1, 0);
    bowTie.lineTo(1, 0);
    bowTie.lineTo(1, 0);
    bowTie.lineTo(0, 1);
    bowTie.lineTo(0, 1);
    bowTie.lineTo(0, 1);
    bowTie.close();
    check_convexity(reporter, bowTie, SkPath::kConcave_Convexity);
    
    SkPath spiral;
    spiral.moveTo(0, 0);
    spiral.lineTo(100, 0);
    spiral.lineTo(100, 100);
    spiral.lineTo(0, 100);
    spiral.lineTo(0, 50);
    spiral.lineTo(50, 50);
    spiral.lineTo(50, 75);
    spiral.close();
    check_convexity(reporter, spiral, SkPath::kConcave_Convexity);
    
    SkPath dent;
    dent.moveTo(SkIntToScalar(0), SkIntToScalar(0));
    dent.lineTo(SkIntToScalar(100), SkIntToScalar(100));
    dent.lineTo(SkIntToScalar(0), SkIntToScalar(100));
    dent.lineTo(SkIntToScalar(-50), SkIntToScalar(200));
    dent.lineTo(SkIntToScalar(-200), SkIntToScalar(100));
    dent.close();
    check_convexity(reporter, dent, SkPath::kConcave_Convexity);
}

static void check_convex_bounds(skiatest::Reporter* reporter, const SkPath& p,
                                const SkRect& bounds) {
    REPORTER_ASSERT(reporter, p.isConvex());
    REPORTER_ASSERT(reporter, p.getBounds() == bounds);

    SkPath p2(p);
    REPORTER_ASSERT(reporter, p2.isConvex());
    REPORTER_ASSERT(reporter, p2.getBounds() == bounds);

    SkPath other;
    other.swap(p2);
    REPORTER_ASSERT(reporter, other.isConvex());
    REPORTER_ASSERT(reporter, other.getBounds() == bounds);
}

static void setFromString(SkPath* path, const char str[]) {
    bool first = true;
    while (str) {
        SkScalar x, y;
        str = SkParse::FindScalar(str, &x);
        if (NULL == str) {
            break;
        }
        str = SkParse::FindScalar(str, &y);
        SkASSERT(str);
        if (first) {
            path->moveTo(x, y);
            first = false;
        } else {
            path->lineTo(x, y);
        }
    }
}

static void test_convexity(skiatest::Reporter* reporter) {
    static const SkPath::Convexity C = SkPath::kConcave_Convexity;
    static const SkPath::Convexity V = SkPath::kConvex_Convexity;

    SkPath path;

    REPORTER_ASSERT(reporter, V == SkPath::ComputeConvexity(path));
    path.addCircle(0, 0, 10);
    REPORTER_ASSERT(reporter, V == SkPath::ComputeConvexity(path));
    path.addCircle(0, 0, 10);   // 2nd circle
    REPORTER_ASSERT(reporter, C == SkPath::ComputeConvexity(path));
    path.reset();
    path.addRect(0, 0, 10, 10, SkPath::kCCW_Direction);
    REPORTER_ASSERT(reporter, V == SkPath::ComputeConvexity(path));
    path.reset();
    path.addRect(0, 0, 10, 10, SkPath::kCW_Direction);
    REPORTER_ASSERT(reporter, V == SkPath::ComputeConvexity(path));
    
    static const struct {
        const char*         fPathStr;
        SkPath::Convexity   fExpectedConvexity;
    } gRec[] = {
        { "", SkPath::kConvex_Convexity },
        { "0 0", SkPath::kConvex_Convexity },
        { "0 0 10 10", SkPath::kConvex_Convexity },
        { "0 0 10 10 20 20 0 0 10 10", SkPath::kConcave_Convexity },
        { "0 0 10 10 10 20", SkPath::kConvex_Convexity },
        { "0 0 10 10 10 0", SkPath::kConvex_Convexity },
        { "0 0 10 10 10 0 0 10", SkPath::kConcave_Convexity },
        { "0 0 10 0 0 10 -10 -10", SkPath::kConcave_Convexity },
    };

    for (size_t i = 0; i < SK_ARRAY_COUNT(gRec); ++i) {
        SkPath path;
        setFromString(&path, gRec[i].fPathStr);
        SkPath::Convexity c = SkPath::ComputeConvexity(path);
        REPORTER_ASSERT(reporter, c == gRec[i].fExpectedConvexity);
    }
}

// Simple isRect test is inline TestPath, below.
// test_isRect provides more extensive testing.
static void test_isRect(skiatest::Reporter* reporter) {
    // passing tests (all moveTo / lineTo...
    SkPoint r1[] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    SkPoint r2[] = {{1, 0}, {1, 1}, {0, 1}, {0, 0}};
    SkPoint r3[] = {{1, 1}, {0, 1}, {0, 0}, {1, 0}};
    SkPoint r4[] = {{0, 1}, {0, 0}, {1, 0}, {1, 1}};
    SkPoint r5[] = {{0, 0}, {0, 1}, {1, 1}, {1, 0}};
    SkPoint r6[] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
    SkPoint r7[] = {{1, 1}, {1, 0}, {0, 0}, {0, 1}};
    SkPoint r8[] = {{1, 0}, {0, 0}, {0, 1}, {1, 1}};
    SkPoint r9[] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
    SkPoint ra[] = {{0, 0}, {0, .5f}, {0, 1}, {.5f, 1}, {1, 1}, {1, .5f},
        {1, 0}, {.5f, 0}};
    SkPoint rb[] = {{0, 0}, {.5f, 0}, {1, 0}, {1, .5f}, {1, 1}, {.5f, 1},
        {0, 1}, {0, .5f}};
    SkPoint rc[] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0, 0}};
    SkPoint rd[] = {{0, 0}, {0, 1}, {1, 1}, {1, 0}, {0, 0}};
    SkPoint re[] = {{0, 0}, {1, 0}, {1, 0}, {1, 1}, {0, 1}};
    
    // failing tests
    SkPoint f1[] = {{0, 0}, {1, 0}, {1, 1}}; // too few points
    SkPoint f2[] = {{0, 0}, {1, 1}, {0, 1}, {1, 0}}; // diagonal
    SkPoint f3[] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0, 0}, {1, 0}}; // wraps
    SkPoint f4[] = {{0, 0}, {1, 0}, {0, 0}, {1, 0}, {1, 1}, {0, 1}}; // backs up
    SkPoint f5[] = {{0, 0}, {1, 0}, {1, 1}, {2, 0}}; // end overshoots
    SkPoint f6[] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0, 2}}; // end overshoots
    SkPoint f7[] = {{0, 0}, {1, 0}, {1, 1}, {0, 2}}; // end overshoots
    SkPoint f8[] = {{0, 0}, {1, 0}, {1, 1}, {1, 0}}; // 'L'
    
    // failing, no close
    SkPoint c1[] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}}; // close doesn't match
    SkPoint c2[] = {{0, 0}, {1, 0}, {1, 2}, {0, 2}, {0, 1}}; // ditto

    size_t testLen[] = {
        sizeof(r1), sizeof(r2), sizeof(r3), sizeof(r4), sizeof(r5), sizeof(r6),
        sizeof(r7), sizeof(r8), sizeof(r9), sizeof(ra), sizeof(rb), sizeof(rc),
        sizeof(rd), sizeof(re),
        sizeof(f1), sizeof(f2), sizeof(f3), sizeof(f4), sizeof(f5), sizeof(f6),
        sizeof(f7), sizeof(f8),
        sizeof(c1), sizeof(c2) 
    };
    SkPoint* tests[] = {
        r1, r2, r3, r4, r5, r6, r7, r8, r9, ra, rb, rc, rd, re,
        f1, f2, f3, f4, f5, f6, f7, f8,
        c1, c2 
    };
    SkPoint* lastPass = re;
    SkPoint* lastClose = f8;
    bool fail = false;
    bool close = true;
    const size_t testCount = sizeof(tests) / sizeof(tests[0]);
    size_t index;
    for (size_t testIndex = 0; testIndex < testCount; ++testIndex) {
        SkPath path;
        path.moveTo(tests[testIndex][0].fX, tests[testIndex][0].fY);
        for (index = 1; index < testLen[testIndex] / sizeof(SkPoint); ++index) {
            path.lineTo(tests[testIndex][index].fX, tests[testIndex][index].fY);
        }
        if (close) {
            path.close();
        }
        REPORTER_ASSERT(reporter, fail ^ path.isRect(0));
        if (tests[testIndex] == lastPass) {
            fail = true;
        }
        if (tests[testIndex] == lastClose) {
            close = false;
        }
    }
    
    // fail, close then line
    SkPath path1;
    path1.moveTo(r1[0].fX, r1[0].fY);
    for (index = 1; index < testLen[0] / sizeof(SkPoint); ++index) {
        path1.lineTo(r1[index].fX, r1[index].fY);
    }
    path1.close();
    path1.lineTo(1, 0);
    REPORTER_ASSERT(reporter, fail ^ path1.isRect(0));
    
    // fail, move in the middle
    path1.reset();
    path1.moveTo(r1[0].fX, r1[0].fY);
    for (index = 1; index < testLen[0] / sizeof(SkPoint); ++index) {
        if (index == 2) {
            path1.moveTo(1, .5f);
        }
        path1.lineTo(r1[index].fX, r1[index].fY);
    }
    path1.close();
    REPORTER_ASSERT(reporter, fail ^ path1.isRect(0));

    // fail, move on the edge
    path1.reset();
    for (index = 1; index < testLen[0] / sizeof(SkPoint); ++index) {
        path1.moveTo(r1[index - 1].fX, r1[index - 1].fY);
        path1.lineTo(r1[index].fX, r1[index].fY);
    }
    path1.close();
    REPORTER_ASSERT(reporter, fail ^ path1.isRect(0));
    
    // fail, quad
    path1.reset();
    path1.moveTo(r1[0].fX, r1[0].fY);
    for (index = 1; index < testLen[0] / sizeof(SkPoint); ++index) {
        if (index == 2) {
            path1.quadTo(1, .5f, 1, .5f);
        }
        path1.lineTo(r1[index].fX, r1[index].fY);
    }
    path1.close();
    REPORTER_ASSERT(reporter, fail ^ path1.isRect(0));
    
    // fail, cubic
    path1.reset();
    path1.moveTo(r1[0].fX, r1[0].fY);
    for (index = 1; index < testLen[0] / sizeof(SkPoint); ++index) {
        if (index == 2) {
            path1.cubicTo(1, .5f, 1, .5f, 1, .5f);
        }
        path1.lineTo(r1[index].fX, r1[index].fY);
    }
    path1.close();
    REPORTER_ASSERT(reporter, fail ^ path1.isRect(0));
}

void TestPath(skiatest::Reporter* reporter);
void TestPath(skiatest::Reporter* reporter) {
    {
        SkSize size;
        size.fWidth = 3.4f;
        size.width();
        size = SkSize::Make(3,4);
        SkISize isize = SkISize::Make(3,4);
    }

    SkTSize<SkScalar>::Make(3,4);

    SkPath  p, p2;
    SkRect  bounds, bounds2;

    REPORTER_ASSERT(reporter, p.isEmpty());
    REPORTER_ASSERT(reporter, p.isConvex());
    REPORTER_ASSERT(reporter, p.getFillType() == SkPath::kWinding_FillType);
    REPORTER_ASSERT(reporter, !p.isInverseFillType());
    REPORTER_ASSERT(reporter, p == p2);
    REPORTER_ASSERT(reporter, !(p != p2));

    REPORTER_ASSERT(reporter, p.getBounds().isEmpty());

    bounds.set(0, 0, SK_Scalar1, SK_Scalar1);

    p.addRoundRect(bounds, SK_Scalar1, SK_Scalar1);
    check_convex_bounds(reporter, p, bounds);

    p.reset();
    p.addOval(bounds);
    check_convex_bounds(reporter, p, bounds);

    p.reset();
    p.addRect(bounds);
    check_convex_bounds(reporter, p, bounds);

    REPORTER_ASSERT(reporter, p != p2);
    REPORTER_ASSERT(reporter, !(p == p2));

    // does getPoints return the right result
    REPORTER_ASSERT(reporter, p.getPoints(NULL, 5) == 4);
    SkPoint pts[4];
    int count = p.getPoints(pts, 4);
    REPORTER_ASSERT(reporter, count == 4);
    bounds2.set(pts, 4);
    REPORTER_ASSERT(reporter, bounds == bounds2);

    bounds.offset(SK_Scalar1*3, SK_Scalar1*4);
    p.offset(SK_Scalar1*3, SK_Scalar1*4);
    REPORTER_ASSERT(reporter, bounds == p.getBounds());

    REPORTER_ASSERT(reporter, p.isRect(NULL));
    bounds2.setEmpty();
    REPORTER_ASSERT(reporter, p.isRect(&bounds2));
    REPORTER_ASSERT(reporter, bounds == bounds2);

    // now force p to not be a rect
    bounds.set(0, 0, SK_Scalar1/2, SK_Scalar1/2);
    p.addRect(bounds);
    REPORTER_ASSERT(reporter, !p.isRect(NULL));
    test_isRect(reporter);

    SkPoint pt;

    p.moveTo(SK_Scalar1, 0);
    p.getLastPt(&pt);
    REPORTER_ASSERT(reporter, pt.fX == SK_Scalar1);

    test_convexity(reporter);
    test_convexity2(reporter);
    test_close(reporter);
}

#include "TestClassDef.h"
DEFINE_TESTCLASS("Path", PathTestClass, TestPath)
