/*
    Copyright 2010 Google Inc.

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

#include "GrGpuGL.h"
#include "GrMemory.h"
#include <stdio.h>
#if GR_WIN32_BUILD
    // need to get wglGetProcAddress
    #undef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN 1
    #include <windows.h>
    #undef WIN32_LEAN_AND_MEAN
#endif


static const GLuint GR_MAX_GLUINT = ~0;
static const GLint  GR_INVAL_GLINT = ~0;

#define SKIP_CACHE_CHECK    true

static const GLenum gXfermodeCoeff2Blend[] = {
    GL_ZERO,
    GL_ONE,
    GL_SRC_COLOR,
    GL_ONE_MINUS_SRC_COLOR,
    GL_DST_COLOR,
    GL_ONE_MINUS_DST_COLOR,
    GL_SRC_ALPHA,
    GL_ONE_MINUS_SRC_ALPHA,
    GL_DST_ALPHA,
    GL_ONE_MINUS_DST_ALPHA,
};

bool has_gl_extension(const char* ext) {
    const char* glstr = (const char*) glGetString(GL_EXTENSIONS);

    int extLength = strlen(ext);

    while (true) {
        int n = strcspn(glstr, " ");
        if (n == extLength && 0 == strncmp(ext, glstr, n)) {
            return true;
        }
        if (0 == glstr[n]) {
            return false;
        }
        glstr += n+1;
    }
}

void gl_version(int* major, int* minor) {
    const char* v = (const char*) glGetString(GL_VERSION);
    if (NULL == v) {
        GrAssert(0);
        *major = 0;
        *minor = 0;
        return;
    }
#if GR_GL_DESKTOP
    int n = sscanf(v, "%d.%d", major, minor);
    if (n != 2) {
        GrAssert(0);
        *major = 0;
        *minor = 0;
        return;
    }
#else
    char profile[2];
    int n = sscanf(v, "OpenGL ES-%c%c %d.%d", profile, profile+1, major, minor);
    bool ok = 4 == n;
    if (!ok) {
        int n = sscanf(v, "OpenGL ES %d.%d", major, minor);
        ok = 2 == n;
    }
    if (!ok) {
        GrAssert(0);
        *major = 0;
        *minor = 0;
        return;
    }
#endif
}
///////////////////////////////////////////////////////////////////////////////

bool fbo_test(GrGLExts exts, int w, int h) {
    GLuint testFBO;
    GR_GLEXT(exts, GenFramebuffers(1, &testFBO));
    GR_GLEXT(exts, BindFramebuffer(GR_FRAMEBUFFER, testFBO));
    GLuint testRTTex;
    GR_GL(GenTextures(1, &testRTTex));
    GR_GL(BindTexture(GL_TEXTURE_2D, testRTTex));
    // some implementations require texture to be mip-map complete before
    // FBO with level 0 bound as color attachment will be framebuffer complete.
    GR_GL(TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
    GR_GL(TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, NULL));
    GR_GL(BindTexture(GL_TEXTURE_2D, 0));
    GR_GLEXT(exts, FramebufferTexture2D(GR_FRAMEBUFFER, GR_COLOR_ATTACHMENT0,
                                        GL_TEXTURE_2D, testRTTex, 0));
    GLenum status = GR_GLEXT(exts, CheckFramebufferStatus(GR_FRAMEBUFFER));
    GR_GLEXT(exts, DeleteFramebuffers(1, &testFBO));
    GR_GL(DeleteTextures(1, &testRTTex));
    return status == GR_FRAMEBUFFER_COMPLETE;
}

///////////////////////////////////////////////////////////////////////////////

static bool gPrintStartupSpew;

GrGpuGL::GrGpuGL() {
    if (gPrintStartupSpew) {
        GrPrintf("------------------------- create GrGpuGL %p --------------\n",
                 this);
        GrPrintf("------ VENDOR %s\n", glGetString(GL_VENDOR));
        GrPrintf("------ RENDERER %s\n", glGetString(GL_RENDERER));
        GrPrintf("------ VERSION %s\n", glGetString(GL_VERSION));
        GrPrintf("------ EXTENSIONS\n %s \n", glGetString(GL_EXTENSIONS));
    }

    GrGLClearErr();

    GrGLInitExtensions(&fExts);

    resetContextHelper();

    GrGLRenderTarget::GLRenderTargetIDs defaultRTIDs;
    GR_GL(GetIntegerv(GR_FRAMEBUFFER_BINDING, (GLint*)&defaultRTIDs.fRTFBOID));
    defaultRTIDs.fTexFBOID = defaultRTIDs.fRTFBOID;
    defaultRTIDs.fMSColorRenderbufferID = 0;
    defaultRTIDs.fStencilRenderbufferID = 0;
    GLint vp[4];
    GR_GL(GetIntegerv(GL_VIEWPORT, vp));
    fHWBounds.fViewportRect.setLTRB(vp[0],
                                    vp[1] + vp[3],
                                    vp[0] + vp[2],
                                    vp[1]);
    defaultRTIDs.fOwnIDs = false;

    fDefaultRenderTarget = new GrGLRenderTarget(defaultRTIDs,
                                                fHWBounds.fViewportRect,
                                                NULL,
                                                this);
    fHWDrawState.fRenderTarget = fDefaultRenderTarget;
    fRenderTargetChanged = true;

    fCurrDrawState = fHWDrawState;

    ////////////////////////////////////////////////////////////////////////////
    // Check for supported features.

    int major, minor;
    gl_version(&major, &minor);

    GLint numFormats;
    GR_GL(GetIntegerv(GL_NUM_COMPRESSED_TEXTURE_FORMATS, &numFormats));
    GrAutoSTMalloc<10, GLint> formats(numFormats);
    GR_GL(GetIntegerv(GL_COMPRESSED_TEXTURE_FORMATS, formats));
    for (int i = 0; i < numFormats; ++i) {
        if (formats[i] == GR_PALETTE8_RGBA8) {
            f8bitPaletteSupport = true;
            break;
        }
    }

    if (gPrintStartupSpew) {
        GrPrintf("Palette8 support: %s\n", (f8bitPaletteSupport ? "YES" : "NO"));
    }

    GR_STATIC_ASSERT(0 == kNone_AALevel);
    GR_STATIC_ASSERT(1 == kLow_AALevel);
    GR_STATIC_ASSERT(2 == kMed_AALevel);
    GR_STATIC_ASSERT(3 == kHigh_AALevel);

    memset(fAASamples, 0, sizeof(fAASamples));
    fMSFBOType = kNone_MSFBO;
    if (has_gl_extension("GL_IMG_multisampled_render_to_texture")) {
        fMSFBOType = kIMG_MSFBO;
        if (gPrintStartupSpew) {
            GrPrintf("MSAA Support: IMG ES EXT.\n");
        }
    }
    else if (has_gl_extension("GL_APPLE_framebuffer_multisample")) {
        fMSFBOType = kApple_MSFBO;
        if (gPrintStartupSpew) {
            GrPrintf("MSAA Support: APPLE ES EXT.\n");
        }
    }
#if GR_GL_DESKTOP
    else if ((major >= 3) ||
             has_gl_extension("GL_ARB_framebuffer_object") ||
             (has_gl_extension("GL_EXT_framebuffer_multisample") &&
              has_gl_extension("GL_EXT_framebuffer_blit"))) {
        fMSFBOType = kDesktop_MSFBO;
         if (gPrintStartupSpew) {
             GrPrintf("MSAA Support: DESKTOP\n");
         }
    }
#endif
    else {
        if (gPrintStartupSpew) {
            GrPrintf("MSAA Support: NONE\n");
        }
    }

    if (kNone_MSFBO != fMSFBOType) {
        GLint maxSamples;
        GLenum maxSampleGetter = (kIMG_MSFBO == fMSFBOType) ?
                                                            GR_MAX_SAMPLES_IMG :
                                                            GR_MAX_SAMPLES;
        GR_GL(GetIntegerv(maxSampleGetter, &maxSamples));
        if (maxSamples > 1 ) {
            fAASamples[kNone_AALevel] = 0;
            fAASamples[kLow_AALevel] = GrMax(2,
                                             GrFixedFloorToInt((GR_FixedHalf) *
                                                             maxSamples));
            fAASamples[kMed_AALevel] = GrMax(2,
                                             GrFixedFloorToInt(((GR_Fixed1*3)/4) *
                                                             maxSamples));
            fAASamples[kHigh_AALevel] = maxSamples;
        }
        if (gPrintStartupSpew) {
            GrPrintf("\tMax Samples: %d\n", maxSamples);
        }
    }

#if GR_GL_DESKTOP
    fHasStencilWrap = (major >= 2 || (major == 1 && minor >= 4)) ||
                      has_gl_extension("GL_EXT_stencil_wrap");
#else
    fHasStencilWrap = (major >= 2) || has_gl_extension("GL_OES_stencil_wrap");
#endif
    if (gPrintStartupSpew) {
        GrPrintf("Stencil Wrap: %s\n", (fHasStencilWrap ? "YES" : "NO"));
    }

#if GR_GL_DESKTOP
    // we could also look for GL_ATI_separate_stencil extension or
    // GL_EXT_stencil_two_side but they use different function signatures
    // than GL2.0+ (and than each other).
    fSingleStencilPassForWinding = (major >= 2);
#else
    // ES 2 has two sided stencil but 1.1 doesn't. There doesn't seem to be
    // an ES1 extension.
    fSingleStencilPassForWinding = (major >= 2);
#endif
    if (gPrintStartupSpew) {
        GrPrintf("Single Stencil Pass For Winding: %s\n", (fSingleStencilPassForWinding ? "YES" : "NO"));
    }


#if GR_GL_DESKTOP
    fRGBA8Renderbuffer = true;
#else
    fRGBA8Renderbuffer = has_gl_extension("GL_OES_rgb8_rgba8");
#endif
    if (gPrintStartupSpew) {
        GrPrintf("RGBA Renderbuffer: %s\n", (fRGBA8Renderbuffer ? "YES" : "NO"));
    }


#if GR_GL_DESKTOP
    fBufferLockSupport = true; // we require VBO support and the desktop VBO
                               // extension includes glMapBuffer.
#else
    fBufferLockSupport = has_gl_extension("GL_OES_mapbuffer");
#endif
    if (gPrintStartupSpew) {
        GrPrintf("Map Buffer: %s\n", (fBufferLockSupport ? "YES" : "NO"));
    }

#if GR_GL_DESKTOP
    fNPOTTextureSupport =
        (major >= 2 || has_gl_extension("GL_ARB_texture_non_power_of_two")) ?
            kFull_NPOTTextureType :
            kNone_NPOTTextureType;
#else
    if (has_gl_extension("GL_OES_texture_npot")) {
        fNPOTTextureSupport = kFull_NPOTTextureType;
    } else if (major >= 2 ||
               has_gl_extension("GL_APPLE_texture_2D_limited_npot")) {
        fNPOTTextureSupport = kNoRepeat_NPOTTextureType;
    } else {
        fNPOTTextureSupport = kNone_NPOTTextureType;
    }
#endif
    ////////////////////////////////////////////////////////////////////////////
    // Experiments to determine limitations that can't be queried. TODO: Make
    // these a preprocess that generate some compile time constants.

    // sanity check to make sure we can at least create an FBO from a POT texture
    if (fNPOTTextureSupport < kFull_NPOTTextureType) {
        bool npotFBOSuccess = fbo_test(fExts, 128, 128);
        if (gPrintStartupSpew) {
            if (!npotFBOSuccess) {
                GrPrintf("FBO Sanity Test: FAILED\n");
            } else {
                GrPrintf("FBO Sanity Test: PASSED\n");
            }
        }
    }
    
    /* Experimentation has found that some GLs that support NPOT textures
       do not support FBOs with a NPOT texture. They report "unsupported" FBO
       status. I don't know how to explicitly query for this. Do an
       experiment. Note they may support NPOT with a renderbuffer but not a
       texture. Presumably, the implementation bloats the renderbuffer
       internally to the next POT.
     */
    if (fNPOTTextureSupport == kFull_NPOTTextureType) {
        bool npotFBOSuccess = fbo_test(fExts, 200, 200);
        if (!npotFBOSuccess) {
            fNPOTTextureSupport = kNonRendertarget_NPOTTextureType;
            if (gPrintStartupSpew) {
                GrPrintf("NPOT Renderbuffer Test: FAILED\n");
            }
        } else {
            if (gPrintStartupSpew) {
                GrPrintf("NPOT Renderbuffer Test: PASSED\n");
            }
        }
    }

    if (gPrintStartupSpew) {
        switch (fNPOTTextureSupport) {
        case kNone_NPOTTextureType:
            GrPrintf("NPOT Support: NONE\n");
            break;
        case kNoRepeat_NPOTTextureType:
            GrPrintf("NPOT Support: NO REPEAT\n");
            break;
        case kNonRendertarget_NPOTTextureType:
            GrPrintf("NPOT Support: NO FBOTEX\n");
            break;
        case kFull_NPOTTextureType:
            GrPrintf("NPOT Support: FULL\n");
            break;
        }
    }

    /* The iPhone 4 has a restriction that for an FBO with texture color
       attachment with height <= 8 then the width must be <= height. Here
       we look for such a limitation.
     */
    fMinRenderTargetHeight = GR_INVAL_GLINT;
    GLint maxRenderSize;
    glGetIntegerv(GR_MAX_RENDERBUFFER_SIZE, &maxRenderSize);

    if (gPrintStartupSpew) {
        GrPrintf("Small height FBO texture experiments\n");
    }
    for (GLuint i = 1; i <= 256;
         (kFull_NPOTTextureType != fNPOTTextureSupport) ? i *= 2 : ++i) {
        GLuint w = maxRenderSize;
        GLuint h = i;
        if (fbo_test(fExts, w, h)) {
            if (gPrintStartupSpew) {
                GrPrintf("\t[%d, %d]: PASSED\n", w, h);
            }
            fMinRenderTargetHeight = i;
            break;
        } else {
            if (gPrintStartupSpew) {
                GrPrintf("\t[%d, %d]: FAILED\n", w, h);
            }
        }
    }
    GrAssert(GR_INVAL_GLINT != fMinRenderTargetHeight);

    if (gPrintStartupSpew) {
        GrPrintf("Small width FBO texture experiments\n");
    }
    fMinRenderTargetWidth = GR_MAX_GLUINT;
    for (GLuint i = 1; i <= 256;
         (kFull_NPOTTextureType != fNPOTTextureSupport) ? i *= 2 : ++i) {
        GLuint w = i;
        GLuint h = maxRenderSize;
        if (fbo_test(fExts, w, h)) {
            if (gPrintStartupSpew) {
                GrPrintf("\t[%d, %d]: PASSED\n", w, h);
            }
            fMinRenderTargetWidth = i;
            break;
        } else {
            if (gPrintStartupSpew) {
                GrPrintf("\t[%d, %d]: FAILED\n", w, h);
            }
        }
    }
    GrAssert(GR_INVAL_GLINT != fMinRenderTargetWidth);

