
/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


#ifndef GrGLProgram_DEFINED
#define GrGLProgram_DEFINED

#include "GrGLInterface.h"
#include "GrStringBuilder.h"
#include "GrGpu.h"

#include "SkXfermode.h"

class GrBinHashKeyBuilder;

struct ShaderCodeSegments;

// optionally compile the experimental GS code. Set to GR_DEBUG
// so that debug build bots will execute the code.
#define GR_GL_EXPERIMENTAL_GS GR_DEBUG

/**
 * This class manages a GPU program and records per-program information.
 * We can specify the attribute locations so that they are constant
 * across our shaders. But the driver determines the uniform locations
 * at link time. We don't need to remember the sampler uniform location
 * because we will bind a texture slot to it and never change it
 * Uniforms are program-local so we can't rely on fHWState to hold the
 * previous uniform state after a program change.
 */
class GrGLProgram {
public:
    enum GLSLVersion {
        k120_GLSLVersion, // Desktop GLSL 1.20 and ES2 shading lang
        k130_GLSLVersion, // Desktop GLSL 1.30
        k150_GLSLVersion  // Dekstop GLSL 1.50
    };

    class CachedData;

    GrGLProgram();
    ~GrGLProgram();

    /**
     *  This is the heavy initilization routine for building a GLProgram.
     *  The result of heavy init is not stored in datamembers of GrGLProgam,
     *  but in a separate cacheable container.
     */
    bool genProgram(const GrGLInterface* gl,
                    GLSLVersion glslVersion,
                    CachedData* programData) const;

     /**
      * The shader may modify the blend coeffecients. Params are in/out
      */
     void overrideBlend(GrBlendCoeff* srcCoeff, GrBlendCoeff* dstCoeff) const;

    /**
     * Attribute indices. These should not overlap. Matrices consume 3 slots.
     */
    static int PositionAttributeIdx() { return 0; }
    static int TexCoordAttributeIdx(int tcIdx) { return 1 + tcIdx; }
    static int ColorAttributeIdx() { return 1 + GrDrawTarget::kMaxTexCoords; }
    static int CoverageAttributeIdx() {
        return 2 + GrDrawTarget::kMaxTexCoords;
    }
    static int EdgeAttributeIdx() { return 3 + GrDrawTarget::kMaxTexCoords; }

    static int ViewMatrixAttributeIdx() {
        return 4 + GrDrawTarget::kMaxTexCoords;
    }
    static int TextureMatrixAttributeIdx(int stage) {
        return 7 + GrDrawTarget::kMaxTexCoords + 3 * stage;
    }

private:

    // Parameters that affect code generation
    // These structs should be kept compact; they are the input to an
    // expensive hash key generator.
    struct ProgramDesc {
        ProgramDesc() {
            // since we use this as part of a key we can't have any unitialized
            // padding
            memset(this, 0, sizeof(ProgramDesc));
        }

        struct StageDesc {
            enum OptFlagBits {
                kNoPerspective_OptFlagBit       = 1 << 0,
                kIdentityMatrix_OptFlagBit      = 1 << 1,
                kCustomTextureDomain_OptFlagBit = 1 << 2,
                kIsEnabled_OptFlagBit           = 1 << 7
            };
            enum Modulation {
                kColor_Modulation,
                kAlpha_Modulation,

                kModulationCnt
            };
            enum FetchMode {
                kSingle_FetchMode,
                k2x2_FetchMode,
                kConvolution_FetchMode,

                kFetchModeCnt,
            };
            enum CoordMapping {
                kIdentity_CoordMapping,
                kRadialGradient_CoordMapping,
                kSweepGradient_CoordMapping,
                kRadial2Gradient_CoordMapping,
                // need different shader computation when quadratic
                // eq describing the gradient degenerates to a linear eq.
                kRadial2GradientDegenerate_CoordMapping,
                kCoordMappingCnt
            };

            uint8_t fOptFlags;
            uint8_t fModulation;  // casts to enum Modulation
            uint8_t fFetchMode;  // casts to enum FetchMode
            uint8_t fCoordMapping;  // casts to enum CoordMapping
            uint8_t fKernelWidth;

