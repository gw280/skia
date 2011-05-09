/*
    Copyright 2011 Google Inc.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

         http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
 */

#include "GrBinHashKey.h"
#include "GrGLEffect.h"
#include "GrGLProgram.h"
#include "GrGpuGLShaders.h"
#include "GrGpuVertex.h"
#include "GrMemory.h"
#include "GrNoncopyable.h"
#include "GrStringBuilder.h"
#include "GrRandom.h"

#define SKIP_CACHE_CHECK    true
#define GR_UINT32_MAX   static_cast<uint32_t>(-1)

#include "GrTHashCache.h"

class GrGpuGLShaders::ProgramCache : public ::GrNoncopyable {
private:
    class Entry;

#if GR_DEBUG
    typedef GrBinHashKey<Entry, 4> ProgramHashKey; // Flex the dynamic allocation muscle in debug
#else
    typedef GrBinHashKey<Entry, 32> ProgramHashKey;
#endif

    class Entry : public ::GrNoncopyable {
    public:
        Entry() {}
    private:
        void copyAndTakeOwnership(Entry& entry) {
            fProgramData.copyAndTakeOwnership(entry.fProgramData);
            fKey.copyAndTakeOwnership(entry.fKey); // ownership transfer
            fLRUStamp = entry.fLRUStamp;
        }

    public:
        int compare(const ProgramHashKey& key) const { return fKey.compare(key); }

    public:
        GrGLProgram::CachedData fProgramData;
        ProgramHashKey          fKey;
        unsigned int            fLRUStamp;
    };

    GrTHashTable<Entry, ProgramHashKey, 8> fHashCache;

    enum {
        kMaxEntries = 32
    };
    Entry        fEntries[kMaxEntries];
    int          fCount;
    unsigned int fCurrLRUStamp;

public:
    ProgramCache() 
        : fCount(0)
        , fCurrLRUStamp(0) {
    }

    ~ProgramCache() {
        for (int i = 0; i < fCount; ++i) {
            GrGpuGLShaders::DeleteProgram(&fEntries[i].fProgramData);
        }
    }

    void abandon() {
        fCount = 0;
    }

    void invalidateViewMatrices() {
        for (int i = 0; i < fCount; ++i) {
            // set to illegal matrix
            fEntries[i].fProgramData.fViewMatrix = GrMatrix::InvalidMatrix();
        }
    }

    GrGLProgram::CachedData* getProgramData(const GrGLProgram& desc) {
        ProgramHashKey key;
        while (key.doPass()) {
            desc.buildKey(key);
        }
        Entry* entry = fHashCache.find(key);
        if (NULL == entry) {
            if (fCount < kMaxEntries) {
                entry = fEntries + fCount;
                ++fCount;
            } else {
                GrAssert(kMaxEntries == fCount);
                entry = fEntries;
                for (int i = 1; i < kMaxEntries; ++i) {
                    if (fEntries[i].fLRUStamp < entry->fLRUStamp) {
                        entry = fEntries + i;
                    }
                }
                fHashCache.remove(entry->fKey, entry);
                GrGpuGLShaders::DeleteProgram(&entry->fProgramData);
            }
            entry->fKey.copyAndTakeOwnership(key);
            if (!desc.genProgram(&entry->fProgramData)) {
                return NULL;
            }
            fHashCache.insert(entry->fKey, entry);
        }

        entry->fLRUStamp = fCurrLRUStamp;
        if (GR_UINT32_MAX == fCurrLRUStamp) {
            // wrap around! just trash our LRU, one time hit.
            for (int i = 0; i < fCount; ++i) {
                fEntries[i].fLRUStamp = 0;
            }
        }
        ++fCurrLRUStamp;
        return &entry->fProgramData;
    }
};

void GrGpuGLShaders::DeleteProgram(GrGLProgram::CachedData* programData) {
    GR_GL(DeleteShader(programData->fVShaderID));
    GR_GL(DeleteShader(programData->fFShaderID));
    GR_GL(DeleteProgram(programData->fProgramID));
    GR_DEBUGCODE(memset(programData, 0, sizeof(*programData));)
}

