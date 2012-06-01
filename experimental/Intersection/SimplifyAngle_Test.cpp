/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "Simplify.h"

namespace SimplifyAngleTest {

#include "Simplify.cpp"

} // end of SimplifyAngleTest namespace

#include "Intersection_Tests.h"

static const SkPoint lines[][2] = {
    { { 10,  10}, { 10,  20} },
    { { 10,  10}, { 20,  10} },
    { { 10,  10}, {-20,  10} },
    { { 10,  10}, { 10, -20} },
    { { 10,  10}, { 20,  20} },
    { { 10,  10}, {-20, -20} },
    { { 10,  10}, {-20,  40} },
    { { 10,  10}, { 40, -20} }
};

static const size_t lineCount = sizeof(lines) / sizeof(lines[0]);

static const SkPoint quads[][3] = {
    {{ 1,  1}, { 2,  2}, { 1,  3}}, // 0
    {{ 1,  1}, { 3,  3}, { 1,  5}}, // 1
    {{ 1,  1}, { 4,  4}, { 1,  7}}, // 2
    {{ 1,  1}, { 5,  5}, { 9,  9}}, // 3
    {{ 1,  1}, { 4,  4}, { 7,  1}}, // 4
    {{ 1,  1}, { 3,  3}, { 5,  1}}, // 5
    {{ 1,  1}, { 2,  2}, { 3,  1}}, // 6
};

static const size_t quadCount = sizeof(quads) / sizeof(quads[0]);

static const SkPoint cubics[][4] = {
    {{ 1,  1}, { 2,  2}, { 2,  3}, { 1,  4}},
    {{ 1,  1}, { 3,  3}, { 3,  5}, { 1,  7}},
    {{ 1,  1}, { 4,  4}, { 4,  7}, { 1,  10}},
    {{ 1,  1}, { 5,  5}, { 8,  8}, { 9,  9}},
    {{ 1,  1}, { 4,  4}, { 7,  4}, { 10, 1}},
    {{ 1,  1}, { 3,  3}, { 5,  3}, { 7,  1}},
    {{ 1,  1}, { 2,  2}, { 3,  2}, { 4,  1}},
};

static const size_t cubicCount = sizeof(cubics) / sizeof(cubics[0]);

static void testLines(bool testFlat) {
    // create angles in a circle
    SkTDArray<SimplifyAngleTest::Angle> angles;
    SkTDArray<SimplifyAngleTest::Angle* > angleList;
    SkTDArray<double> arcTans;
    size_t x;
    for (x = 0; x < lineCount; ++x) {
        SimplifyAngleTest::Angle* angle = angles.append();
        if (testFlat) {
            angle->setFlat(lines[x], SkPath::kLine_Verb, 0, x, x + 1);
        } else {
            angle->set(lines[x], SkPath::kLine_Verb, 0, x, x + 1);
        }
        double arcTan = atan2(lines[x][0].fX - lines[x][1].fX,
                lines[x][0].fY - lines[x][1].fY);
        arcTans.push(arcTan);
    }
    for (x = 0; x < lineCount; ++x) {
        angleList.push(&angles[x]);
    }
    QSort<SimplifyAngleTest::Angle>(angleList.begin(), angleList.end() - 1);
    bool first = true;
    bool wrap = false;
    double base, last;
    for (size_t x = 0; x < lineCount; ++x) {
        const SimplifyAngleTest::Angle* angle = angleList[x];
        int span = angle->start();
//        SkDebugf("%s [%d] %1.9g (%1.9g,%1.9g %1.9g,%1.9g)\n", __FUNCTION__,
//                span, arcTans[span], lines[span][0].fX, lines[span][0].fY,
//                lines[span][1].fX, lines[span][1].fY);
        if (first) {
            base = last = arcTans[span];
            first = false;
            continue;
        }
        if (last < arcTans[span]) {
            last = arcTans[span];
            continue;
        }
        if (!wrap) {
            if (base < arcTans[span]) {
                SkDebugf("%s !wrap [%d] %g\n", __FUNCTION__, span, arcTans[span]);
                SkASSERT(0);
            }
            last = arcTans[span];
            wrap = true;
            continue;
        }
        SkDebugf("%s wrap [%d] %g\n", __FUNCTION__, span, arcTans[span]);
        SkASSERT(0);
    }
}

static void testQuads(bool testFlat) {
    SkTDArray<SimplifyAngleTest::Angle> angles;
    SkTDArray<SimplifyAngleTest::Angle* > angleList;
    size_t x;
    for (x = 0; x < quadCount; ++x) {
        SimplifyAngleTest::Angle* angle = angles.append();
        if (testFlat) {
            angle->setFlat(quads[x], SkPath::kQuad_Verb, 0, x, x + 1);
        } else {
            angle->set(quads[x], SkPath::kQuad_Verb, 0, x, x + 1);
        }
    }
    for (x = 0; x < quadCount; ++x) {
        angleList.push(&angles[x]);
    }
    QSort<SimplifyAngleTest::Angle>(angleList.begin(), angleList.end() - 1);
    for (size_t x = 0; x < quadCount; ++x) {
        *angleList[x] < *angleList[x + 1];
        SkASSERT(x == quadCount - 1 || *angleList[x] < *angleList[x + 1]);
        const SimplifyAngleTest::Angle* angle = angleList[x];
        if (x != angle->start()) {
            SkDebugf("%s [%d] [%d]\n", __FUNCTION__, x, angle->start());
            SkASSERT(0);
        }
    }
}

static void testCubics(bool testFlat) {
    SkTDArray<SimplifyAngleTest::Angle> angles;
    SkTDArray<SimplifyAngleTest::Angle* > angleList;
    for (size_t x = 0; x < cubicCount; ++x) {
        SimplifyAngleTest::Angle* angle = angles.append();
        if (testFlat) {
            angle->setFlat(cubics[x], SkPath::kCubic_Verb, 0, x, x + 1);
        } else {
            angle->set(cubics[x], SkPath::kCubic_Verb, 0, x, x + 1);
        }
        angleList.push(angle);
    }
    QSort<SimplifyAngleTest::Angle>(angleList.begin(), angleList.end() - 1);
    for (size_t x = 0; x < cubicCount; ++x) {
        const SimplifyAngleTest::Angle* angle = angleList[x];
        if (x != angle->start()) {
            SkDebugf("%s [%d] [%d]\n", __FUNCTION__, x, angle->start());
            SkASSERT(0);
        }
    }
}

static void (*tests[])(bool) = {
    testLines,
    testQuads,
    testCubics
};

static const size_t testCount = sizeof(tests) / sizeof(tests[0]);

static void (*firstTest)(bool) = 0;
static bool skipAll = false;

void SimplifyAngle_Test() {
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
        (*tests[index])(true);
        firstTestComplete = true;
    }
}