            inline bool isEnabled() const {
                return SkToBool(fOptFlags & kIsEnabled_OptFlagBit);
            }
            inline void setEnabled(bool newValue) {
                if (newValue) {
                    fOptFlags |= kIsEnabled_OptFlagBit;
                } else {
                    fOptFlags &= ~kIsEnabled_OptFlagBit;
                }
            }
        };

        // Specifies where the intitial color comes from before the stages are
        // applied.
        enum ColorType {
            kSolidWhite_ColorType,
            kTransBlack_ColorType,
            kAttribute_ColorType,
            kUniform_ColorType,

            kColorTypeCnt
        };
        // Dual-src blending makes use of a secondary output color that can be
        // used as a per-pixel blend coeffecient. This controls whether a
        // secondary source is output and what value it holds.
        enum DualSrcOutput {
            kNone_DualSrcOutput,
            kCoverage_DualSrcOutput,
            kCoverageISA_DualSrcOutput,
            kCoverageISC_DualSrcOutput,

            kDualSrcOutputCnt
        };

        GrDrawTarget::VertexEdgeType fVertexEdgeType;

        // stripped of bits that don't affect prog generation
        GrVertexLayout fVertexLayout;

        StageDesc fStages[GrDrawTarget::kNumStages];

        // To enable experimental geometry shader code (not for use in
        // production)
#if GR_GL_EXPERIMENTAL_GS
        bool fExperimentalGS;
#endif

        uint8_t fColorType;  // casts to enum ColorType
        uint8_t fDualSrcOutput;  // casts to enum DualSrcOutput
        int8_t fFirstCoverageStage;
        SkBool8 fEmitsPointSize;
        SkBool8 fEdgeAAConcave;

        int8_t fEdgeAANumEdges;
        uint8_t fColorFilterXfermode;  // casts to enum SkXfermode::Mode

        uint8_t fPadTo32bLengthMultiple [1];

    } fProgramDesc;
    GR_STATIC_ASSERT(!(sizeof(ProgramDesc) % 4));

    const ProgramDesc& getDesc() { return fProgramDesc; }

    // for code readability
    typedef ProgramDesc::StageDesc StageDesc;

public:
    enum {
        kUnusedUniform = -1,
        kSetAsAttribute = 1000,
    };

    struct StageUniLocations {
        GrGLint fTextureMatrixUni;
        GrGLint fNormalizedTexelSizeUni;
        GrGLint fSamplerUni;
        GrGLint fRadial2Uni;
        GrGLint fTexDomUni;
        GrGLint fKernelUni;
        GrGLint fImageIncrementUni;
        void reset() {
            fTextureMatrixUni = kUnusedUniform;
            fNormalizedTexelSizeUni = kUnusedUniform;
            fSamplerUni = kUnusedUniform;
            fRadial2Uni = kUnusedUniform;
            fTexDomUni = kUnusedUniform;
            fKernelUni = kUnusedUniform;
            fImageIncrementUni = kUnusedUniform;
        }
    };

    struct UniLocations {
        GrGLint fViewMatrixUni;
        GrGLint fColorUni;
        GrGLint fEdgesUni;
        GrGLint fColorFilterUni;
        StageUniLocations fStages[GrDrawTarget::kNumStages];
        void reset() {
            fViewMatrixUni = kUnusedUniform;
            fColorUni = kUnusedUniform;
            fEdgesUni = kUnusedUniform;
            fColorFilterUni = kUnusedUniform;
            for (int s = 0; s < GrDrawTarget::kNumStages; ++s) {
                fStages[s].reset();
            }
        }
    };

    class CachedData : public ::GrNoncopyable {
    public:
        CachedData() {
        }

        ~CachedData() {
        }

        void copyAndTakeOwnership(CachedData& other) {
            memcpy(this, &other, sizeof(*this));
        }

    public:

        // IDs
        GrGLuint    fVShaderID;
        GrGLuint    fGShaderID;
        GrGLuint    fFShaderID;
        GrGLuint    fProgramID;
        // shader uniform locations (-1 if shader doesn't use them)
        UniLocations fUniLocations;

        GrMatrix  fViewMatrix;