void GrGpuGLShaders::ProgramUnitTest() {

    static const int STAGE_OPTS[] = {
        0,
        GrGLProgram::ProgramDesc::StageDesc::kNoPerspective_OptFlagBit,
        GrGLProgram::ProgramDesc::StageDesc::kIdentity_CoordMapping
    };
    static const GrGLProgram::ProgramDesc::StageDesc::Modulation STAGE_MODULATES[] = {
        GrGLProgram::ProgramDesc::StageDesc::kColor_Modulation,
        GrGLProgram::ProgramDesc::StageDesc::kAlpha_Modulation
    };
    static const GrGLProgram::ProgramDesc::StageDesc::CoordMapping STAGE_COORD_MAPPINGS[] = {
        GrGLProgram::ProgramDesc::StageDesc::kIdentity_CoordMapping,
        GrGLProgram::ProgramDesc::StageDesc::kRadialGradient_CoordMapping,
        GrGLProgram::ProgramDesc::StageDesc::kSweepGradient_CoordMapping,
        GrGLProgram::ProgramDesc::StageDesc::kRadial2Gradient_CoordMapping
    };
    static const GrGLProgram::ProgramDesc::StageDesc::FetchMode FETCH_MODES[] = {
        GrGLProgram::ProgramDesc::StageDesc::kSingle_FetchMode,
        GrGLProgram::ProgramDesc::StageDesc::k2x2_FetchMode,
    };
    GrGLProgram program;
    GrGLProgram::ProgramDesc& pdesc = program.fProgramDesc;

    static const int NUM_TESTS = 512;

    // GrRandoms nextU() values have patterns in the low bits
    // So using nextU() % array_count might never take some values.
    GrRandom random;
    for (int t = 0; t < NUM_TESTS; ++t) {

        pdesc.fVertexLayout = 0;
        pdesc.fEmitsPointSize = random.nextF() > .5f;
        float colorType = random.nextF();
        if (colorType < 1.f / 3.f) {
            pdesc.fColorType = GrGLProgram::ProgramDesc::kAttribute_ColorType;
        } else if (colorType < 2.f / 3.f) {
            pdesc.fColorType = GrGLProgram::ProgramDesc::kUniform_ColorType;
        } else {
            pdesc.fColorType = GrGLProgram::ProgramDesc::kNone_ColorType;
        }
        for (int s = 0; s < kNumStages; ++s) {
            // enable the stage?
            if (random.nextF() > .5f) {
                // use separate tex coords?
                if (random.nextF() > .5f) {
                    int t = (int)(random.nextF() * kMaxTexCoords);
                    pdesc.fVertexLayout |= StageTexCoordVertexLayoutBit(s, t);
                } else {
                    pdesc.fVertexLayout |= StagePosAsTexCoordVertexLayoutBit(s);
                }
            }
            // use text-formatted verts?
            if (random.nextF() > .5f) {
                pdesc.fVertexLayout |= kTextFormat_VertexLayoutBit;
            }
        }

        for (int s = 0; s < kNumStages; ++s) {
            int x;
            pdesc.fStages[s].fEnabled = VertexUsesStage(s, pdesc.fVertexLayout);
            x = (int)(random.nextF() * GR_ARRAY_COUNT(STAGE_OPTS));
            pdesc.fStages[s].fOptFlags = STAGE_OPTS[x];
            x = (int)(random.nextF() * GR_ARRAY_COUNT(STAGE_MODULATES));
            pdesc.fStages[s].fModulation = STAGE_MODULATES[x];
            x = (int)(random.nextF() * GR_ARRAY_COUNT(STAGE_COORD_MAPPINGS));
            pdesc.fStages[s].fCoordMapping = STAGE_COORD_MAPPINGS[x];
            x = (int)(random.nextF() * GR_ARRAY_COUNT(FETCH_MODES));
            pdesc.fStages[s].fFetchMode = FETCH_MODES[x];
        }
        GrGLProgram::CachedData cachedData;
        program.genProgram(&cachedData);
        DeleteProgram(&cachedData);
        bool again = false;
        if (again) {
            program.genProgram(&cachedData);
            DeleteProgram(&cachedData);
        }
    }
}


GrGpuGLShaders::GrGpuGLShaders() {

    resetContext();
    f4X4DownsampleFilterSupport = true;

    fProgramData = NULL;
    fProgramCache = new ProgramCache();

#if 0
    ProgramUnitTest();
#endif
}

