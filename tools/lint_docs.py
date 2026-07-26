#!/usr/bin/env python3
"""Docs accuracy lint: every Api method call and tgbot:: symbol mentioned in
README.md / docs/*.md must actually exist (inventory + curated runtime names).

Also extracts full-program C++ snippets (those containing `int main`) to
build/doc_snippets/ so the build script can compile them as written.
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

RUNTIME_NAMES = {
    # handwritten public surface
    "Api", "ApiClient", "ApiError", "Bot", "Box", "ChatId", "CurlHttpClient",
    "Dispatcher", "Error", "ErrorHandler", "HttpClient", "HttpRequest",
    "HttpResponse", "InlineKeyboardBuilder", "InputFile", "LongPollOptions",
    "LongPoller", "MessageOrBool", "MultipartPart", "NetworkError",
    "ReplyKeyboardBuilder", "ReplyMarkup", "RetryPolicy", "UpdateHandler",
    "WebhookOptions", "WebhookServer", "answer_webhook_with", "api_version",
    "library_version", "collect_files", "InputRichMessageMediaContent",
    "test",  # tgbot::test mock in testing docs
    "tgbot",  # the CMake target tgbot::tgbot in build snippets
}


def main() -> int:
    inv = json.loads((ROOT / "spec" / "api_inventory.json").read_text(encoding="utf-8"))
    methods = {m["name"] for m in inv["methods"]}
    types = {t["name"] for t in inv["types"]}
    params_structs = {m["name"][0].upper() + m["name"][1:] + "Params" for m in inv["methods"]}
    known = types | RUNTIME_NAMES | params_structs

    files = [ROOT / "README.md"] + sorted((ROOT / "docs").glob("*.md"))
    errors: list[str] = []
    snippets_dir = ROOT / "build" / "doc_snippets"
    snippets_dir.mkdir(parents=True, exist_ok=True)
    extracted = 0

    for f in files:
        text = f.read_text(encoding="utf-8")
        rel = f.relative_to(ROOT)

        # tgbot::Name references anywhere in the file.
        for m in re.finditer(r"tgbot::([A-Za-z_][A-Za-z0-9_]*)", text):
            name = m.group(1)
            if name not in known and name not in methods and name != "detail":
                errors.append(f"{rel}: unknown symbol tgbot::{name}")

        # api().method( / api.method( calls.
        for m in re.finditer(r"\bapi(?:\(\))?\s*\.\s*([a-zA-Z_][A-Za-z0-9_]*)\s*\(", text):
            name = m.group(1)
            if name in ("client",):
                continue
            if name not in methods:
                errors.append(f"{rel}: unknown Api method .{name}()")

        # Full-program snippets are compiled as written by the caller.
        for i, m in enumerate(re.finditer(r"```(?:cpp|c\+\+)\n(.*?)```", text, re.S)):
            code = m.group(1)
            if "int main" in code:
                out = snippets_dir / f"{f.stem}_{i}.cpp"
                out.write_text(code, encoding="utf-8", newline="\n")
                extracted += 1

    if errors:
        print(f"DOC LINT FAILED ({len(errors)} problems):")
        for e in sorted(set(errors)):
            print("  -", e)
        return 1
    print(f"doc lint OK across {len(files)} files; {extracted} full-program "
          f"snippets extracted to {snippets_dir.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
