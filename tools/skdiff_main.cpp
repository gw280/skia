
/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "SkColorPriv.h"
#include "SkData.h"
#include "SkImageDecoder.h"
#include "SkImageEncoder.h"
#include "SkOSFile.h"
#include "SkStream.h"
#include "SkTDArray.h"
#include "SkTemplates.h"
#include "SkTime.h"
#include "SkTSearch.h"
#include "SkTypes.h"

/**
 * skdiff
 *
 * Given three directory names, expects to find identically-named files in
 * each of the first two; the first are treated as a set of baseline,
 * the second a set of variant images, and a diff image is written into the
 * third directory for each pair.
 * Creates an index.html in the current third directory to compare each
 * pair that does not match exactly.
 * Does *not* recursively descend directories.
 */

#if SK_BUILD_FOR_WIN32
    #define PATH_DIV_STR "\\"
    #define PATH_DIV_CHAR '\\'
#else
    #define PATH_DIV_STR "/"
    #define PATH_DIV_CHAR '/'
#endif

// Result of comparison for each pair of files.
enum Result {
    kEqualBits,      // both files in the pair contain exactly the same bits
    kEqualPixels,    // not bitwise equal, but their pixels are exactly the same
    kDifferentSizes, // both are images we can parse, but of different sizes
    kDifferentPixels,// both are images we can parse, but with different pixels
    kDifferentOther, // files have different bits but are not parsable images
    kBaseMissing,      // missing from baseDir
    kComparisonMissing,// missing from comparisonDir
    kUnknown
};

struct DiffRecord {
    DiffRecord (const SkString filename,
                const SkString basePath,
                const SkString comparisonPath,
                const Result result = kUnknown)
        : fFilename (filename)
        , fBasePath (basePath)
        , fComparisonPath (comparisonPath)
        , fBaseBitmap (new SkBitmap ())
        , fComparisonBitmap (new SkBitmap ())
        , fDifferenceBitmap (new SkBitmap ())
        , fWhiteBitmap (new SkBitmap ())
        , fBaseHeight (0)
        , fBaseWidth (0)
        , fFractionDifference (0)
        , fWeightedFraction (0)
        , fAverageMismatchR (0)
        , fAverageMismatchG (0)
        , fAverageMismatchB (0)
        , fMaxMismatchR (0)
        , fMaxMismatchG (0)
        , fMaxMismatchB (0)
        , fResult(result) {
    };

    SkString fFilename;
    SkString fBasePath;
    SkString fComparisonPath;

    SkBitmap* fBaseBitmap;
    SkBitmap* fComparisonBitmap;
    SkBitmap* fDifferenceBitmap;
    SkBitmap* fWhiteBitmap;

    int fBaseHeight;
    int fBaseWidth;

    /// Arbitrary floating-point metric to be used to sort images from most
    /// to least different from baseline; values of 0 will be omitted from the
    /// summary webpage.
    float fFractionDifference;
    float fWeightedFraction;

    float fAverageMismatchR;
    float fAverageMismatchG;
    float fAverageMismatchB;

    uint32_t fMaxMismatchR;
    uint32_t fMaxMismatchG;
    uint32_t fMaxMismatchB;

    /// Which category of diff result.
    Result fResult;
};

#define MAX2(a,b) (((b) < (a)) ? (a) : (b))
#define MAX3(a,b,c) (((b) < (a)) ? MAX2((a), (c)) : MAX2((b), (c)))

const SkPMColor PMCOLOR_WHITE = SkPreMultiplyColor(SK_ColorWHITE);
const SkPMColor PMCOLOR_BLACK = SkPreMultiplyColor(SK_ColorBLACK);

typedef SkTDArray<SkString*> StringArray;
typedef StringArray FileArray;

struct DiffSummary {
    DiffSummary ()
        : fNumMatches (0)
        , fNumMismatches (0)
        , fMaxMismatchV (0)
        , fMaxMismatchPercent (0) { };

    ~DiffSummary() {
        fBaseMissing.deleteAll();
        fComparisonMissing.deleteAll();
    }

    uint32_t fNumMatches;
    uint32_t fNumMismatches;
    uint32_t fMaxMismatchV;
    float fMaxMismatchPercent;

    FileArray fBaseMissing;
    FileArray fComparisonMissing;

    void print () {
        int n = fBaseMissing.count();
        if (n > 0) {
            printf("Missing in baseDir:\n");
            for (int i = 0; i < n; ++i) {
                printf("\t%s\n", fBaseMissing[i]->c_str());
            }
        }
        n = fComparisonMissing.count();
        if (n > 0) {
            printf("Missing in comparisonDir:\n");
            for (int i = 0; i < n; ++i) {
                printf("\t%s\n", fComparisonMissing[i]->c_str());
            }
        }
        printf("%d of %d images matched.\n", fNumMatches,
               fNumMatches + fNumMismatches);
        if (fNumMismatches > 0) {
            printf("Maximum pixel intensity mismatch %d\n", fMaxMismatchV);
            printf("Largest area mismatch was %.2f%% of pixels\n",
                   fMaxMismatchPercent);
        }

    }

    void add (DiffRecord* drp) {
        uint32_t mismatchValue;

        switch (drp->fResult) {
          case kEqualBits:
            fNumMatches++;
            break;
          case kEqualPixels:
            fNumMatches++;
            break;
          case kDifferentSizes:
            fNumMismatches++;
            drp->fFractionDifference = 2.0;// sort as if 200% of pixels differed
            break;
          case kDifferentPixels:
            fNumMismatches++;
            if (drp->fFractionDifference * 100 > fMaxMismatchPercent) {
                fMaxMismatchPercent = drp->fFractionDifference * 100;
            }
            mismatchValue = MAX3(drp->fMaxMismatchR, drp->fMaxMismatchG,
                                 drp->fMaxMismatchB);
            if (mismatchValue > fMaxMismatchV) {
                fMaxMismatchV = mismatchValue;
            }
            break;
          case kDifferentOther:
            fNumMismatches++;
            drp->fFractionDifference = 3.0;// sort as if 300% of pixels differed
            break;
          case kBaseMissing:
            fNumMismatches++;
            fBaseMissing.push(new SkString(drp->fFilename));
            break;
          case kComparisonMissing:
            fNumMismatches++;
            fComparisonMissing.push(new SkString(drp->fFilename));
            break;
          case kUnknown:
            SkDEBUGFAIL("adding uncategorized DiffRecord");
            break;
          default:
            SkDEBUGFAIL("adding DiffRecord with unhandled fResult value");
            break;
        }
    }
};

typedef SkTDArray<DiffRecord*> RecordArray;

/// Comparison routine for qsort;  sorts by fFractionDifference
/// from largest to smallest.
static int compare_diff_metrics (DiffRecord** lhs, DiffRecord** rhs) {
    if ((*lhs)->fFractionDifference < (*rhs)->fFractionDifference) {
        return 1;
    }
    if ((*rhs)->fFractionDifference < (*lhs)->fFractionDifference) {
        return -1;
    }
    return 0;
}

static int compare_diff_weighted (DiffRecord** lhs, DiffRecord** rhs) {
    if ((*lhs)->fWeightedFraction < (*rhs)->fWeightedFraction) {
        return 1;
    }
    if ((*lhs)->fWeightedFraction > (*rhs)->fWeightedFraction) {
        return -1;
    }
    return 0;
}

/// Comparison routine for qsort;  sorts by max(fAverageMismatch{RGB})
/// from largest to smallest.
static int compare_diff_mean_mismatches (DiffRecord** lhs, DiffRecord** rhs) {
    float leftValue = MAX3((*lhs)->fAverageMismatchR,
                           (*lhs)->fAverageMismatchG,
                           (*lhs)->fAverageMismatchB);
    float rightValue = MAX3((*rhs)->fAverageMismatchR,
                            (*rhs)->fAverageMismatchG,
                            (*rhs)->fAverageMismatchB);
    if (leftValue < rightValue) {
        return 1;
    }
    if (rightValue < leftValue) {
        return -1;
    }
    return 0;
}

/// Comparison routine for qsort;  sorts by max(fMaxMismatch{RGB})
/// from largest to smallest.
static int compare_diff_max_mismatches (DiffRecord** lhs, DiffRecord** rhs) {
    uint32_t leftValue = MAX3((*lhs)->fMaxMismatchR,
                              (*lhs)->fMaxMismatchG,
                              (*lhs)->fMaxMismatchB);
    uint32_t rightValue = MAX3((*rhs)->fMaxMismatchR,
                               (*rhs)->fMaxMismatchG,
                               (*rhs)->fMaxMismatchB);
    if (leftValue < rightValue) {
        return 1;
    }
    if (rightValue < leftValue) {
        return -1;
    }
    return compare_diff_mean_mismatches(lhs, rhs);
}



/// Parameterized routine to compute the color of a pixel in a difference image.
typedef SkPMColor (*DiffMetricProc)(SkPMColor, SkPMColor);

static void expand_and_copy (int width, int height, SkBitmap** dest) {
    SkBitmap* temp = new SkBitmap ();
    temp->reset();
    temp->setConfig((*dest)->config(), width, height);
    temp->allocPixels();
    (*dest)->copyPixelsTo(temp->getPixels(), temp->getSize(),
                          temp->rowBytes());
    *dest = temp;
}

/// Returns true if the two buffers passed in are both non-NULL, and include
/// exactly the same byte values (and identical lengths).
static bool are_buffers_equal(SkData* skdata1, SkData* skdata2) {
    if ((NULL == skdata1) || (NULL == skdata2)) {
        return false;
    }
    if (skdata1->size() != skdata2->size()) {
        return false;
    }
    return (0 == memcmp(skdata1->data(), skdata2->data(), skdata1->size()));
}

/// Reads the file at the given path and returns its complete contents as an
/// SkData object (or returns NULL on error).
static SkData* read_file(const char* file_path) {
    SkFILEStream fileStream(file_path);
    if (!fileStream.isValid()) {
        SkDebugf("WARNING: could not open file <%s> for reading\n", file_path);
        return NULL;
    }
    size_t bytesInFile = fileStream.getLength();
    size_t bytesLeftToRead = bytesInFile;

    void* bufferStart = sk_malloc_throw(bytesInFile);
    char* bufferPointer = (char*)bufferStart;
    while (bytesLeftToRead > 0) {
        size_t bytesReadThisTime = fileStream.read(
            bufferPointer, bytesLeftToRead);
        if (0 == bytesReadThisTime) {
            SkDebugf("WARNING: error reading from <%s>\n", file_path);
            sk_free(bufferStart);
            return NULL;
        }
        bytesLeftToRead -= bytesReadThisTime;
        bufferPointer += bytesReadThisTime;
    }
    return SkData::NewFromMalloc(bufferStart, bytesInFile);
}

