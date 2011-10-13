{
  'includes': [
    'common.gypi',
  ],
  'target_defaults': {
    'conditions': [
      ['skia_os != "win"', {
        'sources/': [ ['exclude', '_win.(h|cpp)$'],
        ],
      }],
      ['skia_os != "mac"', {
        'sources/': [ ['exclude', '_mac.(h|cpp)$'],
        ],
      }],
      ['skia_os != "linux"', {
        'sources/': [ ['exclude', '_unix.(h|cpp)$'],
        ],
      }],
      ['skia_os != "ios"', {
        'sources/': [ ['exclude', '_iOS.(h|cpp)$'],
        ],
      }],
      ['skia_os != "android"', {
        'sources/': [ ['exclude', '_android.(h|cpp)$'],
        ],
      }],
      [ 'skia_os == "android"', {
        'defines': [
          'GR_ANDROID_BUILD=1',
        ],
      }],
      [ 'skia_os == "mac"', {
        'defines': [
          'GR_MAC_BUILD=1',
        ],
      }],
      [ 'skia_os == "linux"', {
        'defines': [
          'GR_LINUX_BUILD=1',
        ],
      }],
      [ 'skia_os == "ios"', {
        'defines': [
          'GR_IOS_BUILD=1',
        ],
      }],
      [ 'skia_os == "win"', {
        'defines': [
          'GR_WIN32_BUILD=1',
          'GR_GL_FUNCTION_TYPE=__stdcall',
        ],
      }],
    ],
    'direct_dependent_settings': {
      'conditions': [
        [ 'skia_os == "android"', {
          'defines': [
            'GR_ANDROID_BUILD=1',
          ],
        }],
        [ 'skia_os == "mac"', {
          'defines': [
            'GR_MAC_BUILD=1',
          ],
        }],
        [ 'skia_os == "linux"', {
          'defines': [
            'GR_LINUX_BUILD=1',
          ],
        }],
        [ 'skia_os == "ios"', {
          'defines': [
            'GR_IOS_BUILD=1',
          ],
        }],
        [ 'skia_os == "win"', {
          'defines': [
            'GR_WIN32_BUILD=1',
            'GR_GL_FUNCTION_TYPE=__stdcall',
          ],
        }],
      ],
      'include_dirs': [
        '../include/gpu',
      ],
    },
  },
  'targets': [
    {
      'target_name': 'skgr',
      'type': 'static_library',
      'include_dirs': [
        '../include/config',
        '../include/core',
        '../src/core',
        '../include/gpu',
      ],
      'sources': [
        '../include/gpu/SkGpuCanvas.h',
        '../include/gpu/SkGpuDevice.h',
        '../include/gpu/SkGr.h',
        '../include/gpu/SkGrTexturePixelRef.h',

        '../src/gpu/GrPrintf_skia.cpp',
        '../src/gpu/SkGpuCanvas.cpp',
        '../src/gpu/SkGpuDevice.cpp',
        '../src/gpu/SkGr.cpp',
        '../src/gpu/SkGrFontScaler.cpp',
        '../src/gpu/SkGrTexturePixelRef.cpp',

        '../src/gpu/mac/SkGLContext_mac.cpp',

        '../src/gpu/win/SkGLContext_win.cpp',

        '../src/gpu/unix/SkGLContext_unix.cpp',

        '../src/gpu/mesa/SkGLContext_mesa.cpp',
      ],
      # Removed for now
      'sources!': [
        '../src/gpu/mesa/SkGLContext_mesa.cpp',
      ],
    },
    {
      'target_name': 'gr',
      'type': 'static_library',
      'include_dirs': [
        '../include/core',
        '../include/config',
        '../include/gpu',
      ],
      'dependencies': [
        'libtess.gyp:libtess',
      ],
      'sources': [
        '../include/gpu/GrAllocator.h',
        '../include/gpu/GrAllocPool.h',
        '../include/gpu/GrAtlas.h',
        '../include/gpu/GrClip.h',
        '../include/gpu/GrClipIterator.h',
        '../include/gpu/GrColor.h',
        '../include/gpu/GrConfig.h',
        '../include/gpu/GrContext.h',
        '../include/gpu/GrFontScaler.h',
        '../include/gpu/GrGLConfig.h',
        '../include/gpu/GrGLConfig_chrome.h',
        '../include/gpu/GrGLInterface.h',
        '../include/gpu/GrGlyph.h',
        '../include/gpu/GrGpuVertex.h',
        '../include/gpu/GrInstanceCounter.h',
        '../include/gpu/GrIPoint.h',
        '../include/gpu/GrKey.h',
        '../include/gpu/GrMatrix.h',
        '../include/gpu/GrMesh.h',
        '../include/gpu/GrNoncopyable.h',
        '../include/gpu/GrPaint.h',
        '../include/gpu/GrPath.h',
        '../include/gpu/GrPathSink.h',
        '../include/gpu/GrPlotMgr.h',
        '../include/gpu/GrPoint.h',
        '../include/gpu/GrRandom.h',
        '../include/gpu/GrRect.h',
        '../include/gpu/GrRectanizer.h',
        '../include/gpu/GrRefCnt.h',
        '../include/gpu/GrRenderTarget.h',
        '../include/gpu/GrResource.h',
        '../include/gpu/GrSamplerState.h',
        '../include/gpu/GrScalar.h',
        '../include/gpu/GrStencil.h',
        '../include/gpu/GrStopwatch.h',
        '../include/gpu/GrStringBuilder.h',
        '../include/gpu/GrTBSearch.h',
        '../include/gpu/GrTDArray.h',
        '../include/gpu/GrTextContext.h',
        '../include/gpu/GrTextStrike.h',
        '../include/gpu/GrTexture.h',
        '../include/gpu/GrTHashCache.h',
        '../include/gpu/GrTLList.h',
        '../include/gpu/GrTypes.h',
        '../include/gpu/GrUserConfig.h',

        '../src/gpu/GrAAHairLinePathRenderer.cpp',
        '../src/gpu/GrAAHairLinePathRenderer.h',
        '../src/gpu/GrAddPathRenderers_aahairline.cpp',
        '../src/gpu/GrAllocPool.cpp',
        '../src/gpu/GrAtlas.cpp',
        '../src/gpu/GrBinHashKey.h',
        '../src/gpu/GrBufferAllocPool.cpp',
        '../src/gpu/GrBufferAllocPool.h',
        '../src/gpu/GrClip.cpp',
        '../src/gpu/GrContext.cpp',
        '../src/gpu/GrDefaultPathRenderer.cpp',
        '../src/gpu/GrDefaultPathRenderer.h',
        '../src/gpu/GrDrawTarget.cpp',
        '../src/gpu/GrDrawTarget.h',
        '../src/gpu/GrGeometryBuffer.h',
        '../src/gpu/GrGLDefaultInterface_none.cpp',
        '../src/gpu/GrGLIndexBuffer.cpp',
        '../src/gpu/GrGLIndexBuffer.h',
        '../src/gpu/GrGLInterface.cpp',
        '../src/gpu/GrGLIRect.h',
        '../src/gpu/GrGLProgram.cpp',
        '../src/gpu/GrGLProgram.h',
        '../src/gpu/GrGLRenderTarget.cpp',
        '../src/gpu/GrGLRenderTarget.h',
        '../src/gpu/GrGLShaderVar.h',
        '../src/gpu/GrGLStencilBuffer.cpp',
        '../src/gpu/GrGLStencilBuffer.h',
        '../src/gpu/GrGLTexture.cpp',
        '../src/gpu/GrGLTexture.h',
        '../src/gpu/GrGLUtil.cpp',
        '../src/gpu/GrGLVertexBuffer.cpp',
        '../src/gpu/GrGLVertexBuffer.h',
        '../src/gpu/GrGpu.cpp',
        '../src/gpu/GrGpu.h',
        '../src/gpu/GrGpuFactory.cpp',
        '../src/gpu/GrGpuGL.cpp',
        '../src/gpu/GrGpuGL.h',
        '../src/gpu/GrGpuGLFixed.cpp',
        '../src/gpu/GrGpuGLFixed.h',
        '../src/gpu/GrGpuGLShaders.cpp',
        '../src/gpu/GrGpuGLShaders.h',
        '../src/gpu/GrIndexBuffer.h',
        '../src/gpu/GrInOrderDrawBuffer.cpp',
        '../src/gpu/GrInOrderDrawBuffer.h',
        '../src/gpu/GrMatrix.cpp',
        '../src/gpu/GrMemory.cpp',
        '../src/gpu/GrPathRendererChain.cpp',
        '../src/gpu/GrPathRendererChain.h',
        '../src/gpu/GrPathRenderer.cpp',
        '../src/gpu/GrPathRenderer.h',
        '../src/gpu/GrPathUtils.cpp',
        '../src/gpu/GrPathUtils.h',
        '../src/gpu/GrRectanizer.cpp',
        '../src/gpu/GrRedBlackTree.h',
        '../src/gpu/GrRenderTarget.cpp',
        '../src/gpu/GrResource.cpp',
        '../src/gpu/GrResourceCache.cpp',
        '../src/gpu/GrResourceCache.h',
        '../src/gpu/GrStencil.cpp',
        '../src/gpu/GrStencilBuffer.cpp',
        '../src/gpu/GrStencilBuffer.h',
        '../src/gpu/GrTesselatedPathRenderer.cpp',
        '../src/gpu/GrTesselatedPathRenderer.h',
        '../src/gpu/GrTextContext.cpp',
        '../src/gpu/GrTextStrike.cpp',
        '../src/gpu/GrTextStrike_impl.h',
        '../src/gpu/GrTexture.cpp',
        '../src/gpu/GrVertexBuffer.h',
        '../src/gpu/gr_unittests.cpp',

        '../src/gpu/mac/GrGLDefaultInterface_mac.cpp',

        '../src/gpu/win/GrGLDefaultInterface_win.cpp',

        '../src/gpu/unix/GrGLDefaultInterface_unix.cpp',

        '../src/gpu/mesa/GrGLDefaultInterface_mesa.cpp',
      ],
      # Removed for now
      'sources!': [
        '../src/gpu/mesa/GrGLDefaultInterface_mesa.cpp',
      ],
      'defines': [
        'GR_IMPLEMENTATION=1',
      ],
      'conditions': [
        [ 'skia_os == "linux"', {
          'sources!': [
            '../src/gpu/GrGLDefaultInterface_none.cpp',
          ],
          'link_settings': {
            'libraries': [
              '-lGL',
              '-lX11',
            ],
          },
        }],
        [ 'skia_os == "mac"', {
          'link_settings': {
            'libraries': [
              '$(SDKROOT)/System/Library/Frameworks/OpenGL.framework',
            ],
          },
          'sources!': [
            '../src/gpu/GrGLDefaultInterface_none.cpp',
          ],
          }],
        [ 'skia_os == "win"', {
          'sources!': [
            '../src/gpu/GrGLDefaultInterface_none.cpp',
          ],
        }],
      ],
    },
  ],
}

# Local Variables:
# tab-width:2
# indent-tabs-mode:nil
# End:
# vim: set expandtab tabstop=2 shiftwidth=2:
