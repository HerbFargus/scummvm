# msb-knowwonder branch

This is a personal fork branch adding mTropolis support for the KnowWonder
Magic School Bus titles (World of Animals, Bugs, Mars, Flight, Volcano,
Whales & Dolphins).

**This code is AI-written (Claude) and is not a ScummVM contribution.**
ScummVM's AI-GUIDELINES.md does not allow AI-authored submissions, so nothing
here should be sent upstream as-is. It is published as a reference: what the
plug-in modifiers do, how their data is laid out, and what the games need.
Anyone writing a real contribution should treat it as notes, not code to copy.

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