/// Decodes binary contents of baseFile and comparisonFile into
/// diffRecord->fBaseBitmap and diffRecord->fComparisonBitmap.
/// Returns true if that succeeds.
static bool get_bitmaps (SkData* baseFileContents,
                         SkData* comparisonFileContents,
                         DiffRecord* diffRecord) {
    SkMemoryStream compareStream(comparisonFileContents->data(),
                                 comparisonFileContents->size());
    SkMemoryStream baseStream(baseFileContents->data(),
                              baseFileContents->size());

    SkImageDecoder* codec = SkImageDecoder::Factory(&baseStream);
    if (NULL == codec) {
        SkDebugf("ERROR: no codec found for basePath <%s>\n",
                 diffRecord->fBasePath.c_str());
        return false;
    }

    // In debug, the DLL will automatically be unloaded when this is deleted,
    // but that shouldn't be a problem in release mode.
    SkAutoTDelete<SkImageDecoder> ad(codec);

    baseStream.rewind();
    if (!codec->decode(&baseStream, diffRecord->fBaseBitmap,
                       SkBitmap::kARGB_8888_Config,
                       SkImageDecoder::kDecodePixels_Mode)) {
        SkDebugf("ERROR: codec failed for basePath <%s>\n",
                 diffRecord->fBasePath.c_str());
        return false;
    }

    diffRecord->fBaseWidth = diffRecord->fBaseBitmap->width();
    diffRecord->fBaseHeight = diffRecord->fBaseBitmap->height();

    if (!codec->decode(&compareStream, diffRecord->fComparisonBitmap,
                       SkBitmap::kARGB_8888_Config,
                       SkImageDecoder::kDecodePixels_Mode)) {
        SkDebugf("ERROR: codec failed for comparisonPath <%s>\n",
                 diffRecord->fComparisonPath.c_str());
        return false;
    }

    return true;
}

static bool get_bitmap_height_width(const SkString& path,
                                    int *height, int *width) {
    SkFILEStream stream(path.c_str());
    if (!stream.isValid()) {
        SkDebugf("ERROR: couldn't open file <%s>\n",
                 path.c_str());
        return false;
    }

    SkImageDecoder* codec = SkImageDecoder::Factory(&stream);
    if (NULL == codec) {
        SkDebugf("ERROR: no codec found for <%s>\n",
                 path.c_str());
        return false;
    }

    SkAutoTDelete<SkImageDecoder> ad(codec);
    SkBitmap bm;

    stream.rewind();
    if (!codec->decode(&stream, &bm,
                       SkBitmap::kARGB_8888_Config,
                       SkImageDecoder::kDecodePixels_Mode)) {
        SkDebugf("ERROR: codec failed for <%s>\n",
                 path.c_str());
        return false;
    }

    *height = bm.height();
    *width = bm.width();

    return true;
}

// from gm - thanks to PNG, we need to force all pixels 100% opaque
static void force_all_opaque(const SkBitmap& bitmap) {
   SkAutoLockPixels lock(bitmap);
   for (int y = 0; y < bitmap.height(); y++) {
       for (int x = 0; x < bitmap.width(); x++) {
           *bitmap.getAddr32(x, y) |= (SK_A32_MASK << SK_A32_SHIFT);
       }
   }
}

// from gm
static bool write_bitmap(const SkString& path, const SkBitmap* bitmap) {
    SkBitmap copy;
    bitmap->copyTo(&copy, SkBitmap::kARGB_8888_Config);
    force_all_opaque(copy);
    return SkImageEncoder::EncodeFile(path.c_str(), copy,
                                      SkImageEncoder::kPNG_Type, 100);
}

// from gm
static inline SkPMColor compute_diff_pmcolor(SkPMColor c0, SkPMColor c1) {
    int dr = SkGetPackedR32(c0) - SkGetPackedR32(c1);
    int dg = SkGetPackedG32(c0) - SkGetPackedG32(c1);
    int db = SkGetPackedB32(c0) - SkGetPackedB32(c1);

    return SkPackARGB32(0xFF, SkAbs32(dr), SkAbs32(dg), SkAbs32(db));
}

static inline bool colors_match_thresholded(SkPMColor c0, SkPMColor c1,
                                            const int threshold) {
    int da = SkGetPackedA32(c0) - SkGetPackedA32(c1);
    int dr = SkGetPackedR32(c0) - SkGetPackedR32(c1);
    int dg = SkGetPackedG32(c0) - SkGetPackedG32(c1);
    int db = SkGetPackedB32(c0) - SkGetPackedB32(c1);

    return ((SkAbs32(da) <= threshold) &&
            (SkAbs32(dr) <= threshold) &&
            (SkAbs32(dg) <= threshold) &&
            (SkAbs32(db) <= threshold));
}

