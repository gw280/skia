{
  'includes': [
    'common.gypi',
  ],
  'targets': [
    {
      'target_name': 'skgr',
      'type': 'static_library',
      'include_dirs': [
        '../include/config',
        '../include/core',
        '../src/core',
        '../include/gpu',
        '../gpu/include',
      ],
      'sources': [
        '../include/gpu/SkGpuCanvas.h',
        '../include/gpu/SkGpuDevice.h',
        '../include/gpu/SkGpuDeviceFactory.h',
        '../include/gpu/SkGr.h',
        '../include/gpu/SkGrTexturePixelRef.h',

        '../src/gpu/GrPrintf_skia.cpp',
        '../src/gpu/SkGpuCanvas.cpp',
        '../src/gpu/SkGpuDevice.cpp',
        '../src/gpu/SkGr.cpp',
        '../src/gpu/SkGrFontScaler.cpp',
        '../src/gpu/SkGrTexturePixelRef.cpp',
      ],
      'conditions': [
          [ 'skia_os == "linux"', {
          'defines': [
              'GR_LINUX_BUILD=1',
          ],
          }],
          [ 'skia_os == "mac"', {
          'defines': [
              'GR_MAC_BUILD=1',
          ],
          }],
          [ 'skia_os == "win"', {
          'defines': [
              'GR_WIN32_BUILD=1',
          ],
          }],
      ],
      'direct_dependent_settings': {
        'conditions': [
          [ 'skia_os == "linux"', {
            'defines': [
              'GR_LINUX_BUILD=1',
            ],
          }],
          [ 'skia_os == "mac"', {
            'defines': [
              'GR_MAC_BUILD=1',
            ],
          }],
          [ 'skia_os == "win"', {
            'defines': [
              'GR_WIN32_BUILD=1',
            ],
          }],
        ],
        'include_dirs': [
          '../include/gpu',
        ],
      },
    },
    {
      'target_name': 'gr',
      'type': 'static_library',
      'include_dirs': [
        '../gpu/include',
        '../include/core',
        '../include/config',
      ],
      'dependencies': [
        'libtess.gyp:libtess',
      ],
      'sources': [
        '../gpu/include/GrAllocator.h',
        '../gpu/include/GrAllocPool.h',
        '../gpu/include/GrAtlas.h',
        '../gpu/include/GrClip.h',
        '../gpu/include/GrClipIterator.h',
        '../gpu/include/GrColor.h',
        '../gpu/include/GrConfig.h',
        '../gpu/include/GrContext.h',
        '../gpu/include/GrFontScaler.h',
        '../gpu/include/GrGLConfig.h',
        '../gpu/include/GrGLConfig_chrome.h',
        '../gpu/include/GrGLInterface.h',
        '../gpu/include/GrGlyph.h',
        '../gpu/include/GrGpuVertex.h',
        '../gpu/include/GrInstanceCounter.h',
        '../gpu/include/GrIPoint.h',
        '../gpu/include/GrKey.h',
        '../gpu/include/GrMatrix.h',
        '../gpu/include/GrMesh.h',
        '../gpu/include/GrNoncopyable.h',
        '../gpu/include/GrPaint.h',
        '../gpu/include/GrPath.h',
        '../gpu/include/GrPathSink.h',
        '../gpu/include/GrPlotMgr.h',
        '../gpu/include/GrPoint.h',
        '../gpu/include/GrRandom.h',
        '../gpu/include/GrRect.h',
        '../gpu/include/GrRectanizer.h',
        '../gpu/include/GrRefCnt.h',
        '../gpu/include/GrRenderTarget.h',
        '../gpu/include/GrResource.h',
        '../gpu/include/GrSamplerState.h',
        '../gpu/include/GrScalar.h',
        '../gpu/include/GrStencil.h',
        '../gpu/include/GrStopwatch.h',
        '../gpu/include/GrStringBuilder.h',
        '../gpu/include/GrTArray.h',
        '../gpu/include/GrTBSearch.h',
        '../gpu/include/GrTDArray.h',
        '../gpu/include/GrTextContext.h',
        '../gpu/include/GrTextStrike.h',
        '../gpu/include/GrTexture.h',
        '../gpu/include/GrTHashCache.h',
        '../gpu/include/GrTLList.h',
        '../gpu/include/GrTypes.h',
        '../gpu/include/GrUserConfig.h',

        '../gpu/src/GrAAHairLinePathRenderer.cpp',
        '../gpu/src/GrAAHairLinePathRenderer.h',
        '../gpu/src/GrAddPathRenderers_aahairline.cpp',
        '../gpu/src/GrAllocPool.cpp',
        '../gpu/src/GrAtlas.cpp',
        '../gpu/src/GrBinHashKey.h',
        '../gpu/src/GrBufferAllocPool.cpp',
        '../gpu/src/GrBufferAllocPool.h',
        '../gpu/src/GrClip.cpp',
        '../gpu/src/GrContext.cpp',
        '../gpu/src/GrDefaultPathRenderer.cpp',
        '../gpu/src/GrDefaultPathRenderer.h',
        '../gpu/src/GrDrawTarget.cpp',
        '../gpu/src/GrDrawTarget.h',
        '../gpu/src/GrGeometryBuffer.h',
        '../gpu/src/GrGLDefaultInterface_none.cpp',
        '../gpu/src/GrGLIndexBuffer.cpp',
        '../gpu/src/GrGLIndexBuffer.h',
        '../gpu/src/GrGLInterface.cpp',
        '../gpu/src/GrGLIRect.h',
        '../gpu/src/GrGLProgram.cpp',
        '../gpu/src/GrGLProgram.h',
        '../gpu/src/GrGLRenderTarget.cpp',
        '../gpu/src/GrGLRenderTarget.h',
        '../gpu/src/GrGLStencilBuffer.cpp',
        '../gpu/src/GrGLStencilBuffer.h',
        '../gpu/src/GrGLTexture.cpp',
        '../gpu/src/GrGLTexture.h',
        '../gpu/src/GrGLUtil.cpp',
        '../gpu/src/GrGLVertexBuffer.cpp',
        '../gpu/src/GrGLVertexBuffer.h',
        '../gpu/src/GrGpu.cpp',
        '../gpu/src/GrGpu.h',
        '../gpu/src/GrGpuFactory.cpp',
        '../gpu/src/GrGpuGL.cpp',
        '../gpu/src/GrGpuGL.h',
        '../gpu/src/GrGpuGLFixed.cpp',
        '../gpu/src/GrGpuGLFixed.h',
        '../gpu/src/GrGpuGLShaders.cpp',
        '../gpu/src/GrGpuGLShaders.h',
        '../gpu/src/GrIndexBuffer.h',
        '../gpu/src/GrInOrderDrawBuffer.cpp',
        '../gpu/src/GrInOrderDrawBuffer.h',
        '../gpu/src/GrMatrix.cpp',
        '../gpu/src/GrMemory.cpp',
        '../gpu/src/GrPathRendererChain.cpp',
        '../gpu/src/GrPathRendererChain.h',
        '../gpu/src/GrPathRenderer.cpp',
        '../gpu/src/GrPathRenderer.h',
        '../gpu/src/GrPathUtils.cpp',
        '../gpu/src/GrPathUtils.h',
        '../gpu/src/GrRectanizer.cpp',
        '../gpu/src/GrRedBlackTree.h',
        '../gpu/src/GrRenderTarget.cpp',
        '../gpu/src/GrResource.cpp',
        '../gpu/src/GrResourceCache.cpp',
        '../gpu/src/GrResourceCache.h',
        '../gpu/src/GrStencil.cpp',
        '../gpu/src/GrStencilBuffer.cpp',
        '../gpu/src/GrStencilBuffer.h',
        '../gpu/src/GrTesselatedPathRenderer.cpp',
        '../gpu/src/GrTesselatedPathRenderer.h',
        '../gpu/src/GrTextContext.cpp',
        '../gpu/src/GrTextStrike.cpp',
        '../gpu/src/GrTextStrike_impl.h',
        '../gpu/src/GrTexture.cpp',
        '../gpu/src/GrVertexBuffer.h',
        '../gpu/src/gr_unittests.cpp',

        '../gpu/src/mac/GrGLDefaultInterface_mac.cpp',

        '../gpu/src/win/GrGLDefaultInterface_win.cpp',

        '../gpu/src/unix/GrGLDefaultInterface_unix.cpp',

        '../gpu/src/mesa/GrGLDefaultInterface_mesa.cpp',
      ],
      'sources!': [
        '../gpu/src/mesa/GrGLDefaultInterface_mesa.cpp',
      ],
      'defines': [
        'GR_IMPLEMENTATION=1',
        'GR_USE_PLATFORM_CREATE_SAMPLE_COUNT=1',
      ],
      'conditions': [
        [ 'skia_os == "linux"', {
          'defines': [
              'GR_LINUX_BUILD=1',
          ],
          'sources!': [
            '../gpu/src/GrGLDefaultInterface_none.cpp',
          ],
          'link_settings': {
            'libraries': [
              '-lGL',
              '-lX11',
            ],
          },
        }],
        [ 'skia_os == "mac"', {
          'defines': [
              'GR_MAC_BUILD=1',
          ],
          'link_settings': {
            'libraries': [
              '$(SDKROOT)/System/Library/Frameworks/OpenGL.framework',
            ],
          },
          'sources!': [
            '../gpu/src/GrGLDefaultInterface_none.cpp',
          ],
          }],
        [ 'skia_os == "win"', {
          'defines': [
            'GR_WIN32_BUILD=1',
            'GR_GL_FUNCTION_TYPE=__stdcall',
          ],
          'sources!': [
            '../gpu/src/GrGLDefaultInterface_none.cpp',
          ],
        }],
        [ 'skia_os != "win"', {
          'sources!': [
            '../gpu/src/win/GrGLDefaultInterface_win.cpp',
          ],
        }],
        [ 'skia_os != "mac"', {
          'sources!': [
            '../gpu/src/mac/GrGLDefaultInterface_mac.cpp',
          ],
        }],
        [ 'skia_os != "linux"', {
          'sources!': [
            '../gpu/src/unix/GrGLDefaultInterface_unix.cpp',
          ],
        }],
      ],
      'direct_dependent_settings': {
        'conditions': [
          [ 'skia_os == "linux"', {
            'defines': [
              'GR_LINUX_BUILD=1',
            ],
          }],
          [ 'skia_os == "mac"', {
            'defines': [
              'GR_MAC_BUILD=1',
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
          '../gpu/include',
        ],
      },
    },
  ],
}

# Local Variables:
# tab-width:2
# indent-tabs-mode:nil
# End:
# vim: set expandtab tabstop=2 shiftwidth=2:
