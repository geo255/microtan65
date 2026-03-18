#!/usr/bin/env python3
"""Two-pass 6502 assembler for microtan65.

The assembler reads opcode/mode information from cpu_6502.c so it stays aligned
with the emulator's implemented instruction set.
"""

from __future__ import annotations

import argparse
import ast
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Tuple


MODE_FROM_FUNCTION = {
    "implied": "impl",
    "immediate": "imm",
    "zero_page": "zp",
    "zero_page_x": "zpx",
    "zero_page_y": "zpy",
    "absolute": "abs",
    "absolute_x": "absx",
    "absolute_y": "absy",
    "indirect": "ind",
    "indirect_x": "indx",
    "indirect_y": "indy",
    "relative": "rel",
    "indirect_zero_page": "zpi",
    "indirect_absolute_x": "absindx",
}

MODE_SIZE = {
    "impl": 1,
    "acc": 1,
    "imm": 2,
    "zp": 2,
    "zpx": 2,
    "zpy": 2,
    "abs": 3,
    "absx": 3,
    "absy": 3,
    "ind": 3,
    "indx": 2,
    "indy": 2,
    "rel": 2,
    "zpi": 2,
    "absindx": 3,
}

ACCUMULATOR_ALIASES = {
    "asla": "ASL",
    "rola": "ROL",
    "lsra": "LSR",
    "rora": "ROR",
}

BRANCH_MNEMONICS = {"BCC", "BCS", "BEQ", "BMI", "BNE", "BPL", "BRA", "BVC", "BVS"}


a = re.ASCII
LABEL_RE = re.compile(r"^([A-Za-z_@\.][A-Za-z0-9_@\.]*)\s*:\s*(.*)$", a)
ASSIGN_RE = re.compile(r"^([A-Za-z_\.][A-Za-z0-9_\.]*)\s*=\s*(.+)$", a)
TOKEN_RE = re.compile(r"^([A-Za-z\.][A-Za-z0-9_\.]*)\s*(.*)$", a)
OPCODE_LINE_RE = re.compile(
    r"\{\s*\d+\s*,\s*([a-z0-9_]+)\s*,\s*([a-z0-9_]+)\s*\},\s*//\s*0x([0-9A-Fa-f]{2})"
)
LOCAL_REF_RE = re.compile(r"(?<![A-Za-z0-9_])@([A-Za-z_][A-Za-z0-9_]*)")


class AssemblerError(Exception):
    pass


@dataclass(frozen=True)
class SourceLine:
    text: str
    file_path: Path
    line_no: int


@dataclass
class Op:
    kind: str
    line_no: int
    address: int
    size: int
    mnemonic: Optional[str] = None
    mode: Optional[str] = None
    operand: Optional[str] = None
    directive: Optional[str] = None
    args: Optional[List[str]] = None
    source: Optional[str] = None

@dataclass
class ConditionalFrame:
    parent_active: bool
    this_active: bool
    branch_taken: bool
    seen_else: bool = False


def is_currently_active(stack: List[ConditionalFrame]) -> bool:
    return stack[-1].this_active if stack else True

class SymbolLookup(dict):
    def __init__(self, symbols: Dict[str, int], allow_undefined: bool):
        super().__init__(symbols)
        self.allow_undefined = allow_undefined
        self.missing_symbol = False

    def __missing__(self, key: str) -> int:
        folded = key.upper()
        if folded in self:
            return super().__getitem__(folded)
        self.missing_symbol = True
        if self.allow_undefined:
            return 0
        raise AssemblerError(f"Undefined symbol: {key}")


def format_location(file_path: Path, line_no: int) -> str:
    return f"{file_path}:{line_no}"


def parse_include_path(arg_text: str, file_path: Path, line_no: int) -> Path:
    location = format_location(file_path, line_no)
    text = arg_text.strip()
    if not text:
        raise AssemblerError(f"{location}: .include requires a quoted path")

    try:
        include_name = ast.literal_eval(text)
    except Exception as exc:
        raise AssemblerError(f"{location}: invalid .include path {arg_text!r}") from exc

    if not isinstance(include_name, str) or not include_name:
        raise AssemblerError(f"{location}: .include path must be a non-empty string")

    include_path = Path(include_name)
    if not include_path.is_absolute():
        include_path = (file_path.parent / include_path).resolve()
    else:
        include_path = include_path.resolve()

    if not include_path.is_file():
        raise AssemblerError(f"{location}: include file not found: {include_path}")

    return include_path