// based on gm
// Postcondition: when we exit this method, dr->fResult should have some value
// other than kUnknown.
static void compute_diff(DiffRecord* dr,
                         DiffMetricProc diffFunction,
                         const int colorThreshold) {
    SkAutoLockPixels alpDiff(*dr->fDifferenceBitmap);
    SkAutoLockPixels alpWhite(*dr->fWhiteBitmap);

    const int w = dr->fComparisonBitmap->width();
    const int h = dr->fComparisonBitmap->height();
    int mismatchedPixels = 0;
    int totalMismatchR = 0;
    int totalMismatchG = 0;
    int totalMismatchB = 0;

    if (w != dr->fBaseWidth || h != dr->fBaseHeight) {
        dr->fResult = kDifferentSizes;
        return;
    }
    // Accumulate fractionally different pixels, then divide out
    // # of pixels at the end.
    dr->fWeightedFraction = 0;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            SkPMColor c0 = *dr->fBaseBitmap->getAddr32(x, y);
            SkPMColor c1 = *dr->fComparisonBitmap->getAddr32(x, y);
            SkPMColor trueDifference = compute_diff_pmcolor(c0, c1);
            SkPMColor outputDifference = diffFunction(c0, c1);
            uint32_t thisR = SkGetPackedR32(trueDifference);
            uint32_t thisG = SkGetPackedG32(trueDifference);
            uint32_t thisB = SkGetPackedB32(trueDifference);
            totalMismatchR += thisR;
            totalMismatchG += thisG;
            totalMismatchB += thisB;
            // In HSV, value is defined as max RGB component.
            int value = MAX3(thisR, thisG, thisB);
            dr->fWeightedFraction += ((float) value) / 255;
            if (thisR > dr->fMaxMismatchR) {
                dr->fMaxMismatchR = thisR;
            }
            if (thisG > dr->fMaxMismatchG) {
                dr->fMaxMismatchG = thisG;
            }
            if (thisB > dr->fMaxMismatchB) {
                dr->fMaxMismatchB = thisB;
            }
            if (!colors_match_thresholded(c0, c1, colorThreshold)) {
                mismatchedPixels++;
                *dr->fDifferenceBitmap->getAddr32(x, y) = outputDifference;
                *dr->fWhiteBitmap->getAddr32(x, y) = PMCOLOR_WHITE;
            } else {
                *dr->fDifferenceBitmap->getAddr32(x, y) = 0;
                *dr->fWhiteBitmap->getAddr32(x, y) = PMCOLOR_BLACK;
            }
        }
    }
    if (0 == mismatchedPixels) {
        dr->fResult = kEqualPixels;
        return;
    }
    dr->fResult = kDifferentPixels;
    int pixelCount = w * h;
    dr->fFractionDifference = ((float) mismatchedPixels) / pixelCount;
    dr->fWeightedFraction /= pixelCount;
    dr->fAverageMismatchR = ((float) totalMismatchR) / pixelCount;
    dr->fAverageMismatchG = ((float) totalMismatchG) / pixelCount;
    dr->fAverageMismatchB = ((float) totalMismatchB) / pixelCount;
}

static SkString filename_to_derived_filename (const SkString& filename,
                                              const char *suffix) {
    SkString diffName (filename);
    const char* cstring = diffName.c_str();
    int dotOffset = strrchr(cstring, '.') - cstring;
    diffName.remove(dotOffset, diffName.size() - dotOffset);
    diffName.append(suffix);
    return diffName;
}

/// Given a image filename, returns the name of the file containing the
/// associated difference image.
static SkString filename_to_diff_filename (const SkString& filename) {
    return filename_to_derived_filename(filename, "-diff.png");
}

/// Given a image filename, returns the name of the file containing the
/// "white" difference image.
static SkString filename_to_white_filename (const SkString& filename) {
    return filename_to_derived_filename(filename, "-white.png");
}

static void release_bitmaps(DiffRecord* drp) {
    delete drp->fBaseBitmap;
    drp->fBaseBitmap = NULL;
    delete drp->fComparisonBitmap;
    drp->fComparisonBitmap = NULL;
    delete drp->fDifferenceBitmap;
    drp->fDifferenceBitmap = NULL;
    delete drp->fWhiteBitmap;
    drp->fWhiteBitmap = NULL;
}


/// If outputDir.isEmpty(), don't write out diff files.
static void create_and_write_diff_image(DiffRecord* drp,
                                        DiffMetricProc dmp,
                                        const int colorThreshold,
                                        const SkString& outputDir,
                                        const SkString& filename) {
    const int w = drp->fBaseWidth;
    const int h = drp->fBaseHeight;
    drp->fDifferenceBitmap->setConfig(SkBitmap::kARGB_8888_Config, w, h);
    drp->fDifferenceBitmap->allocPixels();
    drp->fWhiteBitmap->setConfig(SkBitmap::kARGB_8888_Config, w, h);
    drp->fWhiteBitmap->allocPixels();

    SkASSERT(kUnknown == drp->fResult);
    compute_diff(drp, dmp, colorThreshold);
    SkASSERT(kUnknown != drp->fResult);

    if ((kDifferentPixels == drp->fResult) && !outputDir.isEmpty()) {
        SkString differencePath (outputDir);
        differencePath.append(filename_to_diff_filename(filename));
        write_bitmap(differencePath, drp->fDifferenceBitmap);
        SkString whitePath (outputDir);
        whitePath.append(filename_to_white_filename(filename));
        write_bitmap(whitePath, drp->fWhiteBitmap);
    }

    release_bitmaps(drp);
}

