#!/usr/bin/env python3
"""Generate Engine/Scripting/LuaDefs/sat.lua from // @lua metadata in binding sources."""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINDING_SOURCES = [
    ROOT / "Engine/Core/src/module/LuaRuntimeModules/LuaSceneBindings.cpp",
    ROOT / "Engine/Core/src/module/LuaRuntimeModules/LuaFieldBindings.cpp",
    ROOT / "Engine/Core/src/module/LuaRuntimeModules/LuaInputBindings.cpp",
    ROOT / "Engine/Core/src/module/LuaRuntimeModules/LuaTimeBindings.cpp",
    ROOT / "Engine/Core/src/module/LuaRuntimeModules/LuaMessageBindings.cpp",
    ROOT / "Engine/Core/src/module/LuaRuntimeModules/LuaMathBindings.cpp",
    ROOT / "Engine/Core/src/module/LuaRuntimeModules/LuaPhysicsBindings.cpp",
    ROOT / "Engine/Core/src/module/LuaRuntimeModules/LuaAudioBindings.cpp",
    ROOT / "Engine/Core/src/module/LuaRuntimeModules/LuaUiBindings.cpp",
    ROOT / "Engine/Core/src/module/LuaRuntimeModules/LuaLogBindings.cpp",
]
OUTPUT = ROOT / "Engine/Scripting/LuaDefs/sat.lua"

LUA_FUN_RE = re.compile(
    r"^\s*//\s*@lua\s+fun\s+(?P<path>sat(?:\.[A-Za-z_][A-Za-z0-9_]*)*)"
    r"\((?P<params>[^)]*)\)"
    r"(?:\s*:\s*(?P<returns>.+))?\s*$"
)
LUA_ENUM_RE = re.compile(
    r"^\s*//\s*@lua\s+enum\s+(?P<path>sat(?:\.[A-Za-z_][A-Za-z0-9_]*)+)\s*$"
)
REG_ENTRY_RE = re.compile(r'^\s*\{"([^"]+)",')
ENUM_NAME_RE = re.compile(r'^\s*\{"([^"]+)",')
NULL_SENTINEL_RE = re.compile(r"^\s*\{nullptr,\s*nullptr\}")


@dataclass
class Param:
    name: str
    type: str


@dataclass
class Return:
    type: str
    name: str | None = None


@dataclass
class Fun:
    path: str
    params: list[Param]
    returns: list[Return]
    source: Path
    line: int


@dataclass
class Enum:
    path: str
    fields: list[str] = field(default_factory=list)
    source: Path = Path()
    line: int = 0


def parse_params(text: str) -> list[Param]:
    text = text.strip()
    if not text:
        return []
    params: list[Param] = []
    for part in text.split(","):
        part = part.strip()
        if not part:
            continue
        if ":" not in part:
            raise ValueError(f"param missing type: {part!r}")
        name, typ = part.split(":", 1)
        params.append(Param(name=name.strip(), type=typ.strip()))
    return params


def parse_returns(text: str | None) -> list[Return]:
    if text is None:
        return []
    text = text.strip()
    if not text:
        return []
    returns: list[Return] = []
    for part in text.split(","):
        part = part.strip()
        if not part:
            continue
        if ":" in part:
            # Prefer named return "x: number" over union "EntityId|nil" which also has no colon.
            left, right = part.split(":", 1)
            left = left.strip()
            right = right.strip()
            # Union types use | without a name; named returns are simple identifiers.
            if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", left) and "|" not in left:
                returns.append(Return(type=right, name=left))
                continue
        returns.append(Return(type=part, name=None))
    return returns


def class_name_for(path: str) -> str:
    """sat.input -> SatInput; sat.Key -> SatKey; sat.MouseButton -> SatMouseButton."""
    parts = path.split(".")
    if len(parts) < 2 or parts[0] != "sat":
        raise ValueError(f"expected sat.* path, got {path!r}")
    leaf = parts[-1]
    return "Sat" + leaf[:1].upper() + leaf[1:]


