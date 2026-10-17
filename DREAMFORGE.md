# dreamforge branch

This is a personal fork branch adding an engine for DreamForge Intertainment's
1997 DOS adventure **101 Dalmatians: Escape from DeVil Manor** (Disney
Interactive). DreamForge also made Sanitarium (ScummVM's `asylum`), but this
earlier engine shares none of its file formats.

**The whole engine is AI-written (Claude) and is not a ScummVM contribution.**
ScummVM's AI-GUIDELINES.md does not allow AI-authored submissions, so nothing
here should be sent upstream as-is. It is published as a reference for how the
formats and the game work. Anyone writing a real contribution should treat it
as notes, not code to copy.

## Status

M1 (skeleton): detection, data mounting, resource loaders, scene backgrounds
with palette and scrolling, the paw cursor, and a debug console that jumps
between scenes. No game logic yet. The reverse-engineering notes (formats,
code map) live outside the fork, in `escape-from-devil-manor/NOTES.md`.

## Data

The game runs from `101D\` on the CD (`C.PRG`, a Watcom DOS/4GW exe). Four of
its archives, PIG0/2/5/6.USR, exist only inside the InstallShield 3 archive
`DATA.Z` at the CD root. The engine mounts `DATA.Z` with
`Common::InstallShieldV3` when they aren't loose, so pointing ScummVM at the
CD (or a copy of it) is enough.

## Formats

- `.USR` archive: u32 count, count u32 offsets.
- `EH` resource: `'EH'`, u16 type, u16 header length, header, body. Types:
  1 sprite (header: height, frames, width, compression, flags; compression
  0 raw, 1 PackBits, 3 LZSS), 2 tables, 4 sound (u32 size + 16-bit mono PCM,
  22050 Hz; music 11025 Hz), 5 scene, 6 animation (DFIA), 8 lip sync.
- LZSS: Okumura, 4 KB ring pre-filled with 0xFE, writing from 0xFEE.
- Scene (`SCENES\<act>X<area>X<room>.PIG`): version 5.02, name, height,
  width (640 or 854), music, ambient sounds, counts, 6-bit palette, 0x4B0
  hotspots, LZSS background, 0x29C animated objects (each names a
  `CINE\NNNN.DFA`), 0x288 layers (y, x, height, width + LZSS image: state
  overlays), a flag-gated per-column walk map, pairs, start.
- DFIA (`CINE\*.DFA`, PIG2): Chronomaster DFA, the format FFmpeg's
  `libavcodec/dfa.c` decodes.

## Testing

`tools-dreamforge/` in the parent folder: `make.sh` builds, `play.sh [scene]`
plays, and `boot.sh` runs with test hooks (`dreamforge_scene=1X01X06`,
`dreamforge_scroll=N`, `dreamforge_layers=true`, `dreamforge_shot=<png>`,
`dreamforge_allscenes=<dir>`).

Console: `scene 1X01X06`, `scenes`, `hotspots`, `layers`, `layer <n|all|none>`,
`scroll <x>`, `archives`, `shot <png>`. Keys: arrows scroll, L toggles layers.