/// Returns true if string contains any of these substrings.
static bool string_contains_any_of(const SkString& string,
                                   const StringArray& substrings) {
    for (int i = 0; i < substrings.count(); i++) {
        if (string.contains(substrings[i]->c_str())) {
            return true;
        }
    }
    return false;
}

/// Iterate over dir and get all files that:
///  - match any of the substrings in matchSubstrings, but...
///  - DO NOT match any of the substrings in nomatchSubstrings
/// Returns the list of files in *files.
static void get_file_list(const SkString& dir,
                          const StringArray& matchSubstrings,
                          const StringArray& nomatchSubstrings,
                          FileArray *files) {
    SkOSFile::Iter it(dir.c_str());
    SkString filename;
    while (it.next(&filename)) {
        if (string_contains_any_of(filename, matchSubstrings) &&
            !string_contains_any_of(filename, nomatchSubstrings)) {
            files->push(new SkString(filename));
        }
    }
}

static void release_file_list(FileArray *files) {
    files->deleteAll();
}

/// Comparison routines for qsort, sort by file names.
static int compare_file_name_metrics(SkString **lhs, SkString **rhs) {
    return strcmp((*lhs)->c_str(), (*rhs)->c_str());
}

/// Creates difference images, returns the number that have a 0 metric.
/// If outputDir.isEmpty(), don't write out diff files.
static void create_diff_images (DiffMetricProc dmp,
                                const int colorThreshold,
                                RecordArray* differences,
                                const SkString& baseDir,
                                const SkString& comparisonDir,
                                const SkString& outputDir,
                                const StringArray& matchSubstrings,
                                const StringArray& nomatchSubstrings,
                                DiffSummary* summary) {
    SkASSERT(!baseDir.isEmpty());
    SkASSERT(!comparisonDir.isEmpty());

    FileArray baseFiles;
    FileArray comparisonFiles;

    get_file_list(baseDir, matchSubstrings, nomatchSubstrings, &baseFiles);
    get_file_list(comparisonDir, matchSubstrings, nomatchSubstrings,
                  &comparisonFiles);

    if (!baseFiles.isEmpty()) {
        qsort(baseFiles.begin(), baseFiles.count(), sizeof(SkString*),
              SkCastForQSort(compare_file_name_metrics));
    }
    if (!comparisonFiles.isEmpty()) {
        qsort(comparisonFiles.begin(), comparisonFiles.count(),
              sizeof(SkString*), SkCastForQSort(compare_file_name_metrics));
    }

    int i = 0;
    int j = 0;

    while (i < baseFiles.count() &&
           j < comparisonFiles.count()) {

        SkString basePath (baseDir);
        basePath.append(*baseFiles[i]);
        SkString comparisonPath (comparisonDir);
        comparisonPath.append(*comparisonFiles[j]);

        DiffRecord *drp = NULL;
        int v = strcmp(baseFiles[i]->c_str(),
                       comparisonFiles[j]->c_str());

        if (v < 0) {
            // in baseDir, but not in comparisonDir
            drp = new DiffRecord(*baseFiles[i], basePath, comparisonPath,
                                 kComparisonMissing);
            ++i;
        } else if (v > 0) {
            // in comparisonDir, but not in baseDir
            drp = new DiffRecord(*comparisonFiles[j], basePath, comparisonPath,
                                 kBaseMissing);
            ++j;
        } else {
            // Found the same filename in both baseDir and comparisonDir.
            drp = new DiffRecord(*baseFiles[i], basePath, comparisonPath);
            SkASSERT(kUnknown == drp->fResult);

            SkData* baseFileBits;
            SkData* comparisonFileBits;
            if (NULL == (baseFileBits = read_file(basePath.c_str()))) {
                SkDebugf("WARNING: couldn't read base file <%s>\n",
                         basePath.c_str());
                drp->fResult = kBaseMissing;
            } else if (NULL == (comparisonFileBits = read_file(
                comparisonPath.c_str()))) {
                SkDebugf("WARNING: couldn't read comparison file <%s>\n",
                         comparisonPath.c_str());
                drp->fResult = kComparisonMissing;
            } else {
                if (are_buffers_equal(baseFileBits, comparisonFileBits)) {
                    drp->fResult = kEqualBits;
                } else if (get_bitmaps(baseFileBits, comparisonFileBits, drp)) {
                    create_and_write_diff_image(drp, dmp, colorThreshold,
                                                outputDir, *baseFiles[i]);
                } else {
                    drp->fResult = kDifferentOther;
                }
            }
            if (baseFileBits) {
                baseFileBits->unref();
            }
            if (comparisonFileBits) {
                comparisonFileBits->unref();
            }
            ++i;
            ++j;
        }
        SkASSERT(kUnknown != drp->fResult);
        differences->push(drp);
        summary->add(drp);
    }

    for (; i < baseFiles.count(); ++i) {
        // files only in baseDir
        SkString basePath (baseDir);
        basePath.append(*baseFiles[i]);
        SkString comparisonPath;
        DiffRecord *drp = new DiffRecord(*baseFiles[i], basePath,
                                         comparisonPath, kComparisonMissing);
        differences->push(drp);
        summary->add(drp);
    }

    for (; j < comparisonFiles.count(); ++j) {
        // files only in comparisonDir
        SkString basePath;
        SkString comparisonPath(comparisonDir);
        comparisonPath.append(*comparisonFiles[j]);
        DiffRecord *drp = new DiffRecord(*comparisonFiles[j], basePath,
                                         comparisonPath, kBaseMissing);
        differences->push(drp);
        summary->add(drp);
    }

    release_file_list(&baseFiles);
    release_file_list(&comparisonFiles);
}

