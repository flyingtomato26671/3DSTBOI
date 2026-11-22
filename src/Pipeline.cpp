#include "Pipeline.h"
#include "RebuildData.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace Isaac3ds {
    struct TCiaMetadata {
        std::uint64_t titleId;
        std::uint16_t version;
        std::uint16_t ticketVersion;
        std::uint64_t tmdVersionOffset;
        std::uint64_t ticketVersionOffset;
        std::uint32_t mainContentId;
        std::uint32_t manualContentId;
    };

    TBytes ReadAt(std::ifstream &file, std::uint64_t offset, std::size_t size, std::uint64_t fileSize);
    std::uint64_t ReadBig(const TBytes &data, std::size_t offset, std::size_t size);
    std::size_t SignatureSize(const TBytes &data);
    TCiaMetadata ReadCiaMetadata(const std::filesystem::path &path);
    void PreserveTitleVersion(const std::filesystem::path &path, const TCiaMetadata &original);
    std::filesystem::path FindTool(const std::filesystem::path &requested, const std::string &name);
    bool IsSignatureFailure(const std::string &line);
    void CheckToolResult(const TProcessResult &result, const std::string &stage, const std::vector<std::string> &markers);
    std::vector<std::string> HashMarkers();
}

Isaac3ds::TBytes Isaac3ds::ReadAt(std::ifstream &file, std::uint64_t offset, std::size_t size, std::uint64_t fileSize) {
    if (offset > fileSize || size > fileSize - offset) {
        throw std::runtime_error("CIA metadata extends outside the file");
    }

    auto data = TBytes(size);
    file.seekg(static_cast<std::streamoff>(offset));
    if (!file.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(data.size()))) {
        throw std::runtime_error("Cannot read CIA metadata");
    }

    return data;
}

std::uint64_t Isaac3ds::ReadBig(const TBytes &data, std::size_t offset, std::size_t size) {
    RequireRange(data.size(), offset, size);
    auto result = std::uint64_t{0};
    for (auto index = std::size_t{0}; index < size; ++index) {
        result = (result << 8) | data[offset + index];
    }

    return result;
}

std::size_t Isaac3ds::SignatureSize(const TBytes &data) {
    const auto type = ReadBig(data, 0, 4);
    if (type == 0x10003 || type == 0x10000) {
        return 0x240;
    }

    if (type == 0x10004 || type == 0x10001) {
        return 0x140;
    }

    if (type == 0x10005 || type == 0x10002) {
        return 0x80;
    }

    throw std::runtime_error("Unsupported metadata signature type");
}