#if GR_IOS_BUILD
    /*
        The iPad seems to fail, at least sometimes, if the height is < 16,
        so we pin the values here for now. A better fix might be to
        conditionalize this based on known that its an iPad (or some other
        check).
     */
    fMinRenderTargetWidth = GrMax<GLuint>(fMinRenderTargetWidth, 16);
    fMinRenderTargetHeight = GrMax<GLuint>(fMinRenderTargetHeight, 16);
#endif
    // bind back to original FBO
    GR_GLEXT(fExts, BindFramebuffer(GR_FRAMEBUFFER, defaultRTIDs.fRTFBOID));
#if GR_COLLECT_STATS
    ++fStats.fRenderTargetChngCnt;
#endif
    eraseStencil(0, ~0);
}

GrGpuGL::~GrGpuGL() {
    fDefaultRenderTarget->abandon();
    fDefaultRenderTarget->unref();
}

void GrGpuGL::resetContextHelper() {
// We detect cases when blending is effectively off
    fHWBlendDisabled = false;
    GR_GL(Enable(GL_BLEND));

    // this is always disabled
    GR_GL(Disable(GL_CULL_FACE));

    GR_GL(Disable(GL_DITHER));
#if GR_GL_DESKTOP
    GR_GL(Disable(GL_LINE_SMOOTH));
    GR_GL(Disable(GL_POINT_SMOOTH));
    GR_GL(Disable(GL_MULTISAMPLE));
#endif

    // we only ever use lines in hairline mode
    GR_GL(LineWidth(1));

    GR_GL(ActiveTexture(GL_TEXTURE0));

    fHWDrawState.fFlagBits = 0;

    // illegal values
    fHWDrawState.fSrcBlend = (BlendCoeff)-1;
    fHWDrawState.fDstBlend = (BlendCoeff)-1;
    fHWDrawState.fColor = GrColor_ILLEGAL;
    fHWDrawState.fPointSize = -1;
    fHWDrawState.fTexture = NULL;

    GR_GL(Scissor(0,0,0,0));
    fHWBounds.fScissorRect.setLTRB(0,0,0,0);
    fHWBounds.fScissorEnabled = false;
    GR_GL(Disable(GL_SCISSOR_TEST));

    fHWDrawState.fSamplerState.setRadial2Params(-GR_ScalarMax,
                                                -GR_ScalarMax,
                                                true);

    for (int i = 0; i < kMatrixModeCount; i++) {
        fHWDrawState.fMatrixModeCache[i].setScale(GR_ScalarMax, GR_ScalarMax); // illegal
    }

    // disabling the stencil test also disables
    // stencil buffer writes
    GR_GL(Disable(GL_STENCIL_TEST));
    GR_GL(StencilMask(0xffffffff));
    GR_GL(ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE));
    fHWDrawState.fReverseFill = false;
    fHWDrawState.fStencilPass = kNone_StencilPass;
    fHWStencilClip = false;

    fHWGeometryState.fIndexBuffer = NULL;
    fHWGeometryState.fVertexBuffer = NULL;
    GR_GL(BindBuffer(GL_ARRAY_BUFFER, 0));
    GR_GL(BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));

    fHWDrawState.fRenderTarget = NULL;
}

void GrGpuGL::resetContext() {
    INHERITED::resetContext();
    resetContextHelper();
}


// defines stencil formats from more to less preferred
#if GR_GL_ES
    GLenum GR_GL_STENCIL_FORMAT_ARRAY[] = {
        GR_STENCIL_INDEX8,
    };
#else
    GLenum GR_GL_STENCIL_FORMAT_ARRAY[] = {
        GR_STENCIL_INDEX8,
        GR_STENCIL_INDEX16,
        GR_UNSIGNED_INT_24_8,
        GR_DEPTH_STENCIL,
    };
#endif

// good to set a break-point here to know when createTexture fails
static GrTexture* return_null_texture() {
//    GrAssert(!"null texture");
    return NULL;
}

#if GR_DEBUG
static size_t as_size_t(int x) {
    return x;
}
#endif

GrRenderTarget* GrGpuGL::createPlatformRenderTarget(
                                                intptr_t platformRenderTarget,
                                                int width, int height) {
    GrGLRenderTarget::GLRenderTargetIDs rtIDs;
    rtIDs.fStencilRenderbufferID = 0;
    rtIDs.fMSColorRenderbufferID = 0;
    rtIDs.fTexFBOID              = 0;
    rtIDs.fOwnIDs                = false;

    GrIRect viewport;

    // viewport is in GL coords (top >= bottom)
    viewport.setLTRB(0, height, width, 0);

    rtIDs.fRTFBOID  = (GLuint)platformRenderTarget;
    rtIDs.fTexFBOID = (GLuint)platformRenderTarget;

    GrGLRenderTarget* rt = new GrGLRenderTarget(rtIDs, viewport, NULL, this);

    return rt;
}

