#include "Pipeline.h"

#include <iostream>
#include <stdexcept>

namespace Isaac3ds {
    void PrintHelp();
    int Main(const std::vector<std::string> &arguments);
}

void Isaac3ds::PrintHelp() {
    std::cout << "Isaac Old 3DS Patcher " ISAAC3DS_VERSION "\n\n"
                 "Usage: isaac3ds-patcher input.cia [options]\n\n"
                 "  -o, --output FILE       Output CIA (default: input-old3ds.cia)\n"
                 "  --tools-dir DIRECTORY   Folder containing ctrtool and makerom\n"
                 "  --seeddb FILE           Local seeddb.bin for encrypted titles\n"
                 "  --seed-file FILE        Local 16-byte title seed instead of seeddb\n"
                 "  --heap-mib N            EXPERIMENTAL: ordinary heap size in MiB, 32 to 47 (default 42)\n"
                 "                          Lower values leave more memory for textures; higher values for audio\n"
                 "  --dither off|light|full Texture dithering to hide banding in gradients (default full)\n"
                 "  --cpu-limit N           EXPERIMENTAL: APT CPU time limit for core 1 in percent, 30 to 80 (default 30,\n"
                 "                          the game's own value). Raises the share of core 1 given to the game's workers\n"
                 "  --dump-code FILE        Also save the decrypted original executable (code.bin) for analysis\n"
                 "                          of your own copy; never share or commit it\n"
                 "  --dump-romfs FOLDER     Also save a file listing, audio parameters and the small .xml/.anm2 data files\n"
                 "                          of your own copy (a new folder; never share or commit it)\n"
                 "  --dry-run               Decrypt and check all patches without building a CIA\n"
                 "  -h, --help              Show this help\n\n"
                 "The input is never modified and existing outputs are never overwritten.\n"
                 "Only the documented European v2.4.0 original executable is accepted.\n"
                 "RC1 has passed emulation and has been tested on a physical Old 3DS.\n"
                 "The result is an unencrypted, test-signed CIA for CFW, not stock firmware.\n";
}

int Isaac3ds::Main(const std::vector<std::string> &arguments) {
    try {
        if (arguments.empty()) {
            PrintHelp();
            return 0;
        }

        auto options = TOptions{};
        for (auto index = std::size_t{0}; index < arguments.size(); ++index) {
            const auto &argument = arguments[index];
            const auto value = [&]() {
                if (++index >= arguments.size()) {
                    throw std::runtime_error("Missing value after " + argument);
                }

                return std::filesystem::u8path(arguments[index]);
            };

            if (argument == "--help" || argument == "-h") {
                PrintHelp();
                return 0;
            } else if (argument == "--output" || argument == "-o") {
                options.output = value();
            } else if (argument == "--tools-dir") {
                options.toolsDirectory = value();
            } else if (argument == "--seeddb") {
                options.seedDatabase = value();
            } else if (argument == "--seed-file") {
                options.seedFile = value();
            } else if (argument == "--heap-mib") {
                const auto text = value().u8string();
                auto consumed = std::size_t{0};
                auto number = 0UL;
                try {
                    number = std::stoul(text, &consumed);
                } catch (const std::exception &) {
                    consumed = 0;
                }

                if (consumed == 0 || consumed != text.size() || number < minimumHeapMiB || number > maximumHeapMiB) {
                    throw std::runtime_error("--heap-mib needs a whole number from " + std::to_string(minimumHeapMiB) +
                                             " to " + std::to_string(maximumHeapMiB));
                }

                options.heapMiB = static_cast<std::uint32_t>(number);
            } else if (argument == "--cpu-limit") {
                const auto text = value().u8string();
                auto consumed = std::size_t{0};
                auto number = 0UL;
                try {
                    number = std::stoul(text, &consumed);
                } catch (const std::exception &) {
                    consumed = 0;
                }

                if (consumed == 0 || consumed != text.size() || number < defaultCpuLimit || number > maximumCpuLimit) {
                    throw std::runtime_error("--cpu-limit needs a whole number from " + std::to_string(defaultCpuLimit) +
                                             " to " + std::to_string(maximumCpuLimit));
                }

                options.cpuLimit = static_cast<std::uint32_t>(number);
            } else if (argument == "--dump-romfs") {
                options.dumpRomfs = value();
            } else if (argument == "--dump-code") {
                options.dumpCode = value();
            } else if (argument == "--dither") {
                const auto text = value().u8string();
                if (text == "off") {
                    options.dither = 0;
                } else if (text == "light") {
                    options.dither = 1;
                } else if (text == "full") {
                    options.dither = 2;
                } else {
                    throw std::runtime_error("--dither must be off, light, or full");
                }
            } else if (argument == "--dry-run") {
                options.dryRun = true;
            } else if (!argument.empty() && argument.front() == '-') {
                throw std::runtime_error("Unknown option: " + argument);
            } else if (options.input.empty()) {
                options.input = std::filesystem::u8path(argument);
            } else {
                throw std::runtime_error("Only one input CIA is accepted");
            }
        }

        if (options.input.empty()) {
            throw std::runtime_error("An input CIA is required");
        }

        if (!options.seedFile.empty() && !options.seedDatabase.empty()) {
            throw std::runtime_error("Use either --seeddb or --seed-file, not both");
        }

        RunPatcher(options);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Error: " << error.what() << "\nNo changes were made to the input CIA.\n";
        return 1;
    }
}

#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
    auto arguments = std::vector<std::string>{};
    for (auto index = 1; index < argc; ++index) {
        arguments.push_back(std::filesystem::path(argv[index]).u8string());
    }
    return Isaac3ds::Main(arguments);
}
#else
int main(int argc, char **argv) {
    return Isaac3ds::Main(std::vector<std::string>(argv + 1, argv + argc));
}
#endif
