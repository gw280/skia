/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "SkPDFDevice.h"

#include "SkColor.h"
#include "SkGlyphCache.h"
#include "SkPaint.h"
#include "SkPath.h"
#include "SkPDFImage.h"
#include "SkPDFGraphicState.h"
#include "SkPDFFont.h"
#include "SkPDFFormXObject.h"
#include "SkPDFTypes.h"
#include "SkPDFStream.h"
#include "SkRect.h"
#include "SkString.h"
#include "SkTextFormatParams.h"
#include "SkTypeface.h"
#include "SkTypes.h"

#define NOT_IMPLEMENTED(condition, assert)                         \
    do {                                                           \
        if (condition) {                                           \
            fprintf(stderr, "NOT_IMPLEMENTED: " #condition "\n");  \
            SkASSERT(!assert);                                     \
        }                                                          \
    } while(0)

// Utility functions

namespace {

SkString toPDFColor(SkColor color) {
    SkASSERT(SkColorGetA(color) == 0xFF);  // We handle alpha elsewhere.
    SkScalar colorMax = SkIntToScalar(0xFF);
    SkString result;
    result.appendScalar(SkScalarDiv(SkIntToScalar(SkColorGetR(color)),
                                    colorMax));
    result.append(" ");
    result.appendScalar(SkScalarDiv(SkIntToScalar(SkColorGetG(color)),
                                    colorMax));
    result.append(" ");
    result.appendScalar(SkScalarDiv(SkIntToScalar(SkColorGetB(color)),
                                    colorMax));
    result.append(" ");
    return result;
}

SkPaint calculateTextPaint(const SkPaint& paint) {
    SkPaint result = paint;
    if (result.isFakeBoldText()) {
        SkScalar fakeBoldScale = SkScalarInterpFunc(result.getTextSize(),
                                                    kStdFakeBoldInterpKeys,
                                                    kStdFakeBoldInterpValues,
                                                    kStdFakeBoldInterpLength);
        SkScalar width = SkScalarMul(result.getTextSize(), fakeBoldScale);
        if (result.getStyle() == SkPaint::kFill_Style)
            result.setStyle(SkPaint::kStrokeAndFill_Style);
        else
            width += result.getStrokeWidth();
        result.setStrokeWidth(width);
    }
    return result;
}

// Stolen from measure_text in SkDraw.cpp and then tweaked.
void alignText(SkDrawCacheProc glyphCacheProc, const SkPaint& paint,
               const uint16_t* glyphs, size_t len, SkScalar* x, SkScalar* y,
               SkScalar* width) {
    if (paint.getTextAlign() == SkPaint::kLeft_Align && width == NULL)
        return;

    SkMatrix ident;
    ident.reset();
    SkAutoGlyphCache autoCache(paint, &ident);
    SkGlyphCache* cache = autoCache.getCache();

    const char* start = (char*)glyphs;
    const char* stop = (char*)(glyphs + len);
    SkFixed xAdv = 0, yAdv = 0;

    // TODO(vandebo) This probably needs to take kerning into account.
    while (start < stop) {
        const SkGlyph& glyph = glyphCacheProc(cache, &start, 0, 0);
        xAdv += glyph.fAdvanceX;
        yAdv += glyph.fAdvanceY;
    };
    if (width)
        *width = SkFixedToScalar(xAdv);
    if (paint.getTextAlign() == SkPaint::kLeft_Align)
        return;

    SkScalar xAdj = SkFixedToScalar(xAdv);
    SkScalar yAdj = SkFixedToScalar(yAdv);
    if (paint.getTextAlign() == SkPaint::kCenter_Align) {
        xAdj = SkScalarHalf(xAdj);
        yAdj = SkScalarHalf(yAdj);
    }
    *x = *x - xAdj;
    *y = *y - yAdj;
}

}  // namespace

////////////////////////////////////////////////////////////////////////////////

SkDevice* SkPDFDeviceFactory::newDevice(SkBitmap::Config config,
                                        int width, int height,
                                        bool isOpaque, bool isForLayer) {
    return SkNEW_ARGS(SkPDFDevice, (width, height));
}

SkPDFDevice::SkPDFDevice(int width, int height)
    : fWidth(width),
      fHeight(height),
      fGraphicStackIndex(0) {
    fGraphicStack[0].fColor = SK_ColorBLACK;
    fGraphicStack[0].fTextSize = SK_ScalarNaN;  // This has no default value.
    fGraphicStack[0].fTextScaleX = SK_Scalar1;
    fGraphicStack[0].fTextFill = SkPaint::kFill_Style;
    fGraphicStack[0].fClip.setRect(0,0, width, height);
    fGraphicStack[0].fTransform.reset();
}

SkPDFDevice::~SkPDFDevice() {
    fGraphicStateResources.unrefAll();
    fXObjectResources.unrefAll();
    fFontResources.unrefAll();
}

void SkPDFDevice::setMatrixClip(const SkMatrix& matrix,
                                const SkRegion& region) {
    // See the comment in the header file above GraphicStackEntry.
    if (region != fGraphicStack[fGraphicStackIndex].fClip) {
        while (fGraphicStackIndex > 0)
            popGS();
        pushGS();

        SkPath clipPath;
        if (!region.getBoundaryPath(&clipPath))
            clipPath.moveTo(SkIntToScalar(-1), SkIntToScalar(-1));
        emitPath(clipPath);

        SkPath::FillType clipFill = clipPath.getFillType();
        NOT_IMPLEMENTED(clipFill == SkPath::kInverseEvenOdd_FillType, false);
        NOT_IMPLEMENTED(clipFill == SkPath::kInverseWinding_FillType, false);
        if (clipFill == SkPath::kEvenOdd_FillType)
            fContent.append("W* n ");
        else
            fContent.append("W n ");

        fGraphicStack[fGraphicStackIndex].fClip = region;
    }
    setTransform(matrix);
}

void SkPDFDevice::drawPaint(const SkDraw& d, const SkPaint& paint) {
    SkMatrix identityTransform;
    identityTransform.reset();
    SkMatrix curTransform = setTransform(identityTransform);

    SkPaint newPaint = paint;
    newPaint.setStyle(SkPaint::kFill_Style);
    updateGSFromPaint(newPaint, false);

    SkRect all = SkRect::MakeWH(width() + 1, height() + 1);
    drawRect(d, all, newPaint);
    setTransform(curTransform);
}

void SkPDFDevice::drawPoints(const SkDraw& d, SkCanvas::PointMode mode,
                             size_t count, const SkPoint* points,
                             const SkPaint& paint) {
    if (count == 0)
        return;

    switch (mode) {
        case SkCanvas::kPolygon_PointMode:
            updateGSFromPaint(paint, false);
            moveTo(points[0].fX, points[0].fY);
            for (size_t i = 1; i < count; i++)
                appendLine(points[i].fX, points[i].fY);
            strokePath();
            break;
        case SkCanvas::kLines_PointMode:
            updateGSFromPaint(paint, false);
            for (size_t i = 0; i < count/2; i++) {
                moveTo(points[i * 2].fX, points[i * 2].fY);
                appendLine(points[i * 2 + 1].fX, points[i * 2 + 1].fY);
                strokePath();
            }
            break;
        case SkCanvas::kPoints_PointMode:
            if (paint.getStrokeCap() == SkPaint::kRound_Cap) {
                updateGSFromPaint(paint, false);
                for (size_t i = 0; i < count; i++) {
                    moveTo(points[i].fX, points[i].fY);
                    strokePath();
                }
            } else {
                // PDF won't draw a single point with square/butt caps because
                // the orientation is ambiguous.  Draw a rectangle instead.
                SkPaint newPaint = paint;
                newPaint.setStyle(SkPaint::kFill_Style);
                SkScalar strokeWidth = paint.getStrokeWidth();
                SkScalar halfStroke = strokeWidth * SK_ScalarHalf;
                for (size_t i = 0; i < count; i++) {
                    SkRect r = SkRect::MakeXYWH(points[i].fX, points[i].fY,
                                                0, 0);
                    r.inset(-halfStroke, -halfStroke);
                    drawRect(d, r, newPaint);
                }
            }
            break;
        default:
            SkASSERT(false);
    }
}

void SkPDFDevice::drawRect(const SkDraw& d, const SkRect& r,
                           const SkPaint& paint) {
    if (paint.getPathEffect()) {
        // Create a path for the rectangle and apply the path effect to it.
        SkPath path;
        path.addRect(r);
        paint.getFillPath(path, &path);

        SkPaint noEffectPaint(paint);
        SkSafeUnref(noEffectPaint.setPathEffect(NULL));
        drawPath(d, path, noEffectPaint);
        return;
    }
    updateGSFromPaint(paint, false);

    // Skia has 0,0 at top left, pdf at bottom left.  Do the right thing.
    SkScalar bottom = r.fBottom < r.fTop ? r.fBottom : r.fTop;
    appendRectangle(r.fLeft, bottom, r.width(), r.height());
    paintPath(paint.getStyle(), SkPath::kWinding_FillType);
}

void SkPDFDevice::drawPath(const SkDraw& d, const SkPath& path,
                           const SkPaint& paint) {
    if (paint.getPathEffect()) {
        // Apply the path effect to path and draw it that way.
        SkPath noEffectPath;
        paint.getFillPath(path, &noEffectPath);

        SkPaint noEffectPaint(paint);
        SkSafeUnref(noEffectPaint.setPathEffect(NULL));
        drawPath(d, noEffectPath, noEffectPaint);
        return;
    }
    updateGSFromPaint(paint, false);

    emitPath(path);
    paintPath(paint.getStyle(), path.getFillType());
}

void SkPDFDevice::drawBitmap(const SkDraw&, const SkBitmap& bitmap,
                             const SkMatrix& matrix, const SkPaint& paint) {
    SkMatrix transform = matrix;
    transform.postConcat(fGraphicStack[fGraphicStackIndex].fTransform);
    internalDrawBitmap(transform, bitmap, paint);
}

void SkPDFDevice::drawSprite(const SkDraw&, const SkBitmap& bitmap,
                             int x, int y, const SkPaint& paint) {
    SkMatrix matrix;
    matrix.setTranslate(x, y);
    internalDrawBitmap(matrix, bitmap, paint);
}

void SkPDFDevice::drawText(const SkDraw& d, const void* text, size_t len,
                           SkScalar x, SkScalar y, const SkPaint& paint) {
    SkPaint textPaint = calculateTextPaint(paint);
    updateGSFromPaint(textPaint, true);
    SkPDFFont* font = fGraphicStack[fGraphicStackIndex].fFont.get();

    uint16_t glyphs[len];
    size_t glyphsLength;
    glyphsLength = font->textToPDFGlyphs(text, len, textPaint, glyphs, len);
    textPaint.setTextEncoding(SkPaint::kGlyphID_TextEncoding);

    SkScalar width;
    SkScalar* widthPtr = NULL;
    if (textPaint.isUnderlineText() || textPaint.isStrikeThruText())
        widthPtr = &width;

    SkDrawCacheProc glyphCacheProc = textPaint.getDrawCacheProc();
    alignText(glyphCacheProc, textPaint, glyphs, glyphsLength, &x, &y,
              widthPtr);
    fContent.append("BT\n");
    setTextTransform(x, y, textPaint.getTextSkewX());
    fContent.append(SkPDFString::formatString(glyphs, glyphsLength,
                                              font->multiByteGlyphs()));
    fContent.append(" Tj\nET\n");

    // Draw underline and/or strikethrough if the paint has them.
    // drawPosText() and drawTextOnPath() don't draw underline or strikethrough
    // because the raster versions don't.  Use paint instead of textPaint
    // because we may have changed strokeWidth to do fakeBold text.
    if (paint.isUnderlineText() || paint.isStrikeThruText()) {
        SkScalar textSize = paint.getTextSize();
        SkScalar height = SkScalarMul(textSize, kStdUnderline_Thickness);

        if (paint.isUnderlineText()) {
            SkScalar top = SkScalarMulAdd(textSize, kStdUnderline_Offset, y);
            SkRect r = SkRect::MakeXYWH(x, top - height, width, height);
            drawRect(d, r, paint);
        }
        if (paint.isStrikeThruText()) {
            SkScalar top = SkScalarMulAdd(textSize, kStdStrikeThru_Offset, y);
            SkRect r = SkRect::MakeXYWH(x, top - height, width, height);
            drawRect(d, r, paint);
        }
    }
}

void SkPDFDevice::drawPosText(const SkDraw&, const void* text, size_t len,
                              const SkScalar pos[], SkScalar constY,
                              int scalarsPerPos, const SkPaint& paint) {
    SkASSERT(1 == scalarsPerPos || 2 == scalarsPerPos);
    SkPaint textPaint = calculateTextPaint(paint);
    updateGSFromPaint(textPaint, true);
    SkPDFFont* font = fGraphicStack[fGraphicStackIndex].fFont.get();

    uint16_t glyphs[len];
    size_t glyphsLength;
    glyphsLength = font->textToPDFGlyphs(text, len, textPaint, glyphs, len);
    textPaint.setTextEncoding(SkPaint::kGlyphID_TextEncoding);

    SkDrawCacheProc glyphCacheProc = textPaint.getDrawCacheProc();
    fContent.append("BT\n");
    for (size_t i = 0; i < glyphsLength; i++) {
        SkScalar x = pos[i * scalarsPerPos];
        SkScalar y = scalarsPerPos == 1 ? constY : pos[i * scalarsPerPos + 1];
        alignText(glyphCacheProc, textPaint, glyphs + i, 1, &x, &y, NULL);
        setTextTransform(x, y, textPaint.getTextSkewX());
        fContent.append(SkPDFString::formatString(glyphs + i, 1,
                                                  font->multiByteGlyphs()));
        fContent.append(" Tj\n");
    }
    fContent.append("ET\n");
}

void SkPDFDevice::drawTextOnPath(const SkDraw&, const void* text, size_t len,
                                 const SkPath& path, const SkMatrix* matrix,
                                 const SkPaint& paint) {
    NOT_IMPLEMENTED("drawTextOnPath", true);
}

void SkPDFDevice::drawVertices(const SkDraw&, SkCanvas::VertexMode,
                               int vertexCount, const SkPoint verts[],
                               const SkPoint texs[], const SkColor colors[],
                               SkXfermode* xmode, const uint16_t indices[],
                               int indexCount, const SkPaint& paint) {
    NOT_IMPLEMENTED("drawVerticies", true);
}

void SkPDFDevice::drawDevice(const SkDraw& d, SkDevice* device, int x, int y,
                             const SkPaint& paint) {
    if ((device->getDeviceCapabilities() & kVector_Capability) == 0) {
        // If we somehow get a raster device, do what our parent would do.
        SkDevice::drawDevice(d, device, x, y, paint);
        return;
    }

    // Assume that a vector capable device means that it's a PDF Device.
    // TODO(vandebo) handle the paint (alpha and compositing mode).
    SkMatrix matrix;
    matrix.setTranslate(x, y);
    SkPDFDevice* pdfDevice = static_cast<SkPDFDevice*>(device);

    SkPDFFormXObject* xobject = new SkPDFFormXObject(pdfDevice, matrix);
    fXObjectResources.push(xobject);  // Transfer reference.
    fContent.append("/X");
    fContent.appendS32(fXObjectResources.count() - 1);
    fContent.append(" Do\n");
}

const SkRefPtr<SkPDFDict>& SkPDFDevice::getResourceDict() {
    if (fResourceDict.get() == NULL) {
        fResourceDict = new SkPDFDict;
        fResourceDict->unref();  // SkRefPtr and new both took a reference.

        if (fGraphicStateResources.count()) {
            SkRefPtr<SkPDFDict> extGState = new SkPDFDict();
            extGState->unref();  // SkRefPtr and new both took a reference.
            for (int i = 0; i < fGraphicStateResources.count(); i++) {
                SkString nameString("G");
                nameString.appendS32(i);
                SkRefPtr<SkPDFName> name = new SkPDFName(nameString);
                name->unref();  // SkRefPtr and new both took a reference.
                SkRefPtr<SkPDFObjRef> gsRef =
                    new SkPDFObjRef(fGraphicStateResources[i]);
                gsRef->unref();  // SkRefPtr and new both took a reference.
                extGState->insert(name.get(), gsRef.get());
            }
            fResourceDict->insert("ExtGState", extGState.get());
        }

        if (fXObjectResources.count()) {
            SkRefPtr<SkPDFDict> xObjects = new SkPDFDict();
            xObjects->unref();  // SkRefPtr and new both took a reference.
            for (int i = 0; i < fXObjectResources.count(); i++) {
                SkString nameString("X");
                nameString.appendS32(i);
                SkRefPtr<SkPDFName> name = new SkPDFName(nameString);
                name->unref();  // SkRefPtr and new both took a reference.
                SkRefPtr<SkPDFObjRef> xObjRef =
                    new SkPDFObjRef(fXObjectResources[i]);
                xObjRef->unref();  // SkRefPtr and new both took a reference.
                xObjects->insert(name.get(), xObjRef.get());
            }
            fResourceDict->insert("XObject", xObjects.get());
        }

        if (fFontResources.count()) {
            SkRefPtr<SkPDFDict> fonts = new SkPDFDict();
            fonts->unref();  // SkRefPtr and new both took a reference.
            for (int i = 0; i < fFontResources.count(); i++) {
                SkString nameString("F");
                nameString.appendS32(i);
                SkRefPtr<SkPDFName> name = new SkPDFName(nameString);
                name->unref();  // SkRefPtr and new both took a reference.
                SkRefPtr<SkPDFObjRef> fontRef =
                    new SkPDFObjRef(fFontResources[i]);
                fontRef->unref();  // SkRefPtr and new both took a reference.
                fonts->insert(name.get(), fontRef.get());
            }
            fResourceDict->insert("Font", fonts.get());
        }

        // For compatibility, add all proc sets (only used for output to PS
        // devices).
        const char procs[][7] = {"PDF", "Text", "ImageB", "ImageC", "ImageI"};
        SkRefPtr<SkPDFArray> procSets = new SkPDFArray();
        procSets->unref();  // SkRefPtr and new both took a reference.
        procSets->reserve(SK_ARRAY_COUNT(procs));
        for (size_t i = 0; i < SK_ARRAY_COUNT(procs); i++) {
            SkRefPtr<SkPDFName> entry = new SkPDFName(procs[i]);
            entry->unref();  // SkRefPtr and new both took a reference.
            procSets->append(entry.get());
        }
        fResourceDict->insert("ProcSet", procSets.get());
    }
    return fResourceDict;
}

void SkPDFDevice::getResources(SkTDArray<SkPDFObject*>* resourceList) const {
    resourceList->setReserve(resourceList->count() +
                             fGraphicStateResources.count() +
                             fXObjectResources.count() +
                             fFontResources.count());
    for (int i = 0; i < fGraphicStateResources.count(); i++) {
        resourceList->push(fGraphicStateResources[i]);
        fGraphicStateResources[i]->ref();
        fGraphicStateResources[i]->getResources(resourceList);
    }
    for (int i = 0; i < fXObjectResources.count(); i++) {
        resourceList->push(fXObjectResources[i]);
        fXObjectResources[i]->ref();
        fXObjectResources[i]->getResources(resourceList);
    }
    for (int i = 0; i < fFontResources.count(); i++) {
        resourceList->push(fFontResources[i]);
        fFontResources[i]->ref();
        fFontResources[i]->getResources(resourceList);
    }
}

SkRefPtr<SkPDFArray> SkPDFDevice::getMediaBox() const {
    SkRefPtr<SkPDFInt> zero = new SkPDFInt(0);
    zero->unref();  // SkRefPtr and new both took a reference.
    SkRefPtr<SkPDFInt> width = new SkPDFInt(fWidth);
    width->unref();  // SkRefPtr and new both took a reference.
    SkRefPtr<SkPDFInt> height = new SkPDFInt(fHeight);
    height->unref();  // SkRefPtr and new both took a reference.
    SkRefPtr<SkPDFArray> mediaBox = new SkPDFArray();
    mediaBox->unref();  // SkRefPtr and new both took a reference.
    mediaBox->reserve(4);
    mediaBox->append(zero.get());
    mediaBox->append(zero.get());
    mediaBox->append(width.get());
    mediaBox->append(height.get());
    return mediaBox;
}

SkString SkPDFDevice::content(bool flipOrigin) const {
    SkString result;
    // Scale and translate to move the origin from the lower left to the
    // upper left.
    if (flipOrigin)
        result.printf("1 0 0 -1 0 %d cm\n", fHeight);
    result.append(fContent);
    for (int i = 0; i < fGraphicStackIndex; i++)
        result.append("Q\n");
    return result;
}

// Private

// TODO(vandebo) handle these cases.
#define PAINTCHECK(x,y) NOT_IMPLEMENTED(newPaint.x() y, false)

void SkPDFDevice::updateGSFromPaint(const SkPaint& newPaint, bool forText) {
    PAINTCHECK(getXfermode, != NULL);
    PAINTCHECK(getPathEffect, != NULL);
    PAINTCHECK(getMaskFilter, != NULL);
    PAINTCHECK(getShader, != NULL);
    PAINTCHECK(getColorFilter, != NULL);

    SkRefPtr<SkPDFGraphicState> newGraphicState =
        SkPDFGraphicState::getGraphicStateForPaint(newPaint);
    newGraphicState->unref();  // getGraphicState and SkRefPtr both took a ref.
    // newGraphicState has been canonicalized so we can directly compare
    // pointers.
    if (fGraphicStack[fGraphicStackIndex].fGraphicState.get() !=
            newGraphicState.get()) {
        int resourceIndex = fGraphicStateResources.find(newGraphicState.get());
        if (resourceIndex < 0) {
            resourceIndex = fGraphicStateResources.count();
            fGraphicStateResources.push(newGraphicState.get());
            newGraphicState->ref();
        }
        fContent.append("/G");
        fContent.appendS32(resourceIndex);
        fContent.append(" gs\n");
        fGraphicStack[fGraphicStackIndex].fGraphicState = newGraphicState;
    }

    SkColor newColor = newPaint.getColor();
    newColor = SkColorSetA(newColor, 0xFF);
    if (fGraphicStack[fGraphicStackIndex].fColor != newColor) {
        SkString colorString = toPDFColor(newColor);
        fContent.append(colorString);
        fContent.append("RG ");
        fContent.append(colorString);
        fContent.append("rg\n");
        fGraphicStack[fGraphicStackIndex].fColor = newColor;
    }

    if (forText) {
        uint32_t fontID = SkTypeface::UniqueID(newPaint.getTypeface());
        if (fGraphicStack[fGraphicStackIndex].fTextSize !=
                newPaint.getTextSize() ||
                fGraphicStack[fGraphicStackIndex].fFont.get() == NULL ||
                fGraphicStack[fGraphicStackIndex].fFont->fontID() != fontID) {
            int fontIndex = getFontResourceIndex(fontID);
            fContent.append("/F");
            fContent.appendS32(fontIndex);
            fContent.append(" ");
            fContent.appendScalar(newPaint.getTextSize());
            fContent.append(" Tf\n");
            fGraphicStack[fGraphicStackIndex].fTextSize =
                newPaint.getTextSize();
            fGraphicStack[fGraphicStackIndex].fFont = fFontResources[fontIndex];
        }

        if (fGraphicStack[fGraphicStackIndex].fTextScaleX !=
                newPaint.getTextScaleX()) {
            SkScalar scale = newPaint.getTextScaleX();
            SkScalar pdfScale = SkScalarMul(scale, SkIntToScalar(100));
            fContent.appendScalar(pdfScale);
            fContent.append(" Tz\n");
            fGraphicStack[fGraphicStackIndex].fTextScaleX = scale;
        }

        if (fGraphicStack[fGraphicStackIndex].fTextFill !=
                newPaint.getStyle()) {
            SK_COMPILE_ASSERT(SkPaint::kFill_Style == 0, enum_must_match_value);
            SK_COMPILE_ASSERT(SkPaint::kStroke_Style == 1,
                              enum_must_match_value);
            SK_COMPILE_ASSERT(SkPaint::kStrokeAndFill_Style == 2,
                              enum_must_match_value);
            fContent.appendS32(newPaint.getStyle());
            fContent.append(" Tr\n");
            fGraphicStack[fGraphicStackIndex].fTextFill = newPaint.getStyle();
        }
    }
}

int SkPDFDevice::getFontResourceIndex(uint32_t fontID) {
    SkRefPtr<SkPDFFont> newFont = SkPDFFont::getFontResouceByID(fontID);
    newFont->unref();  // getFontResourceByID and SkRefPtr both took a ref.
    int resourceIndex = fFontResources.find(newFont.get());
    if (resourceIndex < 0) {
        resourceIndex = fFontResources.count();
        fFontResources.push(newFont.get());
        newFont->ref();
    }
    return resourceIndex;
}

void SkPDFDevice::moveTo(SkScalar x, SkScalar y) {
    fContent.appendScalar(x);
    fContent.append(" ");
    fContent.appendScalar(y);
    fContent.append(" m\n");
}

void SkPDFDevice::appendLine(SkScalar x, SkScalar y) {
    fContent.appendScalar(x);
    fContent.append(" ");
    fContent.appendScalar(y);
    fContent.append(" l\n");
}

void SkPDFDevice::appendCubic(SkScalar ctl1X, SkScalar ctl1Y,
                              SkScalar ctl2X, SkScalar ctl2Y,
                              SkScalar dstX, SkScalar dstY) {
    SkString cmd("y\n");
    fContent.appendScalar(ctl1X);
    fContent.append(" ");
    fContent.appendScalar(ctl1Y);
    fContent.append(" ");
    if (ctl2X != dstX || ctl2Y != dstY) {
        cmd.set("c\n");
        fContent.appendScalar(ctl2X);
        fContent.append(" ");
        fContent.appendScalar(ctl2Y);
        fContent.append(" ");
    }
    fContent.appendScalar(dstX);
    fContent.append(" ");
    fContent.appendScalar(dstY);
    fContent.append(cmd);
}

void SkPDFDevice::appendRectangle(SkScalar x, SkScalar y,
                                  SkScalar w, SkScalar h) {
    fContent.appendScalar(x);
    fContent.append(" ");
    fContent.appendScalar(y);
    fContent.append(" ");
    fContent.appendScalar(w);
    fContent.append(" ");
    fContent.appendScalar(h);
    fContent.append(" re\n");
}

void SkPDFDevice::emitPath(const SkPath& path) {
    SkPoint args[4];
    SkPath::Iter iter(path, false);
    for (SkPath::Verb verb = iter.next(args);
         verb != SkPath::kDone_Verb;
         verb = iter.next(args)) {
        // args gets all the points, even the implicit first point.
        switch (verb) {
            case SkPath::kMove_Verb:
                moveTo(args[0].fX, args[0].fY);
                break;
            case SkPath::kLine_Verb:
                appendLine(args[1].fX, args[1].fY);
                break;
            case SkPath::kQuad_Verb: {
                // Convert quad to cubic (degree elevation). http://goo.gl/vS4i
                const SkScalar three = SkIntToScalar(3);
                args[1].scale(SkIntToScalar(2));
                SkScalar ctl1X = SkScalarDiv(args[0].fX + args[1].fX, three);
                SkScalar ctl1Y = SkScalarDiv(args[0].fY + args[1].fY, three);
                SkScalar ctl2X = SkScalarDiv(args[2].fX + args[1].fX, three);
                SkScalar ctl2Y = SkScalarDiv(args[2].fY + args[1].fY, three);
                appendCubic(ctl1X, ctl1Y, ctl2X, ctl2Y, args[2].fX, args[2].fY);
                break;
            }
            case SkPath::kCubic_Verb:
                appendCubic(args[1].fX, args[1].fY, args[2].fX, args[2].fY,
                            args[3].fX, args[3].fY);
                break;
            case SkPath::kClose_Verb:
                closePath();
                break;
            case SkPath::kDone_Verb:
                break;
            default:
                SkASSERT(false);
                break;
        }
    }
}

void SkPDFDevice::closePath() {
    fContent.append("h\n");
}

void SkPDFDevice::paintPath(SkPaint::Style style, SkPath::FillType fill) {
    if (style == SkPaint::kFill_Style)
        fContent.append("f");
    else if (style == SkPaint::kStrokeAndFill_Style)
        fContent.append("B");
    else if (style == SkPaint::kStroke_Style)
        fContent.append("S");

    if (style != SkPaint::kStroke_Style) {
        // Not supported yet.
        NOT_IMPLEMENTED(fill == SkPath::kInverseEvenOdd_FillType, false);
        NOT_IMPLEMENTED(fill == SkPath::kInverseWinding_FillType, false);
        if (fill == SkPath::kEvenOdd_FillType)
            fContent.append("*");
    }
    fContent.append("\n");
}

void SkPDFDevice::strokePath() {
    paintPath(SkPaint::kStroke_Style, SkPath::kWinding_FillType);
}

void SkPDFDevice::pushGS() {
    SkASSERT(fGraphicStackIndex < 2);
    fContent.append("q\n");
    fGraphicStackIndex++;
    fGraphicStack[fGraphicStackIndex] = fGraphicStack[fGraphicStackIndex - 1];
}

void SkPDFDevice::popGS() {
    SkASSERT(fGraphicStackIndex > 0);
    fContent.append("Q\n");
    fGraphicStackIndex--;
}

void SkPDFDevice::setTextTransform(SkScalar x, SkScalar y, SkScalar textSkewX) {
    // Flip the text about the x-axis to account for origin swap and include
    // the passed parameters.
    fContent.append("1 0 ");
    fContent.appendScalar(0 - textSkewX);
    fContent.append(" -1 ");
    fContent.appendScalar(x);
    fContent.append(" ");
    fContent.appendScalar(y);
    fContent.append(" Tm\n");
}

void SkPDFDevice::internalDrawBitmap(const SkMatrix& matrix,
                                     const SkBitmap& bitmap,
                                     const SkPaint& paint) {
    SkMatrix scaled;
    // Adjust for origin flip.
    scaled.setScale(1, -1);
    scaled.postTranslate(0, 1);
    // Scale the image up from 1x1 to WxH.
    scaled.postScale(bitmap.width(), bitmap.height());
    scaled.postConcat(matrix);
    SkMatrix curTransform = setTransform(scaled);

    SkPDFImage* image = new SkPDFImage(bitmap, paint);
    fXObjectResources.push(image);  // Transfer reference.
    fContent.append("/X");
    fContent.appendS32(fXObjectResources.count() - 1);
    fContent.append(" Do\n");
    setTransform(curTransform);
}

SkMatrix SkPDFDevice::setTransform(const SkMatrix& m) {
    SkMatrix old = fGraphicStack[fGraphicStackIndex].fTransform;
    if (old == m)
        return old;

    if (old.getType() != SkMatrix::kIdentity_Mask) {
        SkASSERT(fGraphicStackIndex > 0);
        SkASSERT(fGraphicStack[fGraphicStackIndex - 1].fTransform.getType() ==
                 SkMatrix::kIdentity_Mask);
        SkASSERT(fGraphicStack[fGraphicStackIndex].fClip ==
                 fGraphicStack[fGraphicStackIndex - 1].fClip);
        popGS();
    }
    if (m.getType() == SkMatrix::kIdentity_Mask)
        return old;

    if (fGraphicStackIndex == 0 || fGraphicStack[fGraphicStackIndex].fClip !=
            fGraphicStack[fGraphicStackIndex - 1].fClip)
        pushGS();

    SkScalar transform[6];
    SkAssertResult(m.pdfTransform(transform));
    for (size_t i = 0; i < SK_ARRAY_COUNT(transform); i++) {
        fContent.appendScalar(transform[i]);
        fContent.append(" ");
    }
    fContent.append("cm\n");
    fGraphicStack[fGraphicStackIndex].fTransform = m;

    return old;
}