GrTexture* GrGpuGL::createTexture(const TextureDesc& desc,
                                  const void* srcData, size_t rowBytes) {

#if GR_COLLECT_STATS
    ++fStats.fTextureCreateCnt;
#endif

    static const GrGLTexture::TexParams DEFAULT_PARAMS = {
        GL_NEAREST,
        GL_CLAMP_TO_EDGE,
        GL_CLAMP_TO_EDGE
    };

    GrGLTexture::GLTextureDesc glDesc;
    GLenum internalFormat;

    glDesc.fContentWidth  = desc.fWidth;
    glDesc.fContentHeight = desc.fHeight;
    glDesc.fAllocWidth    = desc.fWidth;
    glDesc.fAllocHeight   = desc.fHeight;
    glDesc.fFormat        = desc.fFormat;

    bool renderTarget = 0 != (desc.fFlags & kRenderTarget_TextureFlag);
    if (!canBeTexture(desc.fFormat,
                      &internalFormat,
                      &glDesc.fUploadFormat,
                      &glDesc.fUploadType)) {
        return return_null_texture();
    }

    GrAssert(as_size_t(desc.fAALevel) < GR_ARRAY_COUNT(fAASamples));
    GLint samples = fAASamples[desc.fAALevel];
    if (kNone_MSFBO == fMSFBOType && desc.fAALevel != kNone_AALevel) {
        GrPrintf("AA RT requested but not supported on this platform.");
    }

    GR_GL(GenTextures(1, &glDesc.fTextureID));
    if (!glDesc.fTextureID) {
        return return_null_texture();
    }

    glDesc.fUploadByteCount = GrTexture::BytesPerPixel(desc.fFormat);

    /*
     *  check if our srcData has extra bytes past each row. If so, we need
     *  to trim those off here, since GL doesn't let us pass the rowBytes as
     *  a parameter to glTexImage2D
     */
#if GR_GL_DESKTOP
    if (srcData) {
        GR_GL(PixelStorei(GL_UNPACK_ROW_LENGTH,
                          rowBytes / glDesc.fUploadByteCount));
    }
#else
    GrAutoSMalloc<128 * 128> trimStorage;
    size_t trimRowBytes = desc.fWidth * glDesc.fUploadByteCount;
    if (srcData && (trimRowBytes < rowBytes)) {
        size_t trimSize = desc.fHeight * trimRowBytes;
        trimStorage.realloc(trimSize);
        // now copy the data into our new storage, skipping the trailing bytes
        const char* src = (const char*)srcData;
        char* dst = (char*)trimStorage.get();
        for (uint32_t y = 0; y < desc.fHeight; y++) {
            memcpy(dst, src, trimRowBytes);
            src += rowBytes;
            dst += trimRowBytes;
        }
        // now point srcData to our trimmed version
        srcData = trimStorage.get();
    }
#endif

    if (fNPOTTextureSupport < kNonRendertarget_NPOTTextureType ||
        (fNPOTTextureSupport == kNonRendertarget_NPOTTextureType &&
         renderTarget)) {
        glDesc.fAllocWidth  = GrNextPow2(desc.fWidth);
        glDesc.fAllocHeight = GrNextPow2(desc.fHeight);
    }

    if (renderTarget) {
        glDesc.fAllocWidth = GrMax<int>(fMinRenderTargetWidth,
                                        glDesc.fAllocWidth);
        glDesc.fAllocHeight = GrMax<int>(fMinRenderTargetHeight,
                                         glDesc.fAllocHeight);
    }

    GR_GL(BindTexture(GL_TEXTURE_2D, glDesc.fTextureID));
    GR_GL(TexParameteri(GL_TEXTURE_2D,
                        GL_TEXTURE_MAG_FILTER,
                        DEFAULT_PARAMS.fFilter));
    GR_GL(TexParameteri(GL_TEXTURE_2D,
                        GL_TEXTURE_MIN_FILTER,
                        DEFAULT_PARAMS.fFilter));
    GR_GL(TexParameteri(GL_TEXTURE_2D,
                        GL_TEXTURE_WRAP_S,
                        DEFAULT_PARAMS.fWrapS));
    GR_GL(TexParameteri(GL_TEXTURE_2D,
                        GL_TEXTURE_WRAP_T,
                        DEFAULT_PARAMS.fWrapT));
#if GR_COLLECT_STATS
    ++fStats.fTextureChngCnt;
#endif
    fHWDrawState.fTexture = NULL;

    GR_GL(PixelStorei(GL_UNPACK_ALIGNMENT, glDesc.fUploadByteCount));
    if (GrTexture::kIndex_8_PixelConfig == desc.fFormat &&
        supports8BitPalette()) {
        // ES only supports CompressedTexImage2D, not CompressedTexSubimage2D
        GrAssert(desc.fWidth == glDesc.fAllocWidth);
        GrAssert(desc.fHeight == glDesc.fAllocHeight);
        GLsizei imageSize = glDesc.fAllocWidth * glDesc.fAllocHeight +
                            kColorTableSize;
        GR_GL(CompressedTexImage2D(GL_TEXTURE_2D, 0, glDesc.fUploadFormat,
                                   glDesc.fAllocWidth, glDesc.fAllocHeight,
                                   0, imageSize, srcData));
        GrGL_RestoreResetRowLength();
    } else {
        if (NULL != srcData && (glDesc.fAllocWidth != desc.fWidth ||
                                glDesc.fAllocHeight != desc.fHeight)) {
            GR_GL(TexImage2D(GL_TEXTURE_2D, 0, internalFormat,
                             glDesc.fAllocWidth, glDesc.fAllocHeight,
                             0, glDesc.fUploadFormat, glDesc.fUploadType, NULL));
            GR_GL(TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, desc.fWidth,
                                desc.fHeight, glDesc.fUploadFormat,
                                glDesc.fUploadType, srcData));
            GrGL_RestoreResetRowLength();

            uint32_t extraW = glDesc.fAllocWidth  - desc.fWidth;
            uint32_t extraH = glDesc.fAllocHeight - desc.fHeight;
            uint32_t maxTexels = extraW * extraH;
            maxTexels = GrMax(extraW * desc.fHeight, maxTexels);
            maxTexels = GrMax(desc.fWidth * extraH, maxTexels);

            GrAutoSMalloc<128*128> texels(glDesc.fUploadByteCount * maxTexels);

            uint32_t rowSize = desc.fWidth * glDesc.fUploadByteCount;
            if (extraH) {
                uint8_t* lastRowStart = (uint8_t*) srcData +
                                        (desc.fHeight - 1) * rowSize;
                uint8_t* extraRowStart = (uint8_t*)texels.get();

                for (uint32_t i = 0; i < extraH; ++i) {
                    memcpy(extraRowStart, lastRowStart, rowSize);
                    extraRowStart += rowSize;
                }
                GR_GL(TexSubImage2D(GL_TEXTURE_2D, 0, 0, desc.fHeight, desc.fWidth,
                                    extraH, glDesc.fUploadFormat, glDesc.fUploadType,
                                    texels.get()));
            }
            if (extraW) {
                uint8_t* edgeTexel = (uint8_t*)srcData + rowSize - glDesc.fUploadByteCount;
                uint8_t* extraTexel = (uint8_t*)texels.get();
                for (uint32_t j = 0; j < desc.fHeight; ++j) {
                    for (uint32_t i = 0; i < extraW; ++i) {
                        memcpy(extraTexel, edgeTexel, glDesc.fUploadByteCount);
                        extraTexel += glDesc.fUploadByteCount;
                    }
                    edgeTexel += rowSize;
                }
                GR_GL(TexSubImage2D(GL_TEXTURE_2D, 0, desc.fWidth, 0, extraW,
                                    desc.fHeight, glDesc.fUploadFormat,
                                    glDesc.fUploadType, texels.get()));
            }
            if (extraW && extraH) {
                uint8_t* cornerTexel = (uint8_t*)srcData + desc.fHeight * rowSize
                                       - glDesc.fUploadByteCount;
                uint8_t* extraTexel = (uint8_t*)texels.get();
                for (uint32_t i = 0; i < extraW*extraH; ++i) {
                    memcpy(extraTexel, cornerTexel, glDesc.fUploadByteCount);
                    extraTexel += glDesc.fUploadByteCount;
                }
                GR_GL(TexSubImage2D(GL_TEXTURE_2D, 0, desc.fWidth, desc.fHeight,
                                    extraW, extraH, glDesc.fUploadFormat,
                                    glDesc.fUploadType, texels.get()));
            }

        } else {
            GR_GL(TexImage2D(GL_TEXTURE_2D, 0, internalFormat, glDesc.fAllocWidth,
                             glDesc.fAllocHeight, 0, glDesc.fUploadFormat,
                             glDesc.fUploadType, srcData));
            GrGL_RestoreResetRowLength();
        }
    }

    glDesc.fOrientation = GrGLTexture::kTopDown_Orientation;

    GrGLRenderTarget::GLRenderTargetIDs rtIDs;
    rtIDs.fStencilRenderbufferID = 0;
    rtIDs.fMSColorRenderbufferID = 0;
    rtIDs.fRTFBOID = 0;
    rtIDs.fTexFBOID = 0;
    rtIDs.fOwnIDs = true;
    GLenum msColorRenderbufferFormat = -1;

    if (renderTarget) {
#if GR_COLLECT_STATS
        ++fStats.fRenderTargetCreateCnt;
#endif
        bool failed = true;
        GLenum status;
        GLint err;

        // If need have both RT flag and srcData we have
        // to invert the data before uploading because FBO
        // will be rendered bottom up
        GrAssert(NULL == srcData);
        glDesc.fOrientation =  GrGLTexture::kBottomUp_Orientation;

        GR_GLEXT(fExts, GenFramebuffers(1, &rtIDs.fTexFBOID));
        GrAssert(rtIDs.fTexFBOID);

        // If we are using multisampling and any extension other than the IMG
        // one we will create two FBOs. We render to one and then resolve to
        // the texture bound to the other. The IMG extension does an implicit
        // resolve.
        if (samples > 1 && kIMG_MSFBO != fMSFBOType && kNone_MSFBO != fMSFBOType) {
            GR_GLEXT(fExts, GenFramebuffers(1, &rtIDs.fRTFBOID));
            GrAssert(0 != rtIDs.fRTFBOID);
            GR_GLEXT(fExts, GenRenderbuffers(1, &rtIDs.fMSColorRenderbufferID));
            GrAssert(0 != rtIDs.fMSColorRenderbufferID);
            if (!fboInternalFormat(desc.fFormat, &msColorRenderbufferFormat)) {
                GR_GLEXT(fExts,
                         DeleteRenderbuffers(1, &rtIDs.fMSColorRenderbufferID));
                GR_GL(DeleteTextures(1, &glDesc.fTextureID));
                GR_GLEXT(fExts, DeleteFramebuffers(1, &rtIDs.fTexFBOID));
                GR_GLEXT(fExts, DeleteFramebuffers(1, &rtIDs.fRTFBOID));
                fHWDrawState.fTexture = NULL;
                return return_null_texture();
            }
        } else {
            rtIDs.fRTFBOID = rtIDs.fTexFBOID;
        }
        int attempts = 1;
        if (!(kNoPathRendering_TextureFlag & desc.fFlags)) {
            GR_GLEXT(fExts, GenRenderbuffers(1, &rtIDs.fStencilRenderbufferID));
            GrAssert(0 != rtIDs.fStencilRenderbufferID);
            attempts = GR_ARRAY_COUNT(GR_GL_STENCIL_FORMAT_ARRAY);
        }

        // need to unbind the texture before we call FramebufferTexture2D
        GR_GL(BindTexture(GL_TEXTURE_2D, 0));
#if GR_COLLECT_STATS
        ++fStats.fTextureChngCnt;
#endif
        GrAssert(NULL == fHWDrawState.fTexture);

        err = ~GL_NO_ERROR;
        for (int i = 0; i < attempts; ++i) {
            if (rtIDs.fStencilRenderbufferID) {
                GR_GLEXT(fExts, BindRenderbuffer(GR_RENDERBUFFER,
                                                 rtIDs.fStencilRenderbufferID));
                if (samples > 1) {
                    GR_GLEXT_NO_ERR(fExts, RenderbufferStorageMultisample(
                                                GR_RENDERBUFFER,
                                                samples,
                                                GR_GL_STENCIL_FORMAT_ARRAY[i],
                                                glDesc.fAllocWidth,
                                                glDesc.fAllocHeight));
                } else {
                    GR_GLEXT_NO_ERR(fExts, RenderbufferStorage(
                                                GR_RENDERBUFFER,
                                                GR_GL_STENCIL_FORMAT_ARRAY[i],
                                                glDesc.fAllocWidth,
                                                glDesc.fAllocHeight));
                }
                err = glGetError();
                if (err != GL_NO_ERROR) {
                    continue;
                }
            }
            if (rtIDs.fRTFBOID != rtIDs.fTexFBOID) {
                GrAssert(samples > 1);
                GR_GLEXT(fExts, BindRenderbuffer(GR_RENDERBUFFER,
                                                 rtIDs.fMSColorRenderbufferID));
                GR_GLEXT_NO_ERR(fExts, RenderbufferStorageMultisample(
                                                   GR_RENDERBUFFER,
                                                   samples,
                                                   msColorRenderbufferFormat,
                                                   glDesc.fAllocWidth,
                                                   glDesc.fAllocHeight));
                err = glGetError();
                if (err != GL_NO_ERROR) {
                    continue;
                }
            }
            GR_GLEXT(fExts, BindFramebuffer(GR_FRAMEBUFFER, rtIDs.fTexFBOID));

#if GR_COLLECT_STATS
            ++fStats.fRenderTargetChngCnt;
#endif
            if (kIMG_MSFBO == fMSFBOType && samples > 1) {
                GR_GLEXT(fExts, FramebufferTexture2DMultisample(
                                                         GR_FRAMEBUFFER,
                                                         GR_COLOR_ATTACHMENT0,
                                                         GL_TEXTURE_2D,
                                                         glDesc.fTextureID,
                                                         0,
                                                         samples));

            } else {
                GR_GLEXT(fExts, FramebufferTexture2D(GR_FRAMEBUFFER,
                                                     GR_COLOR_ATTACHMENT0,
                                                     GL_TEXTURE_2D,
                                                     glDesc.fTextureID, 0));
            }
            if (rtIDs.fRTFBOID != rtIDs.fTexFBOID) {
                GLenum status = GR_GLEXT(fExts,
                                         CheckFramebufferStatus(GR_FRAMEBUFFER));
                if (status != GR_FRAMEBUFFER_COMPLETE) {
                    GrPrintf("-- glCheckFramebufferStatus %x %d %d\n",
                             status, desc.fWidth, desc.fHeight);
                    continue;
                }
                GR_GLEXT(fExts, BindFramebuffer(GR_FRAMEBUFFER, rtIDs.fRTFBOID));
            #if GR_COLLECT_STATS
                ++fStats.fRenderTargetChngCnt;
            #endif
                GR_GLEXT(fExts, FramebufferRenderbuffer(GR_FRAMEBUFFER,
                                                 GR_COLOR_ATTACHMENT0,
                                                 GR_RENDERBUFFER,
                                                 rtIDs.fMSColorRenderbufferID));

            }
            if (rtIDs.fStencilRenderbufferID) {
                // bind the stencil to rt fbo if present, othewise the tex fbo
                GR_GLEXT(fExts, FramebufferRenderbuffer(GR_FRAMEBUFFER,
                                                 GR_STENCIL_ATTACHMENT,
                                                 GR_RENDERBUFFER,
                                                 rtIDs.fStencilRenderbufferID));
            }
            status = GR_GLEXT(fExts, CheckFramebufferStatus(GR_FRAMEBUFFER));

#if GR_GL_DESKTOP
            // On some implementations you have to be bound as DEPTH_STENCIL.
            // (Even binding to DEPTH and STENCIL separately with the same
            // buffer doesn't work.)
            if (rtIDs.fStencilRenderbufferID &&
                status != GR_FRAMEBUFFER_COMPLETE) {
                GR_GLEXT(fExts, FramebufferRenderbuffer(GR_FRAMEBUFFER,
                                                        GR_STENCIL_ATTACHMENT,
                                                        GR_RENDERBUFFER,
                                                        0));
                GR_GLEXT(fExts,
                         FramebufferRenderbuffer(GR_FRAMEBUFFER,
                                                 GR_DEPTH_STENCIL_ATTACHMENT,
                                                 GR_RENDERBUFFER,
                                                 rtIDs.fStencilRenderbufferID));
                status = GR_GLEXT(fExts, CheckFramebufferStatus(GR_FRAMEBUFFER));
            }
#endif
            if (status != GR_FRAMEBUFFER_COMPLETE) {
                GrPrintf("-- glCheckFramebufferStatus %x %d %d\n",
                         status, desc.fWidth, desc.fHeight);
#if GR_GL_DESKTOP
                if (rtIDs.fStencilRenderbufferID) {
                    GR_GLEXT(fExts, FramebufferRenderbuffer(GR_FRAMEBUFFER,
                                                     GR_DEPTH_STENCIL_ATTACHMENT,
                                                     GR_RENDERBUFFER,
                                                     0));
                }
#endif
                continue;
            }
            // we're successful!
            failed = false;
            break;
        }
        if (failed) {
            if (rtIDs.fStencilRenderbufferID) {
                GR_GLEXT(fExts,
                         DeleteRenderbuffers(1, &rtIDs.fStencilRenderbufferID));
            }
            if (rtIDs.fMSColorRenderbufferID) {
                GR_GLEXT(fExts,
                         DeleteRenderbuffers(1, &rtIDs.fMSColorRenderbufferID));
            }
            if (rtIDs.fRTFBOID != rtIDs.fTexFBOID) {
                GR_GLEXT(fExts, DeleteFramebuffers(1, &rtIDs.fRTFBOID));
            }
            if (rtIDs.fTexFBOID) {
                GR_GLEXT(fExts, DeleteFramebuffers(1, &rtIDs.fTexFBOID));
            }
            GR_GL(DeleteTextures(1, &glDesc.fTextureID));
            return return_null_texture();
        }
    }