GrGpuGLShaders::~GrGpuGLShaders() {
    delete fProgramCache;
}

const GrMatrix& GrGpuGLShaders::getHWSamplerMatrix(int stage) {
    GrAssert(fProgramData);

    if (GrGLProgram::kSetAsAttribute == 
        fProgramData->fUniLocations.fStages[stage].fTextureMatrixUni) {
        return fHWDrawState.fSamplerStates[stage].getMatrix();
    } else {
        return fProgramData->fTextureMatrices[stage];
    }
}

void GrGpuGLShaders::recordHWSamplerMatrix(int stage, const GrMatrix& matrix) {
    GrAssert(fProgramData);
    if (GrGLProgram::kSetAsAttribute == 
        fProgramData->fUniLocations.fStages[stage].fTextureMatrixUni) {
        fHWDrawState.fSamplerStates[stage].setMatrix(matrix);
    } else {
        fProgramData->fTextureMatrices[stage] = matrix;
    }
}

void GrGpuGLShaders::resetContext() {
    INHERITED::resetContext();

    fHWGeometryState.fVertexLayout = 0;
    fHWGeometryState.fVertexOffset = ~0;
    GR_GL(DisableVertexAttribArray(GrGLProgram::ColorAttributeIdx()));
    for (int t = 0; t < kMaxTexCoords; ++t) {
        GR_GL(DisableVertexAttribArray(GrGLProgram::TexCoordAttributeIdx(t)));
    }
    GR_GL(EnableVertexAttribArray(GrGLProgram::PositionAttributeIdx()));

    fHWProgramID = 0;
}

void GrGpuGLShaders::flushViewMatrix() {
    GrAssert(NULL != fCurrDrawState.fRenderTarget);
    GrMatrix m (
        GrIntToScalar(2) / fCurrDrawState.fRenderTarget->width(), 0, -GR_Scalar1,
        0,-GrIntToScalar(2) / fCurrDrawState.fRenderTarget->height(), GR_Scalar1,
        0, 0, GrMatrix::I()[8]);
    m.setConcat(m, fCurrDrawState.fViewMatrix);

    // ES doesn't allow you to pass true to the transpose param,
    // so do our own transpose
    GrScalar mt[]  = {
        m[GrMatrix::kScaleX],
        m[GrMatrix::kSkewY],
        m[GrMatrix::kPersp0],
        m[GrMatrix::kSkewX],
        m[GrMatrix::kScaleY],
        m[GrMatrix::kPersp1],
        m[GrMatrix::kTransX],
        m[GrMatrix::kTransY],
        m[GrMatrix::kPersp2]
    };

    if (GrGLProgram::kSetAsAttribute ==  
        fProgramData->fUniLocations.fViewMatrixUni) {
        int baseIdx = GrGLProgram::ViewMatrixAttributeIdx();
        GR_GL(VertexAttrib4fv(baseIdx + 0, mt+0));
        GR_GL(VertexAttrib4fv(baseIdx + 1, mt+3));
        GR_GL(VertexAttrib4fv(baseIdx + 2, mt+6));
    } else {
        GrAssert(GrGLProgram::kUnusedUniform != 
                 fProgramData->fUniLocations.fViewMatrixUni);
        GR_GL(UniformMatrix3fv(fProgramData->fUniLocations.fViewMatrixUni,
                               1, false, mt));
    }
}

