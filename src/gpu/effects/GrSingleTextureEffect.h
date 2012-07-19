/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef GrSingleTextureEffect_DEFINED
#define GrSingleTextureEffect_DEFINED

#include "GrCustomStage.h"

class GrGLSingleTextureEffect;

/**
 * An effect that merely blits a single texture; commonly used as a base class.
 */
class GrSingleTextureEffect : public GrCustomStage {

public:
    GrSingleTextureEffect(GrTexture* texture);
    virtual ~GrSingleTextureEffect();

    virtual unsigned int numTextures() const SK_OVERRIDE;
    virtual GrTexture* texture(unsigned int index) const SK_OVERRIDE;

    static const char* Name() { return "Single Texture"; }

    typedef GrGLSingleTextureEffect GLProgramStage;

    virtual const GrProgramStageFactory& getFactory() const SK_OVERRIDE;

private:
    GrTexture* fTexture;

    typedef GrCustomStage INHERITED;
};

#endif
