# mspam branch

This is a personal fork branch adding an engine for **The Magic School Bus
Explores the Solar System** (Microsoft, 1994, Windows), which runs on the
Microsoft Picture Animation Manager ("MSPAM") runtime.

**The whole engine is AI-written (Claude) and is not a ScummVM contribution.**
ScummVM's AI-GUIDELINES.md does not allow AI-authored submissions, so nothing
here should be sent upstream as-is. It is published as a reference for how the
formats and the runtime work. Anyone writing a real contribution should treat
it as notes, not code to copy.

## Status

- M1 (skeleton): detection, the MDF/CMF media files, an OLE2 compound-file
  reader for the title (MSBSOLAR.TDF), palettes and PibList sprites. The
  classroom draws.
- M2 (layout model): every layout's elements, path segments, hotspots,
  triggers and action lists load; any of the 44 layouts renders statically,
  and clicks log what they would trigger. No actions run yet.

The reverse-engineering notes live outside the fork, in
`magic-school-bus/solar-system/NOTES.md`.

## Data

Point ScummVM at the disc root or its `MSBSOLAR\` folder. The game reads
`MSBSOLAR.MDF/CMF` (media index and container), `MSBAMB.MDF/CMF` (ambient
sound, normally installed to the hard disk), `MSBSOLAR.TDF` (the title: an
OLE2 compound file) and the AVIs in `0\` and `1\`.

## Test hooks

Config keys: `mspam_layout=<name>` (start layout, default Classroom),
`mspam_shot=<png>` (save the screen and quit), `mspam_allshots=<dir>` (every
layout to PNG), `mspam_dump=<file>` (text dump of the layout model),
`mspam_click=x,y;x,y` (log what clicks would trigger, Input debug channel),
`mspam_hotspots=true`, `mspam_hidden=true`.

Viewer keys: PgUp/PgDn (P/N) change layout, Home goes to the classroom, H
outlines hotspots and videos, A shows hidden elements.
