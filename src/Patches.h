#pragma once

#include "Bytes.h"

namespace Isaac3ds {
    inline constexpr std::uint64_t testedTitleId = 0x000400000F700900;
    inline constexpr std::uint16_t testedTitleVersion = 2112;
    inline const std::string testedCiaHash = "3316fa392eebf8f26028acaa2b08bfa44e0a2f45b856f63006197ceb8a8f724b";
    inline const std::string testedCodeHash = "674c7d31063abb3daa3f22c0e67da1e83d1a6e65725915dab34783d7f432882e";
    inline const std::string patchedCodeHash = "d4eee88f5ad206c04e495ee42ba92330a2bce7935dba71b47582a54bd1b9b4e3";

    struct TPatchResult {
        TBytes code;
        TBytes exheader;
        TBytes icon;
        std::string report;
    };

    TPatchResult PatchGame(const TBytes &code, const TBytes &exheader, const TBytes &icon);
    void VerifyRebuiltProfile(const TBytes &exheader);
}
