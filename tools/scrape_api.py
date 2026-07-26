#!/usr/bin/env python3
"""Scrape the official Telegram Bot API reference into spec/api_inventory.json.

The inventory is the machine-readable source of truth for code generation and
coverage checking.  It records every API object type (fields, types,
optionality, union subtypes) and every method (parameters, required flags,
return type).

Usage:
    python tools/scrape_api.py [--fetch] [--html PATH] [--out PATH]

By default the scraper parses the committed snapshot in spec/raw/bot_api.html;
pass --fetch to re-download the live reference first (used for version bumps).
"""

from __future__ import annotations

import argparse
import html as html_mod
import json
import re
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_HTML = ROOT / "spec" / "raw" / "bot_api.html"
DEFAULT_OUT = ROOT / "spec" / "api_inventory.json"
API_URL = "https://core.telegram.org/bots/api"

ENTITY_RE = re.compile(r"^[A-Za-z][A-Za-z0-9]*$")
SCALARS = {
    "Integer": "integer",
    "Int": "integer",
    "String": "string",
    "Boolean": "boolean",
    "Bool": "boolean",
    "Float": "float",
    "Float number": "float",
    "True": "true",
}

# Methods whose return type cannot be reliably extracted from prose get an
# explicit override here (raw type-expression syntax, same as table cells).
RETURN_OVERRIDES: dict[str, str] = {}


def strip_tags(fragment: str) -> str:
    """Convert an HTML fragment to readable plain text."""
    text = re.sub(r"<br\s*/?>", "\n", fragment)
    text = re.sub(r"<img[^>]*alt=\"([^\"]*)\"[^>]*>", r"\1", text)
    text = re.sub(r"<[^>]+>", "", text)
    text = html_mod.unescape(text)
    return re.sub(r"[ \t]+", " ", text).strip()


def parse_type_expr(raw: str) -> dict:
    """Parse a doc type expression ("Array of Integer or String", ...) to an AST."""
    raw = raw.strip()
    if raw.startswith("Array of "):
        return {"kind": "array", "item": parse_type_expr(raw[len("Array of "):])}
    # Union lists: "A, B and C", "A or B", "A, B, C or D"
    parts = re.split(r",\s*|\s+and\s+|\s+or\s+", raw)
    parts = [p.strip() for p in parts if p.strip()]
    if len(parts) > 1:
        return {"kind": "union", "options": [parse_type_expr(p) for p in parts]}
    if raw in SCALARS:
        return {"kind": SCALARS[raw]}
    if not ENTITY_RE.match(raw):
        raise ValueError(f"unparseable type expression: {raw!r}")
    return {"kind": "ref", "name": raw}


class Section:
    """One h4-delimited block of the reference: an API type or method."""

    def __init__(self, name: str, anchor: str, body_html: str):
        self.name = name
        self.anchor = anchor
        self.body = body_html

    @property
    def is_method(self) -> bool:
        return self.name[0].islower()

    def description_html(self) -> str:
        """Body HTML up to the fields/params table (or whole body if none)."""
        cut = self.body.find("<table")
        desc = self.body if cut < 0 else self.body[:cut]
        # Drop trailing union <ul> from the description text.
        return re.sub(r"<ul>.*?</ul>", "", desc, flags=re.S)

    def description_text(self) -> str:
        return strip_tags(self.description_html())

    def table_rows(self) -> list[list[str]] | None:
        m = re.search(r"<table[^>]*>(.*?)</table>", self.body, re.S)
        if not m:
            return None
        rows = []
        for row_m in re.finditer(r"<tr>(.*?)</tr>", m.group(1), re.S):
            cells = [
                c.strip()
                for c in re.findall(r"<t[dh][^>]*>(.*?)</t[dh]>", row_m.group(1), re.S)
            ]
            rows.append(cells)
        return rows

    def union_subtypes(self) -> list[str] | None:
        """If the body is a bare <ul> of internal type links, return those names."""
        if self.table_rows() is not None:
            return None
        m = re.search(r"<ul>(.*?)</ul>", self.body, re.S)
        if not m:
            return None
        names = []
        for li in re.finditer(r"<li>(.*?)</li>", m.group(1), re.S):
            link = re.fullmatch(
                r"\s*<a href=\"#([a-z0-9]+)\">([A-Za-z0-9]+)</a>\s*", li.group(1)
            )
            if not link:
                return None  # a prose list, not a subtype list
            names.append(link.group(2))
        return names or None


def split_sections(html: str) -> list[Section]:
    """Split the reference into per-entity sections keyed by h4 headings."""
    heads = [
        (m.start(), m.end(), m.group(1), strip_tags(m.group(2)))
        for m in re.finditer(
            r"<h4><a class=\"anchor\" name=\"([a-z0-9-]+)\"[^>]*>"
            r"<i class=\"anchor-icon\"></i></a>(.*?)</h4>",
            html,
            re.S,
        )
    ]
    boundaries = sorted(
        [m.start() for m in re.finditer(r"<h[34]>", html)] + [len(html)]
    )
    sections = []
    for start, end, anchor, title in heads:
        if not ENTITY_RE.match(title):
            continue  # prose headings like "Sending files", changelog dates, ...
        nxt = min(b for b in boundaries if b > start)
        sections.append(Section(title, anchor, html[end:nxt]))
    return sections


