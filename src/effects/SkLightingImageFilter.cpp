/*
 * Copyright 2012 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkLightingImageFilter.h"
#include "SkBitmap.h"
#include "SkColorPriv.h"
#include "GrProgramStageFactory.h"
#include "effects/GrSingleTextureEffect.h"
#include "gl/GrGLProgramStage.h"
#include "gl/GrGLSL.h"
#include "gl/GrGLTexture.h"
#include "GrCustomStage.h"

class GrGLDiffuseLightingEffect;
class GrGLSpecularLightingEffect;

// For brevity, and these definitions are likely to move to a different class soon.
typedef GrGLShaderBuilder::UniformHandle UniformHandle;
static const UniformHandle kInvalidUniformHandle = GrGLShaderBuilder::kInvalidUniformHandle;

// FIXME:  Eventually, this should be implemented properly, and put in
// SkScalar.h.
#define SkScalarPow(x, y) SkFloatToScalar(powf(SkScalarToFloat(x), SkScalarToFloat(y)))
namespace {

const SkScalar gOneThird = SkScalarInvert(SkIntToScalar(3));
const SkScalar gTwoThirds = SkScalarDiv(SkIntToScalar(2), SkIntToScalar(3));
const SkScalar gOneHalf = SkFloatToScalar(0.5f);
const SkScalar gOneQuarter = SkFloatToScalar(0.25f);

void setUniformPoint3(const GrGLInterface* gl, GrGLint location, const SkPoint3& point) {
    float x = SkScalarToFloat(point.fX);
    float y = SkScalarToFloat(point.fY);
    float z = SkScalarToFloat(point.fZ);
    GR_GL_CALL(gl, Uniform3f(location, x, y, z));
}

void setUniformNormal3(const GrGLInterface* gl, GrGLint location, const SkPoint3& point) {
    setUniformPoint3(gl, location, SkPoint3(point.fX, -point.fY, point.fZ));
}

void setUniformPoint3FlipY(const GrGLInterface* gl, GrGLint location, const SkPoint3& point, int height) {
    setUniformPoint3(gl, location, SkPoint3(point.fX, height-point.fY, point.fZ));
}

// Shift matrix components to the left, as we advance pixels to the right.
inline void shiftMatrixLeft(int m[9]) {
    m[0] = m[1];
    m[3] = m[4];
    m[6] = m[7];
    m[1] = m[2];
    m[4] = m[5];
    m[7] = m[8];
}

class DiffuseLightingType {
public:
    DiffuseLightingType(SkScalar kd)
        : fKD(kd) {}
    SkPMColor light(const SkPoint3& normal, const SkPoint3& surfaceTolight, const SkPoint3& lightColor) const {
        SkScalar colorScale = SkScalarMul(fKD, normal.dot(surfaceTolight));
        colorScale = SkScalarClampMax(colorScale, SK_Scalar1);
        SkPoint3 color(lightColor * colorScale);
        return SkPackARGB32(255,
                            SkScalarFloorToInt(color.fX),
                            SkScalarFloorToInt(color.fY),
                            SkScalarFloorToInt(color.fZ));
    }
private:
    SkScalar fKD;
};

class SpecularLightingType {
public:
    SpecularLightingType(SkScalar ks, SkScalar shininess)
        : fKS(ks), fShininess(shininess) {}
    SkPMColor light(const SkPoint3& normal, const SkPoint3& surfaceTolight, const SkPoint3& lightColor) const {
        SkPoint3 halfDir(surfaceTolight);
        halfDir.fZ += SK_Scalar1;        // eye position is always (0, 0, 1)
        halfDir.normalize();
        SkScalar colorScale = SkScalarMul(fKS,
            SkScalarPow(normal.dot(halfDir), fShininess));
        colorScale = SkScalarClampMax(colorScale, SK_Scalar1);
        SkPoint3 color(lightColor * colorScale);
        return SkPackARGB32(SkScalarFloorToInt(color.maxComponent()),
                            SkScalarFloorToInt(color.fX),
                            SkScalarFloorToInt(color.fY),
                            SkScalarFloorToInt(color.fZ));
    }
private:
    SkScalar fKS;
    SkScalar fShininess;
};

inline SkScalar sobel(int a, int b, int c, int d, int e, int f, SkScalar scale) {
    return SkScalarMul(SkIntToScalar(-a + b - 2 * c + 2 * d -e + f), scale);
}

inline SkPoint3 pointToNormal(SkScalar x, SkScalar y, SkScalar surfaceScale) {
    SkPoint3 vector(SkScalarMul(-x, surfaceScale),
                    SkScalarMul(-y, surfaceScale),
                    SK_Scalar1);
    vector.normalize();
    return vector;
}

inline SkPoint3 topLeftNormal(int m[9], SkScalar surfaceScale) {
    return pointToNormal(sobel(0, 0, m[4], m[5], m[7], m[8], gTwoThirds),
                         sobel(0, 0, m[4], m[7], m[5], m[8], gTwoThirds),
                         surfaceScale);
}

inline SkPoint3 topNormal(int m[9], SkScalar surfaceScale) {
    return pointToNormal(sobel(   0,    0, m[3], m[5], m[6], m[8], gOneThird),
                         sobel(m[3], m[6], m[4], m[7], m[5], m[8], gOneHalf),
                         surfaceScale);
}

inline SkPoint3 topRightNormal(int m[9], SkScalar surfaceScale) {
    return pointToNormal(sobel(   0,    0, m[3], m[4], m[6], m[7], gTwoThirds),
                         sobel(m[3], m[6], m[4], m[7],    0,    0, gTwoThirds),
                         surfaceScale);
}

inline SkPoint3 leftNormal(int m[9], SkScalar surfaceScale) {
    return pointToNormal(sobel(m[1], m[2], m[4], m[5], m[7], m[8], gOneHalf),
                         sobel(   0,    0, m[1], m[7], m[2], m[8], gOneThird),
                         surfaceScale);
}


inline SkPoint3 interiorNormal(int m[9], SkScalar surfaceScale) {
    return pointToNormal(sobel(m[0], m[2], m[3], m[5], m[6], m[8], gOneQuarter),
                         sobel(m[0], m[6], m[1], m[7], m[2], m[8], gOneQuarter),
                         surfaceScale);
}

inline SkPoint3 rightNormal(int m[9], SkScalar surfaceScale) {
    return pointToNormal(sobel(m[0], m[1], m[3], m[4], m[6], m[7], gOneHalf),
                         sobel(m[0], m[6], m[1], m[7],    0,    0, gOneThird),
                         surfaceScale);
}

inline SkPoint3 bottomLeftNormal(int m[9], SkScalar surfaceScale) {
    return pointToNormal(sobel(m[1], m[2], m[4], m[5],    0,    0, gTwoThirds),
                         sobel(   0,    0, m[1], m[4], m[2], m[5], gTwoThirds),
                         surfaceScale);
}

inline SkPoint3 bottomNormal(int m[9], SkScalar surfaceScale) {
    return pointToNormal(sobel(m[0], m[2], m[3], m[5],    0,    0, gOneThird),
                         sobel(m[0], m[3], m[1], m[4], m[2], m[5], gOneHalf),
                         surfaceScale);
}

inline SkPoint3 bottomRightNormal(int m[9], SkScalar surfaceScale) {
    return pointToNormal(sobel(m[0], m[1], m[3], m[4], 0,  0, gTwoThirds),
                         sobel(m[0], m[3], m[1], m[4], 0,  0, gTwoThirds),
                         surfaceScale);
}

template <class LightingType, class LightType> void lightBitmap(const LightingType& lightingType, const SkLight* light, const SkBitmap& src, SkBitmap* dst, SkScalar surfaceScale) {
    const LightType* l = static_cast<const LightType*>(light);
    int y = 0;
    {
        const SkPMColor* row1 = src.getAddr32(0, 0);
        const SkPMColor* row2 = src.getAddr32(0, 1);
        SkPMColor* dptr = dst->getAddr32(0, 0);
        int m[9];
        int x = 0;
        m[4] = SkGetPackedA32(*row1++);
        m[5] = SkGetPackedA32(*row1++);
        m[7] = SkGetPackedA32(*row2++);
        m[8] = SkGetPackedA32(*row2++);
        SkPoint3 surfaceToLight = l->surfaceToLight(x, y, m[4], surfaceScale);
        *dptr++ = lightingType.light(topLeftNormal(m, surfaceScale), surfaceToLight, l->lightColor(surfaceToLight));
        for (x = 1; x < src.width() - 1; ++x)
        {
            shiftMatrixLeft(m);
            m[5] = SkGetPackedA32(*row1++);
            m[8] = SkGetPackedA32(*row2++);
            surfaceToLight = l->surfaceToLight(x, 0, m[4], surfaceScale);
            *dptr++ = lightingType.light(topNormal(m, surfaceScale), surfaceToLight, l->lightColor(surfaceToLight));
        }
        shiftMatrixLeft(m);
        surfaceToLight = l->surfaceToLight(x, y, m[4], surfaceScale);
        *dptr++ = lightingType.light(topRightNormal(m, surfaceScale), surfaceToLight, l->lightColor(surfaceToLight));
    }

    for (++y; y < src.height() - 1; ++y) {
        const SkPMColor* row0 = src.getAddr32(0, y - 1);
        const SkPMColor* row1 = src.getAddr32(0, y);
        const SkPMColor* row2 = src.getAddr32(0, y + 1);
        SkPMColor* dptr = dst->getAddr32(0, y);
        int m[9];
        int x = 0;
        m[1] = SkGetPackedA32(*row0++);
        m[2] = SkGetPackedA32(*row0++);
        m[4] = SkGetPackedA32(*row1++);
        m[5] = SkGetPackedA32(*row1++);
        m[7] = SkGetPackedA32(*row2++);
        m[8] = SkGetPackedA32(*row2++);
        SkPoint3 surfaceToLight = l->surfaceToLight(x, y, m[4], surfaceScale);
        *dptr++ = lightingType.light(leftNormal(m, surfaceScale), surfaceToLight, l->lightColor(surfaceToLight));
        for (x = 1; x < src.width() - 1; ++x) {
            shiftMatrixLeft(m);
            m[2] = SkGetPackedA32(*row0++);
            m[5] = SkGetPackedA32(*row1++);
            m[8] = SkGetPackedA32(*row2++);
            surfaceToLight = l->surfaceToLight(x, y, m[4], surfaceScale);
            *dptr++ = lightingType.light(interiorNormal(m, surfaceScale), surfaceToLight, l->lightColor(surfaceToLight));
        }
        shiftMatrixLeft(m);
        surfaceToLight = l->surfaceToLight(x, y, m[4], surfaceScale);
        *dptr++ = lightingType.light(rightNormal(m, surfaceScale), surfaceToLight, l->lightColor(surfaceToLight));
    }

    {
        const SkPMColor* row0 = src.getAddr32(0, src.height() - 2);
        const SkPMColor* row1 = src.getAddr32(0, src.height() - 1);
        int x = 0;
        SkPMColor* dptr = dst->getAddr32(0, src.height() - 1);
        int m[9];
        m[1] = SkGetPackedA32(*row0++);
        m[2] = SkGetPackedA32(*row0++);
        m[4] = SkGetPackedA32(*row1++);
        m[5] = SkGetPackedA32(*row1++);
        SkPoint3 surfaceToLight = l->surfaceToLight(x, y, m[4], surfaceScale);
        *dptr++ = lightingType.light(bottomLeftNormal(m, surfaceScale), surfaceToLight, l->lightColor(surfaceToLight));
        for (x = 1; x < src.width() - 1; ++x)
        {
            shiftMatrixLeft(m);
            m[2] = SkGetPackedA32(*row0++);
            m[5] = SkGetPackedA32(*row1++);
            surfaceToLight = l->surfaceToLight(x, y, m[4], surfaceScale);
            *dptr++ = lightingType.light(bottomNormal(m, surfaceScale), surfaceToLight, l->lightColor(surfaceToLight));
        }
        shiftMatrixLeft(m);
        surfaceToLight = l->surfaceToLight(x, y, m[4], surfaceScale);
        *dptr++ = lightingType.light(bottomRightNormal(m, surfaceScale), surfaceToLight, l->lightColor(surfaceToLight));
    }
}

SkPoint3 readPoint3(SkFlattenableReadBuffer& buffer) {
    SkPoint3 point;
    point.fX = buffer.readScalar();
    point.fY = buffer.readScalar();
    point.fZ = buffer.readScalar();
    return point;
};

void writePoint3(const SkPoint3& point, SkFlattenableWriteBuffer& buffer) {
    buffer.writeScalar(point.fX);
    buffer.writeScalar(point.fY);
    buffer.writeScalar(point.fZ);
};

class SkDiffuseLightingImageFilter : public SkLightingImageFilter {
public:
    SkDiffuseLightingImageFilter(SkLight* light, SkScalar surfaceScale, SkScalar kd);
    SK_DECLARE_PUBLIC_FLATTENABLE_DESERIALIZATION_PROCS(SkDiffuseLightingImageFilter)

    virtual bool asNewCustomStage(GrCustomStage** stage, GrTexture*) const SK_OVERRIDE;
    SkScalar kd() const { return fKD; }

protected:
    explicit SkDiffuseLightingImageFilter(SkFlattenableReadBuffer& buffer);
    virtual void flatten(SkFlattenableWriteBuffer& buffer) const SK_OVERRIDE;
    virtual bool onFilterImage(Proxy*, const SkBitmap& src, const SkMatrix&,
                               SkBitmap* result, SkIPoint* offset) SK_OVERRIDE;


private:
    typedef SkLightingImageFilter INHERITED;
    SkScalar fKD;
};

class SkSpecularLightingImageFilter : public SkLightingImageFilter {
public:
    SkSpecularLightingImageFilter(SkLight* light, SkScalar surfaceScale, SkScalar ks, SkScalar shininess);
    SK_DECLARE_PUBLIC_FLATTENABLE_DESERIALIZATION_PROCS(SkSpecularLightingImageFilter)

    virtual bool asNewCustomStage(GrCustomStage** stage, GrTexture*) const SK_OVERRIDE;
    SkScalar ks() const { return fKS; }
    SkScalar shininess() const { return fShininess; }

protected:
    explicit SkSpecularLightingImageFilter(SkFlattenableReadBuffer& buffer);
    virtual void flatten(SkFlattenableWriteBuffer& buffer) const SK_OVERRIDE;
    virtual bool onFilterImage(Proxy*, const SkBitmap& src, const SkMatrix&,
                               SkBitmap* result, SkIPoint* offset) SK_OVERRIDE;

private:
    typedef SkLightingImageFilter INHERITED;
    SkScalar fKS;
    SkScalar fShininess;
};


class GrLightingEffect : public GrSingleTextureEffect {
public:
    GrLightingEffect(GrTexture* texture, const SkLight* light, SkScalar surfaceScale);
    virtual ~GrLightingEffect();

    virtual bool isEqual(const GrCustomStage&) const SK_OVERRIDE;

    const SkLight* light() const { return fLight; }
    SkScalar surfaceScale() const { return fSurfaceScale; }
private:
    typedef GrSingleTextureEffect INHERITED;
    const SkLight* fLight;
    SkScalar fSurfaceScale;
};

class GrDiffuseLightingEffect : public GrLightingEffect {
public:
    GrDiffuseLightingEffect(GrTexture* texture,
                            const SkLight* light,
                            SkScalar surfaceScale,
                            SkScalar kd);

    static const char* Name() { return "DiffuseLighting"; }

    typedef GrGLDiffuseLightingEffect GLProgramStage;

    virtual const GrProgramStageFactory& getFactory() const SK_OVERRIDE;
    virtual bool isEqual(const GrCustomStage&) const SK_OVERRIDE;
    SkScalar kd() const { return fKD; }
private:
    typedef GrLightingEffect INHERITED;
    SkScalar fKD;
};

class GrSpecularLightingEffect : public GrLightingEffect {
public:
    GrSpecularLightingEffect(GrTexture* texture,
                             const SkLight* light,
                             SkScalar surfaceScale,
                             SkScalar ks,
                             SkScalar shininess);

    static const char* Name() { return "SpecularLighting"; }

    typedef GrGLSpecularLightingEffect GLProgramStage;

    virtual const GrProgramStageFactory& getFactory() const SK_OVERRIDE;
    virtual bool isEqual(const GrCustomStage&) const SK_OVERRIDE;
    SkScalar ks() const { return fKS; }
    SkScalar shininess() const { return fShininess; }

private:
    typedef GrLightingEffect INHERITED;
    SkScalar fKS;
    SkScalar fShininess;
};

///////////////////////////////////////////////////////////////////////////////

class GrGLLight {
public:
    virtual ~GrGLLight() {}
    virtual void setupVariables(GrGLShaderBuilder* builder, int stage);
    virtual void emitVS(SkString* out) const {}
    virtual void emitFuncs(const GrGLShaderBuilder* builder, SkString* out) const {}
    virtual void emitSurfaceToLight(const GrGLShaderBuilder*,
                                    SkString* out,
                                    const char* z) const = 0;
    virtual void emitLightColor(const GrGLShaderBuilder*,
                                SkString* out,
                                const char *surfaceToLight) const;
    virtual void initUniforms(const GrGLShaderBuilder*, const GrGLInterface* gl, int programID);
    virtual void setData(const GrGLInterface*, const GrRenderTarget* rt, const SkLight* light) const;

private:
    typedef SkRefCnt INHERITED;

protected:
    UniformHandle fColorUni;
    int fColorLocation;
};

///////////////////////////////////////////////////////////////////////////////

class GrGLDistantLight : public GrGLLight {
public:
    virtual ~GrGLDistantLight() {}
    virtual void setupVariables(GrGLShaderBuilder* builder, int stage) SK_OVERRIDE;
    virtual void initUniforms(const GrGLShaderBuilder*,
                              const GrGLInterface* gl,
                              int programID) SK_OVERRIDE;
    virtual void setData(const GrGLInterface* gl, const GrRenderTarget* rt, const SkLight* light) const SK_OVERRIDE;
    virtual void emitSurfaceToLight(const GrGLShaderBuilder*,
                                    SkString* out,
                                    const char* z) const SK_OVERRIDE;

private:
    typedef GrGLLight INHERITED;
    UniformHandle fDirectionUni;
    int fDirectionLocation;
};

///////////////////////////////////////////////////////////////////////////////

class GrGLPointLight : public GrGLLight {
public:
    virtual ~GrGLPointLight() {}
    virtual void setupVariables(GrGLShaderBuilder* builder, int stage) SK_OVERRIDE;
    virtual void initUniforms(const GrGLShaderBuilder*,
                              const GrGLInterface*,
                              int programID) SK_OVERRIDE;
    virtual void setData(const GrGLInterface* gl, const GrRenderTarget* rt, const SkLight* light) const SK_OVERRIDE;
    virtual void emitVS(SkString* out) const SK_OVERRIDE;
    virtual void emitSurfaceToLight(const GrGLShaderBuilder*,
                                    SkString* out,
                                    const char* z) const SK_OVERRIDE;

private:
    typedef GrGLLight INHERITED;
    SkPoint3 fLocation;
    UniformHandle fLocationUni;
    int fLocationLocation;
};

///////////////////////////////////////////////////////////////////////////////

class GrGLSpotLight : public GrGLLight {
public:
    virtual ~GrGLSpotLight() {}
    virtual void setupVariables(GrGLShaderBuilder* builder, int stage) SK_OVERRIDE;
    virtual void initUniforms(const GrGLShaderBuilder* builder,
                              const GrGLInterface* gl,
                              int programID) SK_OVERRIDE;
    virtual void setData(const GrGLInterface* gl, const GrRenderTarget* rt, const SkLight* light) const SK_OVERRIDE;
    virtual void emitVS(SkString* out) const SK_OVERRIDE;
    virtual void emitFuncs(const GrGLShaderBuilder* builder, SkString* out) const;
    virtual void emitSurfaceToLight(const GrGLShaderBuilder* builder,
                                    SkString* out,
                                    const char* z) const SK_OVERRIDE;
    virtual void emitLightColor(const GrGLShaderBuilder*,
                                SkString* out,
                                const char *surfaceToLight) const SK_OVERRIDE;

private:
    typedef GrGLLight INHERITED;

    UniformHandle   fLocationUni;
    int             fLocationLocation;
    UniformHandle   fExponentUni;
    int             fExponentLocation;
    UniformHandle   fCosOuterConeAngleUni;
    int             fCosOuterConeAngleLocation;
    UniformHandle   fCosInnerConeAngleUni;
    int             fCosInnerConeAngleLocation;
    UniformHandle   fConeScaleUni;
    int             fConeScaleLocation;
    UniformHandle   fSUni;
    int             fSLocation;
};

};

///////////////////////////////////////////////////////////////////////////////

class SkLight : public SkFlattenable {
public:
    SK_DECLARE_INST_COUNT(SkLight)

    enum LightType {
        kDistant_LightType,
        kPoint_LightType,
        kSpot_LightType,
    };
    virtual LightType type() const = 0;
    const SkPoint3& color() const { return fColor; }
    virtual GrGLLight* createGLLight() const = 0;
    virtual bool isEqual(const SkLight& other) const {
        return fColor == other.fColor;
    }

protected:
    SkLight(SkColor color)
      : fColor(SkIntToScalar(SkColorGetR(color)),
               SkIntToScalar(SkColorGetG(color)),
               SkIntToScalar(SkColorGetB(color))) {}
    SkLight(SkFlattenableReadBuffer& buffer)
      : INHERITED(buffer) {
        fColor = readPoint3(buffer);
    }
    virtual void flatten(SkFlattenableWriteBuffer& buffer) const SK_OVERRIDE {
        INHERITED::flatten(buffer);
        writePoint3(fColor, buffer);
    }

private:
    typedef SkFlattenable INHERITED;
    SkPoint3 fColor;
};

SK_DEFINE_INST_COUNT(SkLight)

///////////////////////////////////////////////////////////////////////////////

class SkDistantLight : public SkLight {
public:
    SkDistantLight(const SkPoint3& direction, SkColor color)
      : INHERITED(color), fDirection(direction) {
    }

    SkPoint3 surfaceToLight(int x, int y, int z, SkScalar surfaceScale) const {
        return fDirection;
    };
    SkPoint3 lightColor(const SkPoint3&) const { return color(); }
    virtual LightType type() const { return kDistant_LightType; }
    const SkPoint3& direction() const { return fDirection; }
    virtual GrGLLight* createGLLight() const SK_OVERRIDE;
    virtual bool isEqual(const SkLight& other) const SK_OVERRIDE {
        if (other.type() != kDistant_LightType) {
            return false;
        }

        const SkDistantLight& o = static_cast<const SkDistantLight&>(other);
        return INHERITED::isEqual(other) &&
               fDirection == o.fDirection;
    }

    SK_DECLARE_PUBLIC_FLATTENABLE_DESERIALIZATION_PROCS(SkDistantLight)

protected:
    SkDistantLight(SkFlattenableReadBuffer& buffer) : INHERITED(buffer) {
        fDirection = readPoint3(buffer);
    }
    virtual void flatten(SkFlattenableWriteBuffer& buffer) const {
        INHERITED::flatten(buffer);
        writePoint3(fDirection, buffer);
    }

private:
    typedef SkLight INHERITED;
    SkPoint3 fDirection;
};

///////////////////////////////////////////////////////////////////////////////

class SkPointLight : public SkLight {
public:
    SkPointLight(const SkPoint3& location, SkColor color)
     : INHERITED(color), fLocation(location) {}

    SkPoint3 surfaceToLight(int x, int y, int z, SkScalar surfaceScale) const {
        SkPoint3 direction(fLocation.fX - SkIntToScalar(x),
                           fLocation.fY - SkIntToScalar(y),
                           fLocation.fZ - SkScalarMul(SkIntToScalar(z), surfaceScale));
        direction.normalize();
        return direction;
    };
    SkPoint3 lightColor(const SkPoint3&) const { return color(); }
    virtual LightType type() const { return kPoint_LightType; }
    const SkPoint3& location() const { return fLocation; }
    virtual GrGLLight* createGLLight() const SK_OVERRIDE {
        return new GrGLPointLight();
    }
    virtual bool isEqual(const SkLight& other) const SK_OVERRIDE {
        if (other.type() != kPoint_LightType) {
            return false;
        }
        const SkPointLight& o = static_cast<const SkPointLight&>(other);
        return INHERITED::isEqual(other) &&
               fLocation == o.fLocation;
    }

    SK_DECLARE_PUBLIC_FLATTENABLE_DESERIALIZATION_PROCS(SkPointLight)

protected:
    SkPointLight(SkFlattenableReadBuffer& buffer) : INHERITED(buffer) {
        fLocation = readPoint3(buffer);
    }
    virtual void flatten(SkFlattenableWriteBuffer& buffer) const {
        INHERITED::flatten(buffer);
        writePoint3(fLocation, buffer);
    }

private:
    typedef SkLight INHERITED;
    SkPoint3 fLocation;
};

///////////////////////////////////////////////////////////////////////////////

class SkSpotLight : public SkLight {
public:
    SkSpotLight(const SkPoint3& location, const SkPoint3& target, SkScalar specularExponent, SkScalar cutoffAngle, SkColor color)
     : INHERITED(color),
       fLocation(location),
       fTarget(target),
       fSpecularExponent(specularExponent)
    {
       fS = target - location;
       fS.normalize();
       fCosOuterConeAngle = SkScalarCos(SkDegreesToRadians(cutoffAngle));
       const SkScalar antiAliasThreshold = SkFloatToScalar(0.016f);
       fCosInnerConeAngle = fCosOuterConeAngle + antiAliasThreshold;
       fConeScale = SkScalarInvert(antiAliasThreshold);
    }

    SkPoint3 surfaceToLight(int x, int y, int z, SkScalar surfaceScale) const {
        SkPoint3 direction(fLocation.fX - SkIntToScalar(x),
                           fLocation.fY - SkIntToScalar(y),
                           fLocation.fZ - SkScalarMul(SkIntToScalar(z), surfaceScale));
        direction.normalize();
        return direction;
    };
    SkPoint3 lightColor(const SkPoint3& surfaceToLight) const {
        SkScalar cosAngle = -surfaceToLight.dot(fS);
        if (cosAngle < fCosOuterConeAngle) {
            return SkPoint3(0, 0, 0);
        }
        SkScalar scale = SkScalarPow(cosAngle, fSpecularExponent);
        if (cosAngle < fCosInnerConeAngle) {
            scale = SkScalarMul(scale, cosAngle - fCosOuterConeAngle);
            return color() * SkScalarMul(scale, fConeScale);
        }
        return color() * scale;
    }
    virtual GrGLLight* createGLLight() const SK_OVERRIDE {
        return new GrGLSpotLight();
    }
    virtual LightType type() const { return kSpot_LightType; }
    const SkPoint3& location() const { return fLocation; }
    const SkPoint3& target() const { return fTarget; }
    SkScalar specularExponent() const { return fSpecularExponent; }
    SkScalar cosInnerConeAngle() const { return fCosInnerConeAngle; }
    SkScalar cosOuterConeAngle() const { return fCosOuterConeAngle; }
    SkScalar coneScale() const { return fConeScale; }
    const SkPoint3& s() const { return fS; }

    SK_DECLARE_PUBLIC_FLATTENABLE_DESERIALIZATION_PROCS(SkSpotLight)

protected:
    SkSpotLight(SkFlattenableReadBuffer& buffer) : INHERITED(buffer) {
        fLocation = readPoint3(buffer);
        fTarget = readPoint3(buffer);
        fSpecularExponent = buffer.readScalar();
        fCosOuterConeAngle = buffer.readScalar();
        fCosInnerConeAngle = buffer.readScalar();
        fConeScale = buffer.readScalar();
        fS = readPoint3(buffer);
    }
    virtual void flatten(SkFlattenableWriteBuffer& buffer) const {
        INHERITED::flatten(buffer);
        writePoint3(fLocation, buffer);
        writePoint3(fTarget, buffer);
        buffer.writeScalar(fSpecularExponent);
        buffer.writeScalar(fCosOuterConeAngle);
        buffer.writeScalar(fCosInnerConeAngle);
        buffer.writeScalar(fConeScale);
        writePoint3(fS, buffer);
    }

    virtual bool isEqual(const SkLight& other) const SK_OVERRIDE {
        if (other.type() != kSpot_LightType) {
            return false;
        }

        const SkSpotLight& o = static_cast<const SkSpotLight&>(other);
        return INHERITED::isEqual(other) &&
               fLocation == o.fLocation &&
               fTarget == o.fTarget &&
               fSpecularExponent == o.fSpecularExponent &&
               fCosOuterConeAngle == o.fCosOuterConeAngle;
    }

private:
    typedef SkLight INHERITED;
    SkPoint3 fLocation;
    SkPoint3 fTarget;
    SkScalar fSpecularExponent;
    SkScalar fCosOuterConeAngle;
    SkScalar fCosInnerConeAngle;
    SkScalar fConeScale;
    SkPoint3 fS;
};

///////////////////////////////////////////////////////////////////////////////

SkLightingImageFilter::SkLightingImageFilter(SkLight* light, SkScalar surfaceScale)
  : fLight(light),
    fSurfaceScale(SkScalarDiv(surfaceScale, SkIntToScalar(255)))
{
    SkASSERT(fLight);
    // our caller knows that we take ownership of the light, so we don't
    // need to call ref() here.
}

SkImageFilter* SkLightingImageFilter::CreateDistantLitDiffuse(
    const SkPoint3& direction, SkColor lightColor, SkScalar surfaceScale,
    SkScalar kd) {
    return new SkDiffuseLightingImageFilter(
        new SkDistantLight(direction, lightColor), surfaceScale, kd);
}

SkImageFilter* SkLightingImageFilter::CreatePointLitDiffuse(
    const SkPoint3& location, SkColor lightColor, SkScalar surfaceScale,
    SkScalar kd) {
    return new SkDiffuseLightingImageFilter(
        new SkPointLight(location, lightColor), surfaceScale, kd);
}

SkImageFilter* SkLightingImageFilter::CreateSpotLitDiffuse(
    const SkPoint3& location, const SkPoint3& target,
    SkScalar specularExponent, SkScalar cutoffAngle,
    SkColor lightColor, SkScalar surfaceScale, SkScalar kd) {
    return new SkDiffuseLightingImageFilter(
        new SkSpotLight(location, target, specularExponent, cutoffAngle, lightColor),
        surfaceScale, kd);
}

SkImageFilter* SkLightingImageFilter::CreateDistantLitSpecular(
    const SkPoint3& direction, SkColor lightColor, SkScalar surfaceScale,
    SkScalar ks, SkScalar shininess) {
    return new SkSpecularLightingImageFilter(
        new SkDistantLight(direction, lightColor), surfaceScale, ks, shininess);
}

SkImageFilter* SkLightingImageFilter::CreatePointLitSpecular(
    const SkPoint3& location, SkColor lightColor, SkScalar surfaceScale,
    SkScalar ks, SkScalar shininess) {
    return new SkSpecularLightingImageFilter(
        new SkPointLight(location, lightColor), surfaceScale, ks, shininess);
}

SkImageFilter* SkLightingImageFilter::CreateSpotLitSpecular(
    const SkPoint3& location, const SkPoint3& target,
    SkScalar specularExponent, SkScalar cutoffAngle,
    SkColor lightColor, SkScalar surfaceScale,
    SkScalar ks, SkScalar shininess) {
    return new SkSpecularLightingImageFilter(
        new SkSpotLight(location, target, specularExponent, cutoffAngle, lightColor),
        surfaceScale, ks, shininess);
}

SkLightingImageFilter::~SkLightingImageFilter() {
    fLight->unref();
}

SkLightingImageFilter::SkLightingImageFilter(SkFlattenableReadBuffer& buffer)
  : INHERITED(buffer)
{
    fLight = (SkLight*)buffer.readFlattenable();
    fSurfaceScale = buffer.readScalar();
}

void SkLightingImageFilter::flatten(SkFlattenableWriteBuffer& buffer) const {
    this->INHERITED::flatten(buffer);
    buffer.writeFlattenable(fLight);
    buffer.writeScalar(fSurfaceScale);
}

///////////////////////////////////////////////////////////////////////////////

SkDiffuseLightingImageFilter::SkDiffuseLightingImageFilter(SkLight* light, SkScalar surfaceScale, SkScalar kd)
  : SkLightingImageFilter(light, surfaceScale),
    fKD(kd)
{
}

SkDiffuseLightingImageFilter::SkDiffuseLightingImageFilter(SkFlattenableReadBuffer& buffer)
  : INHERITED(buffer)
{
    fKD = buffer.readScalar();
}

void SkDiffuseLightingImageFilter::flatten(SkFlattenableWriteBuffer& buffer) const {
    this->INHERITED::flatten(buffer);
    buffer.writeScalar(fKD);
}

bool SkDiffuseLightingImageFilter::onFilterImage(Proxy*,
                                                 const SkBitmap& src,
                                                 const SkMatrix&,
                                                 SkBitmap* dst,
                                                 SkIPoint*) {
    if (src.config() != SkBitmap::kARGB_8888_Config) {
        return false;
    }
    SkAutoLockPixels alp(src);
    if (!src.getPixels()) {
        return false;
    }
    if (src.width() < 2 || src.height() < 2) {
        return false;
    }
    dst->setConfig(src.config(), src.width(), src.height());
    dst->allocPixels();

    DiffuseLightingType lightingType(fKD);
    switch (light()->type()) {
        case SkLight::kDistant_LightType:
            lightBitmap<DiffuseLightingType, SkDistantLight>(lightingType, light(), src, dst, surfaceScale());
            break;
        case SkLight::kPoint_LightType:
            lightBitmap<DiffuseLightingType, SkPointLight>(lightingType, light(), src, dst, surfaceScale());
            break;
        case SkLight::kSpot_LightType:
            lightBitmap<DiffuseLightingType, SkSpotLight>(lightingType, light(), src, dst, surfaceScale());
            break;
    }
    return true;
}

bool SkDiffuseLightingImageFilter::asNewCustomStage(GrCustomStage** stage,
                                                    GrTexture* texture) const {
    if (stage) {
        SkScalar scale = SkScalarMul(surfaceScale(), SkIntToScalar(255));
        *stage = new GrDiffuseLightingEffect(texture, light(), scale, kd());
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////

SkSpecularLightingImageFilter::SkSpecularLightingImageFilter(SkLight* light, SkScalar surfaceScale, SkScalar ks, SkScalar shininess)
  : SkLightingImageFilter(light, surfaceScale),
    fKS(ks),
    fShininess(shininess)
{
}

SkSpecularLightingImageFilter::SkSpecularLightingImageFilter(SkFlattenableReadBuffer& buffer)
  : INHERITED(buffer)
{
    fKS = buffer.readScalar();
    fShininess = buffer.readScalar();
}

void SkSpecularLightingImageFilter::flatten(SkFlattenableWriteBuffer& buffer) const {
    this->INHERITED::flatten(buffer);
    buffer.writeScalar(fKS);
    buffer.writeScalar(fShininess);
}

bool SkSpecularLightingImageFilter::onFilterImage(Proxy*,
                                                  const SkBitmap& src,
                                                  const SkMatrix&,
                                                  SkBitmap* dst,
                                                  SkIPoint*) {
    if (src.config() != SkBitmap::kARGB_8888_Config) {
        return false;
    }
    SkAutoLockPixels alp(src);
    if (!src.getPixels()) {
        return false;
    }
    if (src.width() < 2 || src.height() < 2) {
        return false;
    }
    dst->setConfig(src.config(), src.width(), src.height());
    dst->allocPixels();

    SpecularLightingType lightingType(fKS, fShininess);
    switch (light()->type()) {
        case SkLight::kDistant_LightType:
            lightBitmap<SpecularLightingType, SkDistantLight>(lightingType, light(), src, dst, surfaceScale());
            break;
        case SkLight::kPoint_LightType:
            lightBitmap<SpecularLightingType, SkPointLight>(lightingType, light(), src, dst, surfaceScale());
            break;
        case SkLight::kSpot_LightType:
            lightBitmap<SpecularLightingType, SkSpotLight>(lightingType, light(), src, dst, surfaceScale());
            break;
    }
    return true;
}

bool SkSpecularLightingImageFilter::asNewCustomStage(GrCustomStage** stage,
                                                     GrTexture* texture) const {
    if (stage) {
        SkScalar scale = SkScalarMul(surfaceScale(), SkIntToScalar(255));
        *stage = new GrSpecularLightingEffect(texture, light(), scale, ks(), shininess());
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////

class GrGLLightingEffect  : public GrGLProgramStage {
public:
    GrGLLightingEffect(const GrProgramStageFactory& factory,
                       const GrCustomStage& stage);
    virtual ~GrGLLightingEffect();

    virtual void setupVariables(GrGLShaderBuilder* builder,
                                int stage) SK_OVERRIDE;
    virtual void emitVS(GrGLShaderBuilder* builder,
                        const char* vertexCoords) SK_OVERRIDE;
    virtual void emitFS(GrGLShaderBuilder* builder,
                        const char* outputColor,
                        const char* inputColor,
                        const char* samplerName) SK_OVERRIDE;

    virtual void emitLightFunc(const GrGLShaderBuilder*, SkString* funcs) = 0;

    static inline StageKey GenKey(const GrCustomStage& s);

    virtual void initUniforms(const GrGLShaderBuilder* builder,
                              const GrGLInterface*,
                              int programID) SK_OVERRIDE;
    virtual void setData(const GrGLInterface*, 
                         const GrCustomStage&,
                         const GrRenderTarget*,
                         int stageNum) SK_OVERRIDE;

private:
    typedef GrGLProgramStage INHERITED;

    UniformHandle   fImageIncrementUni;
    GrGLint         fImageIncrementLocation;
    UniformHandle   fSurfaceScaleUni;
    GrGLint         fSurfaceScaleLocation;
    GrGLLight*      fLight;
};

///////////////////////////////////////////////////////////////////////////////

class GrGLDiffuseLightingEffect  : public GrGLLightingEffect {
public:
    GrGLDiffuseLightingEffect(const GrProgramStageFactory& factory,
                              const GrCustomStage& stage);
    virtual void setupVariables(GrGLShaderBuilder* builder,
                                int stage) SK_OVERRIDE;
    virtual void emitLightFunc(const GrGLShaderBuilder*, SkString* funcs) SK_OVERRIDE;
    virtual void initUniforms(const GrGLShaderBuilder*,
                              const GrGLInterface*,
                              int programID) SK_OVERRIDE;
    virtual void setData(const GrGLInterface*, 
                         const GrCustomStage&,
                         const GrRenderTarget*,
                         int stageNum) SK_OVERRIDE;

private:
    typedef GrGLLightingEffect INHERITED;

    UniformHandle   fKDUni;
    GrGLint         fKDLocation;
};

///////////////////////////////////////////////////////////////////////////////

class GrGLSpecularLightingEffect  : public GrGLLightingEffect {
public:
    GrGLSpecularLightingEffect(const GrProgramStageFactory& factory,
                               const GrCustomStage& stage);
    virtual void setupVariables(GrGLShaderBuilder* builder,
                                int stage) SK_OVERRIDE;
    virtual void emitLightFunc(const GrGLShaderBuilder*, SkString* funcs) SK_OVERRIDE;
    virtual void initUniforms(const GrGLShaderBuilder*,
                              const GrGLInterface*,
                              int programID) SK_OVERRIDE;
    virtual void setData(const GrGLInterface*, 
                         const GrCustomStage&,
                         const GrRenderTarget*,
                         int stageNum) SK_OVERRIDE;

private:
    typedef GrGLLightingEffect INHERITED;

    UniformHandle   fKSUni;
    GrGLint         fKSLocation;
    UniformHandle   fShininessUni;
    GrGLint         fShininessLocation;
};

///////////////////////////////////////////////////////////////////////////////

GrLightingEffect::GrLightingEffect(GrTexture* texture, const SkLight* light, SkScalar surfaceScale)
    : GrSingleTextureEffect(texture)
    , fLight(light)
    , fSurfaceScale(surfaceScale) {
    fLight->ref();
}

GrLightingEffect::~GrLightingEffect() {
    fLight->unref();
}

bool GrLightingEffect::isEqual(const GrCustomStage& sBase) const {
    const GrLightingEffect& s =
        static_cast<const GrLightingEffect&>(sBase);
    return INHERITED::isEqual(sBase) &&
           fLight->isEqual(*s.fLight) &&
           fSurfaceScale == s.fSurfaceScale;
}

///////////////////////////////////////////////////////////////////////////////

GrDiffuseLightingEffect::GrDiffuseLightingEffect(GrTexture* texture, const SkLight* light, SkScalar surfaceScale, SkScalar kd)
    : INHERITED(texture, light, surfaceScale), fKD(kd) {
}

const GrProgramStageFactory& GrDiffuseLightingEffect::getFactory() const {
    return GrTProgramStageFactory<GrDiffuseLightingEffect>::getInstance();
}

bool GrDiffuseLightingEffect::isEqual(const GrCustomStage& sBase) const {
    const GrDiffuseLightingEffect& s =
        static_cast<const GrDiffuseLightingEffect&>(sBase);
    return INHERITED::isEqual(sBase) &&
            this->kd() == s.kd();
}

///////////////////////////////////////////////////////////////////////////////

GrGLLightingEffect::GrGLLightingEffect(const GrProgramStageFactory& factory,
                                       const GrCustomStage& stage)
    : GrGLProgramStage(factory)
    , fImageIncrementUni(kInvalidUniformHandle)
    , fImageIncrementLocation(0)
    , fSurfaceScaleUni(kInvalidUniformHandle)
    , fSurfaceScaleLocation(0) {
    const GrLightingEffect& m = static_cast<const GrLightingEffect&>(stage);
    fLight = m.light()->createGLLight();
}

GrGLLightingEffect::~GrGLLightingEffect() {
    delete fLight;
}

void GrGLLightingEffect::setupVariables(GrGLShaderBuilder* builder, int stage) {
    fImageIncrementUni = builder->addUniform(GrGLShaderBuilder::kFragment_ShaderType,
                                              kVec2f_GrSLType,
                                             "uImageIncrement", stage);
    fSurfaceScaleUni = builder->addUniform(GrGLShaderBuilder::kFragment_ShaderType,
                                           kFloat_GrSLType,
                                           "uSurfaceScale", stage);
    fLight->setupVariables(builder, stage);
}

void GrGLLightingEffect::emitVS(GrGLShaderBuilder* builder,
                                const char* vertexCoords) {
    fLight->emitVS(&builder->fVSCode);
}

void GrGLLightingEffect::initUniforms(const GrGLShaderBuilder* builder,
                                      const GrGLInterface* gl,
                                      int programID) {
    const char* imgInc = builder->getUniformCStr(fImageIncrementUni);
    const char* surfScale = builder->getUniformCStr(fSurfaceScaleUni);

    GR_GL_CALL_RET(gl, fSurfaceScaleLocation, GetUniformLocation(programID, surfScale));
    GR_GL_CALL_RET(gl, fImageIncrementLocation, GetUniformLocation(programID, imgInc));
    fLight->initUniforms(builder, gl, programID);
}

void GrGLLightingEffect::emitFS(GrGLShaderBuilder* builder,
                                const char* outputColor,
                                const char* inputColor,
                                const char* samplerName) {
    SkString* code = &builder->fFSCode;
    SkString* funcs = &builder->fFSFunctions;
    fLight->emitFuncs(builder, funcs);
    this->emitLightFunc(builder, funcs);
    funcs->appendf("float sobel(float a, float b, float c, float d, float e, float f, float scale) {\n");
    funcs->appendf("\treturn (-a + b - 2.0 * c + 2.0 * d -e + f) * scale;\n");
    funcs->appendf("}\n");
    funcs->appendf("vec3 pointToNormal(float x, float y, float scale) {\n");
    funcs->appendf("\treturn normalize(vec3(-x * scale, -y * scale, 1));\n");
    funcs->appendf("}\n");
    funcs->append("\n\
vec3 interiorNormal(float m[9], float surfaceScale) {\n\
    return pointToNormal(sobel(m[0], m[2], m[3], m[5], m[6], m[8], 0.25),\n\
                         sobel(m[0], m[6], m[1], m[7], m[2], m[8], 0.25),\n\
                         surfaceScale);\n}\n");

    code->appendf("\t\tvec2 coord = %s;\n", builder->fSampleCoords.c_str());
    code->appendf("\t\tfloat m[9];\n");

    const char* imgInc = builder->getUniformCStr(fImageIncrementUni);
    const char* surfScale = builder->getUniformCStr(fSurfaceScaleUni);

    int index = 0;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            SkString texCoords;
            texCoords.appendf("coord + vec2(%d, %d) * %s", dx, dy, imgInc);
            code->appendf("\t\tm[%d] = ", index++);
            builder->emitTextureLookup(samplerName, texCoords.c_str());
            code->appendf(".a;\n");
        }
    }
    code->appendf("\t\tvec3 surfaceToLight = ");
    SkString arg;
    arg.appendf("%s * m[4]", surfScale);
    fLight->emitSurfaceToLight(builder, code, arg.c_str());
    code->appendf(";\n");
    code->appendf("\t\t%s = light(interiorNormal(m, %s), surfaceToLight, ", outputColor, surfScale);
    fLight->emitLightColor(builder, code, "surfaceToLight");
    code->appendf(")%s;\n", builder->fModulate.c_str());
}

GrGLProgramStage::StageKey GrGLLightingEffect::GenKey(
  const GrCustomStage& s) {
    return static_cast<const GrLightingEffect&>(s).light()->type();
}

void GrGLLightingEffect::setData(const GrGLInterface* gl,
                                 const GrCustomStage& data,
                                 const GrRenderTarget* rt,
                                 int stageNum) {
    const GrLightingEffect& effect =
        static_cast<const GrLightingEffect&>(data);
    GrGLTexture* texture = static_cast<GrGLTexture*>(data.texture(0));
    float ySign = texture->orientation() == GrGLTexture::kTopDown_Orientation ? -1.0f : 1.0f;
    GR_GL_CALL(gl, Uniform2f(fImageIncrementLocation, 1.0f / texture->width(), ySign / texture->height()));
    GR_GL_CALL(gl, Uniform1f(fSurfaceScaleLocation, effect.surfaceScale()));
    fLight->setData(gl, rt, effect.light());
}

///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////

GrGLDiffuseLightingEffect::GrGLDiffuseLightingEffect(const GrProgramStageFactory& factory,
                                            const GrCustomStage& stage)
    : INHERITED(factory, stage)
    , fKDUni(kInvalidUniformHandle)
    , fKDLocation(0) {
}

void GrGLDiffuseLightingEffect::setupVariables(GrGLShaderBuilder* builder, int stage) {
    INHERITED::setupVariables(builder, stage);
    fKDUni = builder->addUniform(GrGLShaderBuilder::kFragment_ShaderType, kFloat_GrSLType, "uKD",
                                 stage);
}

void GrGLDiffuseLightingEffect::initUniforms(const GrGLShaderBuilder* builder,
                                             const GrGLInterface* gl,
                                             int programID) {
    INHERITED::initUniforms(builder, gl, programID);
    const char* kd = builder->getUniformCStr(fKDUni);
    GR_GL_CALL_RET(gl, fKDLocation, GetUniformLocation(programID, kd));
}

void GrGLDiffuseLightingEffect::emitLightFunc(const GrGLShaderBuilder* builder, SkString* funcs) {
    const char* kd = builder->getUniformCStr(fKDUni);
    funcs->appendf("vec4 light(vec3 normal, vec3 surfaceToLight, vec3 lightColor) {\n");
    funcs->appendf("\tfloat colorScale = %s * dot(normal, surfaceToLight);\n", kd);
    funcs->appendf("\treturn vec4(lightColor * clamp(colorScale, 0.0, 1.0), 1.0);\n");
    funcs->appendf("}\n");
}

void GrGLDiffuseLightingEffect::setData(const GrGLInterface* gl,
                                        const GrCustomStage& data,
                                        const GrRenderTarget* rt,
                                        int stageNum) {
    INHERITED::setData(gl, data, rt, stageNum);
    const GrDiffuseLightingEffect& effect =
        static_cast<const GrDiffuseLightingEffect&>(data);
    GR_GL_CALL(gl, Uniform1f(fKDLocation, effect.kd()));
}

///////////////////////////////////////////////////////////////////////////////

GrSpecularLightingEffect::GrSpecularLightingEffect(GrTexture* texture, const SkLight* light, SkScalar surfaceScale, SkScalar ks, SkScalar shininess)
    : INHERITED(texture, light, surfaceScale),
      fKS(ks),
      fShininess(shininess) {
}

const GrProgramStageFactory& GrSpecularLightingEffect::getFactory() const {
    return GrTProgramStageFactory<GrSpecularLightingEffect>::getInstance();
}

bool GrSpecularLightingEffect::isEqual(const GrCustomStage& sBase) const {
    const GrSpecularLightingEffect& s =
        static_cast<const GrSpecularLightingEffect&>(sBase);
    return INHERITED::isEqual(sBase) &&
           this->ks() == s.ks() &&
           this->shininess() == s.shininess();
}

///////////////////////////////////////////////////////////////////////////////

GrGLSpecularLightingEffect::GrGLSpecularLightingEffect(const GrProgramStageFactory& factory,
                                            const GrCustomStage& stage)
    : GrGLLightingEffect(factory, stage)
    , fKSUni(kInvalidUniformHandle)
    , fKSLocation(0)
    , fShininessUni(kInvalidUniformHandle)
    , fShininessLocation(0) {
}

void GrGLSpecularLightingEffect::setupVariables(GrGLShaderBuilder* builder, int stage) {
    INHERITED::setupVariables(builder, stage);
    fKSUni = builder->addUniform(GrGLShaderBuilder::kFragment_ShaderType,
                                 kFloat_GrSLType, "uKS", stage);
    fShininessUni = builder->addUniform(GrGLShaderBuilder::kFragment_ShaderType,
                                        kFloat_GrSLType, "uShininess", stage);
}

void GrGLSpecularLightingEffect::initUniforms(const GrGLShaderBuilder* builder,
                                              const GrGLInterface* gl,
                                              int programID) {
    INHERITED::initUniforms(builder, gl, programID);
    const char* ks = builder->getUniformCStr(fKSUni);
    const char* shininess = builder->getUniformCStr(fShininessUni);
    GR_GL_CALL_RET(gl, fKSLocation, GetUniformLocation(programID, ks));
    GR_GL_CALL_RET(gl, fShininessLocation, GetUniformLocation(programID, shininess));
}

void GrGLSpecularLightingEffect::emitLightFunc(const GrGLShaderBuilder* builder, SkString* funcs) {
    funcs->appendf("vec4 light(vec3 normal, vec3 surfaceToLight, vec3 lightColor) {\n");
    funcs->appendf("\tvec3 halfDir = vec3(normalize(surfaceToLight + vec3(0, 0, 1)));\n");

    const char* ks = builder->getUniformCStr(fKSUni);
    const char* shininess = builder->getUniformCStr(fShininessUni);
    funcs->appendf("\tfloat colorScale = %s * pow(dot(normal, halfDir), %s);\n", ks, shininess);
    funcs->appendf("\treturn vec4(lightColor * clamp(colorScale, 0.0, 1.0), 1.0);\n");
    funcs->appendf("}\n");
}

void GrGLSpecularLightingEffect::setData(const GrGLInterface* gl,
                                         const GrCustomStage& data,
                                         const GrRenderTarget* rt,
                                         int stageNum) {
    INHERITED::setData(gl, data, rt, stageNum);
    const GrSpecularLightingEffect& effect =
        static_cast<const GrSpecularLightingEffect&>(data);
    GR_GL_CALL(gl, Uniform1f(fKSLocation, effect.ks()));
    GR_GL_CALL(gl, Uniform1f(fShininessLocation, effect.shininess()));
}

///////////////////////////////////////////////////////////////////////////////

void GrGLLight::emitLightColor(const GrGLShaderBuilder* builder,
                               SkString* out,
                               const char *surfaceToLight) const {
    const char* color = builder->getUniformCStr(fColorUni);
    out->append(color);
}

void GrGLLight::setupVariables(GrGLShaderBuilder* builder, int stage) {
    const GrGLShaderVar& colorVar = builder->getUniformVariable(fColorUni);
    fColorUni = builder->addUniform(GrGLShaderBuilder::kFragment_ShaderType,
                                    kVec3f_GrSLType, "uLightColor", stage);
}

void GrGLLight::initUniforms(const GrGLShaderBuilder* builder,
                             const GrGLInterface* gl,
                             int programID) {
    const char* color = builder->getUniformCStr(fColorUni);
    GR_GL_CALL_RET(gl, fColorLocation, GetUniformLocation(programID, color));
}

void GrGLLight::setData(const GrGLInterface* gl, const GrRenderTarget* rt, const SkLight* light) const {
    setUniformPoint3(gl, fColorLocation, light->color() * SkScalarInvert(SkIntToScalar(255)));
}

GrGLLight* SkDistantLight::createGLLight() const {
    return new GrGLDistantLight();
}

///////////////////////////////////////////////////////////////////////////////

void GrGLDistantLight::setupVariables(GrGLShaderBuilder* builder, int stage) {
    INHERITED::setupVariables(builder, stage);
    fDirectionUni = builder->addUniform(GrGLShaderBuilder::kFragment_ShaderType, kVec3f_GrSLType,
                                        "uLightDirection", stage);
}

void GrGLDistantLight::initUniforms(const GrGLShaderBuilder* builder,
                                    const GrGLInterface* gl,
                                    int programID) {
    INHERITED::initUniforms(builder, gl, programID);
    const char* dir = builder->getUniformCStr(fDirectionUni);
    GR_GL_CALL_RET(gl, fDirectionLocation, GetUniformLocation(programID, dir));
}

void GrGLDistantLight::setData(const GrGLInterface* gl, const GrRenderTarget* rt, const SkLight* light) const {
    INHERITED::setData(gl, rt, light);
    SkASSERT(light->type() == SkLight::kDistant_LightType);
    const SkDistantLight* distantLight = static_cast<const SkDistantLight*>(light);
    setUniformNormal3(gl, fDirectionLocation, distantLight->direction());
}

void GrGLDistantLight::emitSurfaceToLight(const GrGLShaderBuilder* builder,
                                          SkString* out,
                                          const char* z) const {
    const char* dir = builder->getUniformCStr(fDirectionUni);
    out->append(dir);
}

///////////////////////////////////////////////////////////////////////////////

void GrGLPointLight::setupVariables(GrGLShaderBuilder* builder, int stage) {
    INHERITED::setupVariables(builder, stage);
    fLocationUni = builder->addUniform(GrGLShaderBuilder::kFragment_ShaderType, kVec3f_GrSLType,
                                       "uLightLocation", stage);
}

void GrGLPointLight::initUniforms(const GrGLShaderBuilder* builder,
                                  const GrGLInterface* gl,
                                  int programID) {
    INHERITED::initUniforms(builder, gl, programID);
    const char* loc = builder->getUniformCStr(fLocationUni);
    GR_GL_CALL_RET(gl, fLocationLocation, GetUniformLocation(programID, loc));
}

void GrGLPointLight::setData(const GrGLInterface* gl, const GrRenderTarget* rt, const SkLight* light) const {
    INHERITED::setData(gl, rt, light);
    SkASSERT(light->type() == SkLight::kPoint_LightType);
    const SkPointLight* pointLight = static_cast<const SkPointLight*>(light);
    setUniformPoint3FlipY(gl, fLocationLocation, pointLight->location(), rt->height());
}

void GrGLPointLight::emitVS(SkString* out) const {
}

void GrGLPointLight::emitSurfaceToLight(const GrGLShaderBuilder* builder, SkString* out, const char* z) const {
    const char* loc = builder->getUniformCStr(fLocationUni);
    out->appendf(
        "normalize(%s - vec3(gl_FragCoord.xy, %s))", loc, z);
}

///////////////////////////////////////////////////////////////////////////////

void GrGLSpotLight::setupVariables(GrGLShaderBuilder* builder, int stage) {
    INHERITED::setupVariables(builder, stage);
    fLocationUni = builder->addUniform(GrGLShaderBuilder::kFragment_ShaderType,
                                       kVec3f_GrSLType, "uLightLocation", stage);
    fExponentUni = builder->addUniform(GrGLShaderBuilder::kFragment_ShaderType,
                                       kFloat_GrSLType, "uExponent", stage);
    fCosInnerConeAngleUni = builder->addUniform(GrGLShaderBuilder::kFragment_ShaderType,
                                                kFloat_GrSLType, "uCosInnerConeAngle", stage);
    fCosOuterConeAngleUni = builder->addUniform(GrGLShaderBuilder::kFragment_ShaderType,
                                                kFloat_GrSLType, "uCosOuterConeAngle", stage);
    fConeScaleUni = builder->addUniform(GrGLShaderBuilder::kFragment_ShaderType,
                                        kFloat_GrSLType, "uConeScale", stage);
    fSUni = builder->addUniform(GrGLShaderBuilder::kFragment_ShaderType,
                              kVec3f_GrSLType, "uS", stage);
}

void GrGLSpotLight::initUniforms(const GrGLShaderBuilder* builder, const GrGLInterface* gl, int programID) {
    INHERITED::initUniforms(builder, gl, programID);
    const char* location = builder->getUniformCStr(fLocationUni);
    const char* exponent = builder->getUniformCStr(fExponentUni);
    const char* cosInner = builder->getUniformCStr(fCosInnerConeAngleUni);
    const char* cosOuter = builder->getUniformCStr(fCosOuterConeAngleUni);
    const char* coneScale = builder->getUniformCStr(fConeScaleUni);
    const char* s = builder->getUniformCStr(fSUni);
    GR_GL_CALL_RET(gl, fLocationLocation, GetUniformLocation(programID, location));
    GR_GL_CALL_RET(gl, fExponentLocation, GetUniformLocation(programID, exponent));
    GR_GL_CALL_RET(gl, fCosInnerConeAngleLocation, GetUniformLocation(programID, cosInner));
    GR_GL_CALL_RET(gl, fCosOuterConeAngleLocation, GetUniformLocation(programID, cosOuter));
    GR_GL_CALL_RET(gl, fConeScaleLocation, GetUniformLocation(programID, coneScale));
    GR_GL_CALL_RET(gl, fSLocation, GetUniformLocation(programID, s));
}

void GrGLSpotLight::setData(const GrGLInterface* gl, const GrRenderTarget* rt, const SkLight* light) const {
    INHERITED::setData(gl, rt, light);
    SkASSERT(light->type() == SkLight::kSpot_LightType);
    const SkSpotLight* spotLight = static_cast<const SkSpotLight *>(light);
    setUniformPoint3FlipY(gl, fLocationLocation, spotLight->location(), rt->height());
    GR_GL_CALL(gl, Uniform1f(fExponentLocation, spotLight->specularExponent()));
    GR_GL_CALL(gl, Uniform1f(fCosInnerConeAngleLocation, spotLight->cosInnerConeAngle()));
    GR_GL_CALL(gl, Uniform1f(fCosOuterConeAngleLocation, spotLight->cosOuterConeAngle()));
    GR_GL_CALL(gl, Uniform1f(fConeScaleLocation, spotLight->coneScale()));
    setUniformNormal3(gl, fSLocation, spotLight->s());
}

void GrGLSpotLight::emitVS(SkString* out) const {
}

void GrGLSpotLight::emitFuncs(const GrGLShaderBuilder* builder, SkString* out) const {
    const char* exponent = builder->getUniformCStr(fExponentUni);
    const char* cosInner = builder->getUniformCStr(fCosInnerConeAngleUni);
    const char* cosOuter = builder->getUniformCStr(fCosOuterConeAngleUni);
    const char* coneScale = builder->getUniformCStr(fConeScaleUni);
    const char* s = builder->getUniformCStr(fSUni);
    const char* color = builder->getUniformCStr(fColorUni);

    out->appendf("vec3 lightColor(vec3 surfaceToLight) {\n");
    out->appendf("\tfloat cosAngle = -dot(surfaceToLight, %s);\n", s);
    out->appendf("\tif (cosAngle < %s) {\n", cosOuter);
    out->appendf("\t\treturn vec3(0);\n");
    out->appendf("\t}\n");
    out->appendf("\tfloat scale = pow(cosAngle, %s);\n", exponent);
    out->appendf("\tif (cosAngle < %s) {\n", cosInner);
    out->appendf("\t\treturn %s * scale * (cosAngle - %s) * %s;\n", color, cosOuter, coneScale);
    out->appendf("\t}\n");
    out->appendf("\treturn %s;\n", color);
    out->appendf("}\n");
}

void GrGLSpotLight::emitSurfaceToLight(const GrGLShaderBuilder* builder,
                                       SkString* out,
                                       const char* z) const {
    const char* location= builder->getUniformCStr(fLocationUni);
    out->appendf("normalize(%s - vec3(gl_FragCoord.xy, %s))", location, z);
}

void GrGLSpotLight::emitLightColor(const GrGLShaderBuilder* builder,
                                   SkString* out, const char *surfaceToLight) const {
    out->appendf("lightColor(%s)", surfaceToLight);
}

SK_DEFINE_FLATTENABLE_REGISTRAR_GROUP_START(SkLightingImageFilter)
    SK_DEFINE_FLATTENABLE_REGISTRAR_ENTRY(SkDiffuseLightingImageFilter)
    SK_DEFINE_FLATTENABLE_REGISTRAR_ENTRY(SkSpecularLightingImageFilter)
    SK_DEFINE_FLATTENABLE_REGISTRAR_ENTRY(SkDistantLight)
    SK_DEFINE_FLATTENABLE_REGISTRAR_ENTRY(SkPointLight)
    SK_DEFINE_FLATTENABLE_REGISTRAR_ENTRY(SkSpotLight)
SK_DEFINE_FLATTENABLE_REGISTRAR_GROUP_END