def load_source_lines(input_path: Path, include_stack: Optional[List[Path]] = None) -> List[SourceLine]:
    resolved = input_path.resolve()
    stack = include_stack or []

    if resolved in stack:
        cycle = " -> ".join(str(p) for p in stack + [resolved])
        raise AssemblerError(f"Include cycle detected: {cycle}")

    try:
        text = resolved.read_text(encoding="utf-8")
    except OSError as exc:
        raise AssemblerError(f"Unable to read source file: {resolved}") from exc

    lines: List[SourceLine] = []
    for line_no, raw in enumerate(text.splitlines(), 1):
        code = raw.split(";", 1)[0].strip()
        if code:
            tm = TOKEN_RE.match(code)
            if tm and tm.group(1).lower() == ".include":
                remainder = tm.group(2).strip() if tm.group(2) else ""
                include_path = parse_include_path(remainder, resolved, line_no)
                lines.extend(load_source_lines(include_path, stack + [resolved]))
                continue

        lines.append(SourceLine(text=raw, file_path=resolved, line_no=line_no))

    return lines

def sanitize_scope_name(name: str) -> str:
    upper = name.upper()
    return re.sub(r"[^A-Z0-9_]", "_", upper)


def mangle_local_label(local_name: str, global_scope: str) -> str:
    scope = sanitize_scope_name(global_scope)
    return f"__LOCAL_{scope}_{local_name.upper()}"


def resolve_local_label_name(label: str, global_scope: Optional[str], location: str) -> str:
    if not label.startswith("@"):
        return label.upper()

    local_name = label[1:]
    if not re.match(r"^[A-Za-z_][A-Za-z0-9_]*$", local_name, a):
        raise AssemblerError(f"{location}: invalid local label {label}")

    if not global_scope:
        raise AssemblerError(f"{location}: local label {label} has no enclosing global label")

    return mangle_local_label(local_name, global_scope)


def resolve_local_refs(expr: str, global_scope: Optional[str], location: str) -> str:
    if "@" not in expr:
        return expr

    def repl(m: re.Match[str]) -> str:
        local_name = m.group(1)
        if not global_scope:
            raise AssemblerError(
                f"{location}: local label reference @{local_name} has no enclosing global label"
            )
        return mangle_local_label(local_name, global_scope)

    return LOCAL_REF_RE.sub(repl, expr)


def parse_opcode_table(cpu_source: Path) -> Dict[str, Dict[str, int]]:
    text = cpu_source.read_text(encoding="utf-8")
    table: Dict[str, Dict[str, int]] = {}

    for m in OPCODE_LINE_RE.finditer(text):
        inst_fn = m.group(1)
        mode_fn = m.group(2)
        opcode = int(m.group(3), 16)

        if mode_fn not in MODE_FROM_FUNCTION:
            continue

        if inst_fn == "brk" and opcode != 0x00:
            continue

        if inst_fn in ACCUMULATOR_ALIASES:
            mnemonic = ACCUMULATOR_ALIASES[inst_fn]
            mode = "acc"
        else:
            mnemonic = inst_fn.upper()
            mode = MODE_FROM_FUNCTION[mode_fn]

        table.setdefault(mnemonic, {})[mode] = opcode

    # 65C02 aliases often written as INC A / DEC A.
    if "INA" in table and "impl" in table["INA"]:
        table.setdefault("INC", {})["acc"] = table["INA"]["impl"]
    if "DEA" in table and "impl" in table["DEA"]:
        table.setdefault("DEC", {})["acc"] = table["DEA"]["impl"]

    if not table:
        raise AssemblerError("Failed to parse opcode table from cpu_6502.c")

    return table


def split_csv(text: str) -> List[str]:
    out: List[str] = []
    current: List[str] = []
    depth = 0
    quote: Optional[str] = None
    escape = False

    for ch in text:
        if quote:
            current.append(ch)
            if escape:
                escape = False
                continue
            if ch == "\\":
                escape = True
            elif ch == quote:
                quote = None
            continue

        if ch in ("'", '"'):
            quote = ch
            current.append(ch)
            continue

        if ch == "(":
            depth += 1
            current.append(ch)
            continue
        if ch == ")":
            if depth > 0:
                depth -= 1
            current.append(ch)
            continue

        if ch == "," and depth == 0:
            piece = "".join(current).strip()
            if piece:
                out.append(piece)
            current = []
            continue

        current.append(ch)

    piece = "".join(current).strip()
    if piece:
        out.append(piece)

    return out


