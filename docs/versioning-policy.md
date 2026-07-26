# Versioning & Migration Policy

tgbot-cpp-full tracks two version numbers, and they answer different questions:

| Question | Answer | Where |
|---|---|---|
| Which Telegram Bot API does this library implement? | `tgbot::api_version()` → `"10.2"` | `<tgbot/version.hpp>` (generated) |
| Which release of the library is this? | `tgbot::library_version()` → `"0.1.0"` | `<tgbot/version.hpp>` |

Both are `constexpr std::string_view`, so you can use them at compile time or print them at startup:

```cpp
#include <tgbot/tgbot.hpp>

#include <iostream>

int main() {
    std::cout << "Telegram Bot API: " << tgbot::api_version() << '\n'
              << "tgbot library:    " << tgbot::library_version() << '\n';
}
```

The library version follows [semantic versioning](https://semver.org). The Bot API version follows Telegram's own numbering (see the [official changelog](https://core.telegram.org/bots/api-changelog)); it is data, not a promise about C++ source compatibility — that promise is the semver number, according to the rules below.

## How the two versions relate

The entire type system (`types.hpp`, 388 types), the `Api` surface (`api.hpp`, 185 methods with their params structs), the JSON glue, and `version.hpp` itself are **generated** from `spec/api_inventory.json`, a machine-readable inventory scraped from the official reference. Generated files are committed, so building the library never requires Python.

When Telegram ships a new Bot API version, the library re-generates against it and releases:

- a **minor** library release when the regenerated code is source-compatible for well-behaved callers (the usual case: new types, new methods, new optional fields, new union alternatives);
- a **major** library release when it is not (see [What is a breaking change](#what-is-a-breaking-change) — and what "well-behaved" means);
- **patch** releases never change the API surface or the Bot API version; they fix bugs in the handwritten runtime (transport, polling, webhook, retry) or the generator's output for the *same* inventory.

## Anatomy of an API bump

An API version bump is mechanical. The pipeline lives in `tools/` and runs in three steps:

```sh
# 1. Re-download the live reference and re-scrape it into the inventory.
#    (Without --fetch, the scraper parses the committed snapshot
#    spec/raw/bot_api.html — that is what reproducible re-generation uses.)
python tools/scrape_api.py --fetch

# 2. Validate the inventory: every type reference in fields, params,
#    returns, and union subtype lists must resolve, plus golden assertions
#    on well-known entries (e.g. sendMessage returns Message,
#    Update.update_id is first and required). Exits non-zero on failure.
python tools/validate_inventory.py

# 3. Regenerate everything carrying the do-not-edit banner:
#    include/tgbot/types.hpp, include/tgbot/api.hpp,
#    include/tgbot/version.hpp, src/generated/*.cpp (JSON glue, method
#    impls, registries), and the generated test corpora under
#    tests/unit/generated/.
python tools/generate.py
```

Then rebuild and run the unit tests. Two generated safety nets prove the bump is *complete*, not just compiling:

- **The coverage gate** (`tests/unit/coverage_test.cpp`): the generated registry (`src/generated/registry.cpp`, `method_registry.cpp`) takes the address of every real serializer and `Api` method, and the test diffs those registered names against `spec/api_inventory.json`. Any type or method present in the inventory but missing from the build fails the test stage with the missing name.
- **The generated corpora**: JSON round-trip tests for all 388 types (synthetic instance → JSON → parse → JSON, byte-equal) and a method smoke corpus that invokes each of the 185 methods against a mock transport primed with a minimal typed result.

If the new reference changed a field type or renamed something, the fallout shows up as compile errors in the handwritten runtime or test expectations — fixed by hand, then committed together with the regenerated files. See [testing.md](testing.md) for the full test taxonomy.

## What is a breaking change

A **major** release is required when regeneration produces any of the following:

- **A field or parameter changes type** — e.g. a `String` becomes an `Integer`, a scalar becomes an array, or an optional field becomes required (`std::optional<T>` → `T`, or vice versa).
- **A union loses an alternative, or an alternative is renamed.** Unions are `std::variant` aliases (`tgbot::ChatMember`, `tgbot::MessageOrigin`, `tgbot::ReactionType`, …).
- **A type, method, or field is removed** because Telegram removed it from the reference.
- **A handwritten runtime signature changes** (`Bot`, `Dispatcher`, `LongPoller`, `WebhookServer`, `ApiClient::Options`, …).

The following are **minor** (source-compatible for well-behaved callers):

- New types, new methods, new fields (required fields default-initialize; optional fields are `std::optional` and default to absent).
- New alternatives **added to an existing union** — and, critically, this means the **order of `std::variant` alternatives may change between minor versions**, because generated unions list subtypes in official documentation order.

That last point is the one migration hazard you control. **Access variant alternatives by type, never by index:**

```cpp
#include <tgbot/tgbot.hpp>

#include <cstdlib>
#include <iostream>

int main() {
    tgbot::Bot bot(std::getenv("TG_BOT_TOKEN"));

    tgbot::ChatMember member = bot.api().getChatMember({
        .chat_id = -1001234567890,
        .user_id = 987654321,
    });

    // GOOD — dispatch by type: stable across API bumps.
    if (const auto* admin = std::get_if<tgbot::ChatMemberAdministrator>(&member)) {
        std::cout << admin->user.first_name
                  << " can_manage_chat=" << admin->can_manage_chat << '\n';
    }

    // Also good — std::visit with if constexpr / overload sets.
    std::visit([](const auto& m) { std::cout << m.status << '\n'; }, member);

    // BAD — dispatch by index: breaks silently when a new subtype is
    // inserted into the union. Never write this:
    //   if (member.index() == 1) { auto& a = std::get<1>(member); ... }
    return 0;
}
```

The same rule applies to `tgbot::ChatId` (`std::variant<std::int64_t, std::string>`) and every other union in `types.hpp`. `std::get_if<T>`, `std::holds_alternative<T>`, and `std::visit` are the supported access patterns; `.index()` and `std::get<N>` are not covered by any compatibility promise.

One more "well-behaved caller" rule: **designated initializers must name fields in declaration order**, and params-struct fields follow official documentation order. Telegram occasionally inserts a parameter in the middle of a method's table; if you initialize fields on both sides of the insertion point your initializer still compiles unchanged, since you name fields rather than count them — order among the fields you *do* name is preserved from the docs, which change additively at the end far more often than in the middle. If a mid-struct insertion does reorder relative to your initializer list, the compiler tells you at the call site; that is a compile-time fix, not a silent behavior change, and it does not count as a semver break.

## Generated-code stability guarantees

Every generated file is stamped:

```cpp
// Generated by tools/generate.py from spec/api_inventory.json — do not edit.
```

Within a given major release series you can rely on:

- **Names mirror the official reference.** Types keep their documented CamelCase names (`Message`, `ChatFullInfo`); fields and parameters keep their documented snake_case names (`chat_id`, `parse_mode`). The library never invents or renames identifiers.
- **Params structs exist for every method**, named `<Method>Params` (`SendMessageParams`, `GetChatMemberParams`), with fields in official doc order so designated initializers read like the docs.
- **Type mapping is fixed**: `Integer` → `std::int64_t`, `String` → `std::string`, `Float` → `double`, `Boolean`/`True` → `bool`, `Array of X` → `std::vector<X>`, optional → `std::optional<T>`, unions → `std::variant` behind a named alias, recursive fields → `tgbot::Box<T>`.
- **Deterministic output**: the same inventory plus the same generator produces byte-identical files, so diffs between releases show exactly what the API bump changed.
- **Do not edit generated files.** Local edits are overwritten by the next `generate.py` run and void the coverage guarantee. If generated output looks wrong, the fix belongs in `tools/generate.py` or the inventory, never in the emitted C++.

Anything *not* listed — private headers under `include/tgbot/detail/`, the layout of `src/generated/` shards, test corpora — is internal and may change in any release.

## Deprecation policy

- **Mirroring Telegram.** The library does not deprecate API surface on its own schedule. A generated type, method, or field exists exactly as long as it exists in the official reference; when Telegram removes it, it leaves the inventory and therefore the generated code, in the next **major** release.
- **Handwritten runtime.** When a runtime API (`Bot`, `LongPoller`, `RetryPolicy`, …) needs to change shape, the old form is kept for at least one minor release, marked `[[deprecated]]` with a message naming the replacement, before removal in the next major release.
- **No silent behavior changes.** Defaults (retry policy, polling timeout, webhook secret-token enforcement) only change in a major release, and the changelog calls them out.

## Checking compatibility in your build

Because both accessors are `constexpr`, you can assert your expectations at compile time:

```cpp
#include <tgbot/version.hpp>

static_assert(tgbot::api_version() == "10.2",
              "this bot relies on Bot API 10.2 semantics — review before bumping");
```

For handling runtime consequences of version drift — unknown update types arriving from a newer Bot API, or errors from removed methods — see [error-handling-and-rate-limits.md](error-handling-and-rate-limits.md). New to the library? Start with [getting-started.md](getting-started.md).
