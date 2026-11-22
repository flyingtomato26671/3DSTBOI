# Diagnostics: music, missing sprites, stale rooms, and frame rate

Working notes for the Old 3DS build. Physical-console observations below are reports from testing on a real Old 3DS. Static executable findings are limited to the exact original executable identified in [compatibility](../COMPATIBILITY.md).

## What has been observed

| Symptom | Observation |
| --- | --- |
| Music | On a physical Old 3DS, music still has stuttering, interruptions, and changes in playback speed, although one recent test suggests slight overall performance improvement. Most tracks have stopped after a few seconds in earlier tests. The boss-defeat track played for approximately 1 minute 37 seconds in the latest report. A separate guitar-like intensity layer/track heard in a room with many enemies plays almost perfectly, with very little stuttering, but the regular game music stops while it plays. The regular music only resumes after the music is reloaded, for example by entering a room with exclusive music and then leaving it. |
| Dithering and transparency | The new ordered-dithering method has been tested on a physical Old 3DS. Its transparency effects look very good; preserve this working behavior when investigating audio. |
| Sprites | Some sprites fail to load, with doors reported most often. |
| Rooms | A room sometimes retains the previous room's background or layout. |
| Crashes | Mom's boss has been reported to crash the console. |
| Frame rate | Performance appears to have improved slightly in the latest hardware test, but still needs work. Earlier tests reported about 50-60 FPS in a 1v1 boss room even with many tears on screen. Rooms with many enemies lag, and fireplaces lag until extinguished. |

## Working hypotheses

### 1. Audio buffering, scheduling, or memory pressure (open)

The persistent music stutter on real hardware is the main unresolved issue. Playback is not uniformly broken: the boss-defeat track lasted approximately 1:37, and a guitar-like intensity layer heard in a room with many enemies reportedly overlaps the normal music while playing almost cleanly. This contrast may help isolate the failure. It could indicate differences in track format, streaming versus preloaded audio, buffer size, loop/segment handling, channel priority, or mixing path, but none of those explanations is confirmed. Record the exact room and track/layer, how long the guitar-like part plays, and what event triggers the regular music to stop and resume. The slight overall performance improvement is encouraging but does not establish that CPU load is the audio cause.

The heap split remains worth measuring: the default ordinary heap is 42 MiB and the remainder of the 80 MiB application memory is used for the linear heap. Raising `--heap-mib` may help ordinary allocations used by audio and game objects, but it reduces space available to textures. Compare the same scene and track at several heap settings and record both audio and graphics symptoms.

### 2. New 3DS extra-core thread assumption (not supported by inspected paths so far)

The original executable was inspected against the SHA-256 in [compatibility](../COMPATIBILITY.md). The inspected thread-creation paths use processor ID `-2` (the ExHeader default) or an explicit processor ID of 1; no processor ID 2 or 3 was found in those paths. The executable also sends APT:SetApplicationCpuTimeLimit with a 30 percent value, which is consistent with enabling use of Old 3DS core 1.

This does not prove every thread path or scheduling detail is understood, but it means the claim that the game's workers are trying to use a New 3DS-only extra core should no longer be treated as the leading explanation. Do not enable `Access Core 2` as an unverified audio fix.

### 3. Texture memory and missing graphics

The patch splits 80 MiB between the ordinary heap and the linear heap used for textures. A failed texture allocation can leave a stale or missing texture. The heap setting therefore remains relevant to missing doors, stale room backgrounds, and boss crashes. Any test that raises ordinary heap should also check whether it makes texture symptoms worse.

### 4. Per-frame cost on a 268 MHz CPU

Enemy updates and fireplaces remain possible causes of frame drops. The dithering conversion work runs when textures are loaded, not continuously for every rendered frame, so it is not currently a strong explanation for sustained slowdowns in rooms with many enemies or particles. FPS measurements by room type are still needed.

## Experiments that distinguish the remaining causes

1. Compare the same music track and playback interval at `--heap-mib 36`, `42`, and `47`. Record whether it stutters, changes speed, or stops, and whether sprites/rooms become worse.
2. Repeat the same test with the same dither setting each time. Do not change dithering and heap size simultaneously.
3. Record which track/layer is playing, elapsed playback time before the first interruption, room/enemy count, and whether the guitar-like intensity layer replaces/stops the normal music, and whether the normal music resumes only after a music reload. Compare the two layers separately if possible.
4. Record FPS in an empty room, with fireplaces lit and extinguished, and with increasing enemy counts. Keep this separate from the audio test.
5. If available, compare a short track, the boss-defeat track, and the guitar-like intensity layer under identical conditions. The guitar-like layer's near-clean playback while the normal track is stopped is especially useful as a comparison case, not proof that the whole audio path is healthy.

## Data that would narrow the audio cause

The original executable's thread-creation and APT CPU-time-limit paths have already been inspected. The next useful reverse-engineering target is the audio path itself: identify its worker entry point, buffer sizes, refill cadence, decoder calls, and any allocation or synchronization failures. Do not patch an audio routine until its calling convention, buffer ownership, and timing assumptions are understood.

## Ground rules for collaborating (people or other AI tools)

- Push only to this fork. Never open pull requests against the upstream repository.
- Never commit game files, seeds, or dumped executables.
- Treat any claimed address or behavior as a hypothesis until it is verified against the executable whose SHA-256 is in `COMPATIBILITY.md`. The patcher already refuses to run if a patch site does not hold the expected original word.
- Payload changes go through `patches/verifyTexturePayload.py` and `patches/testTexturePayload.py`, and unpinned builds stay marked experimental until tested on a console.
