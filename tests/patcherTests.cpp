#include "Patches.h"
#include "RomFs.h"
#include "TextureData.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
    int failures = 0;

    void Check(bool condition, const char *description) {
        if (!condition) {
            ++failures;
            std::cerr << "FAILED: " << description << "\n";
        }
    }

    template <typename TFunction>
    bool Throws(TFunction function) {
        try {
            function();
        } catch (const std::exception &) {
            return true;
        }

        return false;
    }

    struct TSite {
        std::uint32_t address;
        std::uint32_t before;
    };

    // The original words at every patch site, as PatchGame expects to find them.
    const std::array<TSite, 12> sites = {{
        {0x2FF304, 0x4000}, {0x2FF314, 0x8000}, {0x2FF254, 0x80}, {0x2E8AC8, 0x2F00000},
        {0x1C9EFC, 0xE92D40F8}, {0x2C47E4, 0xE92D4070}, {0x16571C, 0xEB024DC4},
        {0x2F5DE4, 0x2C4A54}, {0x2F5DE8, 0x2C49CC}, {0x2F5E74, 0x2C4A54}, {0x2F5E78, 0x2C49CC},
        {0x1055CC, 0xE3A0001E},
    }};

    Isaac3ds::TBytes SyntheticCode() {
        auto code = Isaac3ds::TBytes(2154496);
        for (const auto &site : sites) {
            Isaac3ds::Write32(code, site.address - 0x100000, site.before);
        }

        return code;
    }

    Isaac3ds::TBytes SyntheticExheader() {
        auto exheader = Isaac3ds::TBytes(0x800);
        // Two service dependencies: one kept, one remapped, one dropped (plus zero entries).
        Isaac3ds::Write32(exheader, 0x40, 0x00004002);
        Isaac3ds::Write32(exheader, 0x44, 0x00040130);
        Isaac3ds::Write32(exheader, 0x48, 0x20004002);
        Isaac3ds::Write32(exheader, 0x4C, 0x00040130);
        Isaac3ds::Write32(exheader, 0x50, 0x20004202);
        Isaac3ds::Write32(exheader, 0x54, 0x00040130);
        return exheader;
    }

    Isaac3ds::TBytes SyntheticIcon() {
        auto icon = Isaac3ds::TBytes(0x36C0);
        Isaac3ds::Write32(icon, 0, 0x48444D53);
        Isaac3ds::Write32(icon, 0x2028, 0x11D1);
        return icon;
    }

    // Where an ARM B/BL at `from` lands, or 0 if the word is not a branch.
    std::uint32_t BranchTarget(std::uint32_t word, std::uint32_t from) {
        if ((word & 0x0E000000) != 0x0A000000) {
            return 0;
        }

        auto offset = static_cast<std::int32_t>((word & 0xFFFFFF) << 8) >> 6;
        return static_cast<std::uint32_t>(static_cast<std::int64_t>(from) + 8 + offset);
    }

    void TestBytes() {
        using namespace Isaac3ds;
        Check(Hex(FromHex("00ff10Ab")) == "00ff10ab", "hex round trip");
        Check(Throws([] { FromHex("abc"); }), "odd-length hex rejected");
        Check(Throws([] { FromHex("zz"); }), "non-hex characters rejected");
        auto data = TBytes(8);
        Write32(data, 2, 0xA1B2C3D4);
        Check(Read32(data, 2) == 0xA1B2C3D4 && data[2] == 0xD4 && data[5] == 0xA1, "little-endian 32-bit access");
        Check(Throws([&] { Read32(data, 6); }), "read past the end rejected");
        Check(Throws([&] { Write32(data, 5, 1); }), "write past the end rejected");
        Check(Sha256(FromHex("616263")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 of abc");
    }

    void TestPatchApplication() {
        using namespace Isaac3ds;
        const auto code = SyntheticCode();
        const auto result = ApplyPatches(code, SyntheticExheader(), SyntheticIcon());
        const auto word = [&](std::uint32_t address) { return Read32(result.code, address - 0x100000); };

        Check(word(0x2FF304) == 0x4 && word(0x2FF314) == 0x80 && word(0x2FF254) == 0xFFFFFFFF, "control remaps");
        Check(word(0x2E8AC8) == 42U << 20, "default heap is 42 MiB");
        Check(BranchTarget(word(0x1C9EFC), 0x1C9EFC) == textureUploadAddress, "upload hook lands on UploadTexels");
        Check(BranchTarget(word(0x2C47E4), 0x2C47E4) == textureBindAddress, "bind hook lands on BindTexture");
        Check(BranchTarget(word(0x16571C), 0x16571C) == textureScratchAddress, "scratch hook lands on AllocateProceduralScratch");
        Check((word(0x1C9EFC) >> 24) == 0xEA && (word(0x16571C) >> 24) == 0xEB, "plain branch for upload, branch-with-link for scratch");
        Check(word(0x2F5DE4) == texturePixelWriteAddress && word(0x2F5E74) == texturePixelWriteAddress, "pixel write pointers");
        Check(word(0x2F5DE8) == texturePixelReadAddress && word(0x2F5E78) == texturePixelReadAddress, "pixel read pointers");

        const auto payload = FromHex(texturePayloadHex);
        auto expectedPayload = payload;
        expectedPayload[textureDitherAddress - texturePayloadBase] = defaultDither;
        Check(std::equal(expectedPayload.begin(), expectedPayload.end(), result.code.begin() + (texturePayloadBase - 0x100000)), "payload copied into the padding");
        Check(result.code[textureDitherAddress - 0x100000] == defaultDither, "default dither strength written");
        Check(Read32(result.exheader, 0x18) == texturePayloadBase - 0x100000 + payload.size(), "text size covers the payload");
        Check(Read32(result.exheader, 0x40) == 0x00004002 && Read32(result.exheader, 0x44) == 0x00040130, "dependency kept");
        Check(Read32(result.exheader, 0x48) == 0x00004002, "dependency 20004002 remapped to 00004002");
        Check(Read32(result.exheader, 0x50) == 0 && Read32(result.exheader, 0x54) == 0, "dependency 20004202 dropped");
        Check(Read32(result.icon, 0x2028) == 0x1D1, "icon flag updated");
        Check(Sha256(result.code) != Sha256(code), "executable actually changed");
        Check(result.report.find("42 MiB") != std::string::npos, "report states the heap size");
    }

    void TestOptions() {
        using namespace Isaac3ds;
        const auto code = SyntheticCode();
        const auto exheader = SyntheticExheader();
        const auto icon = SyntheticIcon();
        auto custom = TPatchOptions{};
        custom.heapMiB = 45;
        custom.dither = 0;
        const auto result = ApplyPatches(code, exheader, icon, custom);
        Check(Read32(result.code, 0x2E8AC8 - 0x100000) == 45U << 20, "custom heap size applied");
        Check(result.code[textureDitherAddress - 0x100000] == 0, "custom dither applied");
        Check(Throws([&] { ApplyPatches(code, exheader, icon, TPatchOptions{minimumHeapMiB - 1, 2}); }), "heap below minimum rejected");
        Check(Throws([&] { ApplyPatches(code, exheader, icon, TPatchOptions{maximumHeapMiB + 1, 2}); }), "heap above maximum rejected");
        Check(Throws([&] { ApplyPatches(code, exheader, icon, TPatchOptions{42, 3}); }), "dither above maximum rejected");
        Check(Read32(ApplyPatches(code, exheader, icon).code, 0x1055CC - 0x100000) == 0xE3A0001E, "default leaves the CPU limit untouched");
        Check(Read32(ApplyPatches(code, exheader, icon, TPatchOptions{42, 2, 60}).code, 0x1055CC - 0x100000) == 0xE3A0003C, "CPU limit 60 encodes mov r0,#60");
        Check(Throws([&] { ApplyPatches(code, exheader, icon, TPatchOptions{42, 2, 29}); }), "CPU limit below 30 rejected");
        Check(Throws([&] { ApplyPatches(code, exheader, icon, TPatchOptions{42, 2, 81}); }), "CPU limit above 80 rejected");
        Check(!Throws([&] { ApplyPatches(code, exheader, icon, TPatchOptions{minimumHeapMiB, 0}); }), "minimum heap accepted");
        Check(!Throws([&] { ApplyPatches(code, exheader, icon, TPatchOptions{maximumHeapMiB, 2}); }), "maximum heap accepted");
    }

    void TestRejections() {
        using namespace Isaac3ds;
        const auto code = SyntheticCode();
        const auto exheader = SyntheticExheader();
        const auto icon = SyntheticIcon();

        auto changed = code;
        Write32(changed, 0x2FF304 - 0x100000, 0x1234);
        Check(Throws([&] { ApplyPatches(changed, exheader, icon); }), "unexpected original word rejected");

        auto occupied = code;
        occupied[0x1E6200 + 10] = 1;
        Check(Throws([&] { ApplyPatches(occupied, exheader, icon); }), "occupied payload storage rejected");
        Check(Throws([&] { ApplyPatches(TBytes(100), exheader, icon); }), "wrong executable size rejected");
        Check(Throws([&] { ApplyPatches(code, TBytes(0x400), icon); }), "short extended header rejected");

        // The synthetic executable is not the supported game, so the full entry point must refuse it.
        Check(Throws([&] { PatchGame(code, exheader, icon); }), "PatchGame refuses an unsupported executable");

        const auto patched = ApplyPatches(code, exheader, icon);
        Check(!Throws([&] { VerifyRebuiltProfile(patched.exheader, patched.exheader); }), "identical headers verify");
        auto rebuilt = patched.exheader;
        rebuilt[0x30] ^= 1;
        Check(Throws([&] { VerifyRebuiltProfile(rebuilt, patched.exheader); }), "changed header detected");
        rebuilt = patched.exheader;
        rebuilt[0xD] ^= 1;
        Check(!Throws([&] { VerifyRebuiltProfile(rebuilt, patched.exheader); }), "only the two regenerated bits may differ");
    }

    void Put64(Isaac3ds::TBytes &data, std::size_t offset, std::uint64_t value) {
        Isaac3ds::Write32(data, offset, static_cast<std::uint32_t>(value));
        Isaac3ds::Write32(data, offset + 4, static_cast<std::uint32_t>(value >> 32));
    }

    void PutName(Isaac3ds::TBytes &data, std::size_t offset, const std::string &name) {
        for (auto i = std::size_t{0}; i < name.size(); ++i) {
            data[offset + 2 * i] = static_cast<std::uint8_t>(name[i]);
        }
    }

    Isaac3ds::TBytes SyntheticOgg() {
        auto ogg = Isaac3ds::TBytes(58 + 27);
        std::copy_n("OggS", 4, ogg.begin());
        ogg[26] = 1;
        ogg[27] = 30;
        ogg[28] = 1;
        std::copy_n("vorbis", 6, ogg.begin() + 29);
        ogg[28 + 11] = 2;
        Isaac3ds::Write32(ogg, 28 + 12, 44100);
        Isaac3ds::Write32(ogg, 28 + 20, 128000);
        std::copy_n("OggS", 4, ogg.begin() + 58);
        Put64(ogg, 58 + 6, 88200);
        return ogg;
    }

    // Root with a.xml and a "music" folder holding b.ogg; hash tables are empty because the parser ignores them.
    Isaac3ds::TBytes SyntheticRomFs(const std::string &folderName = "music") {
        using namespace Isaac3ds;
        const auto ogg = SyntheticOgg();
        const auto xml = std::string("<a/>\n");
        auto image = TBytes(0xBC + 8 + ogg.size());
        Write32(image, 0x00, 0x28);
        Write32(image, 0x0C, 0x28);
        Write32(image, 0x10, 0x3C);
        Write32(image, 0x1C, 0x64);
        Write32(image, 0x20, 0x58);
        Write32(image, 0x24, 0xBC);
        // Directory table: root at 0, the folder at 0x18.
        Write32(image, 0x28 + 0x04, 0xFFFFFFFF);
        Write32(image, 0x28 + 0x08, 0x18);
        Write32(image, 0x28 + 0x0C, 0);
        Write32(image, 0x28 + 0x10, 0xFFFFFFFF);
        Write32(image, 0x28 + 0x14, 0);
        Write32(image, 0x28 + 0x18 + 0x04, 0xFFFFFFFF);
        Write32(image, 0x28 + 0x18 + 0x08, 0xFFFFFFFF);
        Write32(image, 0x28 + 0x18 + 0x0C, 0x2C);
        Write32(image, 0x28 + 0x18 + 0x10, 0xFFFFFFFF);
        Write32(image, 0x28 + 0x18 + 0x14, static_cast<std::uint32_t>(folderName.size() * 2));
        PutName(image, 0x28 + 0x18 + 0x18, folderName);
        // File table: a.xml at 0, b.ogg at 0x2C.
        Write32(image, 0x64 + 0x04, 0xFFFFFFFF);
        Put64(image, 0x64 + 0x08, 0);
        Put64(image, 0x64 + 0x10, xml.size());
        Write32(image, 0x64 + 0x18, 0xFFFFFFFF);
        Write32(image, 0x64 + 0x1C, 10);
        PutName(image, 0x64 + 0x20, "a.xml");
        Write32(image, 0x64 + 0x2C + 0x00, 0x18);
        Write32(image, 0x64 + 0x2C + 0x04, 0xFFFFFFFF);
        Put64(image, 0x64 + 0x2C + 0x08, 8);
        Put64(image, 0x64 + 0x2C + 0x10, ogg.size());
        Write32(image, 0x64 + 0x2C + 0x18, 0xFFFFFFFF);
        Write32(image, 0x64 + 0x2C + 0x1C, 10);
        PutName(image, 0x64 + 0x2C + 0x20, "b.ogg");
        std::copy(xml.begin(), xml.end(), image.begin() + 0xBC);
        std::copy(ogg.begin(), ogg.end(), image.begin() + 0xBC + 8);
        return image;
    }

    void TestRomFs() {
        using namespace Isaac3ds;
        const auto image = SyntheticRomFs();
        const auto reader = [&](std::uint64_t offset, std::uint64_t length) {
            if (offset + length > image.size()) {
                throw std::runtime_error("read past the end");
            }

            return TBytes(image.begin() + static_cast<std::ptrdiff_t>(offset), image.begin() + static_cast<std::ptrdiff_t>(offset + length));
        };

        const auto entries = ListRomFs(reader, image.size());
        Check(entries.size() == 2 && entries[0].path == "a.xml" && entries[1].path == "music/b.ogg", "RomFS tree is listed with paths");
        Check(entries.size() == 2 && entries[0].size == 5 && entries[1].offset == 0xBC + 8, "RomFS sizes and offsets are absolute");
        const auto ogg = SyntheticOgg();
        const auto described = DescribeOgg(TBytes(ogg.begin(), ogg.begin() + 58), ogg, ogg.size());
        Check(described.find("2 ch, 44100 Hz, nominal bitrate 128000 bps, 2.0 s") != std::string::npos, "Ogg parameters are described");
        Check(DescribeOgg(TBytes(8), TBytes(8), 8).find("not an Ogg") != std::string::npos, "non-Ogg data is reported");

        const auto readerFor = [](const TBytes &data) {
            return [data](std::uint64_t offset, std::uint64_t length) {
                return TBytes(data.begin() + static_cast<std::ptrdiff_t>(offset), data.begin() + static_cast<std::ptrdiff_t>(offset + length));
            };
        };
        const auto traversal = SyntheticRomFs("..");
        Check(Throws([&] { ListRomFs(readerFor(traversal), traversal.size()); }), "a '..' folder name is rejected");
        auto looping = SyntheticRomFs();
        Write32(looping, 0x28 + 0x08, 0);
        Check(Throws([&] { ListRomFs(readerFor(looping), looping.size()); }), "a directory loop is rejected");
        auto outside = SyntheticRomFs();
        Put64(outside, 0x64 + 0x10, 1ULL << 40);
        Check(Throws([&] { ListRomFs(readerFor(outside), outside.size()); }), "a file past the end of the image is rejected");

        // ctrtool writes the whole partition: an IVFC header, then the file-system data at the next 0x1000 boundary.
        auto wrapped = TBytes(0x1000);
        Write32(wrapped, 0, 0x43465649);
        Write32(wrapped, 4, 0x10000);
        Write32(wrapped, 0x08, 0x20);
        Put64(wrapped, 0x44, image.size());
        Write32(wrapped, 0x4C, 12);
        wrapped.insert(wrapped.end(), image.begin(), image.end());
        wrapped.resize(wrapped.size() + 0x800); // trailing hash levels
        const auto located = LocateRomFsData(readerFor(wrapped), wrapped.size());
        Check(located.offset == 0x1000 && located.size == image.size(), "IVFC-wrapped data is located after the header");
        Check(LocateRomFsData(reader, image.size()).offset == 0, "a bare file-system image is accepted");
        Check(Throws([&] { LocateRomFsData(readerFor(TBytes(0x2000)), 0x2000); }), "an unrecognised image is rejected");

        const auto base = std::filesystem::temp_directory_path() / ("isaac3ds-romfs-test-" + std::to_string(reinterpret_cast<std::uintptr_t>(&image)));
        std::filesystem::remove_all(base);
        std::filesystem::create_directory(base);
        WriteFile(base / "romfs.bin", wrapped);
        DumpRomFs(base / "romfs.bin", base / "dump");
        Check(std::filesystem::is_regular_file(base / "dump/files/a.xml") && !std::filesystem::exists(base / "dump/files/music/b.ogg"),
            "only small data files are copied");
        Check(ReadFile(base / "dump/files/a.xml") == TBytes({'<', 'a', '/', '>', '\n'}), "copied data comes from the right place in the image");
        Check(std::filesystem::is_regular_file(base / "dump/listing.txt") && std::filesystem::is_regular_file(base / "dump/ogg-info.txt"),
            "listing and audio info are written");
        Check(Throws([&] { DumpRomFs(base / "romfs.bin", base / "dump"); }), "an existing dump folder is never overwritten");
        Check(!std::filesystem::exists(base / "dump.partial"), "no partial folder is left behind");
        std::filesystem::remove_all(base);
    }
}

int main() {
    TestBytes();
    TestPatchApplication();
    TestOptions();
    TestRejections();
    TestRomFs();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }

    std::cout << "All patcher tests passed\n";
    return 0;
}