def scrape_file(path: Path) -> tuple[list[Fun], list[Enum], list[str]]:
    lines = path.read_text(encoding="utf-8").splitlines()
    funs: list[Fun] = []
    enums: list[Enum] = []
    pending_fun: Fun | None = None
    pending_enum: Enum | None = None
    collecting_enum = False
    in_reg_table = False
    reg_names: list[str] = []
    annotated_leaves: set[str] = set()
    errors: list[str] = []

    def flush_reg_block() -> None:
        nonlocal in_reg_table, reg_names, annotated_leaves
        if in_reg_table:
            missing = [name for name in reg_names if name not in annotated_leaves]
            for name in missing:
                errors.append(f"{path}: luaL_Reg entry {name!r} has no matching // @lua fun")
        in_reg_table = False
        reg_names = []
        annotated_leaves = set()

    i = 0
    while i < len(lines):
        line = lines[i]
        fun_match = LUA_FUN_RE.match(line)
        enum_match = LUA_ENUM_RE.match(line)

        if fun_match:
            try:
                pending_fun = Fun(
                    path=fun_match.group("path"),
                    params=parse_params(fun_match.group("params") or ""),
                    returns=parse_returns(fun_match.group("returns")),
                    source=path,
                    line=i + 1,
                )
            except ValueError as exc:
                errors.append(f"{path}:{i + 1}: {exc}")
                pending_fun = None
            i += 1
            continue

        if enum_match:
            pending_enum = Enum(path=enum_match.group("path"), source=path, line=i + 1)
            collecting_enum = False
            i += 1
            continue

        if pending_fun is not None:
            reg = REG_ENTRY_RE.match(line)
            if reg:
                leaf = reg.group(1)
                expected = pending_fun.path.rsplit(".", 1)[-1]
                if leaf != expected:
                    errors.append(
                        f"{path}:{i + 1}: @lua fun path leaf {expected!r} "
                        f"does not match luaL_Reg name {leaf!r}"
                    )
                funs.append(pending_fun)
                annotated_leaves.add(leaf)
                pending_fun = None
                if not in_reg_table:
                    in_reg_table = True
                reg_names.append(leaf)
                i += 1
                continue
            if line.strip() and not line.strip().startswith("//"):
                errors.append(
                    f"{path}:{pending_fun.line}: @lua fun not followed by luaL_Reg entry"
                )
                pending_fun = None

        if pending_enum is not None and not collecting_enum:
            if "NamedCode" in line or line.strip().startswith("{"):
                collecting_enum = True
                if "};" in line:
                    name_match = ENUM_NAME_RE.match(line)
                    if name_match:
                        pending_enum.fields.append(name_match.group(1))
                    if not pending_enum.fields:
                        errors.append(f"{path}:{pending_enum.line}: @lua enum has no fields")
                    enums.append(pending_enum)
                    pending_enum = None
                    collecting_enum = False
                i += 1
                continue

        if pending_enum is not None and collecting_enum:
            name_match = ENUM_NAME_RE.match(line)
            if name_match:
                pending_enum.fields.append(name_match.group(1))
            if "};" in line:
                if not pending_enum.fields:
                    errors.append(f"{path}:{pending_enum.line}: @lua enum has no fields")
                enums.append(pending_enum)
                pending_enum = None
                collecting_enum = False
            i += 1
            continue

        if in_reg_table:
            reg = REG_ENTRY_RE.match(line)
            if reg:
                reg_names.append(reg.group(1))
            elif NULL_SENTINEL_RE.search(line):
                flush_reg_block()
            elif "};" in line:
                flush_reg_block()

        # Detect start of a luaL_Reg table without prior annotations on first entry.
        if not in_reg_table and "luaL_Reg" in line and "[" in line:
            in_reg_table = True
            reg_names = []
            annotated_leaves = set()

        i += 1

    if pending_fun is not None:
        errors.append(f"{path}:{pending_fun.line}: @lua fun not followed by luaL_Reg entry")
    if pending_enum is not None:
        errors.append(f"{path}:{pending_enum.line}: @lua enum not closed")
    flush_reg_block()
    return funs, enums, errors


def emit_param_annotations(params: list[Param]) -> list[str]:
    lines: list[str] = []
    for param in params:
        if param.name == "...":
            lines.append("---@param ... any")
        else:
            lines.append(f"---@param {param.name} {param.type}")
    return lines


def emit_return_annotations(returns: list[Return]) -> list[str]:
    lines: list[str] = []
    for ret in returns:
        if ret.name:
            lines.append(f"---@return {ret.type} {ret.name}")
        else:
            lines.append(f"---@return {ret.type}")
    return lines


