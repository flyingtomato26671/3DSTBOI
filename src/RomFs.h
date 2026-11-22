#pragma once

#include "Bytes.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Isaac3ds {
    struct TRomFsEntry {
        std::string path; // '/'-separated, no leading slash
        std::uint64_t offset = 0; // from the start of the RomFS image
        std::uint64_t size = 0;
    };

    // Reads `length` bytes at `offset` from a RomFS image (level 3 data, as written by ctrtool --romfs).
    using TRomFsReader = std::function<TBytes(std::uint64_t offset, std::uint64_t length)>;

    struct TRomFsLevel3 {
        std::uint64_t offset = 0; // where the file-system tables start inside the image
        std::uint64_t size = 0;
    };

    // The image written by ctrtool is the whole RomFS partition: an IVFC hash-tree header followed by the data. This finds
    // the file-system data inside it (also accepts a bare file-system image).
    TRomFsLevel3 LocateRomFsData(const TRomFsReader &read, std::uint64_t imageSize);

    // Walks the directory tree of a RomFS image. Throws on malformed or hostile tables.
    std::vector<TRomFsEntry> ListRomFs(const TRomFsReader &read, std::uint64_t imageSize);

    // One line describing an Ogg Vorbis file from its first and last bytes (channels, sample rate, bitrate, duration).
    std::string DescribeOgg(const TBytes &head, const TBytes &tail, std::uint64_t fileSize);

    // Writes listing.txt, ogg-info.txt and a copy of the small data files (.xml, .anm2) into `outputDirectory`,
    // which must not exist yet. Game audio, textures and other assets are not copied.
    void DumpRomFs(const std::filesystem::path &romfsImage, const std::filesystem::path &outputDirectory);
}
