/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PictureRenderer_DEFINED
#define PictureRenderer_DEFINED
#include "SkMath.h"
#include "SkTypes.h"
#include "SkTDArray.h"
#include "SkRefCnt.h"

#if SK_SUPPORT_GPU
#include "GrContextFactory.h"
#include "GrContext.h"
#endif

class SkBitmap;
class SkCanvas;
class SkGLContext;
class SkPicture;
class SkString;

namespace sk_tools {

class PictureRenderer : public SkRefCnt {
public:
    enum SkDeviceTypes {
        kBitmap_DeviceType,
#if SK_SUPPORT_GPU
        kGPU_DeviceType
#endif
    };

    virtual void init(SkPicture* pict);
    virtual void render() = 0;
    virtual void end();
    void resetState();

    void setDeviceType(SkDeviceTypes deviceType) {
        fDeviceType = deviceType;
    }

    bool isUsingBitmapDevice() {
        return kBitmap_DeviceType == fDeviceType;
    }

#if SK_SUPPORT_GPU
    bool isUsingGpuDevice() {
        return kGPU_DeviceType == fDeviceType;
    }

    SkGLContext* getGLContext() {
        if (this->isUsingGpuDevice()) {
            return fGrContextFactory.getGLContext(GrContextFactory::kNative_GLContextType);
        } else {
            return NULL;
        }
    }
#endif

    PictureRenderer()
        : fPicture(NULL)
        , fDeviceType(kBitmap_DeviceType)
#if SK_SUPPORT_GPU
        , fGrContext(fGrContextFactory.get(GrContextFactory::kNative_GLContextType))
#endif
        {}

    bool write(const SkString& path) const;

protected:
    virtual void finishDraw();
    SkCanvas* setupCanvas();
    SkCanvas* setupCanvas(int width, int height);

    SkAutoTUnref<SkCanvas> fCanvas;
    SkPicture* fPicture;
    SkDeviceTypes fDeviceType;

#if SK_SUPPORT_GPU
    GrContextFactory fGrContextFactory;
    GrContext* fGrContext;
#endif

private:
    typedef SkRefCnt INHERITED;
};

class PipePictureRenderer : public PictureRenderer {
public:
    virtual void render() SK_OVERRIDE;

private:
    typedef PictureRenderer INHERITED;
};

class SimplePictureRenderer : public PictureRenderer {
public:
    virtual void render () SK_OVERRIDE;

private:
    typedef PictureRenderer INHERITED;
};

class TiledPictureRenderer : public PictureRenderer {
public:
    TiledPictureRenderer();

    virtual void init(SkPicture* pict) SK_OVERRIDE;
    virtual void render() SK_OVERRIDE;
    virtual void end() SK_OVERRIDE;
    void drawTiles();

    void setTileWidth(int width) {
        fTileWidth = width;
    }

    int getTileWidth() const {
        return fTileWidth;
    }

    void setTileHeight(int height) {
        fTileHeight = height;
    }

    int getTileHeight() const {
        return fTileHeight;
    }

    void setTileWidthPercentage(double percentage) {
        fTileWidthPercentage = percentage;
    }

    double getTileWidthPercentage() const {
        return fTileWidthPercentage;
    }

    void setTileHeightPercentage(double percentage) {
        fTileHeightPercentage = percentage;
    }

    double getTileHeightPercentage() const {
        return fTileHeightPercentage;
    }

    void setTileMinPowerOf2Width(int width) {
        SkASSERT(SkIsPow2(width) && width > 0);
        if (!SkIsPow2(width) || width <= 0) {
            return;
        }

        fTileMinPowerOf2Width = width;
    }

    int getTileMinPowerOf2Width() const {
        return fTileMinPowerOf2Width;
    }

    int numTiles() const {
        return fTiles.count();
    }

    void setMultiThreaded(bool multi) {
        fMultiThreaded = multi;
    }

    bool isMultiThreaded() const {
        return fMultiThreaded;
    }

    void setUsePipe(bool usePipe) {
        fUsePipe = usePipe;
    }

    bool isUsePipe() const {
        return fUsePipe;
    }

    ~TiledPictureRenderer();

protected:
    virtual void finishDraw();

private:
    bool fMultiThreaded;
    bool fUsePipe;
    int fTileWidth;
    int fTileHeight;
    double fTileWidthPercentage;
    double fTileHeightPercentage;
    int fTileMinPowerOf2Width;

    SkTDArray<SkCanvas*> fTiles;

    // Clips the tile to an area that is completely in what the SkPicture says is the
    // drawn-to area. This is mostly important for tiles on the right and bottom edges
    // as they may go over this area and the picture may have some commands that
    // draw outside of this area and so should not actually be written.
    void clipTile(SkCanvas* tile);
    void addTile(int tile_x_start, int tile_y_start, int width, int height);
    void setupTiles();
    void setupPowerOf2Tiles();
    void deleteTiles();
    void copyTilesToCanvas();

    typedef PictureRenderer INHERITED;
};

}

#endif  // PictureRenderer_DEFINED
