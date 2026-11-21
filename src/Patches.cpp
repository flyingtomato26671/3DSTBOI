#include "Patches.h"
#include "TextureData.h"

#include <algorithm>
#include <array>
#include <sstream>
#include <stdexcept>

namespace Isaac3ds {
    struct TWordPatch {
        std::uint32_t address;
        std::uint32_t before;
        std::uint32_t after;
        const char *description;
    };

    // The payload lives in zeroed text padding. Virtual addresses start 0x100000 above file offsets.
    inline constexpr std::uint32_t virtualBase = 0x100000;
    inline constexpr std::size_t payloadStorageEnd = 0x1E7000;
    inline constexpr std::size_t payloadFileOffset = texturePayloadBase - virtualBase;

    void Require(bool condition, const std::string &message);
    std::string NormalizedSystemProfile(TBytes exheader);
    std::uint32_t BranchWord(std::uint32_t from, std::uint32_t target, bool link);
}

void Isaac3ds::Require(bool condition, const std::string &message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string Isaac3ds::NormalizedSystemProfile(TBytes exheader) {
    Require(exheader.size() == 0x800, "Expected a complete 0x800-byte extended header");
    exheader.resize(0x400);
    exheader[0xD] &= 0xFE;
    exheader[0x3F1] &= 0xFE;
    return Sha256(exheader);
}

// Encodes an ARM B/BL at `from` that lands on `target`.
std::uint32_t Isaac3ds::BranchWord(std::uint32_t from, std::uint32_t target, bool link) {
    const auto delta = static_cast<std::int64_t>(target) - (static_cast<std::int64_t>(from) + 8);
    Require(delta % 4 == 0 && delta >= -(std::int64_t{1} << 25) && delta < (std::int64_t{1} << 25),
        "Texture payload hook is out of branch range");
    return (link ? 0xEB000000U : 0xEA000000U) | (static_cast<std::uint32_t>(delta >> 2) & 0xFFFFFFU);
}

void Isaac3ds::VerifyRebuiltProfile(const TBytes &rebuilt, const TBytes &intended) {
    Require(NormalizedSystemProfile(rebuilt) == NormalizedSystemProfile(intended),
        "Rebuilt extended header differs from the intended Old 3DS profile");
}

Isaac3ds::TPatchResult Isaac3ds::ApplyPatches(const TBytes &code, const TBytes &exheader, const TBytes &icon, const TPatchOptions &options) {
    Require(options.heapMiB >= minimumHeapMiB && options.heapMiB <= maximumHeapMiB,
        "Heap size must be between " + std::to_string(minimumHeapMiB) + " and " + std::to_string(maximumHeapMiB) + " MiB");
    Require(options.dither <= maximumDither, "Dither strength must be 0, 1, or 2");
    Require(code.size() == 2154496, "Unsupported executable size");
    Require(exheader.size() == 0x800, "Expected a complete 0x800-byte extended header");

    auto result = TPatchResult{code, exheader, icon, {}};
    const auto payload = FromHex(texturePayloadHex);
    Require(!payload.empty() && payloadFileOffset + payload.size() <= payloadStorageEnd,
        "Texture payload does not fit its storage");
    Require(Sha256(payload) == texturePayloadHash, "Authored texture payload checksum mismatch");
    const auto ditherOffset = textureDitherAddress - texturePayloadBase;
    Require(ditherOffset < payload.size(), "Texture payload dither setting is outside the payload");
    Require(std::all_of(code.begin() + static_cast<std::ptrdiff_t>(payloadFileOffset), code.begin() + static_cast<std::ptrdiff_t>(payloadStorageEnd),
                [](std::uint8_t value) {
                    return value == 0;
                }),
        "Texture payload storage is occupied");
    std::copy(payload.begin(), payload.end(), result.code.begin() + static_cast<std::ptrdiff_t>(payloadFileOffset));
    result.code[payloadFileOffset + ditherOffset] = options.dither;

    const auto patches = std::array<TWordPatch, 11>{{
        {0x2FF304, 0x4000, 0x4, "Active item: ZL to SELECT"},
        {0x2FF314, 0x8000, 0x80, "Drop: ZR to D-pad Down"},
        {0x2FF254, 0x80, 0xFFFFFFFF, "Keep Circle Pad Down; free D-pad Down from movement"},
        {0x2E8AC8, 0x2F00000, options.heapMiB << 20, "Ordinary heap size"},
        {0x1C9EFC, 0xE92D40F8, BranchWord(0x1C9EFC, textureUploadAddress, false), "Redirect texture upload"},
        {0x2C47E4, 0xE92D4070, BranchWord(0x2C47E4, textureBindAddress, false), "Redirect GPU texture binding"},
        {0x16571C, 0xEB024DC4, BranchWord(0x16571C, textureScratchAddress, true), "Reuse shared procedural scratch"},
        {0x2F5DE4, 0x2C4A54, texturePixelWriteAddress, "PCX packed pixel writes"},
        {0x2F5DE8, 0x2C49CC, texturePixelReadAddress, "PCX packed pixel reads"},
        {0x2F5E74, 0x2C4A54, texturePixelWriteAddress, "PNG packed pixel writes"},
        {0x2F5E78, 0x2C49CC, texturePixelReadAddress, "PNG packed pixel reads"},
    }};
    auto report = std::ostringstream{};
    report << "Reference executable SHA-256: " << Sha256(code) << "\n";
    for (const auto &patch : patches) {
        const auto offset = patch.address - virtualBase;
        Require(Read32(code, offset) == patch.before, "Unexpected original word at 0x" + HexNumber(patch.address));
        Write32(result.code, offset, patch.after);
        report << "0x" << HexNumber(patch.address, 8) << ": " << patch.description
               << " [" << HexNumber(patch.before, 8) << " -> " << HexNumber(patch.after, 8) << "]\n";
    }

    auto dependencies = TBytes(0x180);
    auto destination = std::size_t{0};
    for (auto offset = std::size_t{0x40}; offset < 0x1C0; offset += 8) {
        auto titleId = Read64(exheader, offset);
        if (titleId == 0 || titleId == 0x0004013020004202) {
            continue;
        }

        if (titleId == 0x0004013020004002) {
            titleId = 0x0004013000004002;
        }

        Write32(dependencies, destination, static_cast<std::uint32_t>(titleId));
        Write32(dependencies, destination + 4, static_cast<std::uint32_t>(titleId >> 32));
        destination += 8;
    }

    std::copy(dependencies.begin(), dependencies.end(), result.exheader.begin() + 0x40);
    Write32(result.exheader, 0x18, static_cast<std::uint32_t>(payloadFileOffset + payload.size()));
    result.exheader[0x20C] = 0;
    result.exheader[0x20D] = 0;
    result.exheader[0x20E] = 0x34;
    Write32(result.icon, 0x2028, 0x1D1);
    const auto ditherNames = std::array<const char *, 3>{"off (round to nearest)", "light", "full"};
    report << "Authored ARM payload: " << payload.size() << " bytes at 0x" << HexNumber(texturePayloadBase, 8) << ", no additional pages.\n"
           << "80 MiB application mode; legacy CPU/cache settings; Old 3DS system dependencies.\n"
           << "PCX/PNG: RGB565 or RGBA4444, rounded to nearest with ordered dithering: " << ditherNames[options.dither] << ".\n"
           << "Procedural/render textures retain their original formats.\n"
           << "Circle Pad movement, ABXY shooting, L bomb, SELECT active item, R pill/card, D-pad Down drop.\n"
           << "Existing saved controls can override defaults; reset them in the game's Controls menu if needed.\n"
           << "Ordinary heap: " << options.heapMiB << " MiB (original 47 MiB).\n";
    result.report = report.str();
    return result;
}

Isaac3ds::TPatchResult Isaac3ds::PatchGame(const TBytes &code, const TBytes &exheader, const TBytes &icon, const TPatchOptions &options) {
    Require(code.size() == 2154496, "Unsupported executable size");
    const auto originalHash = Sha256(code);
    Require(originalHash != rc1PatchedCodeHash, "This executable is already patched (RC1). Start with an original CIA.");
    Require(originalHash == testedCodeHash, "Unsupported executable. Only the original European v2.4.0 executable is accepted.");
    Require(NormalizedSystemProfile(exheader) == "1bb78a0c2d7638080aa2086f02d4545e3531ce1e8e8578a79832f910c47d299c",
        "Unsupported executable memory layout or system profile");
    Require(icon.size() == 0x36C0 && Read32(icon, 0) == 0x48444D53 && Read32(icon, 0x2028) == 0x11D1,
        "Unexpected original icon metadata");

    auto result = ApplyPatches(code, exheader, icon, options);
    const auto resultHash = Sha256(result.code);
    const auto defaults = options.heapMiB == defaultHeapMiB && options.dither == defaultDither;
    if (defaults && !pinnedPatchedCodeHash.empty()) {
        Require(resultHash == pinnedPatchedCodeHash, "Patches did not reproduce the pinned reference executable");
    }

    result.report += "Patched executable SHA-256: " + resultHash + "\n";
    if (!defaults) {
        result.report += "EXPERIMENTAL: non-default heap or dither settings were requested; this executable has not been validated.\n";
    } else if (pinnedPatchedCodeHash.empty()) {
        result.report += "This build's executable hash is not pinned yet. After testing it on a console, copy the hash above into pinnedPatchedCodeHash in src/Patches.h.\n";
    } else {
        result.report += "Executable matches the pinned reference build.\n";
    }

    return result;
}
