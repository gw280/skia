#include "EdgeWalker_Test.h"
#include "Intersection_Tests.h"
#include "SkBitmap.h"
#include "SkCanvas.h"
#include <assert.h>


static void* testSimplify4x4QuadraticsMain(void* data)
{
    char pathStr[1024];
    bzero(pathStr, sizeof(pathStr));
    SkASSERT(data);
    State4& state = *(State4*) data;
    int ax = state.a & 0x03;
    int ay = state.a >> 2;
    int bx = state.b & 0x03;
    int by = state.b >> 2;
    int cx = state.c & 0x03;
    int cy = state.c >> 2;
    int dx = state.d & 0x03;
    int dy = state.d >> 2;
    for (int e = 0 ; e < 16; ++e) {
        int ex = e & 0x03;
        int ey = e >> 2;
        for (int f = e ; f < 16; ++f) {
            int fx = f & 0x03;
            int fy = f >> 2;
            for (int g = f ; g < 16; ++g) {
                int gx = g & 0x03;
                int gy = g >> 2;
                for (int h = g ; h < 16; ++h) {
                    int hx = h & 0x03;
                    int hy = h >> 2;
                    SkPath path, out;
                    path.setFillType(SkPath::kWinding_FillType);
                    path.moveTo(ax, ay);
                    path.quadTo(bx, by, cx, cy);
                    path.lineTo(dx, dy);
                    path.close();
                    path.moveTo(ex, ey);
                    path.lineTo(fx, fy);
                    path.quadTo(gx, gy, hx, hy);
                    path.close();
                    if (1) {  // gdb: set print elements 400
                        char* str = pathStr;
                        str += sprintf(str, "    path.moveTo(%d, %d);\n", ax, ay);
                        str += sprintf(str, "    path.quadTo(%d, %d, %d, %d);\n", bx, by, cx, cy);
                        str += sprintf(str, "    path.lineTo(%d, %d);\n", dx, dy);
                        str += sprintf(str, "    path.close();\n");
                        str += sprintf(str, "    path.moveTo(%d, %d);\n", ex, ey);
                        str += sprintf(str, "    path.lineTo(%d, %d);\n", fx, fy);
                        str += sprintf(str, "    path.quadTo(%d, %d, %d, %d);\n", gx, gy, hx, hy);
                        str += sprintf(str, "    path.close();");
                    }
                    if (!testSimplify(path, true, out, state.bitmap, state.canvas)) {
                        SkDebugf("*/\n{ SkPath::kWinding_FillType, %d, %d, %d, %d,"
                                " %d, %d, %d, %d },\n/*\n", state.a, state.b, state.c, state.d,
                                e, f, g, h);
                    }
                    path.setFillType(SkPath::kEvenOdd_FillType);
                    if (!testSimplify(path, true, out, state.bitmap, state.canvas)) {
                        SkDebugf("*/\n{ SkPath::kEvenOdd_FillType, %d, %d, %d, %d,"
                                " %d, %d, %d, %d },\n/*\n", state.a, state.b, state.c, state.d,
                                e, f, g, h);
                    }
                }
            }
        }
    }
    return NULL;
}

const int maxThreads = gRunTestsInOneThread ? 1 : 24;

void Simplify4x4QuadraticsThreaded_Test()
{
    State4 threadState[maxThreads];
    int threadIndex = 0;
    for (int a = 0; a < 16; ++a) {
        for (int b = a ; b < 16; ++b) {
            for (int c = b ; c < 16; ++c) {
                for (int d = c; d < 16; ++d) {                 
                    State4* statePtr = &threadState[threadIndex];
                    statePtr->a = a;
                    statePtr->b = b;
                    statePtr->c = c;
                    statePtr->d = d;
                    if (maxThreads > 1) {
                        createThread(statePtr, testSimplify4x4QuadraticsMain);
                        if (++threadIndex >= maxThreads) {
                            waitForCompletion(threadState, threadIndex);
                        }
                    } else {
                        testSimplify4x4QuadraticsMain(statePtr);
                    }
                }
            }
        }
    }
    waitForCompletion(threadState, threadIndex);
}