void GrGpuGLShaders::flushTextureMatrix(int s) {
    const int& uni = fProgramData->fUniLocations.fStages[s].fTextureMatrixUni;
    GrGLTexture* texture = (GrGLTexture*) fCurrDrawState.fTextures[s];
    if (NULL != texture) {
        if (GrGLProgram::kUnusedUniform != uni &&
            (((1 << s) & fDirtyFlags.fTextureChangedMask) ||
            getHWSamplerMatrix(s) != getSamplerMatrix(s))) {

            GrAssert(NULL != fCurrDrawState.fTextures[s]);

            GrGLTexture* texture = (GrGLTexture*) fCurrDrawState.fTextures[s];

            GrMatrix m = getSamplerMatrix(s);
            GrSamplerState::SampleMode mode = 
                fCurrDrawState.fSamplerStates[s].getSampleMode();
            AdjustTextureMatrix(texture, mode, &m);

            // ES doesn't allow you to pass true to the transpose param,
            // so do our own transpose
            GrScalar mt[]  = {
                m[GrMatrix::kScaleX],
                m[GrMatrix::kSkewY],
                m[GrMatrix::kPersp0],
                m[GrMatrix::kSkewX],
                m[GrMatrix::kScaleY],
                m[GrMatrix::kPersp1],
                m[GrMatrix::kTransX],
                m[GrMatrix::kTransY],
                m[GrMatrix::kPersp2]
            };
            if (GrGLProgram::kSetAsAttribute ==
                fProgramData->fUniLocations.fStages[s].fTextureMatrixUni) {
                int baseIdx = GrGLProgram::TextureMatrixAttributeIdx(s);
                GR_GL(VertexAttrib4fv(baseIdx + 0, mt+0));
                GR_GL(VertexAttrib4fv(baseIdx + 1, mt+3));
                GR_GL(VertexAttrib4fv(baseIdx + 2, mt+6));
            } else {
                GR_GL(UniformMatrix3fv(uni, 1, false, mt));
            }
            recordHWSamplerMatrix(s, getSamplerMatrix(s));
        }
    }
}

void GrGpuGLShaders::flushRadial2(int s) {

    const int &uni = fProgramData->fUniLocations.fStages[s].fRadial2Uni;
    const GrSamplerState& sampler = fCurrDrawState.fSamplerStates[s];
    if (GrGLProgram::kUnusedUniform != uni &&
        (fProgramData->fRadial2CenterX1[s] != sampler.getRadial2CenterX1() ||
         fProgramData->fRadial2Radius0[s]  != sampler.getRadial2Radius0()  ||
         fProgramData->fRadial2PosRoot[s]  != sampler.isRadial2PosRoot())) {

        GrScalar centerX1 = sampler.getRadial2CenterX1();
        GrScalar radius0 = sampler.getRadial2Radius0();

        GrScalar a = GrMul(centerX1, centerX1) - GR_Scalar1;

        float values[6] = {
            GrScalarToFloat(a),
            1 / (2.f * values[0]),
            GrScalarToFloat(centerX1),
            GrScalarToFloat(radius0),
            GrScalarToFloat(GrMul(radius0, radius0)),
            sampler.isRadial2PosRoot() ? 1.f : -1.f
        };
        GR_GL(Uniform1fv(uni, 6, values));
        fProgramData->fRadial2CenterX1[s] = sampler.getRadial2CenterX1();
        fProgramData->fRadial2Radius0[s]  = sampler.getRadial2Radius0();
        fProgramData->fRadial2PosRoot[s]  = sampler.isRadial2PosRoot();
    }
}

void GrGpuGLShaders::flushTexelSize(int s) {
    const int& uni = fProgramData->fUniLocations.fStages[s].fNormalizedTexelSizeUni;
    if (GrGLProgram::kUnusedUniform != uni) {
        GrGLTexture* texture = (GrGLTexture*) fCurrDrawState.fTextures[s];
        if (texture->allocWidth() != fProgramData->fTextureWidth[s] ||
            texture->allocHeight() != fProgramData->fTextureWidth[s]) {

            float texelSize[] = {1.f / texture->allocWidth(),
                                 1.f / texture->allocHeight()};
            GR_GL(Uniform2fv(uni, 1, texelSize));
        }
    }
}

