#define SCALE_FILTER_NAME       MAKENAME(_filter_DX_shaderproc)

#ifndef PREAMBLE
    #define PREAMBLE(state)
    #define PREAMBLE_PARAM_X
    #define PREAMBLE_PARAM_Y
    #define PREAMBLE_ARG_X
    #define PREAMBLE_ARG_Y
#endif


static void SCALE_FILTER_NAME(const SkBitmapProcState& s, int x, int y,
                              DSTTYPE* SK_RESTRICT colors, int count) {
    SkASSERT((s.fInvType & ~(SkMatrix::kTranslate_Mask |
                             SkMatrix::kScale_Mask)) == 0);
    SkASSERT(s.fInvKy == 0);
    SkASSERT(count > 0 && colors != NULL);
    SkASSERT(s.fDoFilter);
    SkDEBUGCODE(CHECKSTATE(s);)

    PREAMBLE(s);

    const unsigned maxX = s.fBitmap->width() - 1;
    const SkFixed oneX = s.fFilterOneX;
    const SkFixed dx = s.fInvSx;
    SkFixed fx;
    const SRCTYPE* SK_RESTRICT row0;
    const SRCTYPE* SK_RESTRICT row1;
    unsigned subY;

    {
        SkPoint pt;
        s.fInvProc(*s.fInvMatrix, SkIntToScalar(x) + SK_ScalarHalf,
                   SkIntToScalar(y) + SK_ScalarHalf, &pt);
        SkFixed fy = SkScalarToFixed(pt.fY) - (s.fFilterOneY >> 1);
        const unsigned maxY = s.fBitmap->height() - 1;
        // compute our two Y values up front
        subY = TILEY_LOW_BITS(fy, maxY);
        int y0 = TILEY_PROCF(fy, maxY);
        int y1 = TILEY_PROCF((fy + s.fFilterOneY), maxY);

        const char* SK_RESTRICT srcAddr = (const char*)s.fBitmap->getPixels();
        unsigned rb = s.fBitmap->rowBytes();
        row0 = (const SRCTYPE*)(srcAddr + y0 * rb);
        row1 = (const SRCTYPE*)(srcAddr + y1 * rb);
        // now initialize fx
        fx = SkScalarToFixed(pt.fX) - (oneX >> 1);
    }

    do {
        unsigned subX = TILEX_LOW_BITS(fx, maxX);
        unsigned x0 = TILEX_PROCF(fx, maxX);
        unsigned x1 = TILEX_PROCF((fx + oneX), maxX);

        uint32_t c = FILTER_PROC(subX, subY,
                                 SRC_TO_FILTER(row0[x0]),
                                 SRC_TO_FILTER(row0[x1]),
                                 SRC_TO_FILTER(row1[x0]),
                                 SRC_TO_FILTER(row1[x1]));
        *colors++ = FILTER_TO_DST(c);

        fx += dx;
    } while (--count != 0);
}

///////////////////////////////////////////////////////////////////////////////

#undef MAKENAME
#undef TILEX_PROCF
#undef TILEY_PROCF
#undef TILEX_LOW_BITS
#undef TILEY_LOW_BITS
#undef DSTTYPE
#ifdef CHECK_FOR_DECAL
    #undef CHECK_FOR_DECAL
#endif

#undef SCALE_FILTER_NAME

#undef PREAMBLE
#undef PREAMBLE_PARAM_X
#undef PREAMBLE_PARAM_Y
#undef PREAMBLE_ARG_X
#undef PREAMBLE_ARG_Y
