/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef GrGLShaderBuilder_DEFINED
#define GrGLShaderBuilder_DEFINED

#include "GrAllocator.h"
#include "gl/GrGLShaderVar.h"
#include "gl/GrGLSL.h"

typedef GrTAllocator<GrGLShaderVar> VarArray;

/**
  Containts all the incremental state of a shader as it is being built,
  as well as helpers to manipulate that state.
  TODO: migrate CompileShaders() here?
*/

class GrGLShaderBuilder {

public:

    GrGLShaderBuilder();

    void appendVarying(GrSLType type,
                       const char* name,
                       const char** vsOutName = NULL,
                       const char** fsInName = NULL);

    void appendVarying(GrSLType type,
                       const char* name,
                       int stageNum,
                       const char** vsOutName = NULL,
                       const char** fsInName = NULL);

    void computeSwizzle(uint32_t configFlags);
    void computeModulate(const char* fsInColor);

    void emitTextureSetup();

    /** texture2D(samplerName, coordName), with projection
        if necessary; if coordName is not specified,
        uses fSampleCoords. */
    void emitTextureLookup(const char* samplerName,
                           const char* coordName = NULL);

    /** sets outColor to results of texture lookup, with
        swizzle, and/or modulate as necessary */
    void emitDefaultFetch(const char* outColor,
                          const char* samplerName);

    // TODO: needs a better name
    enum SamplerMode {
        kDefault_SamplerMode,
        kProj_SamplerMode,
        kExplicitDivide_SamplerMode  // must do an explicit divide
    };

    // TODO: computing this requires information about fetch mode
    // && coord mapping, as well as StageDesc::fOptFlags - proably
    // need to set up default value and have some custom stages
    // override as necessary?
    void setSamplerMode(SamplerMode samplerMode) {
        fSamplerMode = samplerMode;
    }


    GrStringBuilder fHeader; // VS+FS, GLSL version, etc
    VarArray        fVSUnis;
    VarArray        fVSAttrs;
    VarArray        fVSOutputs;
    VarArray        fGSInputs;
    VarArray        fGSOutputs;
    VarArray        fFSInputs;
    GrStringBuilder fGSHeader; // layout qualifiers specific to GS
    VarArray        fFSUnis;
    VarArray        fFSOutputs;
    GrStringBuilder fFSFunctions;
    GrStringBuilder fVSCode;
    GrStringBuilder fGSCode;
    GrStringBuilder fFSCode;
    bool            fUsesGS;

    /// Per-stage settings - only valid while we're inside
    /// GrGLProgram::genStageCode().
    //@{

    int              fVaryingDims;
    static const int fCoordDims = 2;

protected:

    SamplerMode      fSamplerMode;

public:

    /// True if fSampleCoords is an expression; false if it's a bare
    /// variable name
    bool             fComplexCoord;
    GrStringBuilder  fSampleCoords;

    GrStringBuilder  fSwizzle;
    GrStringBuilder  fModulate;

    //@}

};

#endif