void GrGpuGLShaders::flushColor() {
    const GrGLProgram::ProgramDesc& desc = fCurrentProgram.getDesc();
    if (fGeometrySrc.fVertexLayout & kColor_VertexLayoutBit) {
        // color will be specified per-vertex as an attribute
        // invalidate the const vertex attrib color
        fHWDrawState.fColor = GrColor_ILLEGAL;
    } else {
        switch (desc.fColorType) {
            case GrGLProgram::ProgramDesc::kAttribute_ColorType:
                if (fHWDrawState.fColor != fCurrDrawState.fColor) {
                    // OpenGL ES only supports the float varities of glVertexAttrib
                    float c[] = {
                        GrColorUnpackR(fCurrDrawState.fColor) / 255.f,
                        GrColorUnpackG(fCurrDrawState.fColor) / 255.f,
                        GrColorUnpackB(fCurrDrawState.fColor) / 255.f,
                        GrColorUnpackA(fCurrDrawState.fColor) / 255.f
                    };
                    GR_GL(VertexAttrib4fv(GrGLProgram::ColorAttributeIdx(), c));
                    fHWDrawState.fColor = fCurrDrawState.fColor;
                }
                break;
            case GrGLProgram::ProgramDesc::kUniform_ColorType:
                if (fProgramData->fColor != fCurrDrawState.fColor) {
                    // OpenGL ES only supports the float varities of glVertexAttrib
                    float c[] = {
                        GrColorUnpackR(fCurrDrawState.fColor) / 255.f,
                        GrColorUnpackG(fCurrDrawState.fColor) / 255.f,
                        GrColorUnpackB(fCurrDrawState.fColor) / 255.f,
                        GrColorUnpackA(fCurrDrawState.fColor) / 255.f
                    };
                    GrAssert(GrGLProgram::kUnusedUniform != 
                             fProgramData->fUniLocations.fColorUni);
                    GR_GL(Uniform4fv(fProgramData->fUniLocations.fColorUni, 1, c));
                    fProgramData->fColor = fCurrDrawState.fColor;
                }
                break;
            case GrGLProgram::ProgramDesc::kNone_ColorType:
                GrAssert(0xffffffff == fCurrDrawState.fColor);
                break;
            default:
                GrCrash("Unknown color type.");
        }
    }
}


bool GrGpuGLShaders::flushGraphicsState(GrPrimitiveType type) {
    if (!flushGLStateCommon(type)) {
        return false;
    }

    if (fDirtyFlags.fRenderTargetChanged) {
        // our coords are in pixel space and the GL matrices map to NDC
        // so if the viewport changed, our matrix is now wrong.
        fHWDrawState.fViewMatrix = GrMatrix::InvalidMatrix();
        // we assume all shader matrices may be wrong after viewport changes
        fProgramCache->invalidateViewMatrices();
    }

    buildProgram(type);
    fProgramData = fProgramCache->getProgramData(fCurrentProgram);
    if (NULL == fProgramData) {
        GrAssert(!"Failed to create program!");
        return false;
    }

    if (fHWProgramID != fProgramData->fProgramID) {
        GR_GL(UseProgram(fProgramData->fProgramID));
        fHWProgramID = fProgramData->fProgramID;
    }

    if (!fCurrentProgram.doGLSetup(type, fProgramData)) {
        return false;
    }

    this->flushColor();

    GrMatrix* currViewMatrix;
    if (GrGLProgram::kSetAsAttribute == 
        fProgramData->fUniLocations.fViewMatrixUni) {
        currViewMatrix = &fHWDrawState.fViewMatrix;
    } else {
        currViewMatrix = &fProgramData->fViewMatrix;
    }

    if (*currViewMatrix != fCurrDrawState.fViewMatrix) {
        flushViewMatrix();
        *currViewMatrix = fCurrDrawState.fViewMatrix;
    }

    for (int s = 0; s < kNumStages; ++s) {
        this->flushTextureMatrix(s);

        this->flushRadial2(s);

        this->flushTexelSize(s);
    }
    resetDirtyFlags();
    return true;
}

void GrGpuGLShaders::postDraw() {
    fCurrentProgram.doGLPost();
}