def normalize_expression(expr: str) -> str:
    expr = expr.strip()
    expr = re.sub(r"\$([0-9A-Fa-f]+)", r"0x\1", expr)
    expr = re.sub(r"%([01_]+)", lambda m: "0b" + m.group(1).replace("_", ""), expr)
    # Assembly arithmetic uses integer division semantics.
    expr = re.sub(r"(?<!/)/(?!/)", "//", expr)

    def char_repl(m: re.Match[str]) -> str:
        literal = m.group(0)
        try:
            return str(ord(ast.literal_eval(literal)))
        except Exception as exc:  # pragma: no cover - defensive
            raise AssemblerError(f"Invalid character literal: {literal}") from exc

    expr = re.sub(r"'([^'\\]|\\.)'", char_repl, expr)
    return expr


def eval_expr(expr: str, symbols: Dict[str, int], allow_undefined: bool) -> Tuple[int, bool]:
    normalized = normalize_expression(expr)
    lookup = SymbolLookup(symbols, allow_undefined)
    try:
        value = eval(normalized, {"__builtins__": {}}, lookup)
    except AssemblerError:
        raise
    except Exception as exc:
        raise AssemblerError(f"Invalid expression: {expr}") from exc

    if not isinstance(value, int):
        raise AssemblerError(f"Expression did not evaluate to integer: {expr}")

    return int(value), lookup.missing_symbol

def parse_operand_shape(operand: Optional[str]) -> Tuple[str, Optional[str]]:
    if operand is None:
        return "none", None

    text = operand.strip()
    if not text:
        return "none", None

    if text.upper() == "A":
        return "acc", None

    if text.startswith("#"):
        return "imm", text[1:].strip()

    m = re.match(r"^\(\s*(.+)\s*,\s*[Xx]\s*\)$", text)
    if m:
        return "paren_x", m.group(1).strip()

    m = re.match(r"^\(\s*(.+)\s*\)\s*,\s*[Yy]\s*$", text)
    if m:
        return "indy", m.group(1).strip()

    m = re.match(r"^\(\s*(.+)\s*\)$", text)
    if m:
        return "paren", m.group(1).strip()

    m = re.match(r"^(.+)\s*,\s*[Xx]\s*$", text)
    if m:
        return "x", m.group(1).strip()

    m = re.match(r"^(.+)\s*,\s*[Yy]\s*$", text)
    if m:
        return "y", m.group(1).strip()

    return "plain", text


def choose_mode(
    mnemonic: str,
    operand: Optional[str],
    opcode_map: Dict[str, Dict[str, int]],
    symbols: Dict[str, int],
    allow_undefined: bool,
) -> Tuple[str, Optional[str]]:
    if mnemonic not in opcode_map:
        raise AssemblerError(f"Unknown instruction: {mnemonic}")

    available = opcode_map[mnemonic]
    shape, expr = parse_operand_shape(operand)

    def eval_for_size(expression: str) -> Tuple[int, bool]:
        return eval_expr(expression, symbols, allow_undefined)

    if shape == "none":
        if "impl" in available:
            return "impl", None
        if "acc" in available:
            return "acc", None
        raise AssemblerError(f"Instruction requires an operand: {mnemonic}")

    if shape == "acc":
        if "acc" not in available:
            raise AssemblerError(f"Accumulator mode not supported for {mnemonic}")
        return "acc", None

    if shape == "imm":
        if "imm" not in available:
            raise AssemblerError(f"Immediate mode not supported for {mnemonic}")
        return "imm", expr

    if shape == "paren_x":
        value, unresolved = eval_for_size(expr)
        if "indx" in available and not unresolved and 0 <= value <= 0xFF:
            return "indx", expr
        if "absindx" in available:
            return "absindx", expr
        if "indx" in available:
            return "indx", expr
        raise AssemblerError(f"(expr,X) mode not supported for {mnemonic}")

    if shape == "indy":
        if "indy" not in available:
            raise AssemblerError(f"(expr),Y mode not supported for {mnemonic}")
        return "indy", expr

    if shape == "paren":
        value, unresolved = eval_for_size(expr)
        if "zpi" in available and not unresolved and 0 <= value <= 0xFF:
            return "zpi", expr
        if "ind" in available:
            return "ind", expr
        if "zpi" in available:
            return "zpi", expr
        raise AssemblerError(f"Indirect mode not supported for {mnemonic}")

    if shape == "x":
        value, unresolved = eval_for_size(expr)
        if "zpx" in available and not unresolved and 0 <= value <= 0xFF:
            return "zpx", expr
        if "absx" in available:
            return "absx", expr
        if "zpx" in available:
            return "zpx", expr
        raise AssemblerError(f"Indexed-X mode not supported for {mnemonic}")

    if shape == "y":
        value, unresolved = eval_for_size(expr)
        if "zpy" in available and not unresolved and 0 <= value <= 0xFF:
            return "zpy", expr
        if "absy" in available:
            return "absy", expr
        if "zpy" in available:
            return "zpy", expr
        raise AssemblerError(f"Indexed-Y mode not supported for {mnemonic}")

    # plain
    if mnemonic in BRANCH_MNEMONICS and "rel" in available:
        return "rel", expr

    value, unresolved = eval_for_size(expr)
    if "zp" in available and not unresolved and 0 <= value <= 0xFF:
        return "zp", expr
    if "abs" in available:
        return "abs", expr
    if "zp" in available:
        return "zp", expr
    if "rel" in available:
        return "rel", expr

    raise AssemblerError(f"No valid addressing mode for {mnemonic} {operand}")


