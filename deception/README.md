Deception frontend
==================

A decompilation of the C/C++ frontend (preprocessor, tokenizer, parser, semantic analysis) of `mwcceppc.exe` GC 2.7
(Version 2.4.7 build 108), the compiler of Mortal Kombat: Deception. Functions target the original's bytes, built with
the compiler that builds the main project's 1.3 sources. A function that is not exact but scores at least 98% (objdiff's
fuzzy match: register allocation and similar differences) counts as done when its behavior is verified identical: for
the same input, the frontend hands the backend the same trees and the compiler writes the same object. Only the
frontend is in scope.

The sources started as a copy of the main project's 1.2.5 frontend (`src/frontend`, `include/compiler`) and diverge
from it toward 2.7. The backend, optimizer and driver are the original executable's.

How equivalence is measured
===========================

- **Hybrid compiler.** `tools/hybrid.py` links compiled functions into a new section of the original executable and
  redirects the original's functions to them. Symbols resolve to the original's addresses
  (`config/GC_2_7/functions.json` and `config/GC_2_7/bindings.json` of the main project, then
  `deception/config/bindings.json`); the sources declare all global data `extern`, so an injected function uses the
  original's own state. `config/inject.json` lists the functions the hybrid replaces, by source.
- **AST oracle.** `tools/astdump.c` runs in front of `CodeGen_Generator`, where the frontend passes each function's
  statements to the backend, and writes them canonically (statement kinds, labels by order, expression trees with
  types, objects by name, constants; no pointers) to the file `MWCC_AST_DUMP` names. It is linked into both the oracle
  (the original frontend) and the hybrid.
- **Corpus.** `tools/ast_check.py` runs a compiler over the units a game decompilation builds with GC/2.7 (from its
  `build.ninja`, with each unit's own flags). `record` stores the original's dumps and objects; `check` compares the
  hybrid's with them. A unit is identical when its dump and its object file are both the original's.
- **Matching.** `tools/match.py` scores each frontend function against the original's (relocations resolved to the
  original's addresses; objdiff's fuzzy match) and combines the score with the screen into a status: exact,
  equivalent (at least 98% and identical behavior), near (at least 98%, behavior not verified), divergent, nonmatching.
  `--learn` binds symbols from near matches (a relocation whose preceding bytes are the original's reads the address
  there; kept when every site agrees).
- **Screening.** `tools/screen.py` injects each frontend function on its own and classifies it over part of the
  corpus: identical, different (ASTs), object differs (same ASTs, different object), crash or unlinked (a referenced
  symbol has no known address).

`deception/config/bindings.json` also holds function addresses from pairings of medium confidence (similar code, same
references). A wrong one shows up as a crash or a difference in the functions that call it.

Building
========

Build the main project for GC 2.7 once (`python configure.py --version GC_2_7 && ninja`), then:

```sh
python deception/build.py
python deception/tools/ast_check.py record --game PATH_TO_GAME_DECOMP
python deception/tools/ast_check.py check --game PATH_TO_GAME_DECOMP
python deception/tools/screen.py --game PATH_TO_GAME_DECOMP [FUNCTION ...]
python deception/tools/match.py [--learn]
```

The game decompilation must be built (its compilers and `wibo` downloaded) for its `build.ninja` and tools.
