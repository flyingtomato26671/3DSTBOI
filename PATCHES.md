# Included patches

Version 0.1.0 includes the full RC1 patch set:

- Old 3DS launch flags, 80 MiB application mode, normal CPU/cache settings, and no extra-core requirement.
- A 42 MiB ordinary heap, leaving more memory for the linear graphics heap.
- Loaded PCX/PNG textures stored as RGB565 or RGBA4444, with matching GPU binding and pixel access.
- Reuse of existing scratch storage during procedural texture initialization.
- SELECT for active items and D-pad Down for Drop, replacing ZL and ZR.

The original banner, electronic manual, game resources, title ID, and 4 MiB save allocation are preserved. The patcher requires the exact supported executable and checks the completed CIA before writing the output.

## Texture memory

The new texture formats reduce color and alpha precision to save memory. Procedural textures and render targets retain their original formats because the game's framebuffer copies expect four-byte pixels. Initialization reuses the existing 4 MiB decode scratch when available, falling back to the original allocator otherwise.

The authored ARM payload uses 1,332 bytes of text padding at `0x2E6200`, with no additional memory pages. Upload, GPU binding, pixel access, and scratch allocation hooks are defined in [src/Patches.cpp](src/Patches.cpp). Source is in [patches/Texture16.cpp](patches/Texture16.cpp), with its checked payload in [src/TextureData.h](src/TextureData.h).

## Controls

Isaac already supports ABXY shooting on Old 3DS. The patch remaps the two New 3DS shoulder actions and removes the competing D-pad Down movement binding. No C-stick bypass is needed. See [controls](INPUTS_EXPLAINED.md).

## Patch source and verification

Normal builds embed the checked payload. To recompile and compare it, install Python 3 and LLVM 22.1.8 with clang++, ld.lld, llvm-objcopy, and llvm-nm:

```sh
python3 patches/verifyTexturePayload.py --output build/payload
```

This checks every payload byte and the embedded rebuild settings against their source files. Different compiler versions can produce different code. See [compatibility](COMPATIBILITY.md) for validation results.
