# Diagnostics: music, missing sprites, stale rooms, and frame rate

Working notes for the Old 3DS build. Hardware observations come from play-testing on a physical Old 3DS; nothing below about the game's internals is confirmed until it is checked against the pinned executable (see [compatibility](../COMPATIBILITY.md)).

## What has been observed

| Symptom | Observation |
| --- | --- |
| Music | Most tracks stop after 1-3 seconds. Shop and secret-room tracks last a bit longer, the basement track once lasted about 8 seconds, and the boss-defeat jingle lasted about 3 minutes. Playback that does run is choppy and sometimes slows down or speeds up. |
| Sprites | Some sprites never load, doors most often. |
| Rooms | A room sometimes keeps the previous room's background or layout. |
| Crashes | Mom's boss crashes the console. |
| Frame rate | About 50-60 FPS in a 1v1 boss room even with many tears on screen. Rooms with many enemies lag. Fireplaces lag until they are put out. |

## Working hypotheses

### 1. Threads that expect the New 3DS extra core (leading)

- The original title is New 3DS only, and this project's rebuild settings turn the extra-core permission off (`Access Core 2: NO` in `src/Pipeline.cpp`, `patches/isaac-old80.rsf`). A game that asks for that permission very likely starts worker threads on it.
- According to [3dbrew's multi-threading page](https://www.3dbrew.org/wiki/Multi-threading), an Old 3DS has only cores 0 and 1, and an application cannot use core 1 unless it raises its CPU time limit. Scheduling is priority-based and first-in-first-out with no time slicing, so a worker sharing core 0 with a busy main thread only runs when the main thread blocks.
- That would explain several symptoms at once: streamed music that starves or never gets going, loads that finish late or never (missing sprites, stale rooms), and a frame rate that depends on how busy the main thread is. Emulators are often more forgiving about core ids than hardware, so it would also explain why Azahar looked fine.
- **Not confirmed.** It needs the thread-creation call sites in the executable: their processor id and priority arguments.

### 2. Memory pressure from the heap split

- The patch splits 80 MiB between the ordinary heap (42 MiB by default) and the linear heap that holds textures. A failed texture allocation makes `UploadTexels` return 0, which would leave a stale or missing texture.
- Test with `--heap-mib`. If symptoms barely change between 36 and 47, this is not the main cause.

### 3. Per-frame cost on a 268 MHz CPU

- Tears are cheap, so projectile handling is probably fine. Enemy updates and fireplaces (many particles and glow sprites, so possibly fill-rate rather than game logic) are the suspects.
- Needs profiling data. A table of FPS per room type (empty, fireplace lit and out, N enemies, boss) would show whether the cost scales with entity count or with on-screen effects.

## Experiments that need no reverse engineering

1. Play the same patched CIA in Azahar in Old 3DS mode. If music and sprites are fine there, hypothesis 1 gains weight.
2. Build with `--heap-mib 36`, `42`, and `47` and note whether the music and sprite symptoms change.
3. Fill the FPS table above for the default build and with `--dither off` (to rule out any conversion-related cost).

## Data that would settle it

Run the patcher with `--dry-run --dump-code code-original.bin` on your own CIA. The file is the decrypted original executable. It is for analysis only; never commit or share it (`*.bin` is already ignored). From it, find:

- every `svc 0x08` (CreateThread) site, with the processor id and priority passed;
- any call that sets the application CPU time limit;
- how the audio and loading code is scheduled.

## Ground rules for collaborating (people or other AI tools)

- Push only to this fork. Never open pull requests against the upstream repository.
- Never commit game files, seeds, or dumped executables.
- Treat any claimed address or behavior as a hypothesis until it is verified against the executable whose SHA-256 is in `COMPATIBILITY.md`. The patcher already refuses to run if a patch site does not hold the expected original word.
- Payload changes go through `patches/verifyTexturePayload.py` and `patches/testTexturePayload.py`, and unpinned builds stay marked experimental until tested on a console.

## Experiment: `--cpu-limit` (audio on a physical Old 3DS)

Verified in the original executable: it calls `APT:SetApplicationCpuTimeLimit` with 30 (`mov r0,#30` at 0x1055CC,
IPC wrapper at 0x121BF4), so the game's core-1 workers get at most 30 % of that core. Unverified hypothesis: the audio or
decoding worker needs more than that when the scene is busy, so the DSP runs dry and the game never restarts the track.

`--cpu-limit N` (30 to 80, default 30 = the game's own value, no change) rewrites only that immediate. It is an
experiment to confirm or discard the hypothesis, not a fix. Suggested test: build with `--cpu-limit 50`, play a basement
floor with several enemies, and note how long the floor music lasts and whether the frame rate changes. Repeat with 70.
A very high value may starve system services, so raise it gradually. If nothing changes, the hypothesis is discarded.

## `--dump-romfs`: data tables and audio parameters

`--dump-romfs FOLDER` (works with `--dry-run`) saves, from your own copy, into a new folder: `listing.txt` (size and path of
every file), `ogg-info.txt` (channels, sample rate, nominal bitrate and duration of every `.ogg`), and a copy of the small
`.xml` and `.anm2` data files under `files/`. It never copies audio or textures and never overwrites anything. It is meant
for analysis of your own copy: do not share or commit it. The tables (`music.xml`, `sounds.xml`, entities) and the audio
parameters let the music problem be studied without guessing.
