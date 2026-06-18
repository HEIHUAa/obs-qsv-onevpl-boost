#!/usr/bin/env python3
"""
Generate C++ field tables (CO_FIELDS, CO2_FIELDS, CO3_FIELDS, CODDI_FIELDS)
for ParseCustomCodingOptions from Intel oneVPL SDK header files.

Usage:
    python gen_coding_option_fields.py mfxstructures.h [mfxddi.h ...]

Drag-and-drop one or more header files onto this script (Windows).
The script auto-detects which target structs exist in each file.

Output: ready-to-use C++ code to stdout.
Redirect to a file: python gen_coding_option_fields.py mfxstructures.h > tables.cpp
"""

import re
import os
import sys
from dataclasses import dataclass
from typing import Optional

# ── type mappings ──────────────────────────────────────────────────────────
TYPE_MAP = {
    "mfxU16":   "FT_U16",
    "mfxI16":   "FT_S16",
    "mfxU8":    "FT_U8",
    "mfxU32":   "FT_U32",
    "mfxI8":    "FT_S8",
}

# Pair types: split into two scalar entries with .x / .y suffix
# value = (scalar_type, first_type, second_type)
PAIR_TYPES = {
    "mfxI16Pair": ("FT_S16", "FT_S16"),
}

# Structs we care about and their output table names
TARGET_STRUCTS = {
    "mfxExtCodingOption":     "CO_FIELDS",
    "mfxExtCodingOption2":    "CO2_FIELDS",
    "mfxExtCodingOption3":    "CO3_FIELDS",
    "mfxExtCodingOptionDDI":  "CODDI_FIELDS",
}

# Fields to always skip (exact name match)
SKIP_FIELD_NAMES = {"Header"}

# ── C parser helpers ───────────────────────────────────────────────────────

@dataclass
class FieldDef:
    c_type: str           # raw C type name, e.g. "mfxU16"
    name: str             # field name, e.g. "CAVLC"
    array_len: Optional[int]  # None = scalar, N = array[N]
    is_nested_struct: bool      # True for `struct { ... } name;`
    nested_fields: list  # list[FieldDef] when is_nested_struct
    is_deprecated: bool  # marked with MFX_DEPRECATED
    is_pair: bool        # known pair type


