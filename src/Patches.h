#pragma once

#include "Bytes.h"

namespace Isaac3ds {
    inline constexpr std::uint64_t testedTitleId = 0x000400000F700900;
    inline constexpr std::uint16_t testedTitleVersion = 2112;
    inline const std::string testedCiaHash = "3316fa392eebf8f26028acaa2b08bfa44e0a2f45b856f63006197ceb8a8f724b";
    inline const std::string testedCodeHash = "674c7d31063abb3daa3f22c0e67da1e83d1a6e65725915dab34783d7f432882e";
    // Executable produced by patcher 0.1.0 (RC1). Only used to recognize already-patched input.
    inline const std::string rc1PatchedCodeHash = "d4eee88f5ad206c04e495ee42ba92330a2bce7935dba71b47582a54bd1b9b4e3";
    // Hash of the default-settings executable once it has been validated on a console. Empty means "not pinned yet":
    // the patcher then reports the hash instead of enforcing it.
    inline const std::string pinnedPatchedCodeHash = "";

    inline constexpr std::uint32_t defaultHeapMiB = 42;
    inline constexpr std::uint32_t minimumHeapMiB = 32;
    inline constexpr std::uint32_t maximumHeapMiB = 47;

    inline constexpr std::uint8_t defaultDither = 2;
    inline constexpr std::uint8_t maximumDither = 2;

    struct TPatchOptions {
        std::uint32_t heapMiB = defaultHeapMiB;
        std::uint8_t dither = defaultDither;
    };

    struct TPatchResult {
        TBytes code;
        TBytes exheader;
        TBytes icon;
        std::string report;
    };

    // Applies every patch without checking the executable against the pinned release; the patch-site words are still checked.
    TPatchResult ApplyPatches(const TBytes &code, const TBytes &exheader, const TBytes &icon, const TPatchOptions &options = {});
    // PatchGame also requires the exact supported executable, extended header and icon.
    TPatchResult PatchGame(const TBytes &code, const TBytes &exheader, const TBytes &icon, const TPatchOptions &options = {});
    void VerifyRebuiltProfile(const TBytes &rebuilt, const TBytes &intended);
}