def parse_fields(section: Section) -> list[dict] | None:
    rows = section.table_rows()
    if rows is None:
        return None
    header = [strip_tags(c) for c in rows[0]]
    fields = []
    if header[:3] == ["Field", "Type", "Description"]:
        for cells in rows[1:]:
            name, type_html, desc_html = cells[0], cells[1], cells[2]
            desc = strip_tags(desc_html)
            required = not desc.startswith("Optional")
            fields.append(
                {
                    "name": strip_tags(name),
                    "type_raw": strip_tags(type_html),
                    "type": parse_type_expr(strip_tags(type_html)),
                    "required": required,
                    "description": desc,
                }
            )
    elif header[:4] == ["Parameter", "Type", "Required", "Description"]:
        for cells in rows[1:]:
            name, type_html, req_html, desc_html = cells[:4]
            fields.append(
                {
                    "name": strip_tags(name),
                    "type_raw": strip_tags(type_html),
                    "type": parse_type_expr(strip_tags(type_html)),
                    "required": strip_tags(req_html) == "Yes",
                    "description": strip_tags(desc_html),
                }
            )
    else:
        raise ValueError(f"{section.name}: unrecognized table header {header}")
    return fields


def extract_return_type(section: Section, type_anchors: dict[str, str]) -> tuple[dict, str]:
    """Determine a method's return type from its description prose.

    Scans sentences containing 'return' for linked type names (resolved via
    their href anchors, so plural link text is fine), 'Array of' prefixes and
    scalar keywords.  Multiple distinct candidates become a union (e.g. the
    editMessage* family returns Message or True).
    """
    if section.name in RETURN_OVERRIDES:
        raw = RETURN_OVERRIDES[section.name]
        return parse_type_expr(raw), f"override: {raw}"

    desc = self_desc = section.description_html()
    sentences = re.split(r"(?<=[.!])\s+", self_desc)
    ret_sentences = [s for s in sentences if re.search(r"eturn", s)]
    candidates: list[tuple[int, dict]] = []
    seen: set[str] = set()

    def add(pos: int, ast: dict) -> None:
        key = json.dumps(ast, sort_keys=True)
        if key not in seen:
            seen.add(key)
            candidates.append((pos, ast))

    offset = 0
    for sent in ret_sentences:
        pos0 = desc.find(sent, offset)
        # Linked types, optionally prefixed by "Array of" / "an array of".
        for m in re.finditer(
            r"((?:[Aa]n\s+)?[Aa]rray of\s+)?<a href=\"#([a-z0-9]+)\">", sent
        ):
            anchor = m.group(2)
            name = type_anchors.get(anchor)
            if name is None:
                continue  # link to a method or a doc section, not a type
            ast: dict = {"kind": "ref", "name": name}
            if m.group(1):
                ast = {"kind": "array", "item": ast}
            add(pos0 + m.start(), ast)
        # Scalar keywords outside tags.
        text = strip_tags(sent)
        for m in re.finditer(r"\b(True|Integer|Int|String|Boolean|Float)\b", text):
            word = m.group(1)
            if word == "True":
                ast = {"kind": "true"}
            elif word in ("Int", "Integer"):
                ast = {"kind": "integer"}
            else:
                ast = {"kind": SCALARS[word]}
            add(pos0 + len(sent) + m.start(), ast)  # after links in same sentence
    if not candidates:
        raise ValueError(f"{section.name}: no return type found")
    candidates.sort(key=lambda c: c[0])
    asts = [c[1] for c in candidates]
    ret = asts[0] if len(asts) == 1 else {"kind": "union", "options": asts}
    return ret, " | ".join(json.dumps(a) for a in asts)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--fetch", action="store_true", help="re-download the reference")
    ap.add_argument("--html", type=Path, default=DEFAULT_HTML)
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    args = ap.parse_args()

    if args.fetch:
        args.html.parent.mkdir(parents=True, exist_ok=True)
        with urllib.request.urlopen(API_URL) as resp:
            args.html.write_bytes(resp.read())

    html = args.html.read_text(encoding="utf-8")

    version_m = re.search(r"<strong>Bot API (\d+\.\d+)</strong>", html)
    if not version_m:
        print("error: could not determine Bot API version", file=sys.stderr)
        return 1
    version = version_m.group(1)

    sections = split_sections(html)
    type_sections = [s for s in sections if not s.is_method]
    method_sections = [s for s in sections if s.is_method]
    type_anchors = {s.anchor: s.name for s in type_sections}

    types = []
    for s in type_sections:
        fields = parse_fields(s)
        subtypes = s.union_subtypes()
        if fields is not None:
            kind = "struct"
        elif subtypes is not None:
            kind = "union"
        elif s.name == "InputFile":
            kind = "special"
        else:
            kind = "marker"  # documented object with no fields
        entry = {
            "name": s.name,
            "anchor": s.anchor,
            "kind": kind,
            "description": s.description_text(),
        }
        if fields is not None:
            entry["fields"] = fields
        if subtypes is not None:
            entry["subtypes"] = subtypes
        types.append(entry)

    methods = []
    failures = []
    for s in method_sections:
        try:
            ret, ret_raw = extract_return_type(s, type_anchors)
        except ValueError as exc:
            failures.append(str(exc))
            ret, ret_raw = None, None
        methods.append(
            {
                "name": s.name,
                "anchor": s.anchor,
                "description": s.description_text(),
                "params": parse_fields(s) or [],
                "return": ret,
                "return_raw": ret_raw,
            }
        )

    if failures:
        print("UNRESOLVED RETURN TYPES:", file=sys.stderr)
        for f in failures:
            print("  -", f, file=sys.stderr)
        return 1

    inventory = {
        "bot_api_version": version,
        "source": API_URL,
        "types": types,
        "methods": methods,
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(
        json.dumps(inventory, indent=1, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    print(
        f"Bot API {version}: {len(types)} types "
        f"({sum(1 for t in types if t['kind'] == 'union')} unions, "
        f"{sum(1 for t in types if t['kind'] == 'marker')} marker), "
        f"{len(methods)} methods -> {args.out}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
