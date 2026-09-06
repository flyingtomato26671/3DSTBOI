#pragma once

#include "Platform.h"
#include "Patches.h"

namespace Isaac3ds {
    struct TOptions {
        std::filesystem::path input;
        std::filesystem::path output;
        std::filesystem::path toolsDirectory;
        std::filesystem::path seedDatabase;
        std::filesystem::path seedFile;
        bool dryRun = false;
    };

    void RunPatcher(const TOptions &options);
}