/// Make layout more consistent by scaling image to 240 height, 360 width,
/// or natural size, whichever is smallest.
static int compute_image_height (int height, int width) {
    int retval = 240;
    if (height < retval) {
        retval = height;
    }
    float scale = (float) retval / height;
    if (width * scale > 360) {
        scale = (float) 360 / width;
        retval = static_cast<int>(height * scale);
    }
    return retval;
}

static void print_table_header (SkFILEWStream* stream,
                                const int matchCount,
                                const int colorThreshold,
                                const RecordArray& differences,
                                const SkString &baseDir,
                                const SkString &comparisonDir,
                                bool doOutputDate=false) {
    stream->writeText("<table>\n");
    stream->writeText("<tr><th>");
    if (doOutputDate) {
        SkTime::DateTime dt;
        SkTime::GetDateTime(&dt);
        stream->writeText("SkDiff run at ");
        stream->writeDecAsText(dt.fHour);
        stream->writeText(":");
        if (dt.fMinute < 10) {
            stream->writeText("0");
        }
        stream->writeDecAsText(dt.fMinute);
        stream->writeText(":");
        if (dt.fSecond < 10) {
            stream->writeText("0");
        }
        stream->writeDecAsText(dt.fSecond);
        stream->writeText("<br>");
    }
    stream->writeDecAsText(matchCount);
    stream->writeText(" of ");
    stream->writeDecAsText(differences.count());
    stream->writeText(" images matched ");
    if (colorThreshold == 0) {
        stream->writeText("exactly");
    } else {
        stream->writeText("within ");
        stream->writeDecAsText(colorThreshold);
        stream->writeText(" color units per component");
    }
    stream->writeText(".<br>");
    stream->writeText("</th>\n<th>");
    stream->writeText("every different pixel shown in white");
    stream->writeText("</th>\n<th>");
    stream->writeText("color difference at each pixel");
    stream->writeText("</th>\n<th>");
    stream->writeText(baseDir.c_str());
    stream->writeText("</th>\n<th>");
    stream->writeText(comparisonDir.c_str());
    stream->writeText("</th>\n");
    stream->writeText("</tr>\n");
}

static void print_pixel_count (SkFILEWStream* stream,
                               const DiffRecord& diff) {
    stream->writeText("<br>(");
    stream->writeDecAsText(static_cast<int>(diff.fFractionDifference *
                                            diff.fBaseWidth *
                                            diff.fBaseHeight));
    stream->writeText(" pixels)");
/*
    stream->writeDecAsText(diff.fWeightedFraction *
                           diff.fBaseWidth *
                           diff.fBaseHeight);
    stream->writeText(" weighted pixels)");
*/
}

static void print_label_cell (SkFILEWStream* stream,
                              const DiffRecord& diff) {
    char metricBuf [20];

    stream->writeText("<td><b>");
    stream->writeText(diff.fFilename.c_str());
    stream->writeText("</b><br>");
    switch (diff.fResult) {
      case kEqualBits:
        SkDEBUGFAIL("should not encounter DiffRecord with kEqualBits here");
        return;
      case kEqualPixels:
        SkDEBUGFAIL("should not encounter DiffRecord with kEqualPixels here");
        return;
      case kDifferentSizes:
        stream->writeText("Image sizes differ</td>");
        return;
      case kDifferentPixels:
        sprintf(metricBuf, "%12.4f%%", 100 * diff.fFractionDifference);
        stream->writeText(metricBuf);
        stream->writeText(" of pixels differ");
        stream->writeText("\n  (");
        sprintf(metricBuf, "%12.4f%%", 100 * diff.fWeightedFraction);
        stream->writeText(metricBuf);
        stream->writeText(" weighted)");
        // Write the actual number of pixels that differ if it's < 1%
        if (diff.fFractionDifference < 0.01) {
            print_pixel_count(stream, diff);
        }
        stream->writeText("<br>Average color mismatch ");
        stream->writeDecAsText(static_cast<int>(MAX3(diff.fAverageMismatchR,
                                                     diff.fAverageMismatchG,
                                                     diff.fAverageMismatchB)));
        stream->writeText("<br>Max color mismatch ");
        stream->writeDecAsText(MAX3(diff.fMaxMismatchR,
                                    diff.fMaxMismatchG,
                                    diff.fMaxMismatchB));
        stream->writeText("</td>");
        break;
      case kDifferentOther:
        stream->writeText("Files differ; unable to parse one or both files</td>");
        return;
      case kBaseMissing:
        stream->writeText("Missing from baseDir</td>");
        return;
      case kComparisonMissing:
        stream->writeText("Missing from comparisonDir</td>");
        return;
      default:
        SkDEBUGFAIL("encountered DiffRecord with unknown result type");
        return;
    }
}

