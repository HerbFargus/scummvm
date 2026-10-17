# bigtuna branch

This is a personal fork branch adding an engine for Big Tuna New Media's
"Implay" player, which runs the Mercer Mayer games that aren't Director:
Just Me and My Mom (1996), Little Monster: The Smelly Mystery (1997), Little
Critter and the Great Race (1999) and Little Monster: The Mummy Mystery (2001).

**The whole engine is AI-written (Claude) and is not a ScummVM contribution.**
ScummVM's AI-GUIDELINES.md does not allow AI-authored submissions, so nothing
here should be sent upstream as-is. It is published as a reference for how the
format and player work. Anyone writing a real contribution should treat it as
notes, not code to copy.

## How Implay works

Big Tuna authored in Director and compiled each movie to an `.IM2` file:
`"IM20"`, an entry count, the data, and a directory of `{u8 type, u32 size,
u32 offset}` at the end. Resource numbers are 1-based. Fixed slots: 1 score,
2 symbol table, 3 frame labels, 4 cast names, 5 palette. Lingo member N is
resource N + 7.

- Bitmaps: u16 w, u16 h, u8 format (0 row RLE, 1 raw, 2 RGB555, 3 LZ),
  u8 opaque, s16 reg x/y, u16 extra (missing in Mom). The RLE uses `00` end of
  row, `01-7F` literal, `80-BF` run, `C0-FF` transparent skip.
- Sounds: u8 bits, u16 rate, unsigned 8-bit mono PCM.
- Score: a delta stream of film operations (sprite member/loc, sprite and
  frame scripts, sound, tempo, palette, volume, film-loop origin), each frame
  ended by `0x11` or `0x0D n`. A sprite whose member is a score is a film loop.
- Scripts: a stack-machine bytecode compiled from Lingo.

## Status

Playable, not yet playtested start to finish. Detection, the movie loader,
all bitmap formats, sound with fades, the score with film loops, the
compiled-Lingo interpreter with its builtins, the player's event model
(enterFrame/exitFrame, mouse enter/within/leave/down/up, keyDown, startMovie,
go), cursors, and getPref/setPref as save files, so the games' own save and
load screens work. Every frame of all 190 movies plays without errors;
random-click runs through all four games produce no interpreter warnings.

Not supported: video (unused by these games), PrintOMatic printing,
ScummVM's own save/load menu.

Test hooks (config keys): `bigtuna_movie`, `bigtuna_frames`, `bigtuna_fast`,
`bigtuna_shots`, `bigtuna_shot_dir`, `bigtuna_allmovies`, `bigtuna_actions`
(`ms:c:x:y`, `ms:k:code`, `ms:s`), `bigtuna_fuzz`, `bigtuna_seed`,
`bigtuna_quit`, `bigtuna_trace`.
