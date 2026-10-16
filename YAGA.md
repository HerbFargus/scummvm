# yaga branch

This is a personal fork branch adding support for Humongous's Yaga engine
(Pajama Sam 4: Life Is Rough When You Lose Your Stuff and Putt-Putt: Pep's
Birthday Surprise). It builds on Filippos Karapetis' `yaga` branch, whose
three commits (detection, MNG/RLE animation, the McMillan Installer reader,
audio, text and video tests) are kept as he wrote them.

**Everything after those three commits is AI-written (Claude) and is not a
ScummVM contribution.** ScummVM's AI-GUIDELINES.md does not allow
AI-authored submissions, so nothing here should be sent upstream as-is. It is
published as a reference for how the engine and its scripts work. Anyone
writing a real contribution should treat it as notes, not code to copy.

## How Yaga works

Yaga games are C++ DLLs driven by Python 2.2 scripts. The executable is a
McMillan Installer package: a table of contents, a few bootstrap entries
(`archive`, `iu` as bytecode; `boot`, `_mountzlib`, `useUnicode` as source)
and a PYZ archive holding every game module as zlib-compressed marshalled
bytecode, including the parts of the Python standard library the game uses.
The game data is in `.he` files, which are plain uncompressed ZIP archives.

The engine runs the scripts with its own Python 2.2 virtual machine in
`engines/yaga/python/`, which executes the shipped bytecode directly and
compiles the bootstrap source with its own compiler.

## Sources

The formats and engine behaviour come from the games themselves and from
openyaga (github.com/HerbFargus/openyaga), a Python player that runs both
games start to finish and documents the engine API its scripts use. The
Python virtual machine is checked against CPython 2.2.3, the version the
games shipped with.

## Status

- Detection: both games.
- Python VM: done, with a compiler, and checked against CPython 2.2.3
  with a probe suite (the games' own standard library included).
- Boot: both games start through their own McMillan bootstrap and run
  their event loop: the logos and opening movies play (a click or a key
  skips a movie, as the scripts offer no way to), the first room draws,
  characters speak with lipsync, animations play their sound effects, and
  clicking a hotspot plays its animation and line. Saving (with the room
  photo as a thumbnail) and loading work.
- Engine modules: yagares (the .he archives), yagaxml, yagaevents (the
  event loop: timers, mouse, keyboard and .evb event streams), yagasound
  (WAV and MP3 on the mixer; ScummVM needs MAD for MP3) and yagafont
  (bitmap fonts, wrapped and justified text) are written. yagagraphics
  (render target, cameras, cursors, MNG/RLE animations, images),
  yagascene (positions, scenes, point colliders) and yagasprite (sprites,
  talkies and Bink movies) are written in part. The rest of them, and yaga
  and yagacollisionlib, are tracing stand-ins that log what the scripts ask
  of them. Sprites are never drawn flipped: the engine has hFlip and
  vFlip, but neither game's scripts or data ever set them.

Testing: `yaga_ticks=N` stops after N game ticks, `yaga_shots=a,b,...`
saves the screen after those ticks into `yaga_shot_dir`, and
`yaga_clicks=tick:x:y,...` clicks at those ticks, and `yaga_probe=<python>`
runs a line of Python after each of the `yaga_probe_ticks` ticks.
