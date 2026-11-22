# Included patches

Version 0.1.0 includes the RC1 patch set plus the texture-quality and tuning changes below:

- Old 3DS launch flags, 80 MiB application mode, normal CPU/cache settings, and no extra-core requirement.
- A 42 MiB ordinary heap by default (`--heap-mib` changes it), leaving more memory for the linear graphics heap.
- Loaded PCX/PNG textures stored as RGB565 or RGBA4444, rounded to nearest and ordered-dithered to hide banding in gradients, with matching GPU binding and pixel access.
- Reuse of existing scratch storage during procedural texture initialization.
- SELECT for active items and D-pad Down for Drop, replacing ZL and ZR.

The original banner, electronic manual, game resources, title ID, and 4 MiB save allocation are preserved. The patcher requires the exact supported executable and checks the completed CIA before writing the output.

## Texture memory

The new texture formats reduce color and alpha precision to save memory. Procedural textures and render targets retain their original formats because the game's framebuffer copies expect four-byte pixels. Initialization reuses the existing 4 MiB decode scratch when available, falling back to the original allocator otherwise.

The authored ARM payload uses 1,693 bytes of text padding at `0x2E6200`, with no additional memory pages. Upload, GPU binding, pixel access, and scratch allocation hooks are defined in [src/Patches.cpp](src/Patches.cpp); their branch targets are computed from the payload's symbol addresses, which `patches/verifyTexturePayload.py --update` writes into [src/TextureData.h](src/TextureData.h). Source is in [patches/Texture16.cpp](patches/Texture16.cpp).

### Dithering

Earlier builds truncated each channel, which darkened images slightly and left visible bands in shadows, vignettes, and other soft gradients. The payload now rounds to nearest and adds a 4x4 ordered (Bayer) dither pattern before reducing to 4 or 5-6 bits, but only where banding would actually show. For each pixel it looks 8 pixels away in each direction (along each axis only the quieter side counts, so sprite edges do not hide a gradient): flat areas and sharp features are rounded without a pattern, and slowly varying areas get the dither. Black and white are preserved exactly, and all channels of a pixel share one threshold so the grain stays gray.

`--dither off|light|full` (default `full`) sets the pattern strength; `off` is plain rounding. Pixel writes after load (`WritePixel`) always round without a pattern. Conversion costs roughly 110-150 emulated ARM instructions per pixel with dithering on and about 65-70 with it off, against 57 in RC1; the cost only applies while textures load.

## Memory tuning

The 80 MiB application mode is split between the ordinary heap and the linear heap that holds textures. If both are short, the symptoms can look like missing music, a previous room's background staying on screen, doors disappearing, or crashes on large boss rooms. `--heap-mib N` (32 to 47, default 42) moves the boundary so it can be tuned: try raising it if audio fails and lowering it if graphics vanish. Builds with a non-default heap or dither are marked experimental in the report and are not hash-checked.

## Controls

Isaac already supports ABXY shooting on Old 3DS. The patch remaps the two New 3DS shoulder actions and removes the competing D-pad Down movement binding. No C-stick bypass is needed. See [controls](INPUTS_EXPLAINED.md).

## Patch source and verification

Normal builds embed the checked payload. To recompile and compare it, install Python 3 and LLVM 22.1.8 with clang++, ld.lld, llvm-objcopy, and llvm-nm:

```sh
python3 patches/verifyTexturePayload.py --output build/payload
```

This checks every payload byte, the hook symbol addresses, and the embedded rebuild settings against their source files. After editing [patches/Texture16.cpp](patches/Texture16.cpp), run it with `--update` to regenerate `src/TextureData.h`. Different compiler versions can produce different code. Continuous integration uses the pinned LLVM 22.1.8 release.

`python3 patches/testTexturePayload.py` runs the compiled payload in an ARM11 emulator (`pip install unicorn`) and compares texture conversion and pixel writes with a Python model. The C++ unit tests (`ctest --test-dir build -C Release`) cover the patch logic with a synthetic executable. Neither needs game files. See [compatibility](COMPATIBILITY.md) for validation results.