static void print_image_cell (SkFILEWStream* stream,
                              const SkString& path,
                              int height) {
    stream->writeText("<td><a href=\"");
    stream->writeText(path.c_str());
    stream->writeText("\"><img src=\"");
    stream->writeText(path.c_str());
    stream->writeText("\" height=\"");
    stream->writeDecAsText(height);
    stream->writeText("px\"></a></td>");
}

static void print_text_cell (SkFILEWStream* stream, const char* text) {
    stream->writeText("<td align=center>");
    if (NULL != text) {
        stream->writeText(text);
    }
    stream->writeText("</td>");
}

static void print_diff_with_missing_file(SkFILEWStream* stream,
                                         DiffRecord& diff,
                                         const SkString& relativePath) {
    stream->writeText("<tr>\n");
    print_label_cell(stream, diff);
    stream->writeText("<td>N/A</td>");
    stream->writeText("<td>N/A</td>");
    if (kBaseMissing != diff.fResult) {
        int h, w;
        if (!get_bitmap_height_width(diff.fBasePath, &h, &w)) {
            stream->writeText("<td>N/A</td>");
        } else {
            int height = compute_image_height(h, w);
            if (!diff.fBasePath.startsWith(PATH_DIV_STR)) {
                diff.fBasePath.prepend(relativePath);
            }
            print_image_cell(stream, diff.fBasePath, height);
        }
    } else {
        stream->writeText("<td>N/A</td>");
    }
    if (kComparisonMissing != diff.fResult) {
        int h, w;
        if (!get_bitmap_height_width(diff.fComparisonPath, &h, &w)) {
            stream->writeText("<td>N/A</td>");
        } else {
            int height = compute_image_height(h, w);
            if (!diff.fComparisonPath.startsWith(PATH_DIV_STR)) {
                diff.fComparisonPath.prepend(relativePath);
            }
            print_image_cell(stream, diff.fComparisonPath, height);
        }
    } else {
        stream->writeText("<td>N/A</td>");
    }
    stream->writeText("</tr>\n");
    stream->flush();
}

static void print_diff_page (const int matchCount,
                             const int colorThreshold,
                             const RecordArray& differences,
                             const SkString& baseDir,
                             const SkString& comparisonDir,
                             const SkString& outputDir) {

    SkASSERT(!baseDir.isEmpty());
    SkASSERT(!comparisonDir.isEmpty());
    SkASSERT(!outputDir.isEmpty());

    SkString outputPath (outputDir);
    outputPath.append("index.html");
    //SkFILEWStream outputStream ("index.html");
    SkFILEWStream outputStream (outputPath.c_str());

    // Need to convert paths from relative-to-cwd to relative-to-outputDir
    // FIXME this doesn't work if there are '..' inside the outputDir
    unsigned int ui;
    SkString relativePath;
    for (ui = 0; ui < outputDir.size(); ui++) {
        if (outputDir[ui] == PATH_DIV_CHAR) {
            relativePath.append(".." PATH_DIV_STR);
        }
    }

    outputStream.writeText("<html>\n<body>\n");
    print_table_header(&outputStream, matchCount, colorThreshold, differences,
                       baseDir, comparisonDir);
    int i;
    for (i = 0; i < differences.count(); i++) {
        DiffRecord* diff = differences[i];

        switch (diff->fResult) {
          // Cases in which there is no diff to report.
          case kEqualBits:
          case kEqualPixels:
            continue;
          // Cases in which we want a detailed pixel diff.
          case kDifferentPixels:
            break;
          // Cases in which the files differed, but we can't display the diff.
          case kDifferentSizes:
          case kDifferentOther:
          case kBaseMissing:
          case kComparisonMissing:
            print_diff_with_missing_file(&outputStream, *diff, relativePath);
            continue;
          default:
            SkDEBUGFAIL("encountered DiffRecord with unknown result type");
            continue;
        }

        if (!diff->fBasePath.startsWith(PATH_DIV_STR)) {
            diff->fBasePath.prepend(relativePath);
        }
        if (!diff->fComparisonPath.startsWith(PATH_DIV_STR)) {
            diff->fComparisonPath.prepend(relativePath);
        }

        int height = compute_image_height(diff->fBaseHeight, diff->fBaseWidth);
        outputStream.writeText("<tr>\n");
        print_label_cell(&outputStream, *diff);
        print_image_cell(&outputStream,
                         filename_to_white_filename(diff->fFilename), height);
        print_image_cell(&outputStream,
                         filename_to_diff_filename(diff->fFilename), height);
        print_image_cell(&outputStream, diff->fBasePath, height);
        print_image_cell(&outputStream, diff->fComparisonPath, height);
        outputStream.writeText("</tr>\n");
        outputStream.flush();
    }
    outputStream.writeText("</table>\n");
    outputStream.writeText("</body>\n</html>\n");
    outputStream.flush();
}

