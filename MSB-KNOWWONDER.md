# msb-knowwonder branch

This is a personal fork branch adding mTropolis support for the KnowWonder
Magic School Bus titles (World of Animals, Bugs, Mars, Flight, Volcano,
Whales & Dolphins).

**This code is AI-written (Claude) and is not a ScummVM contribution.**
ScummVM's AI-GUIDELINES.md does not allow AI-authored submissions, so nothing
here should be sent upstream as-is. It is published as a reference: what the
plug-in modifiers do, how their data is laid out, and what the games need.
Anyone writing a real contribution should treat it as notes, not code to copy.

## Sources

Everything here was worked out from the games themselves: their project
data, and disassembly of the plug-in DLLs and player executable that ship
on the game discs. The mTropolis 1.1 manuals and MOM SDK were later used to
check that behaviour, but nothing from them is copied here.

## Plug-in modifiers used by these games

| Modifier | Plug-in file | Games |
|---|---|---|
| DlgMgrClient, DlgMgrServer, EditCtrl | KnowWonder.r32 | all |
| AVICtrl | KnowWonder.r32 | Animals, Bugs |
| FillMod | KnowWonder.r32 | Bugs |
| KWSmacker | KWSmacker.r32 | Mars, Flight, Volcano, Whales |
| FileMod, PointMotion | EXTRAS.R32 (stock mFactory) | all |
| hlPrintBitmap, hlCaptureBitmap | BITMAP.R32 | all |
| AntMaze, BugEatBug | AntMaze.r32, BugEatBug.r32 | Bugs |
| Lander, RoverRace | Lander.r32, RoverRace.r32 | Mars |
| Balloon, Eyefollower, SmkJump | balloon.r32, eyefollow.r32, smkjump.r32 | Flight |
| Art, LLava, SvTown, Sub | ArtAct.r32, LLava.r32, SvTown.r32, Sub.r32 | Volcano |
| WhaleMaze | WhaleMaze.r32 | Whales |

## Status

Whales & Dolphins is playable from sign-in through the bus and most of its
activities (map, periscope, monitor, Crew Pass, game show, Whale-A-Teria,
rotation puzzle, eco poster), with dialogue, lip sync and closed captions.
The other five titles are untested past startup.

Implemented here: DlgMgrServer/Client (including captions from the .ccd
files), KWSmacker, EditCtrl, FileMod, PointMotion, Open Title (with return
to the opening title), and the mTropolis 2.0 data and Miniscript changes the
games need. Their game-specific minigame modifiers are still stubs.

## Notes for implementers

- The titles of a game share player data through a Save and Restore file.
  Each .mfx has its own GUIDs, so restored variables have to match by name.
- Scripts check a save exists by opening it with the File modifier, so both
  modifiers must use the same file names.
- Miniscript jump offsets are signed; loops jump backwards.
- Captions: the .ccd file is a hash table (bucket count, then
  `textOffset, nameOffset, compareNames` per bucket, then the names and the
  `SPEAKER<tab>text` strings). Keys are dialogue line names. The hash walks the
  name backwards: `h += c * m; m *= isdigit(c) ? 5 : 13; m <<= 1`, from
  `h = m = 1`. The speaker, two-line pages of text and a clear message are sent
  to the active scene using the events in the DlgMgrServer data.
- Point Motion: the u32 after the destination is two u16s, ease flags (1 out,
  2 in, over 20% each) and mode (1 = duration, otherwise speed in px/s). The
  int is the update rate in Hz.

## Testing

`mtropolis_test_input` names a script of input commands (see
`TestInputScript` in mtropolis.cpp) so the games can be driven and
screenshotted without a real mouse or keyboard.
