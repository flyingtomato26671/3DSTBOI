#include "RomFs.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>

namespace Isaac3ds {
    namespace {
        constexpr std::uint32_t none = 0xFFFFFFFF;
        constexpr std::size_t maxTableBytes = 32 * 1024 * 1024;
        constexpr std::uint64_t maxCopiedFile = 4 * 1024 * 1024;

        void Require(bool condition, const char *message) {
            if (!condition) {
                throw std::runtime_error(message);
            }
        }

        std::uint16_t Read16(const TBytes &data, std::size_t offset) {
            RequireRange(data.size(), offset, 2);
            return static_cast<std::uint16_t>(data[offset] | (data[offset + 1] << 8));
        }

        // UTF-16LE name from a RomFS table, converted to UTF-8 and checked for path-traversal tricks.
        std::string ReadName(const TBytes &table, std::size_t offset, std::uint32_t byteLength) {
            Require(byteLength % 2 == 0, "RomFS name has an odd byte length");
            RequireRange(table.size(), offset, byteLength);
            auto text = std::string{};
            for (auto i = std::size_t{0}; i < byteLength; i += 2) {
                auto unit = static_cast<std::uint32_t>(Read16(table, offset + i));
                if (unit >= 0xD800 && unit < 0xDC00 && i + 2 < byteLength) {
                    const auto low = static_cast<std::uint32_t>(Read16(table, offset + i + 2));
                    if (low >= 0xDC00 && low < 0xE000) {
                        unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
                        i += 2;
                    }
                }

                if (unit < 0x80) {
                    text += static_cast<char>(unit);
                } else if (unit < 0x800) {
                    text += static_cast<char>(0xC0 | (unit >> 6));
                    text += static_cast<char>(0x80 | (unit & 0x3F));
                } else if (unit < 0x10000) {
                    text += static_cast<char>(0xE0 | (unit >> 12));
                    text += static_cast<char>(0x80 | ((unit >> 6) & 0x3F));
                    text += static_cast<char>(0x80 | (unit & 0x3F));
                } else {
                    text += static_cast<char>(0xF0 | (unit >> 18));
                    text += static_cast<char>(0x80 | ((unit >> 12) & 0x3F));
                    text += static_cast<char>(0x80 | ((unit >> 6) & 0x3F));
                    text += static_cast<char>(0x80 | (unit & 0x3F));
                }
            }

            return text;
        }

        void CheckComponent(const std::string &name) {
            Require(!name.empty() && name != "." && name != "..", "RomFS contains an unsafe name");
            Require(name.find_first_of(std::string("/\\:\0", 4)) == std::string::npos, "RomFS contains an unsafe name");
        }

        std::string Lower(std::string text) {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text;
        }

        bool EndsWith(const std::string &text, const std::string &suffix) {
            return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
        }
    }
}

std::vector<Isaac3ds::TRomFsEntry> Isaac3ds::ListRomFs(const TRomFsReader &read, std::uint64_t imageSize) {
    Require(imageSize >= 0x28, "RomFS image is too small");
    const auto header = read(0, 0x28);
    Require(header.size() == 0x28 && Read32(header, 0) == 0x28, "Unexpected RomFS header");
    const auto dirOffset = Read32(header, 0x0C);
    const auto dirSize = Read32(header, 0x10);
    const auto fileOffset = Read32(header, 0x1C);
    const auto fileSize = Read32(header, 0x20);
    const auto dataOffset = Read32(header, 0x24);
    Require(dirSize >= 0x18 && dirSize <= maxTableBytes && fileSize <= maxTableBytes, "RomFS table sizes are implausible");
    Require(static_cast<std::uint64_t>(dirOffset) + dirSize <= imageSize && static_cast<std::uint64_t>(fileOffset) + fileSize <= imageSize &&
                dataOffset <= imageSize,
        "RomFS tables lie outside the image");

    const auto dirs = read(dirOffset, dirSize);
    const auto files = read(fileOffset, fileSize);
    Require(dirs.size() == dirSize && files.size() == fileSize, "Short read of RomFS tables");

    auto entries = std::vector<TRomFsEntry>{};
    auto seenDirs = std::set<std::uint32_t>{};
    auto seenFiles = std::set<std::uint32_t>{};
    auto pending = std::vector<std::pair<std::uint32_t, std::string>>{{0, ""}};
    while (!pending.empty()) {
        const auto [dir, prefix] = pending.back();
        pending.pop_back();
        Require(dir % 4 == 0 && static_cast<std::uint64_t>(dir) + 0x18 <= dirSize, "RomFS directory offset out of range");
        Require(seenDirs.insert(dir).second, "RomFS directory tree contains a loop");
        Require(seenDirs.size() <= 200000, "RomFS has too many directories");

        for (auto file = Read32(dirs, dir + 0x0C); file != none;) {
            Require(file % 4 == 0 && static_cast<std::uint64_t>(file) + 0x20 <= fileSize, "RomFS file offset out of range");
            Require(seenFiles.insert(file).second, "RomFS file list contains a loop");
            Require(seenFiles.size() <= 400000, "RomFS has too many files");
            const auto length = Read32(files, file + 0x1C);
            const auto name = ReadName(files, file + 0x20, length);
            CheckComponent(name);
            const auto start = Read64(files, file + 0x08);
            const auto size = Read64(files, file + 0x10);
            Require(start <= imageSize && size <= imageSize && dataOffset + start + size <= imageSize, "RomFS file lies outside the image");
            entries.push_back({prefix + name, dataOffset + start, size});
            file = Read32(files, file + 0x04);
        }

        for (auto child = Read32(dirs, dir + 0x08); child != none;) {
            Require(child % 4 == 0 && static_cast<std::uint64_t>(child) + 0x18 <= dirSize, "RomFS directory offset out of range");
            const auto length = Read32(dirs, child + 0x14);
            const auto name = ReadName(dirs, child + 0x18, length);
            CheckComponent(name);
            pending.emplace_back(child, prefix + name + "/");
            child = Read32(dirs, child + 0x04);
            Require(pending.size() <= 200000, "RomFS has too many directories");
        }
    }

    std::sort(entries.begin(), entries.end(), [](const TRomFsEntry &a, const TRomFsEntry &b) { return a.path < b.path; });
    return entries;
}

