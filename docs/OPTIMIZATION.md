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