#ifdef TRACE_TEXTURE_CREATION
    GrPrintf("--- new texture [%d] size=(%d %d) bpp=%d\n",
             tex->fTextureID, width, height, tex->fUploadByteCount);
#endif
    GrGLTexture* tex = new GrGLTexture(glDesc, rtIDs, DEFAULT_PARAMS, this);

    if (0 != rtIDs.fTexFBOID) {
        GrRenderTarget* rt = tex->asRenderTarget();
        // We've messed with FBO state but may not have set the correct viewport
        // so just dirty the rendertarget state to force a resend.
        fHWDrawState.fRenderTarget = NULL;

        // clear the new stencil buffer if we have one
        if (!(desc.fFlags & kNoPathRendering_TextureFlag)) {
            GrRenderTarget* rtSave = fCurrDrawState.fRenderTarget;
            fCurrDrawState.fRenderTarget = rt;
            eraseStencil(0, ~0);
            fCurrDrawState.fRenderTarget = rtSave;
        }
    }
    return tex;
}

GrRenderTarget* GrGpuGL::defaultRenderTarget() {
    return fDefaultRenderTarget;
}

GrVertexBuffer* GrGpuGL::createVertexBuffer(uint32_t size, bool dynamic) {
    GLuint id;
    GR_GL(GenBuffers(1, &id));
    if (id) {
        GR_GL(BindBuffer(GL_ARRAY_BUFFER, id));
        GrGLClearErr();
        // make sure driver can allocate memory for this buffer
        GR_GL_NO_ERR(BufferData(GL_ARRAY_BUFFER, size, NULL,
                                dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW));
        if (glGetError() != GL_NO_ERROR) {
            GR_GL(DeleteBuffers(1, &id));
            // deleting bound buffer does implicit bind to 0
            fHWGeometryState.fVertexBuffer = NULL;
            return NULL;
        }
        GrGLVertexBuffer* vertexBuffer = new GrGLVertexBuffer(id, this,
                                                              size, dynamic);
        fHWGeometryState.fVertexBuffer = vertexBuffer;
        return vertexBuffer;
    }
    return NULL;
}

