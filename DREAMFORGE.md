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
between scenes.

M2 (cutscenes and sound): the DFA decoder, cutscenes with their ACT sound
cues, effects, voices and scene music.

Lip sync: the viseme data, its timing and the talking animations, shown by a
stand-alone demo (`say`). Cutscene mouths are drawn into the DFAs; the caption
panel the cutaway code can draw beside a cutscene (C.PRG 0x14260) is never
switched on by the game.

M3 (gameplay, in progress): the action-script interpreter (most of the room
interactions are bytecode in the scene records), and the puppies: placing,
facing, turning, one-shots, walking with the original step tables and
scaling, routing over the waypoint graph, depth-sorted drawing with layers
and objects, depth shading, and talking with lip sync. Clicks walk puppy 1
or send a hotspot's puppy over before its script; walking into a region
(exits) runs its script. Scene objects animate on the game timer, in-room
animations are temporary objects in depth order, and the cursor follows what
is under the mouse.

The game starts as the original does: the intro cutscenes, the title menu
(New Game plays the opening cutscenes, randomizes the per-game flags and
runs the first room's opening), and in game the bone menu (Escape, Enter or
Space) with ScummVM's load, save and options dialogs; L, S and O are
shortcuts. Saves keep the room, flags, record states, held item, the
puppies and the music. Act 1's scene entry handler is ported in full;
the after-load handler only for the first room so far.

Not yet: the rest of the act handlers written in C (after-load and
click/hover logic), banter, puppy 2's wandering, start/stop/footstep
details, the game's own options and quit screens.

The reverse-engineering notes (formats, code map) live outside the fork, in
`escape-from-devil-manor/NOTES.md`.

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
- DFIA (`CINE\*.DFA`, PIG2): Chronomaster DFA. `dfa.cpp` follows FFmpeg's
  `libavcodec/dfa.c` (LGPL 2.1+) and matches its output frame for frame,
  except that FFmpeg's DDS1 skip check is inverted (it rejects every skip,
  so FFmpeg can't play the title, CINE 0322).
- ACT (one per DFA): a voice preload list and an effect preload list, each
  ended by 0xFFFF, then (opcode, argument) pairs per frame ended by 0xFFFF.
  0xFFCC..0xFFC9 play an effect on channel 0..3, 0xFFC8..0xFFC5 the same
  looping, 0xFFCD music (0xFFFD stops it), 0xFFCF music looping, 0xFFCE a
  voice line, 0xFFC4 ends the cutscene.
- Sound numbers come in banks of 1000. INIT.PIG (31 words) gives each bank's
  first archive index: effects (11000-14000, 20000-23000, 30000, 31000,
  40000, 41000) are in PIG1, voices (10000, 11000, 20000-22000, 30000,
  42000-46000) in PIG3. Music track n is PIG4 entry n.
- Puppy animations (PIG2): entry = (actor - 1) * 165 + slot * 5 + view, for
  2 actors, 33 slots (frame counts at C.PRG 0x16340) and 5 drawn views. They
  carry their own 16-colour palette (indices 0-46, 0 is the green key);
  how the game fits them to each scene's palette isn't worked out yet.
- Lip sync (PIG6, C.PRG 0x1bd20): entry n goes with voice n in PIG3, for the
  801 voices before the 20000 bank. One viseme per game step; viseme v shows
  frame v - 1 of a 6-frame talking slot (9/30, 11/14 or 18/26, picked by two
  actor state flags and whether the line is quick), 0 ends the line, and the
  line also ends with its sound. A step is two ticks of the game timer; the
  data averages 10.2 visemes per second of speech, so it's 100 ms here.

- Action scripts (hotspots, objects and layers share a record header: rect
  as top, left, height, width; per-puppy walk-to x, y, dir; who acts; the
  script at +0x34; name at +0x258): 21-byte instructions, u8 opcode + 10 u16
  args, 64 opcodes, run to an end marker. Hotspot masks: 1 walk-in region,
  4 click, 2 use, 0x10/0x20 room logic.

## Testing

`tools-dreamforge/` in the parent folder: `make.sh` builds, `play.sh [scene]`
plays, and `boot.sh` runs with test hooks (`dreamforge_scene=1X01X06`,
`dreamforge_scroll=N`, `dreamforge_layers=true`, `dreamforge_shot=<png>`,
`dreamforge_allscenes=<dir>`, `dreamforge_cine=<n,...>` with
`dreamforge_fast=true`, `dreamforge_cine_frames=<f,...>` and
`dreamforge_shot_dir=<dir>`, `dreamforge_say=<voice>[,actor,slot,view]`,
`dreamforge_use=<hotspot id,...>`, `dreamforge_walk=<y,x>`, `dreamforge_run_ms=<ms>`,
`dreamforge_shot_every=<ms>`, `dreamforge_quit=true`, `dreamforge_skip_intro=true`,
`dreamforge_skip_cutscenes=true`, `dreamforge_title=<1-3>`, `dreamforge_save=<slot>`,
`dreamforge_title_shot=<png>`, `dreamforge_bone_shot=<png>`, `dreamforge_cursor_probe=<x:y,...>`).

Console: `scene 1X01X06`, `scenes`, `hotspots`, `layers`, `layer <n|all|none>`,
`scroll <x>`, `archives`, `shot <png>`, `cine <n>`, `sfx <n> [channel]`,
`voice <n>`, `music <n|stop>`, `say <voice> [actor] [slot] [view]`, `use <id>`,
`flag <n> [0|1]`. Keys: arrows scroll, Escape/Enter/Space the bone menu, L load,
S save, O options, Ctrl+L shows every layer.
