# Binding of Isaac Old 3DS Patcher

Patches The Binding of Isaac: Rebirth to run on the original 3DS, with lower memory use and controls for its Circle Pad and buttons.

Version 0.1.0 includes the RC1 patch set, tested in Azahar and on a physical Old 3DS, plus smoother texture gradients (dithering) and tunable memory settings. You need your own European v2.4.0 CIA and a console running CFW. No game files or title seeds are included.

## Before you start

- Use a 64-bit x86 Windows or Linux PC.
- Allow about 3 GB of temporary space plus 500 MB for the output, in addition to your original CIA.
- Start with the original base-game CIA, including its electronic manual. Updates, DLC, other versions, and already-patched CIAs are not supported.
- The console needs CFW with signature patches.

| Property | Supported reference |
| --- | --- |
| Region | Europe |
| Version | v2.4.0, CIA version 2112 |
| Product code | `KTR-N-CBRP` |
| Title ID | `000400000F700900` |

See [compatibility and hashes](COMPATIBILITY.md).

## 1. Get the patcher

Download the Linux `.tar.gz` or Windows `.zip` from [Releases](https://github.com/s4ammy/3DSTBOI/releases), extract it, and open a terminal in the extracted folder. Keep the `tools` folder beside the executable. Release builds target Ubuntu 24.04 or newer and Windows 10/11, both x64. Linux needs the OpenSSL 3 runtime (`libssl3t64` on Ubuntu 24.04).

To build from source instead, download **Code > Download ZIP** or clone the repository:

```sh
git clone https://github.com/s4ammy/3DSTBOI.git
cd 3DSTBOI
```

### Linux source build

```sh
sudo apt update
sudo apt install build-essential cmake ninja-build libssl-dev ca-certificates curl
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DISAAC3DS_FETCH_TOOLS=ON
cmake --build build
cd build
```

### Windows source build

Install Visual Studio 2022 Build Tools with **Desktop development with C++**, the Windows SDK, and CMake tools. Open **Developer PowerShell for VS 2022** in the source folder:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DISAAC3DS_FETCH_TOOLS=ON
cmake --build build --config Release
cd build\Release
```

`ISAAC3DS_FETCH_TOOLS=ON` downloads pinned copies of `ctrtool` and `makerom` and checks their archive hashes. It does not download the game or its seed. MinGW-w64 is also supported. See [dependencies](THIRD_PARTY.md).

Run the remaining commands from the folder containing the patcher executable.

## 2. Prepare your CIA and title seed

Copy your original CIA into that folder as `Isaac.cia`, or substitute its quoted path below. The input is never modified.

Download the encrypted title's 16-byte seed from Nintendo:

### Linux

```sh
curl --fail --location --output "title-seed.bin" "https://kagiya-ctr.cdn.nintendo.net/title/0x000400000F700900/ext_key?country=GB" &&
wc -c < "title-seed.bin"
```

### Windows PowerShell

```powershell
curl.exe --fail --location --output "title-seed.bin" "https://kagiya-ctr.cdn.nintendo.net/title/0x000400000F700900/ext_key?country=GB"
if ($LASTEXITCODE -ne 0) {
    throw "Seed download failed. See Troubleshooting."
}

(Get-Item ".\title-seed.bin").Length
```

The size must be `16`. You can also [download it in a browser](https://kagiya-ctr.cdn.nintendo.net/title/0x000400000F700900/ext_key?country=GB) and save the binary file as `title-seed.bin`. Do not copy its displayed contents into a text file.

- Already have a raw seed? Use `--seed-file "path/to/seed.bin"`.
- Have a seed database? Replace that option with `--seeddb "path/to/seeddb.bin"`, or put `seeddb.bin` beside the CIA or patcher.
- Have an already-decrypted original CIA? Omit the seed option.

Use either `--seed-file` or `--seeddb`, not both. Minecraft's seed will not work for Isaac.

## 3. Check and patch the CIA

Run an optional dry run first:

### Linux

```sh
./isaac3ds-patcher "Isaac.cia" --seed-file "title-seed.bin" --dry-run
```

### Windows PowerShell

```powershell
.\isaac3ds-patcher.exe "Isaac.cia" --seed-file "title-seed.bin" --dry-run
```

After `Dry run passed`, build the patched CIA:

### Linux

```sh
./isaac3ds-patcher "Isaac.cia" --seed-file "title-seed.bin" -o "Isaac-old3ds.cia"
```

### Windows PowerShell

```powershell
.\isaac3ds-patcher.exe "Isaac.cia" --seed-file "title-seed.bin" -o "Isaac-old3ds.cia"
```

Wait for `Done:`. This creates `Isaac-old3ds.cia` and `Isaac-old3ds.cia.report.txt`, containing hashes and verification results. Existing outputs are never overwritten; choose another name for a repeat run. Without `-o`, the filename adds `-old3ds` before `.cia`.

The banner, electronic manual, and game resources are preserved. See [included patches](PATCHES.md).

## 4. Install and play

1. Back up existing Isaac saves. The patch uses the same title ID and replaces the installed base game.
2. Remove any separate Isaac update, which can override the patched code. Check that you selected the update rather than the base game or save data.
3. Power off the console and copy `Isaac-old3ds.cia` into `cias` on its SD card. Leave space for both the CIA and installed game.
4. Reinsert the card and open [FBI](https://github.com/Steveice10/FBI). Select **SD > cias > Isaac-old3ds.cia > Install CIA**.
5. Launch Isaac from the HOME Menu. Do not install an update over the patched game.

Move with the Circle Pad and shoot with ABXY. SELECT uses the active item, L places a bomb, R uses a pill/card, and holding D-pad Down drops held items. No C-stick or Circle Pad Pro is needed. See [all controls](INPUTS_EXPLAINED.md).

If saved bindings override these defaults, reset them in the main menu's **Options > Controls**.

## Troubleshooting

| Problem | What to do |
| --- | --- |
| Seed download fails | Retry or use an existing seed/database. The file must be exactly 16 bytes; an error page is not a seed. |
| Seed certificate error | Prefer an existing seed or a trusted certificate with `--cacert`. The development workaround was adding `--insecure` to this download only; it disables server authentication. Size alone does not prove authenticity. See [curl certificates](https://curl.se/docs/sslcerts.html). |
| Missing `ctrtool` or `makerom` | Restore the `tools` folder or pass `--tools-dir "folder"`. |
| Output or report already exists | Choose a new output filename with `-o`. |
| Unsupported executable or integrity error | Use the original European v2.4.0 CIA with its manual and correct seed. See [hashes](COMPATIBILITY.md). |
| Game still requires New 3DS hardware | Check that you installed the patched output and no separate update overrides it. |
| Colors or transparency look different | Textures use lower color and alpha precision to reduce memory use. |

Run `--help` for all options. Include the patch report and console/crash details when reporting a problem, without game files or seeds.

## Known issues and tuning

Old 3DS memory is tight, and the reports so far are consistent with memory pressure rather than CPU speed: music that is missing or choppy (shorter tracks like the shop theme work), a previous room's background staying, doors disappearing, and crashes against Mom. The patch splits 80 MiB between the ordinary heap (audio and game objects) and the linear heap (textures). Try other splits and see which symptoms change:

```sh
./isaac3ds-patcher "Isaac.cia" --seed-file "title-seed.bin" --heap-mib 45 -o "Isaac-heap45.cia"
```

Higher values favor audio; lower values favor graphics. Patched CIAs share one title ID, so install one at a time. If gradients look grainy rather than smooth, try `--dither light`; `--dither off` restores plain rounding. Settings other than the defaults are listed as experimental in the report.

## Automatic builds and releases

GitHub Actions builds Linux and Windows packages on pushes and pull requests. Pushing a version tag publishes both packages and `SHA256SUMS.txt` as a release after both builds pass. The tag must match the version in `CMakeLists.txt`:

```sh
git tag v0.1.0
git push origin v0.1.0
```

Push the committed project first. Tags such as `v0.1.0-rc1` create prereleases. The workflow uses GitHub's built-in token; no personal token is needed. See [GitHub's tag triggers](https://docs.github.com/en/actions/how-tos/write-workflows/choose-when-workflows-run/trigger-a-workflow#using-filters-to-target-specific-branches-or-tags-for-push-events).

## Licence

The patcher is [MIT licensed](LICENSE), with shared infrastructure adapted from [Minecraft Old 3DS Patcher](https://github.com/s4ammy/3DSMinecraft). Isaac and external dependencies keep their own licences. This project is not affiliated with Nicalis or Nintendo.