GrIndexBuffer* GrGpuGL::createIndexBuffer(uint32_t size, bool dynamic) {
    GLuint id;
    GR_GL(GenBuffers(1, &id));
    if (id) {
        GR_GL(BindBuffer(GL_ELEMENT_ARRAY_BUFFER, id));
        GrGLClearErr();
        // make sure driver can allocate memory for this buffer
        GR_GL_NO_ERR(BufferData(GL_ELEMENT_ARRAY_BUFFER, size, NULL,
                                dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW));
        if (glGetError() != GL_NO_ERROR) {
            GR_GL(DeleteBuffers(1, &id));
            // deleting bound buffer does implicit bind to 0
            fHWGeometryState.fIndexBuffer = NULL;
            return NULL;
        }
        GrIndexBuffer* indexBuffer = new GrGLIndexBuffer(id, this,
                                                         size, dynamic);
        fHWGeometryState.fIndexBuffer = indexBuffer;
        return indexBuffer;
    }
    return NULL;
}

void GrGpuGL::setDefaultRenderTargetSize(uint32_t width, uint32_t height) {
    GrIRect viewport(0, height, width, 0);
    if (viewport != fDefaultRenderTarget->viewport()) {
        fDefaultRenderTarget->setViewport(viewport);
        if (fHWDrawState.fRenderTarget == fDefaultRenderTarget) {
            fHWDrawState.fRenderTarget = NULL;
        }
    }
}

void GrGpuGL::flushScissor(const GrIRect* rect) {
    GrAssert(NULL != fCurrDrawState.fRenderTarget);
    const GrIRect& vp =
            ((GrGLRenderTarget*)fCurrDrawState.fRenderTarget)->viewport();

    if (NULL != rect &&
        rect->contains(vp)) {
        rect = NULL;
    }

    if (NULL != rect) {
        GrIRect scissor;
        // viewport is already in GL coords
        // create a scissor in GL coords (top > bottom)
        scissor.setLTRB(vp.fLeft + rect->fLeft,
                        vp.fTop  - rect->fTop,
                        vp.fLeft + rect->fRight,
                        vp.fTop  - rect->fBottom);

        if (fHWBounds.fScissorRect != scissor) {
            GR_GL(Scissor(scissor.fLeft, scissor.fBottom,
                          scissor.width(), -scissor.height()));
            fHWBounds.fScissorRect = scissor;
        }

        if (!fHWBounds.fScissorEnabled) {
            GR_GL(Enable(GL_SCISSOR_TEST));
            fHWBounds.fScissorEnabled = true;
        }
    } else {
        if (fHWBounds.fScissorEnabled) {
            GR_GL(Disable(GL_SCISSOR_TEST));
            fHWBounds.fScissorEnabled = false;
        }
    }
}

void GrGpuGL::eraseColor(GrColor color) {
    flushRenderTarget();
    if (fHWBounds.fScissorEnabled) {
        GR_GL(Disable(GL_SCISSOR_TEST));
    }
    GR_GL(ColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE));
    GR_GL(ClearColor(GrColorUnpackR(color)/255.f,
                     GrColorUnpackG(color)/255.f,
                     GrColorUnpackB(color)/255.f,
                     GrColorUnpackA(color)/255.f));
    GR_GL(Clear(GL_COLOR_BUFFER_BIT));
    fHWBounds.fScissorEnabled = false;
    fWriteMaskChanged = true;
}

void GrGpuGL::eraseStencil(uint32_t value, uint32_t mask) {
    flushRenderTarget();
    if (fHWBounds.fScissorEnabled) {
        GR_GL(Disable(GL_SCISSOR_TEST));
    }
    GR_GL(StencilMask(mask));
    GR_GL(ClearStencil(value));
    GR_GL(Clear(GL_STENCIL_BUFFER_BIT));
    fHWBounds.fScissorEnabled = false;
    fWriteMaskChanged = true;
}

void GrGpuGL::eraseStencilClip() {
    GLint stencilBitCount;
    GR_GL(GetIntegerv(GL_STENCIL_BITS, &stencilBitCount));
    GrAssert(stencilBitCount > 0);
    GLint clipStencilMask  = (1 << (stencilBitCount - 1));
    eraseStencil(0, clipStencilMask);
}

void GrGpuGL::forceRenderTargetFlush() {
    flushRenderTarget();
}

bool GrGpuGL::readPixels(int left, int top, int width, int height,
                         GrTexture::PixelConfig config, void* buffer) {
    GLenum internalFormat;  // we don't use this for glReadPixels
    GLenum format;
    GLenum type;
    if (!this->canBeTexture(config, &internalFormat, &format, &type)) {
        return false;
    }

    GrAssert(NULL != fCurrDrawState.fRenderTarget);
    const GrIRect& vp = ((GrGLRenderTarget*)fCurrDrawState.fRenderTarget)->viewport();

    // Brian says that viewport rects are already upside down (grrrrr)
    glReadPixels(left, -vp.height() - top - height, width, height,
                 format, type, buffer);

    // now reverse the order of the rows, since GL's are bottom-to-top, but our
    // API presents top-to-bottom
    {
        size_t stride = width * GrTexture::BytesPerPixel(config);
        GrAutoMalloc rowStorage(stride);
        void* tmp = rowStorage.get();

        const int halfY = height >> 1;
        char* top = reinterpret_cast<char*>(buffer);
        char* bottom = top + (height - 1) * stride;
        for (int y = 0; y < halfY; y++) {
            memcpy(tmp, top, stride);
            memcpy(top, bottom, stride);
            memcpy(bottom, tmp, stride);
            top += stride;
            bottom -= stride;
        }
    }
    return true;
}

void GrGpuGL::flushRenderTarget() {
    if (fHWDrawState.fRenderTarget != fCurrDrawState.fRenderTarget) {
        GrGLRenderTarget* rt = (GrGLRenderTarget*)fCurrDrawState.fRenderTarget;
        GR_GLEXT(fExts, BindFramebuffer(GR_FRAMEBUFFER, rt->renderFBOID()));
    #if GR_COLLECT_STATS
        ++fStats.fRenderTargetChngCnt;
    #endif
        rt->setDirty(true);
    #if GR_DEBUG
        GLenum status = GR_GLEXT(fExts, CheckFramebufferStatus(GR_FRAMEBUFFER));
        if (status != GR_FRAMEBUFFER_COMPLETE) {
            GrPrintf("-- glCheckFramebufferStatus %x\n", status);
        }
    #endif
        fHWDrawState.fRenderTarget = fCurrDrawState.fRenderTarget;
        const GrIRect& vp = rt->viewport();
        fRenderTargetChanged = true;
        if (fHWBounds.fViewportRect != vp) {
            GR_GL(Viewport(vp.fLeft,
                           vp.fBottom,
                           vp.width(),
                           -vp.height()));
            fHWBounds.fViewportRect = vp;
        }
    }
}

GLenum gPrimitiveType2GLMode[] = {
    GL_TRIANGLES,
    GL_TRIANGLE_STRIP,
    GL_TRIANGLE_FAN,
    GL_POINTS,
    GL_LINES,
    GL_LINE_STRIP
};

void GrGpuGL::drawIndexedHelper(PrimitiveType type,
                                uint32_t startVertex,
                                uint32_t startIndex,
                                uint32_t vertexCount,
                                uint32_t indexCount) {
    GrAssert((size_t)type < GR_ARRAY_COUNT(gPrimitiveType2GLMode));

    GLvoid* indices = (GLvoid*)(sizeof(uint16_t) * startIndex);
    if (kReserved_GeometrySrcType == fGeometrySrc.fIndexSrc) {
        indices = (GLvoid*)((intptr_t)indices + (intptr_t)fIndices.get());
    } else if (kArray_GeometrySrcType == fGeometrySrc.fIndexSrc) {
        indices = (GLvoid*)((intptr_t)indices +
                            (intptr_t)fGeometrySrc.fIndexArray);
    }

    GR_GL(DrawElements(gPrimitiveType2GLMode[type], indexCount,
                       GL_UNSIGNED_SHORT, indices));
}

void GrGpuGL::drawNonIndexedHelper(PrimitiveType type,
                                   uint32_t startVertex,
                                   uint32_t vertexCount) {
    GrAssert((size_t)type < GR_ARRAY_COUNT(gPrimitiveType2GLMode));

    GR_GL(DrawArrays(gPrimitiveType2GLMode[type], 0, vertexCount));
}

#if !defined(SK_GL_HAS_COLOR4UB)
static inline GrFixed byte2fixed(unsigned value) {
    return (value + (value >> 7)) << 8;
}
#endif

void GrGpuGL::resolveTextureRenderTarget(GrGLTexture* texture) {
    GrGLRenderTarget* rt = (GrGLRenderTarget*) texture->asRenderTarget();

    if (NULL != rt && rt->needsResolve()) {
        GrAssert(kNone_MSFBO != fMSFBOType);
        GrAssert(rt->textureFBOID() != rt->renderFBOID());
        GR_GLEXT(fExts, BindFramebuffer(GR_READ_FRAMEBUFFER,
                                        rt->renderFBOID()));
        GR_GLEXT(fExts, BindFramebuffer(GR_DRAW_FRAMEBUFFER,
                                        rt->textureFBOID()));
    #if GR_COLLECT_STATS
        ++fStats.fRenderTargetChngCnt;
    #endif
        // make sure we go through set render target
        fHWDrawState.fRenderTarget = NULL;

        GLint left = 0;
        GLint right = texture->contentWidth();
        // we will have rendered to the top of the FBO.
        GLint top = texture->allocHeight();
        GLint bottom = texture->allocHeight() - texture->contentHeight();
        if (kApple_MSFBO == fMSFBOType) {
            GR_GL(Enable(GL_SCISSOR_TEST));
            GR_GL(Scissor(left, bottom, right-left, top-bottom));
            GR_GLEXT(fExts, ResolveMultisampleFramebuffer());
            fHWBounds.fScissorRect.setEmpty();
            fHWBounds.fScissorEnabled = true;
        } else {
            GR_GLEXT(fExts, BlitFramebuffer(left, bottom, right, top,
                                     left, bottom, right, top,
                                     GL_COLOR_BUFFER_BIT, GL_NEAREST));
        }
        rt->setDirty(false);

    }
}

