#!/usr/bin/env python3
"""Validate spec/api_inventory.json for internal consistency.

Checks that every type reference (fields, params, returns, union subtypes)
resolves to a scraped type, plus golden assertions on well-known entries.
Run after scrape_api.py; exits non-zero on any failure.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INV = ROOT / "spec" / "api_inventory.json"

SCALAR_KINDS = {"integer", "string", "boolean", "float", "true"}


def iter_refs(ast: dict):
    if ast["kind"] == "ref":
        yield ast["name"]
    elif ast["kind"] == "array":
        yield from iter_refs(ast["item"])
    elif ast["kind"] == "union":
        for opt in ast["options"]:
            yield from iter_refs(opt)
    elif ast["kind"] not in SCALAR_KINDS:
        raise AssertionError(f"unknown AST kind {ast['kind']}")


def main() -> int:
    inv = json.loads(INV.read_text(encoding="utf-8"))
    types = {t["name"]: t for t in inv["types"]}
    methods = {m["name"]: m for m in inv["methods"]}
    errors: list[str] = []

    def check_ref(owner: str, name: str) -> None:
        if name not in types:
            errors.append(f"{owner}: dangling type reference {name!r}")

    for t in inv["types"]:
        for f in t.get("fields", []):
            for ref in iter_refs(f["type"]):
                check_ref(f"{t['name']}.{f['name']}", ref)
        for sub in t.get("subtypes", []):
            check_ref(f"{t['name']} subtype", sub)

    for m in inv["methods"]:
        for p in m["params"]:
            for ref in iter_refs(p["type"]):
                check_ref(f"{m['name']}({p['name']})", ref)
        if m["return"] is None:
            errors.append(f"{m['name']}: missing return type")
        else:
            for ref in iter_refs(m["return"]):
                check_ref(f"{m['name']} return", ref)

    # Golden assertions on entries whose shape is well known.
    def expect(cond: bool, msg: str) -> None:
        if not cond:
            errors.append(f"golden: {msg}")

    expect("Message" in types and "Update" in types, "Message/Update present")
    upd = types.get("Update", {"fields": []})
    expect(
        [f["name"] for f in upd["fields"]][0] == "update_id"
        and upd["fields"][0]["required"],
        "Update.update_id is first and required",
    )
    sm = methods.get("sendMessage")
    expect(sm is not None, "sendMessage present")
    if sm:
        params = {p["name"]: p for p in sm["params"]}
        expect(params["chat_id"]["required"], "sendMessage chat_id required")
        expect(not params["parse_mode"]["required"], "sendMessage parse_mode optional")
        expect(sm["return"] == {"kind": "ref", "name": "Message"},
               f"sendMessage returns Message (got {sm['return']})")
    gu = methods.get("getUpdates")
    if gu:
        expect(gu["return"] == {"kind": "array", "item": {"kind": "ref", "name": "Update"}},
               f"getUpdates returns Array of Update (got {gu['return']})")
    emt = methods.get("editMessageText")
    if emt:
        expect(emt["return"]["kind"] == "union",
               f"editMessageText returns a union (got {emt['return']})")
    cm = types.get("ChatMember")
    expect(cm is not None and cm["kind"] == "union" and len(cm.get("subtypes", [])) == 6,
           "ChatMember is a 6-way union")
    expect(types.get("InputFile", {}).get("kind") == "special", "InputFile special")
    gm = methods.get("getMe")
    if gm:
        expect(gm["return"] == {"kind": "ref", "name": "User"}, "getMe returns User")
        expect(gm["params"] == [], "getMe has no params")

    if errors:
        print(f"INVENTORY INVALID ({len(errors)} errors):")
        for e in errors:
            print("  -", e)
        return 1
    n_fields = sum(len(t.get("fields", [])) for t in inv["types"])
    n_params = sum(len(m["params"]) for m in inv["methods"])
    print(
        f"inventory OK: {len(inv['types'])} types / {n_fields} fields, "
        f"{len(inv['methods'])} methods / {n_params} params, "
        f"Bot API {inv['bot_api_version']}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
