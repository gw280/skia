/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gl/GrGLShaderBuilder.h"
#include "gl/GrGLProgram.h"

namespace {

// number of each input/output type in a single allocation block
static const int sVarsPerBlock = 8;

// except FS outputs where we expect 2 at most.
static const int sMaxFSOutputs = 2;

}

// Architectural assumption: always 2-d input coords.
// Likely to become non-constant and non-static, perhaps even
// varying by stage, if we use 1D textures for gradients!
//const int GrGLShaderBuilder::fCoordDims = 2;

GrGLShaderBuilder::GrGLShaderBuilder()
    : fVSUnis(sVarsPerBlock)
    , fVSAttrs(sVarsPerBlock)
    , fVSOutputs(sVarsPerBlock)
    , fGSInputs(sVarsPerBlock)
    , fGSOutputs(sVarsPerBlock)
    , fFSInputs(sVarsPerBlock)
    , fFSUnis(sVarsPerBlock)
    , fFSOutputs(sMaxFSOutputs)
    , fUsesGS(false)
    , fVaryingDims(0)
    , fSamplerMode(kDefault_SamplerMode)
    , fComplexCoord(false) {

}

void GrGLShaderBuilder::appendVarying(GrSLType type,
                                      const char* name,
                                      const char** vsOutName,
                                      const char** fsInName) {
    fVSOutputs.push_back();
    fVSOutputs.back().setType(type);
    fVSOutputs.back().setTypeModifier(GrGLShaderVar::kOut_TypeModifier);
    fVSOutputs.back().accessName()->printf("v%s", name);
    if (vsOutName) {
        *vsOutName = fVSOutputs.back().getName().c_str();
    }
    // input to FS comes either from VS or GS
    const GrStringBuilder* fsName;
    if (fUsesGS) {
        // if we have a GS take each varying in as an array
        // and output as non-array.
        fGSInputs.push_back();
        fGSInputs.back().setType(type);
        fGSInputs.back().setTypeModifier(GrGLShaderVar::kIn_TypeModifier);
        fGSInputs.back().setUnsizedArray();
        *fGSInputs.back().accessName() = fVSOutputs.back().getName();
        fGSOutputs.push_back();
        fGSOutputs.back().setType(type);
        fGSOutputs.back().setTypeModifier(GrGLShaderVar::kOut_TypeModifier);
        fGSOutputs.back().accessName()->printf("g%s", name);
        fsName = fGSOutputs.back().accessName();
    } else {
        fsName = fVSOutputs.back().accessName();
    }
    fFSInputs.push_back();
    fFSInputs.back().setType(type);
    fFSInputs.back().setTypeModifier(GrGLShaderVar::kIn_TypeModifier);
    fFSInputs.back().setName(*fsName);
    if (fsInName) {
        *fsInName = fsName->c_str();
    }
}


void GrGLShaderBuilder::appendVarying(GrSLType type,
                                      const char* name,
                                      int stageNum,
                                      const char** vsOutName,
                                      const char** fsInName) {
    GrStringBuilder nameWithStage(name);
    nameWithStage.appendS32(stageNum);
    this->appendVarying(type, nameWithStage.c_str(), vsOutName, fsInName);
}

void GrGLShaderBuilder::computeSwizzle(uint32_t configFlags) {
   static const uint32_t kMulByAlphaMask =
        (GrGLProgram::StageDesc::kMulRGBByAlpha_RoundUp_InConfigFlag |
         GrGLProgram::StageDesc::kMulRGBByAlpha_RoundDown_InConfigFlag);

    fSwizzle = "";
    if (configFlags & GrGLProgram::StageDesc::kSwapRAndB_InConfigFlag) {
        GrAssert(!(configFlags &
                   GrGLProgram::StageDesc::kSmearAlpha_InConfigFlag));
        GrAssert(!(configFlags &
                   GrGLProgram::StageDesc::kSmearRed_InConfigFlag));
        fSwizzle = ".bgra";
    } else if (configFlags & GrGLProgram::StageDesc::kSmearAlpha_InConfigFlag) {
        GrAssert(!(configFlags & kMulByAlphaMask));
        GrAssert(!(configFlags &
                   GrGLProgram::StageDesc::kSmearRed_InConfigFlag));
        fSwizzle = ".aaaa";
    } else if (configFlags & GrGLProgram::StageDesc::kSmearRed_InConfigFlag) {
        GrAssert(!(configFlags & kMulByAlphaMask));
        GrAssert(!(configFlags &
                   GrGLProgram::StageDesc::kSmearAlpha_InConfigFlag));
        fSwizzle = ".rrrr";
    }
}

void GrGLShaderBuilder::computeModulate(const char* fsInColor) {
    if (NULL != fsInColor) {
        fModulate.printf(" * %s", fsInColor);
    }
}

void GrGLShaderBuilder::emitTextureSetup() {
    GrStringBuilder retval;

    switch (fSamplerMode) {
        case kDefault_SamplerMode:
            // Fall through
        case kProj_SamplerMode:
            // Do nothing
            break;
        case kExplicitDivide_SamplerMode:
            retval = "inCoord";
            fFSCode.appendf("\t %s %s = %s%s / %s%s\n",
                GrGLShaderVar::TypeString
                    (GrSLFloatVectorType(fCoordDims)),
                retval.c_str(),
                fSampleCoords.c_str(),
                GrGLSLVectorNonhomogCoords(fVaryingDims),
                fSampleCoords.c_str(),
                GrGLSLVectorHomogCoord(fVaryingDims));
            fSampleCoords = retval;
            break;
    }
}

void GrGLShaderBuilder::emitTextureLookup(const char* samplerName,
                                          const char* coordName) {
    if (NULL == coordName) {
        coordName = fSampleCoords.c_str();
    }
    switch (fSamplerMode) {
        default:
            SkDEBUGFAIL("Unknown sampler mode");
            // Fall through
        case kDefault_SamplerMode:
            // Fall through
        case kExplicitDivide_SamplerMode:
            fFSCode.appendf("texture2D(%s, %s)", samplerName, coordName);
            break;
        case kProj_SamplerMode:
            fFSCode.appendf("texture2DProj(%s, %s)", samplerName, coordName);
            break;
    }

}

void GrGLShaderBuilder::emitDefaultFetch(const char* outColor,
                                         const char* samplerName) {
    fFSCode.appendf("\t%s = ", outColor);
    this->emitTextureLookup(samplerName);
    fFSCode.appendf("%s%s;\n", fSwizzle.c_str(), fModulate.c_str());
}