void GrGpuGL::flushStencil() {

    // use stencil for clipping if clipping is enabled and the clip
    // has been written into the stencil.
    bool stencilClip = fClipState.fClipInStencil &&
                       (kClip_StateBit & fCurrDrawState.fFlagBits);
    bool stencilChange =
        fWriteMaskChanged                                         ||
        fHWStencilClip != stencilClip                             ||
        fHWDrawState.fStencilPass != fCurrDrawState.fStencilPass  ||
        (kNone_StencilPass != fCurrDrawState.fStencilPass &&
         (StencilPass)kSetClip_StencilPass != fCurrDrawState.fStencilPass &&
         fHWDrawState.fReverseFill != fCurrDrawState.fReverseFill);

    if (stencilChange) {
        GLint stencilBitCount;
        GLint clipStencilMask;
        GLint pathStencilMask;
        GR_GL(GetIntegerv(GL_STENCIL_BITS, &stencilBitCount));
        GrAssert(stencilBitCount > 0 ||
                 kNone_StencilPass == fCurrDrawState.fStencilPass);
        clipStencilMask  = (1 << (stencilBitCount - 1));
        pathStencilMask = clipStencilMask - 1;
        switch (fCurrDrawState.fStencilPass) {
            case kNone_StencilPass:
                if (stencilClip) {
                    GR_GL(Enable(GL_STENCIL_TEST));
                    GR_GL(StencilFunc(GL_EQUAL,
                                      clipStencilMask,
                                      clipStencilMask));
                    GR_GL(StencilOp(GL_KEEP, GL_KEEP, GL_KEEP));
                } else {
                    GR_GL(Disable(GL_STENCIL_TEST));
                }
                GR_GL(ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE));
                if (!fSingleStencilPassForWinding) {
                    GR_GL(Disable(GL_CULL_FACE));
                }
                break;
            case kEvenOddStencil_StencilPass:
                GR_GL(Enable(GL_STENCIL_TEST));
                if (stencilClip) {
                    GR_GL(StencilFunc(GL_EQUAL, clipStencilMask, clipStencilMask));
                } else {
                    GR_GL(StencilFunc(GL_ALWAYS, 0x0, 0x0));
                }
                GR_GL(StencilMask(pathStencilMask));
                GR_GL(ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE));
                GR_GL(StencilOp(GL_KEEP, GL_INVERT, GL_INVERT));
                GR_GL(ColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE));
                if (!fSingleStencilPassForWinding) {
                    GR_GL(Disable(GL_CULL_FACE));
                }
                break;
            case kEvenOddColor_StencilPass: {
                GR_GL(Enable(GL_STENCIL_TEST));
                GLint  funcRef  = 0;
                GLuint funcMask = pathStencilMask;
                if (stencilClip) {
                    funcRef  |= clipStencilMask;
                    funcMask |= clipStencilMask;
                }
                if (!fCurrDrawState.fReverseFill) {
                    funcRef |= pathStencilMask;
                }
                glStencilFunc(GL_EQUAL, funcRef, funcMask);
                glStencilMask(pathStencilMask);
                GR_GL(StencilOp(GL_ZERO, GL_ZERO, GL_ZERO));
                GR_GL(ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE));
                if (!fSingleStencilPassForWinding) {
                    GR_GL(Disable(GL_CULL_FACE));
                }
                } break;
            case kWindingStencil1_StencilPass:
                GR_GL(Enable(GL_STENCIL_TEST));
                if (fHasStencilWrap) {
                    if (stencilClip) {
                        GR_GL(StencilFunc(GL_EQUAL,
                                          clipStencilMask,
                                          clipStencilMask));
                    } else {
                        GR_GL(StencilFunc(GL_ALWAYS, 0x0, 0x0));
                    }
                    if (fSingleStencilPassForWinding) {
                        GR_GL(StencilOpSeparate(GL_FRONT, GL_KEEP,
                                                GL_INCR_WRAP, GL_INCR_WRAP));
                        GR_GL(StencilOpSeparate(GL_BACK,  GL_KEEP,
                                                GL_DECR_WRAP, GL_DECR_WRAP));
                    } else {
                        GR_GL(StencilOp(GL_KEEP, GL_INCR_WRAP, GL_INCR_WRAP));
                        GR_GL(Enable(GL_CULL_FACE));
                        GR_GL(CullFace(GL_BACK));
                    }
                } else {
                    // If we don't have wrap then we use the Func to detect
                    // values that would wrap (0 on decr and mask on incr). We
                    // make the func fail on these values and use the sfail op
                    // to effectively wrap by inverting.
                    // This applies whether we are doing a two-pass (front faces
                    // followed by back faces) or a single pass (separate func/op)

                    // Note that in the case where we are also using stencil to
                    // clip this means we will write into the path bits in clipped
                    // out pixels. We still apply the clip bit in the color pass
                    // stencil func so we don't draw color outside the clip.
                    // We also will clear the stencil bits in clipped pixels by
                    // using zero in the sfail op with write mask set to the
                    // path mask.
                    GR_GL(Enable(GL_STENCIL_TEST));
                    if (fSingleStencilPassForWinding) {
                        GR_GL(StencilFuncSeparate(GL_FRONT,
                                                  GL_NOTEQUAL,
                                                  pathStencilMask,
                                                  pathStencilMask));
                        GR_GL(StencilFuncSeparate(GL_BACK,
                                                  GL_NOTEQUAL,
                                                  0x0,
                                                  pathStencilMask));
                        GR_GL(StencilOpSeparate(GL_FRONT, GL_INVERT,
                                                GL_INCR, GL_INCR));
                        GR_GL(StencilOpSeparate(GL_BACK,  GL_INVERT,
                                                GL_DECR, GL_DECR));
                    } else {
                        GR_GL(StencilFunc(GL_NOTEQUAL,
                                          pathStencilMask,
                                          pathStencilMask));
                        GR_GL(StencilOp(GL_INVERT, GL_INCR, GL_INCR));
                        GR_GL(Enable(GL_CULL_FACE));
                        GR_GL(CullFace(GL_BACK));
                    }
                }
                GR_GL(StencilMask(pathStencilMask));
                GR_GL(ColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE));
                break;
            case kWindingStencil2_StencilPass:
                GrAssert(!fSingleStencilPassForWinding);
                GR_GL(Enable(GL_STENCIL_TEST));
                if (fHasStencilWrap) {
                    if (stencilClip) {
                        GR_GL(StencilFunc(GL_EQUAL,
                                          clipStencilMask,
                                          clipStencilMask));
                    } else {
                        GR_GL(StencilFunc(GL_ALWAYS, 0x0, 0x0));
                    }
                    GR_GL(StencilOp(GL_DECR_WRAP, GL_DECR_WRAP, GL_DECR_WRAP));
                } else {
                    GR_GL(StencilFunc(GL_NOTEQUAL, 0x0, pathStencilMask));
                    GR_GL(StencilOp(GL_INVERT, GL_DECR, GL_DECR));
                }
                GR_GL(StencilMask(pathStencilMask));
                GR_GL(Enable(GL_CULL_FACE));
                GR_GL(CullFace(GL_FRONT));
                GR_GL(ColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE));
                break;
            case kWindingColor_StencilPass: {
                GR_GL(Enable(GL_STENCIL_TEST));
                GLint  funcRef   = 0;
                GLuint funcMask  = pathStencilMask;
                GLenum funcFunc;
                if (stencilClip) {
                    funcRef  |= clipStencilMask;
                    funcMask |= clipStencilMask;
                }
                if (fCurrDrawState.fReverseFill) {
                    funcFunc = GL_EQUAL;
                } else {
                    funcFunc = GL_LESS;
                }
                GR_GL(StencilFunc(funcFunc, funcRef, funcMask));
                GR_GL(StencilMask(pathStencilMask));
                // must zero in sfail because winding w/o wrap will write
                // path stencil bits in clipped out pixels
                GR_GL(StencilOp(GL_ZERO, GL_ZERO, GL_ZERO));
                GR_GL(ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE));
                if (!fSingleStencilPassForWinding) {
                    GR_GL(Disable(GL_CULL_FACE));
                }
                } break;
            case kSetClip_StencilPass:
                GR_GL(Enable(GL_STENCIL_TEST));
                GR_GL(StencilFunc(GL_ALWAYS, clipStencilMask, clipStencilMask));
                GR_GL(StencilOp(GL_REPLACE, GL_REPLACE, GL_REPLACE));
                GR_GL(StencilMask(clipStencilMask));
                GR_GL(ColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE));
                if (!fSingleStencilPassForWinding) {
                    GR_GL(Disable(GL_CULL_FACE));
                }
                break;
            default:
                GrAssert(!"Unexpected stencil pass.");
                break;

        }
        fHWDrawState.fStencilPass = fCurrDrawState.fStencilPass;
        fHWDrawState.fReverseFill = fCurrDrawState.fReverseFill;
        fWriteMaskChanged = false;
        fHWStencilClip = stencilClip;
    }
}