def parse_struct_body(lines: list[str]) -> list[FieldDef]:
    """
    Parse the list of lines inside a `typedef struct { ... } Name;` block.
    Returns a flat list of FieldDef entries.
    """
    fields: list[FieldDef] = []
    i = 0
    while i < len(lines):
        line = lines[i]
        stripped = line.strip()
        # skip blank / comment-only lines
        if not stripped or stripped.startswith("/*") or stripped.startswith("*") or stripped.startswith("//") or stripped.startswith("#"):
            i += 1
            continue

        # remove trailing C comments
        code = re.sub(r"/\*.*?\*/", "", stripped).strip()

        # detect nested struct
        if code.startswith("struct {"):
            # collect nested struct body — scan lines until matching }
            brace_depth = 0
            j = i
            while j < len(lines):
                raw = lines[j]
                processed = re.sub(r"/\*.*?\*/", "", raw)
                for ch in processed:
                    if ch == '{':
                        brace_depth += 1
                    elif ch == '}':
                        brace_depth -= 1
                        if brace_depth == 0:
                            break
                if brace_depth == 0:
                    break
                j += 1

            if brace_depth == 0 and j < len(lines):
                # concatenate all lines from i to j (inclusive)
                all_text = ""
                for k in range(i, j + 1):
                    # strip both /* */ and // comments
                    cleaned = re.sub(r"/\*.*?\*/", "", lines[k])
                    cleaned = re.sub(r"//.*$", "", cleaned)
                    all_text += " " + cleaned
                all_text = all_text.strip()
                # find the struct name after the closing }
                m = re.search(r"struct\s*\{", all_text)
                if m:
                    inner_start = m.end()
                    # find matching }
                    depth = 1
                    pos = inner_start
                    while depth > 0 and pos < len(all_text):
                        if all_text[pos] == '{':
                            depth += 1
                        elif all_text[pos] == '}':
                            depth -= 1
                        pos += 1
                    inner_body = all_text[inner_start:pos-1]
                    # after }, find the field name
                    rest = all_text[pos:].strip()
                    nm = re.match(r"(\w+)", rest)
                    if nm:
                        struct_name = nm.group(1)
                        # parse inner body for fields
                        inner_fields = []
                        for part in inner_body.split(";"):
                            part = part.strip()
                            if not part:
                                continue
                            inner_fm = re.match(r"(mfx\w+)\s+(\w+)", part)
                            if inner_fm:
                                inner_type = inner_fm.group(1)
                                inner_field = inner_fm.group(2)
                                if inner_field.startswith("reserved"):
                                    continue
                                ft = TYPE_MAP.get(inner_type)
                                if ft:
                                    inner_fields.append(FieldDef(inner_type, inner_field, None, False, [], False, False))
                        if inner_fields:
                            fields.append(FieldDef("struct", struct_name, None, True, inner_fields, False, False))
                i = j + 1
                continue

        # remove MFX_DEPRECATED prefix
        deprecated = False
        if "MFX_DEPRECATED" in code:
            deprecated = True
            code = re.sub(r"MFX_DEPRECATED\s+", "", code).strip()

        # match: type name;   or type name[N];
        # type could have trailing * or be a complex type
        m = re.match(r"(mfx\w+(?:\s*\*)?)\s+(\w+)(?:\[(\d+)\])?\s*;", code)
        if m:
            c_type = m.group(1).strip()
            name = m.group(2)
            array_len = int(m.group(3)) if m.group(3) else None

            if name in SKIP_FIELD_NAMES:
                i += 1
                continue
            if re.match(r"^reserved\d*$", name):
                i += 1
                continue

            is_pair = c_type in PAIR_TYPES
            ft = TYPE_MAP.get(c_type)
            if ft or is_pair:
                fields.append(FieldDef(c_type, name, array_len, False, [], deprecated, is_pair))
            # else: unknown type, skip (e.g. mfxExtBuffer)
        i += 1
    return fields


def extract_struct_block(lines: list[str], struct_name: str) -> Optional[tuple[int, int]]:
    """Find the typedef struct block for the given struct name."""
    for i, line in enumerate(lines):
        if f"}} {struct_name};" in line:
            # walk backwards to find the matching typedef struct {
            brace_depth = 1
            j = i - 1
            while j >= 0:
                text = lines[j]
                for ch in text:
                    if ch == '}':
                        brace_depth += 1
                    elif ch == '{':
                        brace_depth -= 1
                        if brace_depth == 0:
                            return (j, i)
                j -= 1
    return None


def parse_header_for_struct(filepath: str, struct_name: str) -> list[FieldDef]:
    """Parse a header file and return fields for the given struct."""
    with open(filepath, encoding="utf-8") as f:
        text = f.read()
    lines = text.split("\n")
    span = extract_struct_block(lines, struct_name)
    if not span:
        print(f"  [WARN] struct {struct_name} not found in {filepath}", file=sys.stderr)
        return []
    start, end = span
    body_lines = lines[start:end+1]
    return parse_struct_body(body_lines)


