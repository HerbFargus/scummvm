# scumm-llm branch

A personal fork branch that gathers the engines written with an LLM
(Claude) into one build. It is upstream ScummVM plus these branches, each
merged as it stands on its own branch:

| Branch | Engine | Games | Notes |
|---|---|---|---|
| `msb-knowwonder` | `mtropolis` additions | The Magic School Bus (KnowWonder): Whales, Flight, Volcano, Mars, Bugs, Animals | MSB-KNOWWONDER.md |
| `yaga` | `yaga` | Pajama Sam 4, Putt-Putt: Pep's Birthday Surprise | YAGA.md |
| `bigtuna` | `bigtuna` | Big Tuna's Mercer Mayer games: Just Me and My Mom and Dad (PC), The Smelly Mystery, The Great Race, The Mummy Mystery | BIGTUNA.md |

**All of this is AI-written and is not a ScummVM contribution.** ScummVM's
AI-GUIDELINES.md does not allow AI-authored submissions, so nothing here
should be sent upstream as-is. Detection-only changes for existing engines
are kept out of this branch.

New engines get their own branch first and are merged here when they play.

Build: `./configure --disable-all-engines --enable-engines=mtropolis,yaga,bigtuna`