Isaac3ds::TCiaMetadata Isaac3ds::ReadCiaMetadata(const std::filesystem::path &path) {
    const auto size = std::filesystem::file_size(path);
    if (size < 0x2020 || size > 0x100000000ULL) {
        throw std::runtime_error("Input is not a supported-size CIA");
    }

    auto file = std::ifstream(path, std::ios::binary);
    const auto header = ReadAt(file, 0, 0x20, size);
    if (Read32(header, 0) != 0x2020 || Read32(header, 4) != 0) {
        throw std::runtime_error("Expected a normal CIA header");
    }

    const auto align = [](std::uint64_t value) {
        return (value + 63) & ~std::uint64_t{63};
    };
    const auto certificateOffset = align(Read32(header, 0));
    const auto ticketOffset = align(certificateOffset + Read32(header, 8));
    const auto tmdOffset = align(ticketOffset + Read32(header, 0xc));
    const auto tmdSize = Read32(header, 0x10);
    if (tmdSize < 0x180 || tmdSize > 4 * 1024 * 1024) {
        throw std::runtime_error("Unsupported TMD size");
    }

    const auto tmd = ReadAt(file, tmdOffset, tmdSize, size);
    const auto contentOffset = align(tmdOffset + tmdSize);
    const auto contentSize = Read64(header, 0x18);
    if (contentOffset > size || contentSize > size - contentOffset) {
        throw std::runtime_error("CIA content size exceeds the file");
    }

    const auto signatureSize = SignatureSize(tmd);
    const auto titleId = ReadBig(tmd, signatureSize + 0x4c, 8);
    if ((titleId >> 32) != 0x00040000 || (titleId & 0xff) != 0) {
        throw std::runtime_error("Only base-game application CIAs are supported, not updates or DLC");
    }

    if (ReadBig(tmd, signatureSize + 0x9E, 2) != 2) {
        throw std::runtime_error("Expected the base application and its electronic manual (two contents)");
    }

    const auto mainRecord = signatureSize + 0x9C4;
    const auto manualRecord = mainRecord + 0x30;
    RequireRange(tmd.size(), mainRecord, 0x60);
    if (ReadBig(tmd, mainRecord + 4, 2) != 0 || ReadBig(tmd, manualRecord + 4, 2) != 1) {
        throw std::runtime_error("Unexpected application/manual content indices");
    }

    const auto ticketSize = Read32(header, 0xc);
    if (ticketSize < 0x180 || ticketSize > 4 * 1024 * 1024) {
        throw std::runtime_error("Unsupported ticket size");
    }

    const auto ticket = ReadAt(file, ticketOffset, ticketSize, size);
    const auto ticketSignatureSize = SignatureSize(ticket);
    if (ReadBig(ticket, ticketSignatureSize + 0x9c, 8) != titleId) {
        throw std::runtime_error("Ticket and TMD title IDs disagree");
    }

    return {titleId, static_cast<std::uint16_t>(ReadBig(tmd, signatureSize + 0x9c, 2)),
        static_cast<std::uint16_t>(ReadBig(ticket, ticketSignatureSize + 0xa6, 2)),
        tmdOffset + signatureSize + 0x9c, ticketOffset + ticketSignatureSize + 0xa6,
        static_cast<std::uint32_t>(ReadBig(tmd, mainRecord, 4)),
        static_cast<std::uint32_t>(ReadBig(tmd, manualRecord, 4))};
}

void Isaac3ds::PreserveTitleVersion(const std::filesystem::path &path, const TCiaMetadata &original) {
    const auto rebuilt = ReadCiaMetadata(path);
    if (rebuilt.titleId != original.titleId) {
        throw std::runtime_error("Rebuild changed the title ID");
    }

    auto file = std::fstream(path, std::ios::binary | std::ios::in | std::ios::out);
    const auto version = std::array<char, 2>{static_cast<char>(original.version >> 8),
        static_cast<char>(original.version & 0xff)};
    for (const auto offset : {rebuilt.tmdVersionOffset, rebuilt.ticketVersionOffset}) {
        file.seekp(static_cast<std::streamoff>(offset));
        file.write(version.data(), static_cast<std::streamsize>(version.size()));
    }

    file.close();
    if (!file) {
        throw std::runtime_error("Cannot preserve the title version in the private output");
    }
}

std::filesystem::path Isaac3ds::FindTool(const std::filesystem::path &requested, const std::string &name) {
#ifdef _WIN32
    const auto filename = name + ".exe";
#else
    const auto filename = name;
#endif
    const auto directories = requested.empty() ? std::vector<std::filesystem::path>{
                                                     ExecutableDirectory() / "tools", ExecutableDirectory()} :
                                                 std::vector<std::filesystem::path>{requested};
    for (const auto &directory : directories) {
        const auto candidate = directory / filename;
        if (std::filesystem::is_regular_file(candidate)) {
            return std::filesystem::canonical(candidate);
        }
    }

    throw std::runtime_error("Missing " + filename + ". Put the pinned Project_CTR tools in the tools folder, "
                                                     "use --tools-dir, or build with ISAAC3DS_FETCH_TOOLS=ON. See README.md.");
}

bool Isaac3ds::IsSignatureFailure(const std::string &line) {
    return line.find("Signature") != std::string::npos || line.find("signature") != std::string::npos;
}

