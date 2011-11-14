/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkBlitMask_DEFINED
#define SkBlitMask_DEFINED

#include "SkBitmap.h"
#include "SkColor.h"
#include "SkMask.h"

class SkBlitMask {
public:
    /**
     *  Returns true if the device config and mask format were supported.
     *  else return false (nothing was drawn)
     */
    static bool BlitColor(const SkBitmap& device, const SkMask& mask,
                          const SkIRect& clip, SkColor color);

    /**
     *  Function pointer that blits the mask into a device (dst) colorized
     *  by color. The number of pixels to blit is specified by width and height,
     *  but each scanline is offset by dstRB (rowbytes) and srcRB respectively.
     */
    typedef void (*ColorProc)(void* dst, size_t dstRB,
                              const void* mask, size_t maskRB,
                              SkColor color, int width, int height);

    /**
     *  Function pointer that blits a row of src colors through a row of a mask
     *  onto a row of dst colors. The RowFactory that returns this function ptr
     *  will have been told the formats for the mask and the dst.
     */
    typedef void (*RowProc)(void* dst, const void* mask,
                            const SkPMColor* src, int width);
    
    /**
     *  Public entry-point to return a blitmask ColorProc.
     *  May return NULL if config or format are not supported.
     */
    static ColorProc ColorFactory(SkBitmap::Config, SkMask::Format, SkColor);
    
    /**
     *  Public entry-point to return a blitmask RowProc.
     *  May return NULL if config or format are not supported.
     */
    static RowProc RowFactory(SkBitmap::Config, SkMask::Format);
    
    /**
     *  Return either platform specific optimized blitmask ColorProc,
     *  or NULL if no optimized routine is available.
     */
    static ColorProc PlatformColorProcs(SkBitmap::Config, SkMask::Format, SkColor);
    
    /**
     *  Return either platform specific optimized blitmask RowProc,
     *  or NULL if no optimized routine is available.
     */
    static RowProc PlatformRowProcs(SkBitmap::Config, SkMask::Format);
};

#endif