def parse_string_literal(token: str) -> bytes:
    try:
        value = ast.literal_eval(token)
    except Exception as exc:
        raise AssemblerError(f"Invalid string literal: {token}") from exc

    if isinstance(value, bytes):
        return value
    if isinstance(value, str):
        return value.encode("latin1", errors="strict")
    raise AssemblerError(f"Invalid string literal: {token}")


def pass1(lines: List[SourceLine], opcode_map: Dict[str, Dict[str, int]]) -> Tuple[List[Op], Dict[str, int]]:
    symbols: Dict[str, int] = {}
    operations: List[Op] = []
    pc = 0
    global_scope: Optional[str] = None
    cond_stack: List[ConditionalFrame] = []

    for source in lines:
        line_no = source.line_no
        location = format_location(source.file_path, line_no)
        code = source.text.split(";", 1)[0].strip()
        if not code:
            continue

        # Handle conditional-assembly directives regardless of current active state.
        lead = TOKEN_RE.match(code)
        if lead and lead.group(1).startswith("."):
            directive = lead.group(1).lower()
            remainder = lead.group(2).strip() if lead.group(2) else ""

            if directive == ".if":
                if not remainder:
                    raise AssemblerError(f"{location}: .if requires an expression")
                parent_active = is_currently_active(cond_stack)
                cond = False
                if parent_active:
                    expr = resolve_local_refs(remainder, global_scope, location)
                    cond_value, _ = eval_expr(expr, symbols, allow_undefined=False)
                    cond = cond_value != 0
                this_active = parent_active and cond
                cond_stack.append(
                    ConditionalFrame(
                        parent_active=parent_active,
                        this_active=this_active,
                        branch_taken=this_active,
                        seen_else=False,
                    )
                )
                continue

            if directive in {".ifdef", ".ifndef"}:
                if not remainder:
                    raise AssemblerError(f"{location}: {directive} requires a symbol name")
                parent_active = is_currently_active(cond_stack)
                cond = False
                if parent_active:
                    symbol_name = resolve_local_refs(remainder.strip(), global_scope, location).upper()
                    if not re.match(r"^[A-Za-z_\.][A-Za-z0-9_\.]*$", symbol_name, a):
                        raise AssemblerError(f"{location}: invalid symbol name {symbol_name}")
                    defined = symbol_name in symbols
                    cond = defined if directive == ".ifdef" else (not defined)
                this_active = parent_active and cond
                cond_stack.append(
                    ConditionalFrame(
                        parent_active=parent_active,
                        this_active=this_active,
                        branch_taken=this_active,
                        seen_else=False,
                    )
                )
                continue

            if directive in {".elif", ".elseif"}:
                if not cond_stack:
                    raise AssemblerError(f"{location}: {directive} without matching .if")
                if not remainder:
                    raise AssemblerError(f"{location}: {directive} requires an expression")

                frame = cond_stack[-1]
                if frame.seen_else:
                    raise AssemblerError(f"{location}: {directive} after .else")

                if not frame.parent_active or frame.branch_taken:
                    frame.this_active = False
                else:
                    expr = resolve_local_refs(remainder, global_scope, location)
                    cond_value, _ = eval_expr(expr, symbols, allow_undefined=False)
                    cond = cond_value != 0
                    frame.this_active = cond
                    if cond:
                        frame.branch_taken = True

                cond_stack[-1] = frame
                continue

            if directive == ".else":
                if not cond_stack:
                    raise AssemblerError(f"{location}: .else without matching .if")

                frame = cond_stack[-1]
                if frame.seen_else:
                    raise AssemblerError(f"{location}: duplicate .else in conditional block")

                frame.seen_else = True
                frame.this_active = frame.parent_active and (not frame.branch_taken)
                frame.branch_taken = frame.branch_taken or frame.this_active
                cond_stack[-1] = frame
                continue

            if directive == ".endif":
                if not cond_stack:
                    raise AssemblerError(f"{location}: .endif without matching .if")
                cond_stack.pop()
                continue

        # Skip non-conditional content in inactive conditional branches.
        if not is_currently_active(cond_stack):
            continue

        while True:
            lm = LABEL_RE.match(code)
            if not lm:
                break
            raw_label = lm.group(1)
            label = resolve_local_label_name(raw_label, global_scope, location)
            if label in symbols and symbols[label] != pc:
                raise AssemblerError(f"{location}: duplicate label {raw_label.upper()}")
            symbols[label] = pc
            if not raw_label.startswith("@"):
                global_scope = raw_label.upper()
            code = lm.group(2).strip()
            if not code:
                break
        if not code:
            continue

        am = ASSIGN_RE.match(code)
        if am:
            name = am.group(1).upper()
            expr = resolve_local_refs(am.group(2).strip(), global_scope, location)
            value, _ = eval_expr(expr, symbols, allow_undefined=True)
            symbols[name] = value & 0xFFFF
            continue

        tm = TOKEN_RE.match(code)
        if not tm:
            raise AssemblerError(f"{location}: cannot parse line")

        token = tm.group(1)
        remainder = tm.group(2).strip() if tm.group(2) else ""

        if token.startswith("."):
            directive = token.lower()
            if directive == ".org":
                if not remainder:
                    raise AssemblerError(f"{location}: .org requires an expression")
                org_expr = resolve_local_refs(remainder, global_scope, location)
                value, _ = eval_expr(org_expr, symbols, allow_undefined=True)
                if not (0 <= value <= 0xFFFF):
                    raise AssemblerError(f"{location}: .org out of range")
                pc = value
                continue

            if directive == ".equ":
                parts = split_csv(remainder)
                if len(parts) != 2:
                    raise AssemblerError(f"{location}: .equ requires name, expression")
                name = parts[0].strip().upper()
                if not re.match(r"^[A-Za-z_\.][A-Za-z0-9_\.]*$", name, a):
                    raise AssemblerError(f"{location}: invalid symbol name {name}")
                expr = resolve_local_refs(parts[1], global_scope, location)
                value, _ = eval_expr(expr, symbols, allow_undefined=True)
                symbols[name] = value & 0xFFFF
                continue

            if directive in {".byte", ".word", ".text", ".ascii", ".fill"}:
                args_raw = split_csv(remainder)
                args: List[str] = []
                for part in args_raw:
                    stripped = part.strip()
                    if stripped.startswith(('"', "'")):
                        args.append(stripped)
                    else:
                        args.append(resolve_local_refs(stripped, global_scope, location))

                if directive == ".fill" and len(args) not in {1, 2}:
                    raise AssemblerError(f"{location}: .fill requires count[, value]")
                if directive in {".byte", ".word"} and not args:
                    raise AssemblerError(f"{location}: {directive} requires data")
                if directive in {".text", ".ascii"} and not args:
                    raise AssemblerError(f"{location}: {directive} requires data")

                size = 0
                if directive == ".byte":
                    for part in args:
                        if part.startswith(('"', "'")):
                            size += len(parse_string_literal(part))
                        else:
                            size += 1
                elif directive == ".word":
                    size = 2 * len(args)
                elif directive in {".text", ".ascii"}:
                    for part in args:
                        if part.startswith(('"', "'")):
                            size += len(parse_string_literal(part))
                        else:
                            size += 1
                elif directive == ".fill":
                    count, _ = eval_expr(args[0], symbols, allow_undefined=True)
                    if count < 0:
                        raise AssemblerError(f"{location}: .fill count must be >= 0")
                    size = count

                operations.append(
                    Op(
                        kind="directive",
                        line_no=line_no,
                        address=pc,
                        size=size,
                        directive=directive,
                        args=args,
                        source=str(source.file_path),
                    )
                )
                pc += size
                continue

            raise AssemblerError(f"{location}: unknown directive {directive}")

        mnemonic = token.upper()
        resolved_operand = resolve_local_refs(remainder, global_scope, location) if remainder else None
        mode, operand_expr = choose_mode(mnemonic, resolved_operand, opcode_map, symbols, allow_undefined=True)
        size = MODE_SIZE[mode]
        operations.append(
            Op(
                kind="instruction",
                line_no=line_no,
                address=pc,
                size=size,
                mnemonic=mnemonic,
                mode=mode,
                operand=operand_expr,
                source=str(source.file_path),
            )
        )
        pc += size

    if cond_stack:
        raise AssemblerError("Unterminated conditional block: missing .endif")

    return operations, symbols

