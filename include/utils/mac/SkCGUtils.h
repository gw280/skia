#ifndef SkCGUtils_DEFINED
#define SkCGUtils_DEFINED

#include "SkTypes.h"

#ifdef SK_BUILD_FOR_MAC
    #include <Carbon/Carbon.h>
#else
    #include <CoreGraphics/CoreGraphics.h>
#endif

class SkBitmap;

/**
 *  Create an imageref from the specified bitmap using the specified colorspace.
 */
CGImageRef SkCreateCGImageRefWithColorspace(const SkBitmap& bm,
                                            CGColorSpaceRef space);

/**
 *  Create an imageref from the specified bitmap using the colorspace
 *  kCGColorSpaceGenericRGB
 */
static inline CGImageRef SkCreateCGImageRef(const SkBitmap& bm) {
    return SkCreateCGImageRefWithColorspace(bm, NULL);
}

/**
 *  Draw the bitmap into the specified CG context. The bitmap will be converted
 *  to a CGImage using the generic RGB colorspace. (x,y) specifies the position
 *  of the top-left corner of the bitmap.
 */
void SkCGDrawBitmap(CGContextRef, const SkBitmap&, float x, float y);

#endif