def flatten_fields(struct_name: str, fields: list[FieldDef]) -> list[tuple[str, str, str]]:
    """
    Flatten parsed fields into (field_name, offset_expr, field_type) triples.
    offset_expr is a C++ expression usable with offsetof.
    """
    result: list[tuple[str, str, str]] = []
    current_offset = 0  # simulated offset for sequential layout
    # We can't reliably simulate alignment, so we use offsetof() instead.

    for f in fields:
        if f.is_nested_struct:
            # nested struct: fields accessed as StructName.NestedName.FieldName
            for nf in f.nested_fields:
                full_name = f"{f.name}.{nf.name}"
                offset_expr = f"offsetof({struct_name}, {f.name}.{nf.name})"
                ft = TYPE_MAP.get(nf.c_type, "FT_U16")
                result.append((full_name, offset_expr, ft))
        elif f.is_pair:
            # pair type: split into .x and .y
            scalar_type = PAIR_TYPES[f.c_type][0]
            base_offset = f"offsetof({struct_name}, {f.name})"
            sz = type_size(f.c_type.replace("Pair", ""))  # mfxI16Pair -> sizeof(mfxI16)
            result.append((f"{f.name}.x", base_offset, scalar_type))
            result.append((f"{f.name}.y", f"{base_offset} + {sz}", scalar_type))
        elif f.array_len:
            # array: expose each element individually
            base_offset = f"offsetof({struct_name}, {f.name})"
            elem_size = type_size(f.c_type)
            ft = TYPE_MAP.get(f.c_type, "FT_U16")
            for idx in range(f.array_len):
                if idx == 0:
                    result.append((f"{f.name}[{idx}]", base_offset, ft))
                else:
                    result.append((f"{f.name}[{idx}]", f"{base_offset} + {idx} * {elem_size}", ft))
        else:
            ft = TYPE_MAP.get(f.c_type)
            if ft:
                result.append((f.name, f"offsetof({struct_name}, {f.name})", ft))
    return result


def type_size(c_type: str) -> int:
    """Return sizeof for known types."""
    sizes = {
        "mfxU16": 2, "mfxI16": 2, "mfxU8": 1, "mfxI8": 1,
        "mfxU32": 4, "mfxI32": 4, "mfxU64": 8, "mfxI64": 8,
    }
    return sizes.get(c_type, 2)


# ── C++ code generation ────────────────────────────────────────────────────

def generate_field_table(struct_name: str, table_name: str, fields: list[FieldDef]) -> str:
    """Generate the C++ code for a single field table."""
    entries = flatten_fields(struct_name, fields)
    count = len(entries)

    lines = []
    lines.append(f"static constexpr std::array<FieldEntry, {count}> {table_name}{{")
    for i, (name, offset_expr, field_type) in enumerate(entries):
        comma = "," if i < count - 1 else ","  # all but last get comma
        lines.append(f"  FieldEntry{{\"{name}\", {offset_expr}, {field_type}}},")
    lines.append("};")
    return "\n".join(lines)


def scan_header_for_target_structs(filepath: str) -> dict[str, list[FieldDef]]:
    """Scan a single header file and return {struct_name: fields} for known targets."""
    result: dict[str, list[FieldDef]] = {}
    for struct_name in TARGET_STRUCTS:
        fields = parse_header_for_struct(filepath, struct_name)
        if fields:
            result[struct_name] = fields
    return result


def generate_all(files: list[str]) -> str:
    """Scan all header files for target structs and generate C++ tables."""
    found: dict[str, list[FieldDef]] = {}
    for fp in files:
        if not os.path.isfile(fp):
            print(f"  [SKIP] not a file: {fp}", file=sys.stderr)
            continue
        file_found = scan_header_for_target_structs(fp)
        if file_found:
            for s, f in file_found.items():
                if s in found:
                    print(f"  [SKIP] struct {s} already resolved from another file (duplicate in {fp})", file=sys.stderr)
                else:
                    found[s] = f
        else:
            print(f"  [SKIP] no target structs found in {fp}", file=sys.stderr)

    if not found:
        print("  [ERROR] no target structs found in any input file.", file=sys.stderr)
        return ""

    parts: list[str] = []
    for struct_name, table_name in TARGET_STRUCTS.items():
        fields = found.get(struct_name)
        if not fields:
            print(f"  [SKIP] struct {struct_name} not found in input files, skipping table.", file=sys.stderr)
            continue
        code = generate_field_table(struct_name, table_name, fields)
        parts.append(code)

    return "\n\n".join(parts)


# ── main ───────────────────────────────────────────────────────────────────

def main():
    if len(sys.argv) < 2 or sys.argv[1] in ("-h", "--help"):
        print(__doc__.strip())
        sys.exit(0 if len(sys.argv) < 2 else 1)

    code = generate_all(sys.argv[1:])
    if code:
        print(code)


if __name__ == "__main__":
    main()