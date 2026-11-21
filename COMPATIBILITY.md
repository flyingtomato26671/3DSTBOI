# Compatibility

Verified input: European The Binding of Isaac: Rebirth v2.4.0, product code `KTR-N-CBRP`, title ID `000400000F700900`, CIA version 2112. Both the application and electronic manual are required.

Original CIA SHA-256:

```text
3316fa392eebf8f26028acaa2b08bfa44e0a2f45b856f63006197ceb8a8f724b
```

Original decompressed executable SHA-256:

```text
674c7d31063abb3daa3f22c0e67da1e83d1a6e65725915dab34783d7f432882e
```

Patched executable SHA-256 of the RC1 build, which the patcher now only uses to recognize already-patched input:

```text
d4eee88f5ad206c04e495ee42ba92330a2bce7935dba71b47582a54bd1b9b4e3
```

The current build adds dithering and a different payload, so its executable hash is different and is not pinned yet. The patch report prints it as `Patched executable SHA-256`. Once the build has been tested on a console, copy that value into `pinnedPatchedCodeHash` in `src/Patches.h` and the patcher will enforce it for default settings.

RC1 has been tested on a physical Old 3DS; the dithering build has been tested for payload correctness in an emulator but not yet on hardware. Azahar in Old 3DS mode at 100% CPU timing also passed boot, gameplay, room transitions, movement, shooting, bombing, death/restart, and save/resume. It reported 60 FPS at roughly 100% emulation speed in the tested rooms; this is not a physical-console FPS measurement.

Texture conversion, GPU binding, pixel access, scratch reuse, and default controls passed ARM execution checks. Full CIA rebuilds passed on Linux and with the Windows executable under Wine, including re-extraction of the expected code, icon, system profile, banner, manual, and unchanged game resources. The authored payload recompiled byte-for-byte with LLVM 22.1.8.

The patcher checks the full original executable hash, patch-site words, unused payload space, and system profile. It preserves the title ID, CIA version 2112, content IDs, and 4 MiB save allocation. Whole CIA hashes can differ because installation metadata is regenerated; the executable hash identifies the patch.

Other regions, versions, stripped-manual packages, updates, DLC, and already-patched executables are not supported. Repacked or decrypted input must retain the verified original executable, system profile, title/version, and both contents. There is no `--allow-similar` mode.
