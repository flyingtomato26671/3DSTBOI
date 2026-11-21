#include "Texture16.h"

namespace IsaacOld {
    extern "C" {
    const volatile unsigned char DitherStrength = 2;
    }

    constexpr unsigned int TileIndex(unsigned int x, unsigned int y, unsigned int width);
    constexpr unsigned int DitherThreshold(unsigned int x, unsigned int y, unsigned int strength);
    constexpr unsigned int Quantize(unsigned int value, unsigned int maximum, unsigned int threshold);
    constexpr unsigned short PackColor(unsigned int format, unsigned int red, unsigned int green,
        unsigned int blue, unsigned int alpha, unsigned int threshold);
    constexpr unsigned int Clamp255(unsigned int value);
    bool IsPackedTexture(const unsigned char *texture);
}

bool IsaacOld::IsPackedTexture(const unsigned char *texture) {
    const auto vtable = *reinterpret_cast<const unsigned int *>(texture);
    return vtable == 0x2F5D98 || vtable == 0x2F5E28;
}

constexpr unsigned int IsaacOld::TileIndex(unsigned int x, unsigned int y, unsigned int width) {
    const auto tile = ((y >> 3) * (width >> 3) + (x >> 3)) << 6;
    const auto morton = (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
    return tile + morton;
}

// Ordered 4x4 Bayer dither, centered on 127 so that strength 0 is plain round-to-nearest.
// Every channel of a pixel shares one threshold, which keeps the noise gray instead of colored.
constexpr unsigned int IsaacOld::DitherThreshold(unsigned int x, unsigned int y, unsigned int strength) {
    const auto column = x & 3;
    const auto row = y & 3;
    const auto mixed = column ^ row;
    const auto level = ((mixed & 1) << 3) | ((row & 1) << 2) | (mixed & 2) | ((row >> 1) & 1);
    const auto centered = static_cast<int>(level * 16 + 8) - 127;
    const auto amount = strength > 2 ? 2 : static_cast<int>(strength);
    return static_cast<unsigned int>(127 + ((centered * amount) >> 1));
}

// floor((value * maximum + threshold) / 255) without a division; exact below 65535.
constexpr unsigned int IsaacOld::Quantize(unsigned int value, unsigned int maximum, unsigned int threshold) {
    const auto scaled = value * maximum + threshold;
    return (scaled + 1 + (scaled >> 8)) >> 8;
}

constexpr unsigned int IsaacOld::Clamp255(unsigned int value) {
    return value > 255 ? 255 : value;
}

constexpr unsigned short IsaacOld::PackColor(unsigned int format, unsigned int red, unsigned int green,
    unsigned int blue, unsigned int alpha, unsigned int threshold) {
    if (format == 1) {
        return static_cast<unsigned short>((Quantize(red, 31, threshold) << 11) |
            (Quantize(green, 63, threshold) << 5) | Quantize(blue, 31, threshold));
    }

    return static_cast<unsigned short>((Quantize(red, 15, threshold) << 12) | (Quantize(green, 15, threshold) << 8) |
        (Quantize(blue, 15, threshold) << 4) | Quantize(alpha, 15, threshold));
}

int IsaacOld::SwizzleTexture(unsigned int format, unsigned int width, unsigned int height,
    const unsigned char *source, unsigned short *destination) {
    if (((width | height) & 7) != 0 || (format != 1 && format != 2)) {
        return 0;
    }

    const auto channels = format == 1 ? 3u : 4u;
    const auto strength = static_cast<unsigned int>(DitherStrength);
    for (auto y = 0u; y < height; ++y) {
        for (auto x = 0u; x < width; ++x) {
            const auto alpha = format == 1 ? 255u : source[3];
            destination[TileIndex(x, height - y - 1, width)] =
                PackColor(format, source[0], source[1], source[2], alpha, DitherThreshold(x, y, strength));
            source += channels;
        }
    }

    return 1;
}

unsigned short *IsaacOld::PixelAddress(unsigned char *texture, unsigned int x, unsigned int y) {
    using TDimensionFunction = unsigned int (*)(unsigned char *);
    const auto vtable = *reinterpret_cast<TDimensionFunction **>(texture);
    const auto width = vtable[12](texture);
    const auto height = vtable[13](texture);
    const auto pixels = *reinterpret_cast<unsigned short **>(texture + 108);
    return pixels + TileIndex(x, height - y - 1, width);
}

float *IsaacOld::ReadPixel(float *result, unsigned char *texture, unsigned int x, unsigned int y) {
    const auto pixel = *PixelAddress(texture, x, y);
    auto red = 0u;
    auto green = 0u;
    auto blue = 0u;
    auto alpha = 255u;
    if (texture[24] == 1) {
        red = pixel >> 11;
        green = (pixel >> 5) & 63;
        blue = pixel & 31;
        red = (red << 3) | (red >> 2);
        green = (green << 2) | (green >> 4);
        blue = (blue << 3) | (blue >> 2);
    } else {
        red = (pixel >> 12) * 17;
        green = ((pixel >> 8) & 15) * 17;
        blue = ((pixel >> 4) & 15) * 17;
        alpha = (pixel & 15) * 17;
    }

    using TColorFunction = float *(*)(float *, unsigned int, unsigned int, unsigned int, unsigned int);
    const auto makeColor = reinterpret_cast<TColorFunction>(0x1ED430);
    return makeColor(result, red, green, blue, alpha);
}

int IsaacOld::WritePixel(unsigned char *texture, unsigned int x, unsigned int y, const float *color) {
    auto *pixel = PixelAddress(texture, x, y);
    *pixel = PackColor(texture[24], Clamp255(static_cast<unsigned int>(color[0] * 255.0f)),
        Clamp255(static_cast<unsigned int>(color[1] * 255.0f)),
        Clamp255(static_cast<unsigned int>(color[2] * 255.0f)),
        Clamp255(static_cast<unsigned int>(color[3] * 255.0f)),
        DitherThreshold(x, y, static_cast<unsigned int>(DitherStrength)));
    return 1;
}

int IsaacOld::UploadTexels(unsigned char *texture, unsigned char **source) {
    using TDimensionFunction = unsigned int (*)(unsigned char *);
    using TAllocatorFunction = unsigned char *(*)(unsigned int, unsigned int);
    using TFreeFunction = void (*)(unsigned int, unsigned char *);
    using TFilterFunction = void (*)(unsigned char *, unsigned int, unsigned int);
    using TSwizzleFunction = int (*)(unsigned int, unsigned int, unsigned int,
        const unsigned char *, unsigned char *);
    const auto vtable = *reinterpret_cast<TDimensionFunction **>(texture);
    const auto width = vtable[12](texture);
    const auto height = vtable[13](texture);
    const auto format = texture[24];
    const auto packed = IsPackedTexture(texture);
    const auto channels = format == 1 ? 3u : (format == 2 ? 4u : 0u);
    const auto allocate = reinterpret_cast<TAllocatorFunction>(0x1F8E34);
    auto *destination = allocate(0x4186E0, width * height * (packed ? 2u : channels));
    *reinterpret_cast<unsigned char **>(texture + 108) = destination;
    if (destination == nullptr) {
        return 0;
    }

    if (packed) {
        SwizzleTexture(format, width, height, *source, reinterpret_cast<unsigned short *>(destination));
    } else {
        const auto originalSwizzle = reinterpret_cast<TSwizzleFunction>(0x186444);
        originalSwizzle(format, width, height, *source, destination);
    }

    if (*source != *reinterpret_cast<unsigned char **>(0x41868C)) {
        const auto freeBuffer = reinterpret_cast<TFreeFunction>(0x1E9280);
        freeBuffer(0x4186E0, *source);
        *source = nullptr;
    }

    const auto filters = *reinterpret_cast<TFilterFunction **>(texture);
    filters[15](texture, 0, 0);
    filters[16](texture, 1, 1);
    return 1;
}

unsigned char *IsaacOld::AllocateProceduralScratch(unsigned int allocator, unsigned int size) {
    const auto scratch = *reinterpret_cast<unsigned char **>(0x41868C);
    if (scratch != nullptr && size <= 0x400000) {
        return scratch;
    }

    using TAllocatorFunction = unsigned char *(*)(unsigned int, unsigned int);
    const auto allocate = reinterpret_cast<TAllocatorFunction>(0x1F8E34);
    return allocate(allocator, size);
}

void IsaacOld::BindTexture(unsigned char *texture, unsigned char *state, unsigned int unit) {
    if (unit > 2) {
        return;
    }

    using TDimensionFunction = unsigned int (*)(unsigned char *);
    using TPhysicalFunction = unsigned int (*)(const unsigned char *);
    const auto vtable = *reinterpret_cast<TDimensionFunction **>(texture);
    auto *unitState = state + (unit == 0 ? 0 : (unit == 1 ? 60 : 96));
    unitState[28] = unit == 0 ? 0 : 1;
    *reinterpret_cast<unsigned short *>(unitState + 4) = vtable[12](texture);
    *reinterpret_cast<unsigned short *>(unitState + 6) = vtable[13](texture);
    const auto rgb = texture[24] == 1;
    unitState[8] = IsPackedTexture(texture) ? (rgb ? 3 : 4) : (rgb ? 1 : 0);
    const auto physicalAddress = reinterpret_cast<TPhysicalFunction>(0x1E5BE0);
    *reinterpret_cast<unsigned int *>(unitState) =
        physicalAddress(*reinterpret_cast<unsigned char **>(texture + 108));
    unitState[12] = texture[33] ? 3 : 0;
    unitState[11] = texture[34] ? 1 : 0;
    unitState[10] = texture[35] ? 0 : 2;
    unitState[9] = texture[36] ? 0 : 2;
}