def encode_word(value: int) -> Tuple[int, int]:
    return value & 0xFF, (value >> 8) & 0xFF


def op_location(op: Op) -> str:
    if op.source:
        return f"{op.source}:{op.line_no}"
    return f"Line {op.line_no}"


def encode_instruction(op: Op, symbols: Dict[str, int], opcode_map: Dict[str, Dict[str, int]]) -> List[int]:
    assert op.mnemonic is not None
    assert op.mode is not None
    opcode = opcode_map[op.mnemonic][op.mode]
    out = [opcode]

    if op.mode in {"impl", "acc"}:
        return out

    if op.operand is None:
        raise AssemblerError(f"{op_location(op)}: missing operand")

    value, _ = eval_expr(op.operand, symbols, allow_undefined=False)

    if op.mode == "rel":
        offset = value - (op.address + 2)
        if not (-128 <= offset <= 127):
            raise AssemblerError(f"{op_location(op)}: branch target out of range ({offset})")
        out.append(offset & 0xFF)
        return out

    if op.mode in {"imm", "zp", "zpx", "zpy", "indx", "indy", "zpi"}:
        if not (-128 <= value <= 0xFF):
            raise AssemblerError(f"{op_location(op)}: value out of byte range: {value}")
        out.append(value & 0xFF)
        return out

    if op.mode in {"abs", "absx", "absy", "ind", "absindx"}:
        if not (-32768 <= value <= 0xFFFF):
            raise AssemblerError(f"{op_location(op)}: value out of word range: {value}")
        lo, hi = encode_word(value)
        out.extend([lo, hi])
        return out

    raise AssemblerError(f"{op_location(op)}: unsupported mode {op.mode}")


