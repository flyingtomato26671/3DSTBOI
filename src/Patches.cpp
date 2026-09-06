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

    void Require(bool condition, const std::string &message);
    std::string NormalizedSystemProfile(TBytes exheader);
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

void Isaac3ds::VerifyRebuiltProfile(const TBytes &exheader) {
    Require(NormalizedSystemProfile(exheader) == "7b4d53e6e19fbe815327186c08383052499e8e10284fab6e1fda937d22b5c901",
        "Rebuilt extended header differs from the verified Old 3DS profile");
}

Isaac3ds::TPatchResult Isaac3ds::PatchGame(const TBytes &code, const TBytes &exheader, const TBytes &icon) {
    Require(code.size() == 2154496, "Unsupported executable size");
    const auto originalHash = Sha256(code);
    Require(originalHash != patchedCodeHash, "This executable is already patched. Start with an original CIA.");
    Require(originalHash == testedCodeHash, "Unsupported executable. Only the original European v2.4.0 executable is accepted.");
    Require(NormalizedSystemProfile(exheader) == "1bb78a0c2d7638080aa2086f02d4545e3531ce1e8e8578a79832f910c47d299c",
        "Unsupported executable memory layout or system profile");
    Require(icon.size() == 0x36C0 && Read32(icon, 0) == 0x48444D53 && Read32(icon, 0x2028) == 0x11D1,
        "Unexpected original icon metadata");

    auto result = TPatchResult{code, exheader, icon, {}};
    const auto payload = FromHex(texturePayloadHex);
    Require(payload.size() == 1332 && Sha256(payload) == "fc3d243facb920bdc818e4a042c35d99dc62b1d026aab8173597a1a7ae1e8a05",
        "Authored texture payload checksum mismatch");
    Require(std::all_of(code.begin() + 0x1E6200, code.begin() + 0x1E7000,
                [](std::uint8_t value) {
                    return value == 0;
                }),
        "Texture payload storage is occupied");
    std::copy(payload.begin(), payload.end(), result.code.begin() + 0x1E6200);

    const auto patches = std::array<TWordPatch, 11>{{
        {0x2FF304, 0x4000, 0x4, "Active item: ZL to SELECT"},
        {0x2FF314, 0x8000, 0x80, "Drop: ZR to D-pad Down"},
        {0x2FF254, 0x80, 0xFFFFFFFF, "Keep Circle Pad Down; free D-pad Down from movement"},
        {0x2E8AC8, 0x2F00000, 0x2A00000, "Ordinary heap: 47 MiB to 42 MiB"},
        {0x1C9EFC, 0xE92D40F8, 0xEA047150, "Redirect texture upload"},
        {0x2C47E4, 0xE92D4070, 0xEA008770, "Redirect GPU texture binding"},
        {0x16571C, 0xEB024DC4, 0xEB060397, "Reuse shared procedural scratch"},
        {0x2F5DE4, 0x2C4A54, 0x2E63D4, "PCX packed pixel writes"},
        {0x2F5DE8, 0x2C49CC, 0x2E633C, "PCX packed pixel reads"},
        {0x2F5E74, 0x2C4A54, 0x2E63D4, "PNG packed pixel writes"},
        {0x2F5E78, 0x2C49CC, 0x2E633C, "PNG packed pixel reads"},
    }};
    auto report = std::ostringstream{};
    report << "Reference executable SHA-256: " << originalHash << "\n";
    for (const auto &patch : patches) {
        const auto offset = patch.address - 0x100000;
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
    Write32(result.exheader, 0x18, 0x1E6734);
    result.exheader[0x20C] = 0;
    result.exheader[0x20D] = 0;
    result.exheader[0x20E] = 0x34;
    Write32(result.icon, 0x2028, 0x1D1);
    VerifyRebuiltProfile(result.exheader);
    Require(Sha256(result.code) == patchedCodeHash, "Patches did not reproduce the verified RC1 executable");
    report << "Authored ARM payload: 1332 bytes at 0x002e6200, no additional pages.\n"
           << "80 MiB application mode; legacy CPU/cache settings; Old 3DS system dependencies.\n"
           << "PCX/PNG: RGB565 or RGBA4444. Procedural/render textures retain their original formats.\n"
           << "Circle Pad movement, ABXY shooting, L bomb, SELECT active item, R pill/card, D-pad Down drop.\n"
           << "Existing saved controls can override defaults; reset them in the game's Controls menu if needed.\n"
           << "Patched executable SHA-256: " << patchedCodeHash << "\n"
           << "RC1 passed emulation and has been tested on a physical Old 3DS.\n";
    result.report = report.str();
    return result;
}
