#include "graphics/image_loader.h"

#include "include/codec/SkJpegDecoder.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkStream.h"

#include <cstdlib>
#include <algorithm>
#include <memory>

uint8_t *image_load_rgba8_scaled_from_memory(const uint8_t *bytes, size_t length,
                                             int target_width, int target_height,
                                             int *out_width, int *out_height)
{
    return image_load_rgba8_scaled_cancellable(bytes, length, target_width, target_height,
                                               out_width, out_height, nullptr, nullptr);
}

uint8_t *image_load_rgba8_scaled_cancellable(const uint8_t *bytes, size_t length,
                                             int target_width, int target_height,
                                             int *out_width, int *out_height,
                                             bool (*cancelled)(void *), void *opaque)
{
    if (!bytes || !length || target_width < 1 || target_height < 1)
        return nullptr;
    if (cancelled && cancelled(opaque))
        return nullptr;
    if (!SkJpegDecoder::IsJpeg(bytes, length))
        return image_load_rgba8_from_memory(bytes, length, out_width, out_height);
    auto codec = SkJpegDecoder::Decode(std::make_unique<SkMemoryStream>(bytes, length, false), nullptr);
    if (codec)
    {
        const SkISize original = codec->dimensions();
        SkISize decoded = original;
        for (int denominator : {8, 4, 2})
        {
            SkISize candidate = codec->getScaledDimensions(1.0f / denominator);
            if (candidate.width() >= target_width && candidate.height() >= target_height &&
                candidate.width() <= original.width() && candidate.height() <= original.height())
            {
                decoded = candidate;
                break;
            }
        }
        if (decoded != original && decoded.width() > 0 && decoded.height() > 0 &&
            (size_t)decoded.width() <= SIZE_MAX / 4 / (size_t)decoded.height())
        {
            SkImageInfo info = SkImageInfo::Make(decoded.width(), decoded.height(), kRGBA_8888_SkColorType,
                                                 kUnpremul_SkAlphaType);
            auto *pixels = static_cast<uint8_t *>(std::malloc(info.computeMinByteSize()));
            if (!pixels)
                return nullptr;
            bool success = false;
            if (cancelled)
            {
                success = codec->startScanlineDecode(info) == SkCodec::kSuccess;
                for (int row = 0; success && row < decoded.height(); row += 16)
                {
                    if (cancelled(opaque))
                    {
                        std::free(pixels);
                        return nullptr;
                    }
                    int count = std::min(16, decoded.height() - row);
                    success = codec->getScanlines(pixels + (size_t)row * info.minRowBytes(),
                                                  count, info.minRowBytes()) == count;
                }
            }
            else
                success = codec->getPixels(info, pixels, info.minRowBytes()) == SkCodec::kSuccess;
            if (success)
            {
                if (out_width)
                    *out_width = decoded.width();
                if (out_height)
                    *out_height = decoded.height();
                return pixels;
            }
            std::free(pixels);
        }
    }
    if (cancelled && cancelled(opaque))
        return nullptr;
    return image_load_rgba8_from_memory(bytes, length, out_width, out_height);
}