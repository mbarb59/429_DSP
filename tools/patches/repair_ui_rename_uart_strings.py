#!/usr/bin/env python3
"""Repair newline-corrupted UART string literals in Core/Src/ui.c."""

from pathlib import Path
import re
import shutil

path = Path("Core/Src/ui.c")
backup = Path("Core/Src/ui.c.before_rename_uart_string_repair")

if not path.exists():
    raise SystemExit(f"Missing {path}")

raw = path.read_bytes()
if b"\x00" in raw:
    raise SystemExit("Refusing repair: Core/Src/ui.c contains a binary NUL byte")

text = raw.decode("utf-8")

if not backup.exists():
    shutil.copy2(path, backup)

messages = (
    "UI RENAME CANCELLED",
    "UI RENAME CLEAR",
    "UI RENAME BKSP",
    "UI RENAME SAVE REQUEST",
    "UI RENAME REQUEST",
    "UI RENAME KEY",
)

repaired = 0

for message in messages:
    # Match a C string where the intended CR/LF became literal whitespace
    # before the closing quote in the generated source.
    pattern = re.compile(
        r'UART_SendString\("' + re.escape(message) + r'[\r\n]+"\);'
    )
    text, count = pattern.subn(
        'UART_SendString("' + message + r'\r\n");',
        text,
        count=1,
    )
    if count != 1:
        raise SystemExit(
            f"Expected one broken UART string for {message!r}, found {count}"
        )
    repaired += count

# Sanity checks before writing.
if repaired != len(messages):
    raise SystemExit(f"Expected {len(messages)} repairs, made {repaired}")
if text.count("{") != text.count("}"):
    raise SystemExit("Brace balance failed after repair")
if text.count("/*") != text.count("*/"):
    raise SystemExit("Comment balance failed after repair")
if b"\x00" in text.encode("utf-8"):
    raise SystemExit("Repair generated a binary NUL byte")

for message in messages:
    expected = 'UART_SendString("' + message + r'\r\n");'
    if text.count(expected) != 1:
        raise SystemExit(f"Repaired string missing or duplicated: {message}")

path.write_text(text, encoding="utf-8")

print("Rename UART string repair applied successfully.")
print(f"Repaired strings: {repaired}")
print(f"Backup: {backup}")
print("Next: make clean && make -j4")
