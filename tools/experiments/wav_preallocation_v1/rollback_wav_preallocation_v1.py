#!/usr/bin/env python3
"""Roll back the experimental WAV preallocation v1 patch.

Restores the five automatic .before_wav_preallocation_v1 backups created by
patch_wav_preallocation_v1.py. The script verifies that the backups contain
all previously stable recorder/UI milestones and that preallocation markers
are absent before replacing active files.
"""

from pathlib import Path
import os
import re
import shutil
import tempfile

FILES = (
    Path("Core/Inc/recorder_framework.h"),
    Path("Core/Src/recorder_framework.c"),
    Path("Core/Inc/recording_catalog.h"),
    Path("Core/Src/recording_catalog.c"),
    Path("Core/Src/main.c"),
)

SUFFIX = ".before_wav_preallocation_v1"
PREALLOCATION_SYMBOLS = (
    "WAV_PREALLOCATION_V1_PATCH",
    "Recorder_StartFilePreallocated",
    "Recorder_AbortPreparedFile",
    "RecordingCatalog_StartNextForSamples",
    "Prealloc Bytes",
)
REQUIRED_STABLE_SYMBOLS = (
    "RECORDER_TWO_BLOCK_DRAIN",
    "UI_DELETE_CONFIRMATION_PATCH",
    "CONSOLE_WAV_RENAME_PATCH",
    "UI_WAV_RENAME_PATCH",
)


def fail(message):
    raise SystemExit(message)


def atomic_restore(source: Path, destination: Path):
    fd, temporary_name = tempfile.mkstemp(
        prefix=destination.name + ".rollback.",
        dir=str(destination.parent),
    )
    os.close(fd)
    temporary = Path(temporary_name)

    try:
        shutil.copy2(source, temporary)
        temporary.replace(destination)
    finally:
        if temporary.exists():
            temporary.unlink()


backups = []
backup_text = {}

for active in FILES:
    backup = active.with_name(active.name + SUFFIX)

    if not active.exists():
        fail(f"Missing active file: {active}")
    if not backup.exists():
        fail(f"Missing rollback backup: {backup}")

    data = backup.read_bytes()
    if b"\x00" in data:
        fail(f"Binary NUL found in rollback backup: {backup}")

    text = data.decode("utf-8")
    if re.search(r"&(lt|gt|amp);|<br", text):
        fail(f"HTML artifact found in rollback backup: {backup}")

    for symbol in PREALLOCATION_SYMBOLS:
        if symbol in text:
            fail(f"Preallocation symbol {symbol!r} found in backup: {backup}")

    backups.append((backup, active))
    backup_text[backup] = text

combined = "\n".join(backup_text.values())
for symbol in REQUIRED_STABLE_SYMBOLS:
    if symbol not in combined:
        fail(f"Stable milestone symbol missing from backups: {symbol}")

# Preserve the experimental active source before restoring it.
for active in FILES:
    experiment_copy = active.with_name(
        active.name + ".experimental_wav_preallocation_v1"
    )
    if not experiment_copy.exists():
        shutil.copy2(active, experiment_copy)

# Restore all five files only after all backups have passed validation.
for backup, active in backups:
    atomic_restore(backup, active)

# Post-restore validation.
restored_combined = ""
for active in FILES:
    data = active.read_bytes()
    if b"\x00" in data:
        fail(f"Binary NUL found after restore: {active}")

    text = data.decode("utf-8")
    restored_combined += text + "\n"

    if text.count("{") != text.count("}"):
        fail(f"Brace balance failed after restore: {active}")
    if text.count("/*") != text.count("*/"):
        fail(f"Comment balance failed after restore: {active}")

for symbol in PREALLOCATION_SYMBOLS:
    if symbol in restored_combined:
        fail(f"Preallocation symbol remains after restore: {symbol}")

for symbol in REQUIRED_STABLE_SYMBOLS:
    if symbol not in restored_combined:
        fail(f"Stable milestone missing after restore: {symbol}")

print("WAV preallocation v1 rollback completed successfully.")
print("Restored the five preallocation backups.")
print("Preserved experimental source as *.experimental_wav_preallocation_v1")
print("Stable queue drain, delete, console rename, and touchscreen rename remain.")
print("Next: make clean && make -j4")
