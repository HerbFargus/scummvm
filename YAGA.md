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
  as far as starting the event loop.
- Engine modules: yagares (the .he archives) and yagaxml are written.
  The others (yaga, yagaevents, yagagraphics, yagasprite, yagascene,
  yagasound, yagafont, yagacollisionlib) are tracing stand-ins that log
  what the scripts ask of them, so nothing is drawn or heard yet.