def assemble_pass2(operations: List[Op], symbols: Dict[str, int], opcode_map: Dict[str, Dict[str, int]]) -> Dict[int, int]:
    memory: Dict[int, int] = {}

    for op in operations:
        if op.kind == "instruction":
            encoded = encode_instruction(op, symbols, opcode_map)
            for i, b in enumerate(encoded):
                memory[op.address + i] = b
            continue

        if op.kind != "directive" or op.directive is None:
            continue

        args = op.args or []
        addr = op.address

        if op.directive == ".byte":
            for part in args:
                stripped = part.strip()
                if stripped.startswith(('"', "'")):
                    data = parse_string_literal(stripped)
                    for b in data:
                        memory[addr] = b
                        addr += 1
                else:
                    value, _ = eval_expr(stripped, symbols, allow_undefined=False)
                    if not (0 <= value <= 0xFF):
                        raise AssemblerError(f"{op_location(op)}: .byte value out of range")
                    memory[addr] = value & 0xFF
                    addr += 1
            continue

        if op.directive == ".word":
            for part in args:
                value, _ = eval_expr(part, symbols, allow_undefined=False)
                if not (0 <= value <= 0xFFFF):
                    raise AssemblerError(f"{op_location(op)}: .word value out of range")
                lo, hi = encode_word(value)
                memory[addr] = lo
                memory[addr + 1] = hi
                addr += 2
            continue

        if op.directive in {".text", ".ascii"}:
            for part in args:
                stripped = part.strip()
                if stripped.startswith(('"', "'")):
                    data = parse_string_literal(stripped)
                    for b in data:
                        memory[addr] = b
                        addr += 1
                else:
                    value, _ = eval_expr(stripped, symbols, allow_undefined=False)
                    if not (0 <= value <= 0xFF):
                        raise AssemblerError(f"{op_location(op)}: text data value out of range")
                    memory[addr] = value & 0xFF
                    addr += 1
            continue

        if op.directive == ".fill":
            count, _ = eval_expr(args[0], symbols, allow_undefined=False)
            fill_value = 0
            if len(args) == 2:
                fill_value, _ = eval_expr(args[1], symbols, allow_undefined=False)
            if count < 0:
                raise AssemblerError(f"{op_location(op)}: .fill count must be >= 0")
            if not (0 <= fill_value <= 0xFF):
                raise AssemblerError(f"{op_location(op)}: .fill value out of range")
            for _ in range(count):
                memory[addr] = fill_value
                addr += 1
            continue

    return memory