        // these reflect the current values of uniforms
        // (GL uniform values travel with program)
        GrColor                     fColor;
        GrColor                     fColorFilterColor;
        GrMatrix                    fTextureMatrices[GrDrawTarget::kNumStages];
        // width and height used for normalized texel size
        int                         fTextureWidth[GrDrawTarget::kNumStages];
        int                         fTextureHeight[GrDrawTarget::kNumStages]; 
        GrScalar                    fRadial2CenterX1[GrDrawTarget::kNumStages];
        GrScalar                    fRadial2Radius0[GrDrawTarget::kNumStages];
        bool                        fRadial2PosRoot[GrDrawTarget::kNumStages];
        GrRect                      fTextureDomain[GrDrawTarget::kNumStages];

    private:
        enum Constants {
            kUniLocationPreAllocSize = 8
        };

    }; // CachedData

    enum Constants {
        kProgramKeySize = sizeof(ProgramDesc)
    };

    // Provide an opaque ProgramDesc
    const uint32_t* keyData() const{
        return reinterpret_cast<const uint32_t*>(&fProgramDesc);
    }

private:
    enum {
        kUseUniform = 2000
    };

    // should set all fields in locations var to kUseUniform if the
    // corresponding uniform is required for the program.
    void genStageCode(const GrGLInterface* gl,
                      int stageNum,
                      const ProgramDesc::StageDesc& desc,
                      const char* fsInColor, // NULL means no incoming color
                      const char* fsOutColor,
                      const char* vsInCoord,
                      ShaderCodeSegments* segments,
                      StageUniLocations* locations) const;

    void genGeometryShader(const GrGLInterface* gl,
                           GLSLVersion glslVersion,
                           ShaderCodeSegments* segments) const;

    // generates code to compute coverage based on edge AA.
    void genEdgeCoverage(const GrGLInterface* gl,
                         GrVertexLayout layout,
                         CachedData* programData,
                         GrStringBuilder* coverageVar,
                         ShaderCodeSegments* segments) const;

    static bool CompileShaders(const GrGLInterface* gl,
                               GLSLVersion glslVersion,
                               const ShaderCodeSegments& segments, 
                               CachedData* programData);

    // Compiles a GL shader, returns shader ID or 0 if failed
    // params have same meaning as glShaderSource
    static GrGLuint CompileShader(const GrGLInterface* gl,
                                  GrGLenum type, int stringCnt,
                                  const char** strings,
                                  int* stringLengths);

    // Creates a GL program ID, binds shader attributes to GL vertex attrs, and
    // links the program
    bool bindOutputsAttribsAndLinkProgram(
                const GrGLInterface* gl,
                GrStringBuilder texCoordAttrNames[GrDrawTarget::kMaxTexCoords],
                bool bindColorOut,
                bool bindDualSrcOut,
                CachedData* programData) const;

    // Gets locations for all uniforms set to kUseUniform and initializes cache
    // to invalid values.
    void getUniformLocationsAndInitCache(const GrGLInterface* gl,
                                         CachedData* programData) const;

    const char* genRadialVS(int stageNum, ShaderCodeSegments* segments,
                            StageUniLocations* locations,
                            const char** radial2VaryingVSName,
                            const char** radial2VaryingFSName,
                            const char* varyingVSName,
                            int varyingDims, int coordDims) const;

    void gen2x2FS(int stageNum,
                  ShaderCodeSegments* segments,
                  StageUniLocations* locations,
                  GrStringBuilder* sampleCoords,
                  const char* samplerName,
                  const char* texelSizeName,
                  const char* smear,
                  const char* fsOutColor,
                  GrStringBuilder& texFunc,
                  GrStringBuilder& modulate,
                  bool complexCoord,
                  int coordDims) const;

    void genConvolutionVS(int stageNum,
                          const ProgramDesc::StageDesc& desc,
                          ShaderCodeSegments* segments,
                          StageUniLocations* locations,
                          const char** kernelName,
                          const char** imageIncrementName,
                          const char* varyingVSName) const;


    void genConvolutionFS(int stageNum,
                          const ProgramDesc::StageDesc& desc,
                          ShaderCodeSegments* segments,
                          const char* samplerName,
                          const char* kernelName,
                          const char* smear,
                          const char* imageIncrementName,
                          const char* fsOutColor,
                          GrStringBuilder& sampleCoords,
                          GrStringBuilder& texFunc,
                          GrStringBuilder& modulate) const;

    friend class GrGpuGLShaders;
};

#endif