void GrGpuGLShaders::setupGeometry(int* startVertex,
                                    int* startIndex,
                                    int vertexCount,
                                    int indexCount) {

    int newColorOffset;
    int newTexCoordOffsets[kMaxTexCoords];

    GrGLsizei newStride = VertexSizeAndOffsetsByIdx(fGeometrySrc.fVertexLayout,
                                                  newTexCoordOffsets,
                                                  &newColorOffset);
    int oldColorOffset;
    int oldTexCoordOffsets[kMaxTexCoords];
    GrGLsizei oldStride = VertexSizeAndOffsetsByIdx(fHWGeometryState.fVertexLayout,
                                                  oldTexCoordOffsets,
                                                  &oldColorOffset);
    bool indexed = NULL != startIndex;

    int extraVertexOffset;
    int extraIndexOffset;
    setBuffers(indexed, &extraVertexOffset, &extraIndexOffset);

    GrGLenum scalarType;
    bool texCoordNorm;
    if (fGeometrySrc.fVertexLayout & kTextFormat_VertexLayoutBit) {
        scalarType = GrGLTextType;
        texCoordNorm = GR_GL_TEXT_TEXTURE_NORMALIZED;
    } else {
        scalarType = GrGLType;
        texCoordNorm = false;
    }

    size_t vertexOffset = (*startVertex + extraVertexOffset) * newStride;
    *startVertex = 0;
    if (indexed) {
        *startIndex += extraIndexOffset;
    }

    // all the Pointers must be set if any of these are true
    bool allOffsetsChange =  fHWGeometryState.fArrayPtrsDirty ||
                             vertexOffset != fHWGeometryState.fVertexOffset ||
                             newStride != oldStride;

    // position and tex coord offsets change if above conditions are true
    // or the type/normalization changed based on text vs nontext type coords.
    bool posAndTexChange = allOffsetsChange ||
                           (((GrGLTextType != GrGLType) || GR_GL_TEXT_TEXTURE_NORMALIZED) &&
                                (kTextFormat_VertexLayoutBit &
                                  (fHWGeometryState.fVertexLayout ^
                                   fGeometrySrc.fVertexLayout)));

    if (posAndTexChange) {
        int idx = GrGLProgram::PositionAttributeIdx();
        GR_GL(VertexAttribPointer(idx, 2, scalarType, false, newStride, 
                                  (GrGLvoid*)vertexOffset));
        fHWGeometryState.fVertexOffset = vertexOffset;
    }

    for (int t = 0; t < kMaxTexCoords; ++t) {
        if (newTexCoordOffsets[t] > 0) {
            GrGLvoid* texCoordOffset = (GrGLvoid*)(vertexOffset + newTexCoordOffsets[t]);
            int idx = GrGLProgram::TexCoordAttributeIdx(t);
            if (oldTexCoordOffsets[t] <= 0) {
                GR_GL(EnableVertexAttribArray(idx));
                GR_GL(VertexAttribPointer(idx, 2, scalarType, texCoordNorm, 
                                          newStride, texCoordOffset));
            } else if (posAndTexChange ||
                       newTexCoordOffsets[t] != oldTexCoordOffsets[t]) {
                GR_GL(VertexAttribPointer(idx, 2, scalarType, texCoordNorm, 
                                          newStride, texCoordOffset));
            }
        } else if (oldTexCoordOffsets[t] > 0) {
            GR_GL(DisableVertexAttribArray(GrGLProgram::TexCoordAttributeIdx(t)));
        }
    }

    if (newColorOffset > 0) {
        GrGLvoid* colorOffset = (int8_t*)(vertexOffset + newColorOffset);
        int idx = GrGLProgram::ColorAttributeIdx();
        if (oldColorOffset <= 0) {
            GR_GL(EnableVertexAttribArray(idx));
            GR_GL(VertexAttribPointer(idx, 4, GR_GL_UNSIGNED_BYTE,
                                      true, newStride, colorOffset));
        } else if (allOffsetsChange || newColorOffset != oldColorOffset) {
            GR_GL(VertexAttribPointer(idx, 4, GR_GL_UNSIGNED_BYTE,
                                      true, newStride, colorOffset));
        }
    } else if (oldColorOffset > 0) {
        GR_GL(DisableVertexAttribArray(GrGLProgram::ColorAttributeIdx()));
    }

    fHWGeometryState.fVertexLayout = fGeometrySrc.fVertexLayout;
    fHWGeometryState.fArrayPtrsDirty = false;
}

