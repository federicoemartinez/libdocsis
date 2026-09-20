# Design notes

Why this fork is shaped the way it is. The *what* lives in the README; this
file records the decisions and, more usefully, what was deliberately left out.

## The product is the library

Upstream is a CLI. This fork exists to be called from Python through `ctypes`,
so the library is the deliverable and the `docsis` binary is no longer
installed.

It is not deleted, though: the 142 fixtures under `tests/` are the only
executable definition of "encodes correctly" in this repository, and they are
driven through that binary. It became a test harness (`noinst_PROGRAMS`).

That in turn allowed the important simplification. `encode_one_file` and
`encode_one_string` used to hold two copies of the same pipeline and had
already diverged, which meant the 142 fixtures validated a code path the
library never executed. `encode_one_file` now reads, delegates to
`encode_one_string`, and writes.

## The library must never exit() or write to stdout

A CLI can call `exit()` on a bad value and print progress to stdout. A shared
library loaded into a Python process cannot: `exit()` kills the interpreter and
stdout output corrupts whatever the caller is producing.

Ten reachable `exit()` calls were converted to return codes. The progress
output lives on the CLI side of the split, which is also why `examples/`, whose
test greps for `Final content of config file:`, works again.

One consequence deserves attention: converting `exit()` to `return` widens the
return value's domain. Several checks of the form `if (len <= 0)` were correct
while the only alternative was aborting, and became wrong once encoders started
returning negatives -- zero is a legitimate length for some TLVs. That is what
made seven valid configs fail to parse.

## Encode only

The decoder stays in the tree because the tests use it to verify round trips,
but it is not exported. Refactoring `docsis_decode.c` to write into a buffer
instead of to `stdout` is more work than everything else here combined, and no
consumer has asked for it.

## A lock, not reentrancy

`ctypes` releases the GIL for the duration of a call, so two Python threads
calling `encode_file()` really do run in parallel, over a parser that still has
global state (`_my_tlvtree_head`, `line`, `strbuf`).

`encode_file()` serializes with a mutex. Correct always, slow only if somebody
actually parallelizes. Real reentrancy means moving that state into a context
struct threaded through `%parse-param`, which touches every grammar rule and
therefore maximizes conflicts with the next upstream merge. The TODO is noted
in `src/docsis_yy.y`.

## Ownership is the recurring hazard

Every memory bug found here was an ownership conflict, not a forgotten `free`:

- Bison's `%destructor` collided with the caller's `free_tlv()`, because
  destructors also run on ACCEPT.
- An arena in the lexer collided with the ~20 `free(helper->strval)` calls in
  `docsis_encode.c`.
- A `free()` on one error path collided with the arena added later.

The residual leak in `tests/lsan.supp` is the same class: token copies made by
`TSAVE` are owned by the encoders, so they leak when the parse aborts before
reaching one. Fixing it means unifying ownership across those 20 sites, which
carries a documented landmine (the TODO at `docsis_encode.c:656`: `T_STRING`
values point at the static `strbuf`, not at an allocation). That is a separate
piece of work.

## Not supported

- **Windows.** `parse_config_string()` uses `fmemopen()`, which is POSIX.
  `build_setup.nsi.in` is unmaintained.
- **`make dist`.** It fails today for two reasons: the root `Makefile.am`
  generates `README` from `doc/index.html` with `w3m`, and the 426 fixtures
  under `tests/` are not in `EXTRA_DIST`. Both are fixable; neither is needed
  until someone wants tarball releases.

## Known-broken tests

Ten fixtures and one example fail because of upstream bugs, each verified
against a build of pristine `upstream/master`. See
`docs/known-failures-baseline.txt`. None of them affect the library path --
`tests/test_api.c` encodes the affected fixtures successfully.