def emit_function(owner: str, method: str, fun: Fun) -> list[str]:
    args = ", ".join(
        "..." if param.name == "..." else param.name for param in fun.params
    )
    lines = emit_param_annotations(fun.params)
    lines.extend(emit_return_annotations(fun.returns))
    lines.append(f"function {owner}.{method}({args}) end")
    lines.append("")
    return lines


def generate(funs: list[Fun], enums: list[Enum]) -> str:
    nested_methods: dict[str, list[Fun]] = {}
    root_methods: list[Fun] = []
    for fun in funs:
        parts = fun.path.split(".")
        if len(parts) == 2 and parts[0] == "sat":
            root_methods.append(fun)
        elif len(parts) == 3 and parts[0] == "sat":
            nested_methods.setdefault("sat." + parts[1], []).append(fun)
        else:
            raise ValueError(f"unsupported fun path depth: {fun.path}")

    out: list[str] = [
        "-- DO NOT EDIT. Generated by scripts/generate_lua_defs.py from // @lua metadata",
        "-- in Engine/Core/src/module/LuaRuntimeModules/Lua*Bindings.cpp.",
        "-- Editor-only EmmyLua stubs; not loaded by LuaRuntime.",
        "",
        "---@meta",
        "",
        "---@alias EntityId integer Opaque positive entity id from the scene.",
        "",
    ]

    # Nested classes (e.g. SatInput) before sat root.
    for nested_path in sorted(nested_methods.keys()):
        cls = class_name_for(nested_path)
        out.append(f"---@class {cls}")
        out.append(f"local {cls} = {{}}")
        out.append("")
        for fun in nested_methods[nested_path]:
            method = fun.path.split(".")[-1]
            out.extend(emit_function(cls, method, fun))

    for enum in enums:
        cls = class_name_for(enum.path)
        out.append(f"---@class {cls}")
        for name in enum.fields:
            out.append(f"---@field {name} integer")
        out.append("")

    out.append("---@class sat")
    for nested_path in sorted(nested_methods.keys()):
        leaf = nested_path.split(".")[-1]
        out.append(f"---@field {leaf} {class_name_for(nested_path)}")
    for enum in enums:
        leaf = enum.path.split(".")[-1]
        out.append(f"---@field {leaf} {class_name_for(enum.path)}")
    out.append("sat = {}")
    out.append("")

    for nested_path in sorted(nested_methods.keys()):
        leaf = nested_path.split(".")[-1]
        out.append(f"---@type {class_name_for(nested_path)}")
        out.append(f"sat.{leaf} = {{}}")
        out.append("")

    for enum in enums:
        leaf = enum.path.split(".")[-1]
        out.append(f"---@type {class_name_for(enum.path)}")
        out.append(f"sat.{leaf} = {{}}")
        out.append("")

    for fun in root_methods:
        method = fun.path.split(".")[-1]
        out.extend(emit_function("sat", method, fun))

    # Trim trailing blank lines to a single newline at EOF.
    while out and out[-1] == "":
        out.pop()
    out.append("")
    return "\n".join(out)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="exit non-zero if sat.lua would change (do not write)",
    )
    args = parser.parse_args()

    all_funs: list[Fun] = []
    all_enums: list[Enum] = []
    all_errors: list[str] = []
    for source in BINDING_SOURCES:
        if not source.is_file():
            all_errors.append(f"missing binding source: {source}")
            continue
        funs, enums, errors = scrape_file(source)
        all_funs.extend(funs)
        all_enums.extend(enums)
        all_errors.extend(errors)

    if all_errors:
        for err in all_errors:
            print(err, file=sys.stderr)
        return 1

    if not all_funs:
        print("no // @lua fun directives found", file=sys.stderr)
        return 1

    text = generate(all_funs, all_enums)
    if args.check:
        if not OUTPUT.is_file() or OUTPUT.read_text(encoding="utf-8") != text:
            print(f"{OUTPUT} is out of date; run scripts/generate_lua_defs.py", file=sys.stderr)
            return 1
        print(f"{OUTPUT} is up to date")
        return 0

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    if not OUTPUT.is_file() or OUTPUT.read_bytes() != text.encode("utf-8"):
        OUTPUT.write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote {OUTPUT.relative_to(ROOT)} ({len(all_funs)} funs, {len(all_enums)} enums)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
