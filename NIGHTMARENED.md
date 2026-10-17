# nightmarened branch

This is a personal fork branch adding an engine for **Nightmare Ned** (Creative
Capers / Disney Interactive, 1997, Windows), a side-scrolling action game.

**The whole engine is AI-written (Claude) and is not a ScummVM contribution.**
ScummVM's AI-GUIDELINES.md does not allow AI-authored submissions, so nothing
here should be sent upstream as-is. It is published as a reference for how the
formats and the game work. Anyone writing a real contribution should treat it
as notes, not code to copy.

## Status

M1 (skeleton): detection, data mounting, the NAF reader with every codec
(backgrounds, sprite banks, sounds), the intro videos, and a resource viewer
that stands in for the game (it opens in the attic with its music). No game
logic yet.

M2 (display and data): the Intel RDX compositor (`rdx.cpp`: surfaces, sprites,
backgrounds, grids, groups, draw order, horizontal flip, smorph), the game's
sprite slot records and request lists (`resources.cpp`, read from
NITENED.EXE inside DATA32.Z), sequences, hit boxes, collision maps and .SST
placement tables (`data.cpp`), `slot_place`, the palette modes, the input
bitmask and the 41 ms scene tick. Until the scene code exists, the game starts
in a demo that builds the attic from its own request list and placement table
and animates every sprite (arrows scroll, F9 shows the collision maps).

The reverse-engineering notes (formats, scene graph, code map) live outside
the fork, in `nightmare-ned/NOTES.md`.

## Data

The CD keeps NAF archives in `DATA\DATA\` and Smacker videos in `DATA\VIDEO\`.
`NITENED.EXE`, `NED01000.NAF`, `NED99000.NAF`, the `.SST` placement tables and the
area music (`SOUND\<area>002208.wav`) exist only inside the InstallShield 3
archive `DATA\DATA32.Z`. The engine reads that with `Common::InstallShieldV3`,
so pointing ScummVM at the CD (or a copy of it) is enough.

## Formats

- NAF: RIFF form `NNAF`. `head` gives the scene range and listed scenes; the
  index chunks `xsix dsix spix bmix cbix auix` map (scene, id) to absolute
  file offsets: u32 slots, i32 first[n], i32 last[n], (offset, size)[slots],
  then a u16 slot list for **every** listed scene, including empty ones
  (first = last = -1 still has one entry), 0xFFFF = none.
- `cC` container (sprite banks `TRPS`, backgrounds `KCAB`): u16 1, kind, u16
  counts, codec, header size/offset, data offset, (offset, size). The only
  codec used is `LZ78`, which is LZW: MSB-first, 9-bit start, 0x100 end,
  0x101 widen, 0x102 reset, first code 0x103.
- Background: header u16 w, h, bpp, ?, ? and 256 x (r, g, b, flags); 8-bit
  pixels. Narrow ones are strips or tiles of a scrolling level; the 8x1 ones
  only carry a palette.
- Sprite bank: u32 size, then a `cC` whose payload is u32 size[n] and n Intel
  RDX compressed images (GUID, w, h, ..., opcodes from +44): 00 n skip,
  01 n c c c c run, 02 n literal, 03 n k colours mask... 00 (LSB-first bits
  pick the opaque pixels, the mask is zero-terminated), 04 end of row,
  05 end of image. Sprites use the scene's palette.
- Sound: WAVEFORMATEX (18 bytes) then PCM.

All 7,769 backgrounds, sprite frames and sounds decode identically to the
Python tools in `nightmare-ned/tools`.

## Testing

`tools-nightmarened/` in the parent folder: `make.sh` builds, `play.sh` plays
(`play.sh <naf>` opens the resource viewer on that NAF), and `boot.sh` runs with test hooks (`nightmarened_skipintro=true`,
`nightmarened_viewer=true`, `nightmarened_naf=95000`, `nightmarened_bg=<scene>:<id>`,
`nightmarened_ticks=<n>`, `nightmarened_scroll=<x>,<y>`, `nightmarened_collision=true`,
`nightmarened_sprite=<n>`, `nightmarened_shot=<png>`,
`nightmarened_videoshot=<NAME.SMK>:<frame>:<png>`,
`nightmarened_checksums=<file>`).

Resource viewer keys: arrows scroll (Shift is faster), PgUp/PgDn change background, N/P
change NAF, Tab toggles the sprite overlay, [ ] change sprite bank, , . change
frame. Escape skips the intro, any other key skips one video.
