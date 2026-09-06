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