std::string Isaac3ds::DescribeOgg(const TBytes &head, const TBytes &tail, std::uint64_t fileSize) {
    auto text = std::ostringstream{};
    text << fileSize << " bytes";
    if (head.size() < 28 || std::memcmp(head.data(), "OggS", 4) != 0) {
        text << ", not an Ogg file";
        return text.str();
    }

    const auto packet = std::size_t{27} + head[26];
    if (head.size() < packet + 30 || head[packet] != 1 || std::memcmp(head.data() + packet + 1, "vorbis", 6) != 0) {
        text << ", not Vorbis";
        return text.str();
    }

    const auto channels = head[packet + 11];
    const auto rate = Read32(head, packet + 12);
    const auto nominal = static_cast<std::int32_t>(Read32(head, packet + 20));
    text << ", " << static_cast<int>(channels) << " ch, " << rate << " Hz, nominal bitrate " << nominal << " bps";
    // The last page's granule position is the total number of samples per channel.
    for (auto i = tail.size(); i >= 4 && i > 0; --i) {
        const auto at = i - 4;
        if (std::memcmp(tail.data() + at, "OggS", 4) == 0 && at + 14 <= tail.size()) {
            const auto granule = Read64(tail, at + 6);
            if (rate != 0 && granule != 0xFFFFFFFFFFFFFFFFULL) {
                text << std::fixed << std::setprecision(1) << ", " << static_cast<double>(granule) / rate << " s";
            }

            break;
        }
    }

    return text.str();
}

void Isaac3ds::DumpRomFs(const std::filesystem::path &romfsImage, const std::filesystem::path &outputDirectory) {
    Require(!std::filesystem::exists(outputDirectory), "The RomFS dump folder already exists. Files are never overwritten.");
    Require(std::filesystem::is_directory(outputDirectory.parent_path()), "The RomFS dump parent folder does not exist");
    auto input = std::ifstream(romfsImage, std::ios::binary);
    Require(input.good(), "Cannot open the extracted RomFS image");
    const auto imageSize = static_cast<std::uint64_t>(std::filesystem::file_size(romfsImage));
    const auto read = [&](std::uint64_t offset, std::uint64_t length) {
        Require(offset + length <= imageSize, "RomFS read outside the image");
        auto data = TBytes(static_cast<std::size_t>(length));
        input.clear();
        input.seekg(static_cast<std::streamoff>(offset));
        input.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(length));
        Require(input.gcount() == static_cast<std::streamsize>(length), "Short read of the RomFS image");
        return data;
    };

    const auto entries = ListRomFs(read, imageSize);
    const auto staging = std::filesystem::path(outputDirectory.native() + std::filesystem::path(".partial").native());
    Require(!std::filesystem::exists(staging), "A previous partial RomFS dump exists; remove it first");
    std::filesystem::create_directory(staging);
    try {
        auto listing = std::ostringstream{};
        auto oggInfo = std::ostringstream{};
        auto copied = std::size_t{0};
        for (const auto &entry : entries) {
            listing << entry.size << "\t" << entry.path << "\n";
            const auto lower = Lower(entry.path);
            if (EndsWith(lower, ".ogg")) {
                const auto head = read(entry.offset, std::min<std::uint64_t>(entry.size, 256));
                const auto tailLength = std::min<std::uint64_t>(entry.size, 65536);
                const auto tail = read(entry.offset + entry.size - tailLength, tailLength);
                oggInfo << entry.path << "\t" << DescribeOgg(head, tail, entry.size) << "\n";
            } else if ((EndsWith(lower, ".xml") || EndsWith(lower, ".anm2")) && entry.size <= maxCopiedFile) {
                const auto target = staging / "files" / std::filesystem::path(entry.path);
                std::filesystem::create_directories(target.parent_path());
                WriteFile(target, read(entry.offset, entry.size));
                ++copied;
            }
        }

        WriteText(staging / "listing.txt", listing.str());
        WriteText(staging / "ogg-info.txt", oggInfo.str());
        WriteText(staging / "README.txt",
            "Data from your own copy of the game, for analysis only. Do not share or commit it.\n"
            "listing.txt: size in bytes and path of every file. ogg-info.txt: audio parameters. files/: " +
                std::to_string(copied) + " small .xml/.anm2 files.\n");
        Require(!std::filesystem::exists(outputDirectory), "The RomFS dump folder appeared while it was being written");
        std::filesystem::rename(staging, outputDirectory);
    } catch (...) {
        auto error = std::error_code{};
        std::filesystem::remove_all(staging, error);
        throw;
    }
}
