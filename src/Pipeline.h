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
        std::filesystem::path dumpCode;
        bool dryRun = false;
        std::uint32_t heapMiB = defaultHeapMiB;
        std::uint8_t dither = defaultDither;
        std::uint32_t cpuLimit = defaultCpuLimit;
    };

    void RunPatcher(const TOptions &options);
}
