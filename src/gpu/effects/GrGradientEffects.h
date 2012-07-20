/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef GrGradientEffects_DEFINED
#define GrGradientEffects_DEFINED

#include "GrSingleTextureEffect.h"
#include "GrTypes.h"
#include "GrScalar.h"
#include "SkShader.h"

/*
 * The intepretation of the texture matrix depends on the sample mode. The
 * texture matrix is applied both when the texture coordinates are explicit
 * and  when vertex positions are used as texture  coordinates. In the latter
 * case the texture matrix is applied to the pre-view-matrix position 
 * values.
 *
 * Normal SampleMode
 *  The post-matrix texture coordinates are in normalize space with (0,0) at
 *  the top-left and (1,1) at the bottom right.
 * RadialGradient
 *  The matrix specifies the radial gradient parameters.
 *  (0,0) in the post-matrix space is center of the radial gradient.
 * Radial2Gradient
 *   Matrix transforms to space where first circle is centered at the
 *   origin. The second circle will be centered (x, 0) where x may be 
 *   0 and is provided by setRadial2Params. The post-matrix space is 
 *   normalized such that 1 is the second radius - first radius.
 * SweepGradient
 *  The angle from the origin of texture coordinates in post-matrix space
 *  determines the gradient value.
 */

// Base class for Gr gradient effects
class GrGradientEffect : public GrCustomStage {
public:

    GrGradientEffect(GrTexture* texture);
    GrGradientEffect(GrContext* ctx, const SkShader& shader);

    virtual ~GrGradientEffect();

    unsigned int numTextures() const;
    GrTexture* texture(unsigned int index) const;

    bool useTexture() const { return fUseTexture; }

private:

    GrTexture* fTexture;
    bool fUseTexture;

    typedef GrCustomStage INHERITED;

};

class GrGLLinearGradient;

class GrLinearGradient : public GrGradientEffect {

public:

    GrLinearGradient(GrTexture* texture);
    GrLinearGradient(GrContext* ctx, const SkShader& shader);
    virtual ~GrLinearGradient();

    static const char* Name() { return "Linear Gradient"; }
    virtual const GrProgramStageFactory& getFactory() const SK_OVERRIDE;

    typedef GrGLLinearGradient GLProgramStage;

private:

    typedef GrGradientEffect INHERITED;
};

class GrGLRadialGradient;

class GrRadialGradient : public GrGradientEffect {

public:

    GrRadialGradient(GrTexture* texture);
    GrRadialGradient(GrContext* ctx, const SkShader& shader);
    virtual ~GrRadialGradient();

    static const char* Name() { return "Radial Gradient"; }
    virtual const GrProgramStageFactory& getFactory() const SK_OVERRIDE;

    typedef GrGLRadialGradient GLProgramStage;

private:

    typedef GrGradientEffect INHERITED;
};

class GrGLRadial2Gradient;

class GrRadial2Gradient : public GrGradientEffect {

public:

    GrRadial2Gradient(GrTexture* texture, GrScalar center, GrScalar radius, bool posRoot);
    GrRadial2Gradient(GrContext* ctx, const SkShader& shader);
    virtual ~GrRadial2Gradient();

    static const char* Name() { return "Two-Point Radial Gradient"; }
    virtual const GrProgramStageFactory& getFactory() const SK_OVERRIDE;
    virtual bool isEqual(const GrCustomStage&) const SK_OVERRIDE;

    // The radial gradient parameters can collapse to a linear (instead of quadratic) equation.
    bool isDegenerate() const { return GR_Scalar1 == fCenterX1; }
    GrScalar center() const { return fCenterX1; }
    GrScalar radius() const { return fRadius0; }
    bool isPosRoot() const { return SkToBool(fPosRoot); }

    typedef GrGLRadial2Gradient GLProgramStage;

private:

    // @{
    // Cache of values - these can change arbitrarily, EXCEPT
    // we shouldn't change between degenerate and non-degenerate?!

    GrScalar fCenterX1;
    GrScalar fRadius0;
    SkBool8  fPosRoot;

    // @}

    typedef GrGradientEffect INHERITED;
};

class GrGLConical2Gradient;

class GrConical2Gradient : public GrGradientEffect {

public:

    GrConical2Gradient(GrTexture* texture, GrScalar center, GrScalar radius, GrScalar diffRadius);
    GrConical2Gradient(GrContext* ctx, const SkShader& shader);
    virtual ~GrConical2Gradient();

    static const char* Name() { return "Two-Point Conical Gradient"; }
    virtual const GrProgramStageFactory& getFactory() const SK_OVERRIDE;
    virtual bool isEqual(const GrCustomStage&) const SK_OVERRIDE;

    // The radial gradient parameters can collapse to a linear (instead of quadratic) equation.
    bool isDegenerate() const { return SkScalarAbs(fDiffRadius) == SkScalarAbs(fCenterX1); }
    GrScalar center() const { return fCenterX1; }
    GrScalar diffRadius() const { return fDiffRadius; }
    GrScalar radius() const { return fRadius0; }

    typedef GrGLConical2Gradient GLProgramStage;

private:

    // @{
    // Cache of values - these can change arbitrarily, EXCEPT
    // we shouldn't change between degenerate and non-degenerate?!

    GrScalar fCenterX1;
    GrScalar fRadius0;
    GrScalar fDiffRadius;

    // @}

    typedef GrGradientEffect INHERITED;
};

class GrGLSweepGradient;

class GrSweepGradient : public GrGradientEffect {

public:

    GrSweepGradient(GrTexture* texture);
    GrSweepGradient(GrContext* ctx, const SkShader& shader);
    virtual ~GrSweepGradient();

    static const char* Name() { return "Sweep Gradient"; }
    virtual const GrProgramStageFactory& getFactory() const SK_OVERRIDE;

    typedef GrGLSweepGradient GLProgramStage;

protected:

    typedef GrGradientEffect INHERITED;
};

#endif