static void usage (char * argv0) {
    SkDebugf("Skia baseline image diff tool\n");
    SkDebugf("\n"
"Usage: \n"
"    %s <baseDir> <comparisonDir> [outputDir] \n"
, argv0, argv0);
    SkDebugf("\n"
"Arguments: \n"
"    --nodiffs: don't write out image diffs or index.html, just generate \n"
"               report on stdout \n"
"    --threshold <n>: only report differences > n (per color channel) [default 0]\n"
"    --match: compare files whose filenames contain this substring; if \n"
"             unspecified, compare ALL files. \n"
"             this flag may be repeated to add more matching substrings. \n"
"    --nomatch: regardless of --match, DO NOT compare files whose filenames \n"
"               contain this substring. \n"
"               this flag may be repeated to add more forbidden substrings. \n"
"    --sortbymismatch: sort by average color channel mismatch\n");
    SkDebugf(
"    --sortbymaxmismatch: sort by worst color channel mismatch;\n"
"                         break ties with -sortbymismatch\n"
"    [default sort is by fraction of pixels mismatching]\n");
    SkDebugf(
"    --weighted: sort by # pixels different weighted by color difference\n");
    SkDebugf(
"    baseDir: directory to read baseline images from.\n");
    SkDebugf(
"    comparisonDir: directory to read comparison images from\n");
    SkDebugf(
"    outputDir: directory to write difference images and index.html to; \n"
"               defaults to comparisonDir \n");
}

int main (int argc, char ** argv) {
    DiffMetricProc diffProc = compute_diff_pmcolor;
    int (*sortProc)(const void*, const void*) = SkCastForQSort(compare_diff_metrics);

    // Maximum error tolerated in any one color channel in any one pixel before
    // a difference is reported.
    int colorThreshold = 0;
    SkString baseDir;
    SkString comparisonDir;
    SkString outputDir;
    StringArray matchSubstrings;
    StringArray nomatchSubstrings;

    bool generateDiffs = true;

    RecordArray differences;
    DiffSummary summary;

    int i;
    int numUnflaggedArguments = 0;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help")) {
            usage(argv[0]);
            return 0;
        }
        if (!strcmp(argv[i], "--nodiffs")) {
            generateDiffs = false;
            continue;
        }
        if (!strcmp(argv[i], "--threshold")) {
            colorThreshold = atoi(argv[++i]);
            continue;
        }
        if (!strcmp(argv[i], "--match")) {
            matchSubstrings.push(new SkString(argv[++i]));
            continue;
        }
        if (!strcmp(argv[i], "--nomatch")) {
            nomatchSubstrings.push(new SkString(argv[++i]));
            continue;
        }
        if (!strcmp(argv[i], "--sortbymismatch")) {
            sortProc = SkCastForQSort(compare_diff_mean_mismatches);
            continue;
        }
        if (!strcmp(argv[i], "--sortbymaxmismatch")) {
            sortProc = SkCastForQSort(compare_diff_max_mismatches);
            continue;
        }
        if (!strcmp(argv[i], "--weighted")) {
            sortProc = SkCastForQSort(compare_diff_weighted);
            continue;
        }
        if (argv[i][0] != '-') {
            switch (numUnflaggedArguments++) {
                case 0:
                    baseDir.set(argv[i]);
                    continue;
                case 1:
                    comparisonDir.set(argv[i]);
                    continue;
                case 2:
                    outputDir.set(argv[i]);
                    continue;
                default:
                    SkDebugf("extra unflagged argument <%s>\n", argv[i]);
                    usage(argv[0]);
                    return 0;
            }
        }

        SkDebugf("Unrecognized argument <%s>\n", argv[i]);
        usage(argv[0]);
        return 0;
    }

    if (numUnflaggedArguments == 2) {
        outputDir = comparisonDir;
    } else if (numUnflaggedArguments != 3) {
        usage(argv[0]);
        return 0;
    }

    if (!baseDir.endsWith(PATH_DIV_STR)) {
        baseDir.append(PATH_DIV_STR);
    }
    printf("baseDir is [%s]\n", baseDir.c_str());

    if (!comparisonDir.endsWith(PATH_DIV_STR)) {
        comparisonDir.append(PATH_DIV_STR);
    }
    printf("comparisonDir is [%s]\n", comparisonDir.c_str());

    if (!outputDir.endsWith(PATH_DIV_STR)) {
        outputDir.append(PATH_DIV_STR);
    }
    if (generateDiffs) {
        printf("writing diffs to outputDir is [%s]\n", outputDir.c_str());
    } else {
        printf("not writing any diffs to outputDir [%s]\n", outputDir.c_str());
        outputDir.set("");
    }

    // Default substring matching:
    // - No matter what, don't match any PDF files.
    //   We may want to change this later, but for now this maintains the filter
    //   that get_file_list() used to always apply.
    // - If no matchSubstrings were specified, match ALL strings.
    nomatchSubstrings.push(new SkString(".pdf"));
    if (matchSubstrings.isEmpty()) {
        matchSubstrings.push(new SkString(""));
    }

    create_diff_images(diffProc, colorThreshold, &differences,
                       baseDir, comparisonDir, outputDir,
                       matchSubstrings, nomatchSubstrings, &summary);
    summary.print();

    if (differences.count()) {
        qsort(differences.begin(), differences.count(),
              sizeof(DiffRecord*), sortProc);
    }

    if (generateDiffs) {
        print_diff_page(summary.fNumMatches, colorThreshold, differences,
                        baseDir, comparisonDir, outputDir);
    }
    
    for (i = 0; i < differences.count(); i++) {
        delete differences[i];
    }
    matchSubstrings.deleteAll();
    nomatchSubstrings.deleteAll();
}
