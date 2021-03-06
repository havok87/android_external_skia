/*
 * Copyright 2013 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkErrorInternals.h"
#include "SkConvolver.h"
#include "SkBitmapProcState.h"
#include "SkBitmap.h"
#include "SkColor.h"
#include "SkColorPriv.h"
#include "SkConvolver.h"
#include "SkUnPreMultiply.h"
#include "SkShader.h"
#include "SkRTConf.h"
#include "SkMath.h"

// These are the per-scanline callbacks that are used when we must resort to
// resampling an image as it is blitted.  Typically these are used only when
// the image is rotated or has some other complex transformation applied.
// Scaled images will usually be rescaled directly before rasterization.

void highQualityFilter(const SkBitmapProcState& s, int x, int y,
                   SkPMColor* SK_RESTRICT colors, int count) {

    const int maxX = s.fBitmap->width() - 1;
    const int maxY = s.fBitmap->height() - 1;

    while (count-- > 0) {
        SkPoint srcPt;
        s.fInvProc(s.fInvMatrix, SkFloatToScalar(x + 0.5f),
                    SkFloatToScalar(y + 0.5f), &srcPt);
        srcPt.fX -= SK_ScalarHalf;
        srcPt.fY -= SK_ScalarHalf;

        SkScalar weight = 0;
        SkScalar fr = 0, fg = 0, fb = 0, fa = 0;

        int y0 = SkClampMax(SkScalarCeilToInt(srcPt.fY-s.getBitmapFilter()->width()), maxY);
        int y1 = SkClampMax(SkScalarFloorToInt(srcPt.fY+s.getBitmapFilter()->width()), maxY);
        int x0 = SkClampMax(SkScalarCeilToInt(srcPt.fX-s.getBitmapFilter()->width()), maxX);
        int x1 = SkClampMax(SkScalarFloorToInt(srcPt.fX+s.getBitmapFilter()->width()), maxX);

        for (int srcY = y0; srcY <= y1; srcY++) {
            SkScalar yWeight = s.getBitmapFilter()->lookupScalar((srcPt.fY - srcY));

            for (int srcX = x0; srcX <= x1 ; srcX++) {
                SkScalar xWeight = s.getBitmapFilter()->lookupScalar((srcPt.fX - srcX));

                SkScalar combined_weight = SkScalarMul(xWeight, yWeight);

                SkPMColor c = *s.fBitmap->getAddr32(srcX, srcY);
                fr += combined_weight * SkGetPackedR32(c);
                fg += combined_weight * SkGetPackedG32(c);
                fb += combined_weight * SkGetPackedB32(c);
                fa += combined_weight * SkGetPackedA32(c);
                weight += combined_weight;
            }
        }

        fr = SkScalarDiv(fr, weight);
        fg = SkScalarDiv(fg, weight);
        fb = SkScalarDiv(fb, weight);
        fa = SkScalarDiv(fa, weight);

        int a = SkClampMax(SkScalarRoundToInt(fa), 255);
        int r = SkClampMax(SkScalarRoundToInt(fr), a);
        int g = SkClampMax(SkScalarRoundToInt(fg), a);
        int b = SkClampMax(SkScalarRoundToInt(fb), a);

        *colors++ = SkPackARGB32(a, r, g, b);

        x++;
    }
}

SK_CONF_DECLARE(const char *, c_bitmapFilter, "bitmap.filter", "mitchell", "Which scanline bitmap filter to use [mitchell, lanczos, hamming, gaussian, triangle, box]");

SkBitmapFilter *SkBitmapFilter::Allocate() {
    if (!strcmp(c_bitmapFilter, "mitchell")) {
        return SkNEW_ARGS(SkMitchellFilter,(1.f/3.f,1.f/3.f));
    } else if (!strcmp(c_bitmapFilter, "lanczos")) {
        return SkNEW(SkLanczosFilter);
    } else if (!strcmp(c_bitmapFilter, "hamming")) {
        return SkNEW(SkHammingFilter);
    } else if (!strcmp(c_bitmapFilter, "gaussian")) {
        return SkNEW_ARGS(SkGaussianFilter,(2));
    } else if (!strcmp(c_bitmapFilter, "triangle")) {
        return SkNEW(SkTriangleFilter);
    } else if (!strcmp(c_bitmapFilter, "box")) {
        return SkNEW(SkBoxFilter);
    } else {
        SkASSERT(!!!"Unknown filter type");
    }

    return NULL;
}

SkBitmapProcState::ShaderProc32
SkBitmapProcState::chooseBitmapFilterProc() {

    if (fFilterLevel != SkPaint::kHigh_FilterLevel) {
        return NULL;
    }

    if (fAlphaScale != 256) {
        return NULL;
    }

    // TODO: consider supporting other configs (e.g. 565, A8)
    if (fBitmap->config() != SkBitmap::kARGB_8888_Config) {
        return NULL;
    }

    // TODO: consider supporting repeat and mirror
    if (SkShader::kClamp_TileMode != fTileModeX || SkShader::kClamp_TileMode != fTileModeY) {
        return NULL;
    }

    if (fInvType & (SkMatrix::kAffine_Mask | SkMatrix::kScale_Mask)) {
        fBitmapFilter = SkBitmapFilter::Allocate();
    }

    if (fInvType & SkMatrix::kScale_Mask) {
        return highQualityFilter;
    } else {
        return NULL;
    }
}
