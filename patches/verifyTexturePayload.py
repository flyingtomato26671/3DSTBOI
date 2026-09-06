import argparse
import hashlib
import re
import subprocess
from pathlib import Path


def VerifyTexturePayload():
    parser = argparse.ArgumentParser(description="Compile the authored ARM source and compare it with the embedded RC1 payload")
    parser.add_argument("--output", type=Path, default=Path("build/payload"))
    arguments = parser.parse_args()
    projectPath = Path(__file__).resolve().parents[1]
    patchPath = projectPath / "patches"
    outputPath = arguments.output.resolve()
    outputPath.mkdir(parents=True, exist_ok=True)
    objectPath = outputPath / "texture16.o"
    elfPath = outputPath / "texture16.elf"
    binaryPath = outputPath / "texture16.bin"
    commands = [
        ["clang++", "--target=arm-none-eabi", "-march=armv6k", "-marm", "-mfpu=vfp",
         "-mfloat-abi=softfp", "-Oz", "-std=c++17", "-ffreestanding", "-fno-exceptions",
         "-fno-rtti", "-fno-unwind-tables", "-fno-asynchronous-unwind-tables", "-c",
         str(patchPath / "Texture16.cpp"), "-o", str(objectPath)],
        ["ld.lld", "-T", str(patchPath / "texture16.ld"), str(objectPath), "-o", str(elfPath)],
        ["llvm-objcopy", "-O", "binary", str(elfPath), str(binaryPath)],
    ]
    for command in commands:
        subprocess.run(command, check=True)

    symbolText = subprocess.check_output(["llvm-nm", "-n", str(elfPath)], text=True)
    (outputPath / "texture16.symbols").write_text(symbolText)
    headerText = (projectPath / "src/TextureData.h").read_text()
    expected = bytes.fromhex("".join(re.findall(r'"([0-9a-fA-F]+)"', headerText)))
    actual = binaryPath.read_bytes()
    if actual != expected:
        raise RuntimeError("Compiler output differs from the embedded RC1 payload. The reference used LLVM 22.1.8; do not replace it without revalidating the patch profile.")

    rebuildText = (projectPath / "src/RebuildData.h").read_text()
    for name in ["isaac-old80.rsf", "manual.rsf"]:
        if (patchPath / name).read_text() not in rebuildText:
            raise RuntimeError("Embedded rebuild settings differ from " + name)

    print("Authored payload reproduced: " + str(len(actual)) + " bytes, SHA-256 " + hashlib.sha256(actual).hexdigest())


if __name__ == "__main__":
    VerifyTexturePayload()