void Isaac3ds::CheckToolResult(const TProcessResult &result, const std::string &stage, const std::vector<std::string> &markers) {
    if (result.exitCode != 0) {
        throw std::runtime_error(stage + " failed (tool exit " + std::to_string(result.exitCode) + ")");
    }

    if (result.output.find("no seed is set") != std::string::npos) {
        throw std::runtime_error("This encrypted CIA needs its title seed. Put seeddb.bin beside the input CIA "
                                 "or use --seeddb FILE / --seed-file FILE. A decrypted CIA does not need a seed.");
    }

    if (result.output.find("Seed check mismatch") != std::string::npos) {
        throw std::runtime_error("The supplied seed does not match this title");
    }

    auto lines = std::istringstream(result.output);
    auto line = std::string{};
    while (std::getline(lines, line)) {
        if ((line.find("(FAIL)") != std::string::npos || line.find("ERROR]") != std::string::npos) && !IsSignatureFailure(line)) {
            throw std::runtime_error(stage + " reported an integrity or decoding error. "
                                             "Check that the CIA is complete and any supplied seed is correct.");
        }
    }

    for (const auto &marker : markers) {
        if (result.output.find(marker) == std::string::npos) {
            throw std::runtime_error(stage + " did not confirm: " + marker);
        }
    }
}

std::vector<std::string> Isaac3ds::HashMarkers() {
    return {"ContentInfo:", "Hash: (GOOD)", "Exheader hash: (GOOD)", "ExeFS hash: (GOOD)",
        "RomFS hash: (GOOD)", "Section hash: (GOOD)", "Level 0: (GOOD)", "Level 1: (GOOD)", "Level 2: (GOOD)"};
}