void GrGpuGLShaders::buildProgram(GrPrimitiveType type) {
    GrGLProgram::ProgramDesc& desc = fCurrentProgram.fProgramDesc;

    // Must initialize all fields or cache will have false negatives!
    desc.fVertexLayout = fGeometrySrc.fVertexLayout;

    desc.fEmitsPointSize = kPoints_PrimitiveType == type;

    bool requiresAttributeColors = desc.fVertexLayout & kColor_VertexLayoutBit;
    // fColorType records how colors are specified for the program. Strip
    // the bit from the layout to avoid false negatives when searching for an
    // existing program in the cache.
    desc.fVertexLayout &= ~(kColor_VertexLayoutBit);

#if GR_AGGRESSIVE_SHADER_OPTS
    if (!requiresAttributeColors && (0xffffffff == fCurrDrawState.fColor)) {
        desc.fColorType = GrGLProgram::ProgramDesc::kNone_ColorType;
    } else
#endif
#if GR_GL_NO_CONSTANT_ATTRIBUTES
    if (!requiresAttributeColors) {
        desc.fColorType = GrGLProgram::ProgramDesc::kUniform_ColorType;
    } else
#endif
    {
        if (requiresAttributeColors) {} // suppress unused var warning
        desc.fColorType = GrGLProgram::ProgramDesc::kAttribute_ColorType;
    }

    for (int s = 0; s < kNumStages; ++s) {
        GrGLProgram::ProgramDesc::StageDesc& stage = desc.fStages[s];

        stage.fEnabled = this->isStageEnabled(s);

        if (stage.fEnabled) {
            GrGLTexture* texture = (GrGLTexture*) fCurrDrawState.fTextures[s];
            GrAssert(NULL != texture);
            // we matrix to invert when orientation is TopDown, so make sure
            // we aren't in that case before flagging as identity.
            if (TextureMatrixIsIdentity(texture, fCurrDrawState.fSamplerStates[s])) {
                stage.fOptFlags = GrGLProgram::ProgramDesc::StageDesc::kIdentityMatrix_OptFlagBit;
            } else if (!getSamplerMatrix(s).hasPerspective()) {
                stage.fOptFlags = GrGLProgram::ProgramDesc::StageDesc::kNoPerspective_OptFlagBit;
            } else {
                stage.fOptFlags = 0;
            }
            switch (fCurrDrawState.fSamplerStates[s].getSampleMode()) {
                case GrSamplerState::kNormal_SampleMode:
                    stage.fCoordMapping = GrGLProgram::ProgramDesc::StageDesc::kIdentity_CoordMapping;
                    break;
                case GrSamplerState::kRadial_SampleMode:
                    stage.fCoordMapping = GrGLProgram::ProgramDesc::StageDesc::kRadialGradient_CoordMapping;
                    break;
                case GrSamplerState::kRadial2_SampleMode:
                    stage.fCoordMapping = GrGLProgram::ProgramDesc::StageDesc::kRadial2Gradient_CoordMapping;
                    break;
                case GrSamplerState::kSweep_SampleMode:
                    stage.fCoordMapping = GrGLProgram::ProgramDesc::StageDesc::kSweepGradient_CoordMapping;
                    break;
                default:
                    GrCrash("Unexpected sample mode!");
                    break;
            }

            switch (fCurrDrawState.fSamplerStates[s].getFilter()) {
                // these both can use a regular texture2D()
                case GrSamplerState::kNearest_Filter:
                case GrSamplerState::kBilinear_Filter:
                    stage.fFetchMode = GrGLProgram::ProgramDesc::StageDesc::kSingle_FetchMode;
                    break;
                // performs 4 texture2D()s
                case GrSamplerState::k4x4Downsample_Filter:
                    stage.fFetchMode = GrGLProgram::ProgramDesc::StageDesc::k2x2_FetchMode;
                    break;
                default:
                    GrCrash("Unexpected filter!");
                    break;
            }

            if (GrPixelConfigIsAlphaOnly(texture->config())) {
                stage.fModulation = GrGLProgram::ProgramDesc::StageDesc::kAlpha_Modulation;
            } else {
                stage.fModulation = GrGLProgram::ProgramDesc::StageDesc::kColor_Modulation;
            }

            if (fCurrDrawState.fEffects[s]) {
                fCurrentProgram.fStageEffects[s] = GrGLEffect::Create(fCurrDrawState.fEffects[s]);
            } else {
                delete fCurrentProgram.fStageEffects[s];
                fCurrentProgram.fStageEffects[s] = NULL;
            }
        } else {
            stage.fOptFlags     = 0;
            stage.fCoordMapping = (GrGLProgram::ProgramDesc::StageDesc::CoordMapping)0;
            stage.fModulation   = (GrGLProgram::ProgramDesc::StageDesc::Modulation)0;
            fCurrentProgram.fStageEffects[s] = NULL;
        }
    }
}



