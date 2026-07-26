# Contributing

Thanks for helping! This project has an unusual property worth understanding
before you touch it: **most of the code is generated**. The API surface (all
types in `include/tgbot/types.hpp`, all methods in `include/tgbot/api.hpp`,
the JSON glue, registries and test corpora under `*/generated/`) is emitted by
`tools/generate.py` from `spec/api_inventory.json`, which is itself scraped
from the official Bot API reference by `tools/scrape_api.py`.

**Never edit generated files by hand** — change the generator (or the
scraper) and regenerate. Every generated file carries a banner saying so.

## Building

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build -L unit --output-on-failure
```

Requirements: a C++20 compiler (GCC ≥ 13 / Clang ≥ 17 / MSVC ≥ 19.36),
CMake ≥ 3.20, network access on first configure (FetchContent), and OpenSSL
development headers on Linux. Everything else is fetched and pinned.

## The regeneration workflow

```bash
python tools/scrape_api.py --fetch     # only for a Bot API version bump
python tools/validate_inventory.py     # inventory integrity gate
python tools/generate.py               # re-emit all generated sources
cmake --build build && ctest --test-dir build -L unit
```

`generate.py` re-runs `clang-format` on its output when the binary is on
PATH; regeneration is idempotent, and CI diffs a fresh generation against the
committed files.

## Quality gates (all enforced by CI)

- warnings are errors on GCC, Clang and MSVC; an ASan/UBSan job runs the
  unit suite;
- the coverage tests fail if any documented type or method is missing;
- Doxygen must build with **zero** warnings — every public symbol documented;
- `clang-format --dry-run --Werror` over all tracked sources (config in
  `.clang-format`, version pinned in CI);
- `tools/lint_docs.py`: every `tgbot::` symbol and `Api` method mentioned in
  the docs must exist, and every full-program snippet must compile as
  written.

## Tests

- Unit tests are hermetic: the HTTP transport is a scripted mock
  (`tests/unit/mock_http_client.hpp`); the webhook tests bind localhost only.
- Live integration tests (`-DTGBOT_ENABLE_INTEGRATION_TESTS=ON`,
  `ctest -L integration`) need a real bot token in a local `.env` — see
  `.env.example` and [docs/testing.md](docs/testing.md). Never commit
  credentials; `.env` is git-ignored.

## Pull requests

Keep commits focused; include tests for behavior changes; run the gates
above locally before pushing. For anything touching the generator, include
the regenerated output in the same commit.