void Isaac3ds::RunPatcher(const TOptions &options) {
    auto pathError = std::error_code{};
    const auto input = std::filesystem::canonical(options.input, pathError);
    if (pathError) {
        throw std::runtime_error("Cannot open the input CIA: " + PathText(options.input) + " (" + pathError.message() + ")");
    }

    if (!std::filesystem::is_regular_file(input)) {
        throw std::runtime_error("Input must be a regular CIA file");
    }

    const auto suffix = "-old3ds.cia";
    auto output = options.output.empty() ? input.parent_path() / (input.stem().u8string() + suffix) : std::filesystem::absolute(options.output);
    output = std::filesystem::weakly_canonical(output);
    const auto reportPath = std::filesystem::path(output.native() + std::filesystem::path(".report.txt").native());
    if (output == input || (!options.dryRun && (std::filesystem::exists(output) || std::filesystem::exists(reportPath)))) {
        throw std::runtime_error("Output or report already exists. Choose a new output name; files are never overwritten.");
    }

    auto dumpPath = std::filesystem::path{};
    if (!options.dumpCode.empty()) {
        dumpPath = std::filesystem::weakly_canonical(std::filesystem::absolute(options.dumpCode));
        if (dumpPath == input || dumpPath == output || std::filesystem::exists(dumpPath)) {
            throw std::runtime_error("The --dump-code file already exists. Choose a new name; files are never overwritten.");
        }

        if (!std::filesystem::is_directory(dumpPath.parent_path())) {
            throw std::runtime_error("The --dump-code directory does not exist");
        }
    }

    if (!options.dryRun && !std::filesystem::is_directory(output.parent_path())) {
        throw std::runtime_error("The output directory does not exist");
    }

    const auto ctrtool = FindTool(options.toolsDirectory, "ctrtool");
    const auto makerom = options.dryRun ? std::filesystem::path{} : FindTool(options.toolsDirectory, "makerom");
    const auto metadata = ReadCiaMetadata(input);
    if (metadata.titleId != testedTitleId || metadata.version != testedTitleVersion) {
        throw std::runtime_error("Only European Isaac v2.4.0, title 000400000f700900, CIA version 2112 is supported");
    }

    std::cout << "Hashing input CIA..." << std::endl;
    const auto inputHash = Sha256File(input);
    std::cout << "Input SHA-256: " << inputHash << "\n";
    std::cout << (inputHash == testedCiaHash ? "This is the reference CIA.\n" : "Different CIA package. The executable still has to pass the profile checks.\n");
    auto workspace = CWorkspace(std::filesystem::temp_directory_path());
    const auto &work = workspace.path();
    std::filesystem::create_directory(work / "exefs");
    auto seedDatabase = options.seedDatabase;
    if (!options.seedFile.empty()) {
        const auto seed = ReadFile(options.seedFile, 16);
        if (seed.size() != 16) {
            throw std::runtime_error("A raw title seed must be exactly 16 bytes");
        }

        auto database = TBytes(48);
        Write32(database, 0, 1);
        Write32(database, 16, static_cast<std::uint32_t>(metadata.titleId));
        Write32(database, 20, static_cast<std::uint32_t>(metadata.titleId >> 32));
        std::copy(seed.begin(), seed.end(), database.begin() + 24);
        seedDatabase = work / "seeddb.bin";
        WriteFile(seedDatabase, database);
    } else if (seedDatabase.empty()) {
        for (const auto &candidate : {input.parent_path() / "seeddb.bin", ExecutableDirectory() / "seeddb.bin"}) {
            if (std::filesystem::is_regular_file(candidate)) {
                seedDatabase = candidate;
                break;
            }
        }
    }

    auto arguments = std::vector<std::string>{"-v", "-y", "-n", "0", "--exheader=" + PathText(work / "exheader.bin"),
        "--exefsdir=" + PathText(work / "exefs"), "--romfs=" + PathText(work / "romfs.bin")};
    if (!seedDatabase.empty()) {
        seedDatabase = std::filesystem::canonical(seedDatabase);
        if (!std::filesystem::is_regular_file(seedDatabase) || std::filesystem::file_size(seedDatabase) > 16 * 1024 * 1024) {
            throw std::runtime_error("Invalid or oversized seed database");
        }

        arguments.push_back("--seeddb=" + PathText(seedDatabase));
    }

    arguments.push_back(PathText(input));
    std::cout << "Decrypting, decompressing, and checking the input..." << std::endl;
    const auto extraction = RunProcess(ctrtool, arguments);
    CheckToolResult(extraction, "Input extraction", HashMarkers());
    auto productMatch = std::smatch{};
    if (!std::regex_search(extraction.output, productMatch, std::regex("Product code:[ \\t]+(KTR-N-CBRP)"))) {
        throw std::runtime_error("Expected European Isaac product code KTR-N-CBRP");
    }

    const auto productCode = productMatch[1].str();
    const auto exheader = ReadFile(work / "exheader.bin");
    if (Read64(exheader, 0x200) != metadata.titleId || Read64(exheader, 0x1c8) != metadata.titleId ||
        Read64(exheader, 0x1C0) != 0x400000) {
        throw std::runtime_error("Title identity or save-data mapping is inconsistent");
    }

    if (!dumpPath.empty()) {
        const auto originalCode = ReadFile(work / "exefs/code.bin");
        auto dumpStage = CWorkspace(dumpPath.parent_path());
        WriteFile(dumpStage.path() / "code.bin", originalCode);
        PublishFile(dumpStage.path() / "code.bin", dumpPath);
        std::cout << "Saved the decrypted original executable (SHA-256 " << Sha256(originalCode) << ") to: "
                  << PathText(dumpPath) << "\nFor analysis of your own copy only. Do not share or commit it.\n";
    }

    const auto originalRomfsHash = Sha256File(work / "romfs.bin");
    std::cout << "Checking the complete patch profile..." << std::endl;
    const auto patched = PatchGame(ReadFile(work / "exefs/code.bin"), exheader,
        ReadFile(work / "exefs/icon.bin"), TPatchOptions{options.heapMiB, options.dither});
    std::cout << patched.report;
    auto manualArguments = std::vector<std::string>{"-v", "-y", "-n", "1", "--romfs=" + PathText(work / "manual-romfs.bin")};
    if (!seedDatabase.empty()) {
        manualArguments.push_back("--seeddb=" + PathText(seedDatabase));
    }

    manualArguments.push_back(PathText(input));
    std::cout << "Extracting and verifying the electronic manual..." << std::endl;
    const auto manualMarkers = std::vector<std::string>{"ContentInfo:", "Hash: (GOOD)", "ContentType:         Manual",
        "RomFS hash: (GOOD)", "Level 0: (GOOD)", "Level 1: (GOOD)", "Level 2: (GOOD)"};
    CheckToolResult(RunProcess(ctrtool, manualArguments), "Manual extraction", manualMarkers);
    const auto originalManualHash = Sha256File(work / "manual-romfs.bin");
    const auto originalBannerHash = Sha256File(work / "exefs/banner.bin");
    if (options.dryRun) {
        std::cout << "Dry run passed. No output CIA was written.\n";
        return;
    }

    WriteFile(work / "code.bin", patched.code);
    WriteFile(work / "exheader.bin", patched.exheader);
    WriteFile(work / "icon.bin", patched.icon);
    WriteText(work / "rebuild.rsf", rebuildSettings);
    WriteText(work / "manual.rsf", manualSettings);
    std::cout << "Building an unencrypted, test-signed CIA for CFW..." << std::endl;
    const auto built = RunProcess(makerom, {"-f", "ncch", "-o", PathText(work / "main.cxi"), "-rsf", PathText(work / "rebuild.rsf"), "-target", "t", "-exheader", PathText(work / "exheader.bin"), "-code", PathText(work / "code.bin"), "-romfs", PathText(work / "romfs.bin"), "-icon", PathText(work / "icon.bin"), "-banner", PathText(work / "exefs/banner.bin")});
    CheckToolResult(built, "Application build", {});
    CheckToolResult(RunProcess(makerom, {"-f", "ncch", "-o", PathText(work / "manual.cfa"), "-rsf", PathText(work / "manual.rsf"), "-target", "t", "-romfs", PathText(work / "manual-romfs.bin")}), "Manual build", {});
    CheckToolResult(RunProcess(makerom, {"-f", "cia", "-o", PathText(work / "output.cia"), "-target", "t", "-ver", std::to_string(metadata.version), "-content", PathText(work / "main.cxi") + ":0:" + std::to_string(metadata.mainContentId), "-content", PathText(work / "manual.cfa") + ":1:" + std::to_string(metadata.manualContentId)}), "CIA build", {});
    PreserveTitleVersion(work / "output.cia", metadata);
    std::cout << "Verifying the rebuilt CIA and re-extracting its executable..." << std::endl;
    std::filesystem::create_directory(work / "verify-exefs");
    const auto verification = RunProcess(ctrtool, {"-v", "-y", "-n", "0", "--exefsdir=" + PathText(work / "verify-exefs"), "--exheader=" + PathText(work / "verify-exheader.bin"), "--romfs=" + PathText(work / "verify-romfs.bin"), PathText(work / "output.cia")});
    auto markers = HashMarkers();
    const auto modeMarkers = std::vector<std::string>{"IsSnakeOnly: false",
        "System mode:            dev2 (AppMemory: 80MB) (GOOD)",
        "System mode (New3DS):   ctr dev2 (AppMemory: 80MB) (GOOD)",
        "CPU Speed (New3DS):     268MHz (GOOD)", "Enable L2 Cache:        NO (GOOD)",
        "Affinity mask:          1 (GOOD)", "Access Core 2:       NO"};
    markers.insert(markers.end(), modeMarkers.begin(), modeMarkers.end());
    CheckToolResult(verification, "Output verification", markers);
    if (Sha256File(work / "verify-exefs/code.bin") != Sha256(patched.code) ||
        Sha256File(work / "verify-exefs/icon.bin") != Sha256(patched.icon) ||
        Sha256File(work / "verify-exefs/banner.bin") != originalBannerHash ||
        Sha256File(work / "verify-romfs.bin") != originalRomfsHash) {
        throw std::runtime_error("Rebuilt executable, icon, banner, or RomFS differs from the intended output");
    }

    CheckToolResult(RunProcess(ctrtool, {"-v", "-y", "-n", "1", "--romfs=" + PathText(work / "verify-manual-romfs.bin"), PathText(work / "output.cia")}),
        "Rebuilt manual verification", manualMarkers);
    if (Sha256File(work / "verify-manual-romfs.bin") != originalManualHash) {
        throw std::runtime_error("Rebuilt electronic manual differs from the original");
    }

    const auto outputMetadata = ReadCiaMetadata(work / "output.cia");
    if (outputMetadata.titleId != metadata.titleId || outputMetadata.version != metadata.version ||
        outputMetadata.ticketVersion != metadata.version || outputMetadata.mainContentId != metadata.mainContentId ||
        outputMetadata.manualContentId != metadata.manualContentId) {
        throw std::runtime_error("Rebuild changed title identity: expected " + HexNumber(metadata.titleId, 16) +
            " v" + std::to_string(metadata.version) + ", got " + HexNumber(outputMetadata.titleId, 16) +
            " v" + std::to_string(outputMetadata.version));
    }

    const auto verifiedExheader = ReadFile(work / "verify-exheader.bin");
    VerifyRebuiltProfile(verifiedExheader, patched.exheader);
    for (const auto offset : {0x10, 0x14, 0x18, 0x1c, 0x20, 0x24, 0x28, 0x30, 0x34, 0x38, 0x3c}) {
        if (Read32(verifiedExheader, static_cast<std::size_t>(offset)) != Read32(patched.exheader, static_cast<std::size_t>(offset))) {
            throw std::runtime_error("Rebuild changed the executable memory layout");
        }
    }

    if (Sha256File(input) != inputHash) {
        throw std::runtime_error("Input CIA changed while the patcher was running");
    }

    const auto outputHash = Sha256File(work / "output.cia");
    auto report = std::ostringstream{};
    report << "Isaac Old 3DS Patcher " ISAAC3DS_VERSION "\nInput CIA SHA-256: " << inputHash << "\nOutput CIA SHA-256: " << outputHash << "\nTitle ID: " << HexNumber(metadata.titleId, 16) << "\nTitle version: " << metadata.version << "\nProduct code: " << productCode << "\n"
           << patched.report << "Input file unchanged. Package hashes and re-extracted payload verified.\n"
           << "Unencrypted CFW package. Ticket/TMD version restored after test-key signing. Retail signatures are invalid.\n"
           << "Original game assets, banner, electronic manual, and 4 MiB save allocation preserved.\n"
           << "Game RomFS SHA-256: " << originalRomfsHash << "\nManual RomFS SHA-256: " << originalManualHash << "\n"
           << "This report contains no ticket keys or title seeds.\n";
    WriteText(work / "report.txt", report.str());
    auto outputStage = CWorkspace(output.parent_path());
    std::filesystem::copy_file(work / "output.cia", outputStage.path() / "output.cia");
    if (Sha256File(outputStage.path() / "output.cia") != outputHash) {
        throw std::runtime_error("Final output copy failed its checksum");
    }

    std::filesystem::copy_file(work / "report.txt", outputStage.path() / "report.txt");
    PublishFile(outputStage.path() / "report.txt", reportPath);
    try {
        PublishFile(outputStage.path() / "output.cia", output);
    } catch (...) {
        auto error = std::error_code{};
        std::filesystem::remove(reportPath, error);
        throw;
    }

    std::cout << "Done: " << PathText(output) << "\nOutput SHA-256: " << outputHash << "\nReport: " << PathText(reportPath) << "\n";
}