void GrGpuGL::flushGLStateCommon(PrimitiveType type) {

    bool usingTexture = VertexHasTexCoords(fGeometrySrc.fVertexLayout);

    // bind texture and set sampler state
    if (usingTexture) {
        GrGLTexture* nextTexture = (GrGLTexture*)fCurrDrawState.fTexture;

        if (NULL != nextTexture) {
            // if we created a rt/tex and rendered to it without using a texture
            // and now we're texuring from the rt it will still be the last bound
            // texture, but it needs resolving. So keep this out of the last
            // != next check.
            resolveTextureRenderTarget(nextTexture);

            if (fHWDrawState.fTexture != nextTexture) {

                GR_GL(BindTexture(GL_TEXTURE_2D, nextTexture->textureID()));
            #if GR_COLLECT_STATS
                ++fStats.fTextureChngCnt;
            #endif
                //GrPrintf("---- bindtexture %d\n", nextTexture->textureID());
                fHWDrawState.fTexture = nextTexture;
            }

            const GrGLTexture::TexParams& oldTexParams = nextTexture->getTexParams();
            GrGLTexture::TexParams newTexParams;
            newTexParams.fFilter = fCurrDrawState.fSamplerState.isFilter() ?
                                                                GL_LINEAR :
                                                                GL_NEAREST;
            newTexParams.fWrapS = GrGLTexture::gWrapMode2GLWrap[fCurrDrawState.fSamplerState.getWrapX()];
            newTexParams.fWrapT = GrGLTexture::gWrapMode2GLWrap[fCurrDrawState.fSamplerState.getWrapY()];

            if (newTexParams.fFilter != oldTexParams.fFilter) {
                GR_GL(TexParameteri(GL_TEXTURE_2D,
                                    GL_TEXTURE_MAG_FILTER,
                                    newTexParams.fFilter));
                GR_GL(TexParameteri(GL_TEXTURE_2D,
                                    GL_TEXTURE_MIN_FILTER,
                                    newTexParams.fFilter));
            }
            if (newTexParams.fWrapS != oldTexParams.fWrapS) {
                GR_GL(TexParameteri(GL_TEXTURE_2D,
                                    GL_TEXTURE_WRAP_S,
                                    newTexParams.fWrapS));
            }
            if (newTexParams.fWrapT != oldTexParams.fWrapT) {
                GR_GL(TexParameteri(GL_TEXTURE_2D,
                                    GL_TEXTURE_WRAP_T,
                                    newTexParams.fWrapT));
            }
            nextTexture->setTexParams(newTexParams);
        } else {
            GrAssert(!"Rendering with texture vert flag set but no texture");
            if (NULL != fHWDrawState.fTexture) {
                GR_GL(BindTexture(GL_TEXTURE_2D, 0));
                //            GrPrintf("---- bindtexture 0\n");
            #if GR_COLLECT_STATS
                ++fStats.fTextureChngCnt;
            #endif
                fHWDrawState.fTexture = NULL;
            }
        }
    }

    flushRenderTarget();

    if ((fCurrDrawState.fFlagBits & kDither_StateBit) !=
        (fHWDrawState.fFlagBits & kDither_StateBit)) {
        if (fCurrDrawState.fFlagBits & kDither_StateBit) {
            GR_GL(Enable(GL_DITHER));
        } else {
            GR_GL(Disable(GL_DITHER));
        }
    }

#if GR_GL_DESKTOP
    // ES doesn't support toggling GL_MULTISAMPLE and doesn't have
    // smooth lines.
    if (fRenderTargetChanged ||
        (fCurrDrawState.fFlagBits & kAntialias_StateBit) !=
        (fHWDrawState.fFlagBits & kAntialias_StateBit)) {
        GLint msaa = 0;
        // only perform query if we know MSAA is supported.
        // calling on non-MSAA target caused a crash in one environment,
        // though I don't think it should.
        if (!fAASamples[kHigh_AALevel]) {
            GR_GL(GetIntegerv(GL_SAMPLE_BUFFERS, &msaa));
        }
        if (fCurrDrawState.fFlagBits & kAntialias_StateBit) {
            if (msaa) {
                GR_GL(Enable(GL_MULTISAMPLE));
            } else {
                GR_GL(Enable(GL_LINE_SMOOTH));
            }
        } else {
            if (msaa) {
                GR_GL(Disable(GL_MULTISAMPLE));
            }
            GR_GL(Disable(GL_LINE_SMOOTH));
        }
    }
#endif

    bool blendOff = canDisableBlend();
    if (fHWBlendDisabled != blendOff) {
        if (blendOff) {
            GR_GL(Disable(GL_BLEND));
        } else {
            GR_GL(Enable(GL_BLEND));
        }
        fHWBlendDisabled = blendOff;
    }

    if (!blendOff) {
        if (fHWDrawState.fSrcBlend != fCurrDrawState.fSrcBlend ||
              fHWDrawState.fDstBlend != fCurrDrawState.fDstBlend) {
            GR_GL(BlendFunc(gXfermodeCoeff2Blend[fCurrDrawState.fSrcBlend],
                            gXfermodeCoeff2Blend[fCurrDrawState.fDstBlend]));
            fHWDrawState.fSrcBlend = fCurrDrawState.fSrcBlend;
            fHWDrawState.fDstBlend = fCurrDrawState.fDstBlend;
        }
    }

    // check for circular rendering
    GrAssert(!usingTexture ||
             NULL == fCurrDrawState.fRenderTarget ||
             NULL == fCurrDrawState.fTexture ||
             fCurrDrawState.fTexture->asRenderTarget() != fCurrDrawState.fRenderTarget);

    flushStencil();

    fHWDrawState.fFlagBits = fCurrDrawState.fFlagBits;
}

void GrGpuGL::notifyVertexBufferBind(const GrGLVertexBuffer* buffer) {
    fHWGeometryState.fVertexBuffer = buffer;
}

void GrGpuGL::notifyVertexBufferDelete(const GrGLVertexBuffer* buffer) {
    GrAssert(!(kBuffer_GeometrySrcType == fGeometrySrc.fVertexSrc &&
               buffer == fGeometrySrc.fVertexBuffer));

    if (fHWGeometryState.fVertexBuffer == buffer) {
        // deleting bound buffer does implied bind to 0
        fHWGeometryState.fVertexBuffer = NULL;
    }
}

void GrGpuGL::notifyIndexBufferBind(const GrGLIndexBuffer* buffer) {
    fGeometrySrc.fIndexBuffer = buffer;
}

void GrGpuGL::notifyIndexBufferDelete(const GrGLIndexBuffer* buffer) {
    GrAssert(!(kBuffer_GeometrySrcType == fGeometrySrc.fIndexSrc &&
               buffer == fGeometrySrc.fIndexBuffer));

    if (fHWGeometryState.fIndexBuffer == buffer) {
        // deleting bound buffer does implied bind to 0
        fHWGeometryState.fIndexBuffer = NULL;
    }
}

void GrGpuGL::notifyTextureBind(GrGLTexture* texture) {
    fHWDrawState.fTexture = texture;
#if GR_COLLECT_STATS
    ++fStats.fTextureChngCnt;
#endif
}

void GrGpuGL::notifyRenderTargetDelete(GrRenderTarget* renderTarget) {
    GrAssert(NULL != renderTarget);

    // if the bound FBO is destroyed we can't rely on the implicit bind to 0
    // a) we want the default RT which may not be FBO 0
    // b) we set more state than just FBO based on the RT
    // So trash the HW state to force an RT flush next time
    if (fCurrDrawState.fRenderTarget == renderTarget) {
        fCurrDrawState.fRenderTarget = fDefaultRenderTarget;
    }
    if (fHWDrawState.fRenderTarget == renderTarget) {
        fHWDrawState.fRenderTarget = NULL;
    }
    if (fClipState.fStencilClipTarget == renderTarget) {
        fClipState.fStencilClipTarget = NULL;
    }
}

void GrGpuGL::notifyTextureDelete(GrGLTexture* texture) {
    if (fCurrDrawState.fTexture == texture) {
        fCurrDrawState.fTexture = NULL;
    }
    if (fHWDrawState.fTexture == texture) {
        // deleting bound texture does implied bind to 0
        fHWDrawState.fTexture = NULL;
   }
}

void GrGpuGL::notifyTextureRemoveRenderTarget(GrGLTexture* texture) {
    GrAssert(NULL != texture->asRenderTarget());

    // if there is a pending resolve, perform it.
    resolveTextureRenderTarget(texture);
}

bool GrGpuGL::canBeTexture(GrTexture::PixelConfig config,
                           GLenum* internalFormat,
                           GLenum* format,
                           GLenum* type) {
    switch (config) {
        case GrTexture::kRGBA_8888_PixelConfig:
        case GrTexture::kRGBX_8888_PixelConfig: // todo: can we tell it our X?
            *format = GR_GL_32BPP_COLOR_FORMAT;
            *internalFormat = GL_RGBA;
            *type = GL_UNSIGNED_BYTE;
            break;
        case GrTexture::kRGB_565_PixelConfig:
            *format = GL_RGB;
            *internalFormat = GL_RGB;
            *type = GL_UNSIGNED_SHORT_5_6_5;
            break;
        case GrTexture::kRGBA_4444_PixelConfig:
            *format = GL_RGBA;
            *internalFormat = GL_RGBA;
            *type = GL_UNSIGNED_SHORT_4_4_4_4;
            break;
        case GrTexture::kIndex_8_PixelConfig:
            if (this->supports8BitPalette()) {
                *format = GR_PALETTE8_RGBA8;
                *internalFormat = GR_PALETTE8_RGBA8;
                *type = GL_UNSIGNED_BYTE;   // unused I think
            } else {
                return false;
            }
            break;
        case GrTexture::kAlpha_8_PixelConfig:
            *format = GL_ALPHA;
            *internalFormat = GL_ALPHA;
            *type = GL_UNSIGNED_BYTE;
            break;
        default:
            return false;
    }
    return true;
}

/* On ES the internalFormat and format must match for TexImage and we use
   GL_RGB, GL_RGBA for color formats. We also generally like having the driver
   decide the internalFormat. However, on ES internalFormat for
   RenderBufferStorage* has to be a specific format (not a base format like
   GL_RGBA).
 */
bool GrGpuGL::fboInternalFormat(GrTexture::PixelConfig config, GLenum* format) {
    switch (config) {
        case GrTexture::kRGBA_8888_PixelConfig:
        case GrTexture::kRGBX_8888_PixelConfig:
            if (fRGBA8Renderbuffer) {
                *format = GR_RGBA8;
                return true;
            } else {
                return false;
            }
#if GR_GL_ES // ES2 supports 565. ES1 supports it with FBO extension
             // desktop GL has no such internal format
        case GrTexture::kRGB_565_PixelConfig:
            *format = GR_RGB565;
            return true;
#endif
        case GrTexture::kRGBA_4444_PixelConfig:
            *format = GL_RGBA4;
            return true;
        default:
            return false;
    }
}

///////////////////////////////////////////////////////////////////////////////

