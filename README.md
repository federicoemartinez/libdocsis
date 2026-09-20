# libdocsis

DOCSIS configuration file encoder, packaged as a shared library for use from
Python through `ctypes`.

Fork of [rlaager/docsis](https://github.com/rlaager/docsis). Upstream is a
command-line tool; this fork exposes the encoder as a `.so` that works from
buffer to buffer, without going through files.

## Dependencies

Debian / Ubuntu:

```sh
sudo apt-get install autoconf automake libtool flex bison gcc make \
                     libsnmp-dev libglib2.0-dev libfl-dev
```

## Build

```sh
./autogen.sh
./configure
make
sudo make install
```

That installs `libdocsis.so`, its header, and the DOCSIS MIBs into net-snmp's
search path.

## MIBs

`initialize()` loads the MIBs through net-snmp and does **not** set `MIBDIRS`.

- After `make install` there is nothing to do: the MIBs land in
  `/usr/share/snmp/mibs`, which is net-snmp's default path.
- Running from the build tree, export:

```sh
export MIBDIRS="$PWD/mibs:$PWD/mibs/iana:$PWD/mibs/ietf"
```

If the MIBs are missing, `initialize()` returns 0 and says what is wrong.

## API

Three functions. **`0` means failure in all of them, with no exceptions.**

```c
int  initialize (void);
int  encode_file (unsigned char *content, unsigned content_size,
                  unsigned char **output,
                  unsigned char *key, unsigned int keylen,
                  int encode_docsis, unsigned int hash);
void free_encoded_file (unsigned char *buffer);
```

- `initialize()` — once, before anything else. Returns 1 on success.
- `encode_file()` — returns the length in bytes (`> 0`) and stores the buffer
  in `*output`. On error it returns `0` and leaves `*output` as `NULL`. The
  error detail goes to `stderr`.
- `free_encoded_file()` — releases what `encode_file()` returned. Accepts
  `NULL`.

## ctypes example

```python
import ctypes

lib = ctypes.CDLL("libdocsis.so")

lib.initialize.restype = ctypes.c_int
lib.initialize.argtypes = []

lib.encode_file.restype = ctypes.c_int
lib.encode_file.argtypes = [
    ctypes.c_char_p,                    # content
    ctypes.c_uint,                      # content_size
    ctypes.POINTER(ctypes.c_char_p),    # output
    ctypes.c_char_p,                    # key
    ctypes.c_uint,                      # keylen
    ctypes.c_int,                       # encode_docsis
    ctypes.c_uint,                      # hash
]

lib.free_encoded_file.restype = None
lib.free_encoded_file.argtypes = [ctypes.c_char_p]

if not lib.initialize():
    raise RuntimeError("could not load the MIBs: check MIBDIRS")

config = b"""Main {
  DownstreamFrequency 123000000;
  UpstreamChannelId 1;
  NetworkAccess 1;
}
"""
key = b"aaaaaaaaaaaaaaaa"

out = ctypes.c_char_p()
length = lib.encode_file(config, len(config), ctypes.byref(out),
                         key, len(key), 1, 0)
if length == 0:
    raise RuntimeError("encoding failed; the detail went to stderr")

try:
    data = ctypes.string_at(out, length)   # copy the bytes into Python
finally:
    lib.free_encoded_file(out)

print(f"{len(data)} bytes: {data[:16].hex()}")
```

Use **`ctypes.string_at(out, length)`, not `out.value`**: the buffer is binary
and contains zero bytes, so treating it as a NUL-terminated string truncates it
at the first `0x00`.

## Concurrency

`encode_file()` serializes internally with a mutex. Calling it from several
threads is safe, but the calls do not run in parallel. The lock is needed
because `ctypes` releases the GIL for the duration of the call and the parser
still has global state; the upgrade path is noted in `src/docsis_yy.y`.

## Tests

```sh
make check
```

Runs three suites:

- `tests/test_api` — the public API, error paths included.
- `tests/run-fixtures.sh` — 142 real configurations, triple round-trip. Fails
  only when the set of broken cases changes with respect to
  `tests/known-failures.txt`, in either direction.
- `examples/` — encode-vs-decode comparison over 21 examples.

Ten fixtures and one example fail because of upstream bugs, documented in
`docs/known-failures-baseline.txt`. None of them affect the
library path.

## CI

`.github/workflows/ci.yml` runs four jobs on every push and pull request:

| Job | What it does |
|---|---|
| `test` | `make check`: all three suites |
| `analyzer` | `gcc -fanalyzer -Werror` over the hand-written sources |
| `asan` | `test_api` under AddressSanitizer + LeakSanitizer |
| `valgrind` | `test_api` under valgrind, fails on anything `definitely lost` |

The sanitizers run over `test_api` only, not over the 142 fixtures: the error
paths are where leaks show up, and 426 processes under valgrind multiply the
runtime without adding signal.

`tests/lsan.supp` lists the known leaks, each with its explanation and its
bound. If one gets fixed, drop the line — LSAN reports suppressions that stop
matching.

## Differences from upstream

- The `docsis` binary is **not installed**. It exists only as a test harness.
- The decoder is not exported: the library only encodes.
- No Windows support: `parse_config_string()` uses `fmemopen()`, which is
  POSIX.
- The library never calls `exit()` and never writes to `stdout`. Upstream,
  being a CLI, does both, and either one breaks an embedded consumer.
