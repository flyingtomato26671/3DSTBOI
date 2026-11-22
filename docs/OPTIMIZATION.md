# CPU optimization notes (work in progress)

Each section below is independent, so an interrupted session never leaves half-done code. Labels: **[exe]** found in the
original executable, **[obs]** observed on a console, **[hyp]** hypothesis, **[?]** unknown. Addresses are virtual
(file offset + 0x100000).

## Observations from the console [obs]
- Boss 1-vs-1 with many tears: stable 50-60 FPS. Rooms with many enemies and rooms with fireplaces lag; fireplaces
  stop lagging once extinguished.
- `--cpu-limit` 35, 40 and 50 (raising the APT core-1 limit above the game's 30) fail to boot; 30 boots. So that limit
  cannot be used as a lever.

## Section 1: where fire lives [exe]
- `GridEntity_Fire` has run-time type information (name at 0x2F3D18, vtable near 0x2F46F8). Related assets:
  `gfx/grid/Effect_Ember.anm2`, `FireStage00..03`, and a particle class `CParticle`.
- Its large virtual functions are 0x2AABD0 (about 2.2 KB, ~50 calls, VFP math, 4 SVCs), 0x2AA4FC (about 0.8 KB, 8 SVCs),
  0x2AA838 (about 0.7 KB). Which one is update, render or particle spawn has **not** been established yet [?].
- A rendering or animation cost per lit fire per frame fits the observation that fires lag only while lit [hyp].

## Next
1. Identify update vs. render in those functions (what they call, what they write).
2. Choose a change only if it is verifiable against the original executable and can be switched off.

## FPS counter: findings so far
- [exe] The game has no FPS counter and no frame-time text. No debug overlay strings were found.
- [exe] `Entity_DebugText` (vtable 0x2F4824) is a floating, fading world-space text entity (90-frame life, counter at
  +0xB08, text object at +0x5D0, formatted with a 256-byte vsnprintf-style helper at 0x205178). It is the only text
  drawing path found so far, and it is tied to an entity, so reusing it for a HUD counter is risky.
- [exe] Services in use: `gsp::Gpu` / `gsp::Lcd` (name pointers at 0x30BD48 / 0x30BD4C). The frame-end / buffer-swap
  function has not been located yet [?].
- [exe] HUD time string `Time: %02d:%02d:%02d` exists, but no direct literal reference to it was found [?].
- [hyp] Possible display routes, in order of safety: (1) show the measured FPS in an existing stats screen, (2) a
  dedicated text entity, (3) a non-visual measurement.

## Candidate segments (unverified, by expected benefit / risk)
1. Per-entity animation evaluation (ANM2 interpolation) — shared by every enemy, tear and effect. [hyp]
2. Fire/ember particle spawn and render (GridEntity_Fire, CParticle) — only while lit. [hyp]
3. Entity update loop over all entities each frame (rooms with many enemies lag). [hyp]
4. Allocation churn in per-frame code (heap size already tunable). [hyp]
None of these has been measured; they need the counter first.