def hex_checksum(record: List[int]) -> int:
    return ((~(sum(record) & 0xFF) + 1) & 0xFF)


def write_intel_hex(memory: Dict[int, int], output_path: Path) -> None:
    addresses = sorted(memory.keys())
    lines: List[str] = []
    current_upper: Optional[int] = None
    idx = 0

    while idx < len(addresses):
        start = addresses[idx]
        chunk = [memory[start]]
        idx += 1
        next_addr = start + 1

        while idx < len(addresses) and addresses[idx] == next_addr and len(chunk) < 16:
            chunk.append(memory[addresses[idx]])
            idx += 1
            next_addr += 1

        upper = (start >> 16) & 0xFFFF
        low_addr = start & 0xFFFF

        if current_upper != upper:
            rec = [2, 0x00, 0x00, 0x04, (upper >> 8) & 0xFF, upper & 0xFF]
            lines.append(":" + "".join(f"{b:02X}" for b in rec + [hex_checksum(rec)]))
            current_upper = upper

        rec = [len(chunk), (low_addr >> 8) & 0xFF, low_addr & 0xFF, 0x00] + chunk
        lines.append(":" + "".join(f"{b:02X}" for b in rec + [hex_checksum(rec)]))

    lines.append(":00000001FF")
    output_path.write_text("\n".join(lines) + "\n", encoding="ascii")


def write_binary(memory: Dict[int, int], output_path: Path) -> None:
    if not memory:
        output_path.write_bytes(b"")
        return

    start = min(memory.keys())
    end = max(memory.keys())
    data = bytearray(end - start + 1)
    for addr, value in memory.items():
        data[addr - start] = value & 0xFF
    output_path.write_bytes(bytes(data))


def default_output_path(input_path: Path, fmt: str) -> Path:
    suffix = ".hex" if fmt == "hex" else ".bin"
    return input_path.with_suffix(suffix)


def run(input_path: Path, output_path: Path, fmt: str) -> None:
    repo_root = Path(__file__).resolve().parent.parent
    cpu_source = repo_root / "src" / "cpu_6502.c"
    if not cpu_source.exists():
        cpu_source = repo_root / "cpu_6502.c"
    opcode_map = parse_opcode_table(cpu_source)
    source_lines = load_source_lines(input_path)
    operations, symbols = pass1(source_lines, opcode_map)
    memory = assemble_pass2(operations, symbols, opcode_map)

    if fmt == "hex":
        write_intel_hex(memory, output_path)
    elif fmt == "bin":
        write_binary(memory, output_path)
    else:  # pragma: no cover - argparse prevents this
        raise AssemblerError(f"Unsupported format: {fmt}")


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="2-pass 6502 assembler for microtan65")
    parser.add_argument("input", type=Path, help="Input assembly file")
    parser.add_argument("-o", "--output", type=Path, help="Output file path")
    parser.add_argument("-f", "--format", choices=["hex", "bin"], default="hex", help="Output format (default: hex)")
    args = parser.parse_args(argv)

    input_path = args.input
    if not input_path.is_file():
        print(f"Input file not found: {input_path}", file=sys.stderr)
        return 2

    output_path = args.output or default_output_path(input_path, args.format)

    try:
        run(input_path, output_path, args.format)
    except AssemblerError as exc:
        print(f"Assembler error: {exc}", file=sys.stderr)
        return 1

    print(f"Assembled {input_path} -> {output_path} ({args.format})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