void GrGLCheckErr(const char* location, const char* call) {
    uint32_t err =  glGetError();
    if (GL_NO_ERROR != err) {
        GrPrintf("---- glGetError %x", err);
        if (NULL != location) {
            GrPrintf(" at\n\t%s", location);
        }
        if (NULL != call) {
            GrPrintf("\n\t\t%s", call);
        }
        GrPrintf("\n");
    }
}

///////////////////////////////////////////////////////////////////////////////

typedef void (*glProc)(void);

void get_gl_proc(const char procName[], glProc *address) {
#if GR_WIN32_BUILD
    *address = (glProc)wglGetProcAddress(procName);
    GrAssert(NULL != *address);
#elif GR_MAC_BUILD || GR_IOS_BUILD
    GrAssert(!"Extensions don't need to be initialized!");
#elif GR_ANDROID_BUILD
    *address = eglGetProcAddress(procName);
    GrAssert(NULL != *address);
#elif GR_LINUX_BUILD
//    GR_STATIC_ASSERT(!"Add environment-dependent implementation here");
    //*address = glXGetProcAddressARB(procName);
    *address = NULL;//eglGetProcAddress(procName);
#elif GR_QNX_BUILD
    *address = eglGetProcAddress(procName);
    GrAssert(NULL != *address);
#else
    // hopefully we're on a system with EGL
    *address = eglGetProcAddress(procName);
    GrAssert(NULL != *address);
#endif
}

#define GET_PROC(EXT_STRUCT, PROC_NAME, EXT_TAG) \
    get_gl_proc("gl" #PROC_NAME #EXT_TAG, (glProc*)&EXT_STRUCT-> PROC_NAME);

extern void GrGLInitExtensions(GrGLExts* exts) {
    exts->GenFramebuffers                   = NULL;
    exts->BindFramebuffer                   = NULL;
    exts->FramebufferTexture2D              = NULL;
    exts->CheckFramebufferStatus            = NULL;
    exts->DeleteFramebuffers                = NULL;
    exts->RenderbufferStorage               = NULL;
    exts->GenRenderbuffers                  = NULL;
    exts->DeleteRenderbuffers               = NULL;
    exts->FramebufferRenderbuffer           = NULL;
    exts->BindRenderbuffer                  = NULL;
    exts->RenderbufferStorageMultisample    = NULL;
    exts->BlitFramebuffer                   = NULL;
    exts->ResolveMultisampleFramebuffer     = NULL;
    exts->FramebufferTexture2DMultisample   = NULL;
    exts->MapBuffer                         = NULL;
    exts->UnmapBuffer                       = NULL;

#if GR_MAC_BUILD
    exts->GenFramebuffers                   = glGenFramebuffers;
    exts->BindFramebuffer                   = glBindFramebuffer;
    exts->FramebufferTexture2D              = glFramebufferTexture2D;
    exts->CheckFramebufferStatus            = glCheckFramebufferStatus;
    exts->DeleteFramebuffers                = glDeleteFramebuffers;
    exts->RenderbufferStorage               = glRenderbufferStorage;
    exts->GenRenderbuffers                  = glGenRenderbuffers;
    exts->DeleteRenderbuffers               = glDeleteRenderbuffers;
    exts->FramebufferRenderbuffer           = glFramebufferRenderbuffer;
    exts->BindRenderbuffer                  = glBindRenderbuffer;
    exts->RenderbufferStorageMultisample    = glRenderbufferStorageMultisample;
    exts->BlitFramebuffer                   = glBlitFramebuffer;
    exts->MapBuffer                         = glMapBuffer;
    exts->UnmapBuffer                       = glUnmapBuffer;
#elif GR_IOS_BUILD
    exts->GenFramebuffers                   = glGenFramebuffers;
    exts->BindFramebuffer                   = glBindFramebuffer;
    exts->FramebufferTexture2D              = glFramebufferTexture2D;
    exts->CheckFramebufferStatus            = glCheckFramebufferStatus;
    exts->DeleteFramebuffers                = glDeleteFramebuffers;
    exts->RenderbufferStorage               = glRenderbufferStorage;
    exts->GenRenderbuffers                  = glGenRenderbuffers;
    exts->DeleteRenderbuffers               = glDeleteRenderbuffers;
    exts->FramebufferRenderbuffer           = glFramebufferRenderbuffer;
    exts->BindRenderbuffer                  = glBindRenderbuffer;
    exts->RenderbufferStorageMultisample    = glRenderbufferStorageMultisampleAPPLE;
    exts->ResolveMultisampleFramebuffer     = glResolveMultisampleFramebufferAPPLE;
    exts->MapBuffer                         = glMapBufferOES;
    exts->UnmapBuffer                       = glUnmapBufferOES;
#else
    GLint major, minor;
    gl_version(&major, &minor);
    #if GR_GL_DESKTOP
    if (major >= 3) {// FBO, FBOMS, and FBOBLIT part of 3.0
        exts->GenFramebuffers                   = glGenFramebuffers;
        exts->BindFramebuffer                   = glBindFramebuffer;
        exts->FramebufferTexture2D              = glFramebufferTexture2D;
        exts->CheckFramebufferStatus            = glCheckFramebufferStatus;
        exts->DeleteFramebuffers                = glDeleteFramebuffers;
        exts->RenderbufferStorage               = glRenderbufferStorage;
        exts->GenRenderbuffers                  = glGenRenderbuffers;
        exts->DeleteRenderbuffers               = glDeleteRenderbuffers;
        exts->FramebufferRenderbuffer           = glFramebufferRenderbuffer;
        exts->BindRenderbuffer                  = glBindRenderbuffer;
        exts->RenderbufferStorageMultisample    = glRenderbufferStorageMultisample;
        exts->BlitFramebuffer                   = glBlitFramebuffer;
    } else if (has_gl_extension("GL_ARB_framebuffer_object")) {
        GET_PROC(exts, GenFramebuffers, ARB);
        GET_PROC(exts, BindFramebuffer, ARB);
        GET_PROC(exts, FramebufferTexture2D, ARB);
        GET_PROC(exts, CheckFramebufferStatus, ARB);
        GET_PROC(exts, DeleteFramebuffers, ARB);
        GET_PROC(exts, RenderbufferStorage, ARB);
        GET_PROC(exts, GenRenderbuffers, ARB);
        GET_PROC(exts, DeleteRenderbuffers, ARB);
        GET_PROC(exts, FramebufferRenderbuffer, ARB);
        GET_PROC(exts, BindRenderbuffer, ARB);
        GET_PROC(exts, RenderbufferStorageMultisample, ARB);
        GET_PROC(exts, BlitFramebuffer, ARB);
    } else {
        // we require some form of FBO
        GrAssert(has_gl_extension("GL_EXT_framebuffer_object"));
        GET_PROC(exts, GenFramebuffers, EXT);
        GET_PROC(exts, BindFramebuffer, EXT);
        GET_PROC(exts, FramebufferTexture2D, EXT);
        GET_PROC(exts, CheckFramebufferStatus, EXT);
        GET_PROC(exts, DeleteFramebuffers, EXT);
        GET_PROC(exts, RenderbufferStorage, EXT);
        GET_PROC(exts, GenRenderbuffers, EXT);
        GET_PROC(exts, DeleteRenderbuffers, EXT);
        GET_PROC(exts, FramebufferRenderbuffer, EXT);
        GET_PROC(exts, BindRenderbuffer, EXT);
        if (has_gl_extension("GL_EXT_framebuffer_multisample")) {
            GET_PROC(exts, RenderbufferStorageMultisample, EXT);
        }
        if (has_gl_extension("GL_EXT_framebuffer_blit")) {
            GET_PROC(exts, BlitFramebuffer, EXT);
        }
    }
    // we assume we have at least GL 1.5 or higher (VBOs introduced in 1.5)
    exts->MapBuffer     = glMapBuffer;
    exts->UnmapBuffer   = glUnmapBuffer;
    #else // !GR_GL_DESKTOP
    if (major >= 2) {// ES 2.0 supports FBO
        exts->GenFramebuffers                   = glGenFramebuffers;
        exts->BindFramebuffer                   = glBindFramebuffer;
        exts->FramebufferTexture2D              = glFramebufferTexture2D;
        exts->CheckFramebufferStatus            = glCheckFramebufferStatus;
        exts->DeleteFramebuffers                = glDeleteFramebuffers;
        exts->RenderbufferStorage               = glRenderbufferStorage;
        exts->GenRenderbuffers                  = glGenRenderbuffers;
        exts->DeleteRenderbuffers               = glDeleteRenderbuffers;
        exts->FramebufferRenderbuffer           = glFramebufferRenderbuffer;
        exts->BindRenderbuffer                  = glBindRenderbuffer;
    } else {
        // we require some form of FBO
        GrAssert(has_gl_extension("GL_OES_framebuffer_object"));

        GET_PROC(exts, GenFramebuffers, OES);
        GET_PROC(exts, BindFramebuffer, OES);
        GET_PROC(exts, FramebufferTexture2D, OES);
        GET_PROC(exts, CheckFramebufferStatus, OES);
        GET_PROC(exts, DeleteFramebuffers, OES);
        GET_PROC(exts, RenderbufferStorage, OES);
        GET_PROC(exts, GenRenderbuffers, OES);
        GET_PROC(exts, DeleteRenderbuffers, OES);
        GET_PROC(exts, FramebufferRenderbuffer, OES);
        GET_PROC(exts, BindRenderbuffer, OES);
    }
    if (has_gl_extension("GL_APPLE_framebuffer_multisample")) {
        GET_PROC(exts, ResolveMultisampleFramebuffer, APPLE);
    }
    if (has_gl_extension("GL_IMG_multisampled_render_to_texture")) {
        GET_PROC(exts, FramebufferTexture2DMultisample, IMG);
    }
    if (has_gl_extension("GL_OES_mapbuffer")) {
        GET_PROC(exts, MapBuffer, OES);
        GET_PROC(exts, UnmapBuffer, OES);
    }
    #endif // !GR_GL_DESKTOP
#endif // BUILD
}

bool gPrintGL = true;

