#!/usr/bin/env python3
"""Add target, remaining time, and progress to the REC screen."""

from pathlib import Path
import re
import shutil

UI_C = Path("Core/Src/ui.c")
MARKER = "UI_RECORDING_PROGRESS_PATCH"


def fail(message):
    raise SystemExit(message)


def once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        fail(f"Expected exactly one {label}, found {count}")
    return text.replace(old, new, 1)


def function_range(text, signature):
    start = text.find(signature)
    if start < 0:
        fail("Missing function: " + signature.strip())
    brace = text.find("{", start)
    if brace < 0:
        fail("Missing opening brace: " + signature.strip())
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                end = index + 1
                while end < len(text) and text[end] in " \t\r\n":
                    end += 1
                return start, end
    fail("Unbalanced function: " + signature.strip())


if not UI_C.exists():
    fail(f"Missing required file: {UI_C}")

raw = UI_C.read_bytes()
if b"\x00" in raw:
    fail("Binary NUL byte found in Core/Src/ui.c")

text = raw.decode("utf-8")

if MARKER in text:
    fail("Recording progress patch is already installed")

if re.search(r"&(lt|gt|amp);|<br", text):
    fail("HTML artifact found in Core/Src/ui.c")

for prerequisite in (
    "UI_MODE_RECORD_DURATION",
    "requested_recording_seconds",
    "UI_GetRequestedRecordingSeconds",
    "UI_REC_SCREEN_FLICKER_POLISH",
    "static void ui_draw_recorder_body(void)",
):
    if prerequisite not in text:
        fail("Missing prerequisite: " + prerequisite)

backup = UI_C.with_name(UI_C.name + ".before_recording_progress")
if not backup.exists():
    shutil.copy2(UI_C, backup)

# Add a feature marker beside the duration-selector geometry marker.
text = once(
    text,
    "/* UI_WAV_RENAME_PATCH */\n",
    """/* UI_WAV_RENAME_PATCH */
/* UI_RECORDING_PROGRESS_PATCH */
""",
    "WAV rename feature marker",
)

start, end = function_range(text, "static void ui_draw_recorder_body(void)\n")
function = text[start:end]

# Add progress variables after elapsed_ms.
function = once(
    function,
    """    uint32_t elapsed_ms =
        ui.recorder_status.captured_samples / 48U;
""",
    """    uint32_t elapsed_ms =
        ui.recorder_status.captured_samples / 48U;
    uint32_t target_seconds = ui.requested_recording_seconds;
    uint32_t target_samples;
    uint32_t remaining_ms;
    uint32_t progress_percent;
    uint32_t progress_width;
""",
    "recorder elapsed-ms declaration",
)

# Calculate target, remaining, and progress before the state switch.
function = once(
    function,
    "    switch (ui.recorder_status.recorder_state)\n",
    """    if ((target_seconds != 5U) &&
        (target_seconds != 10U) &&
        (target_seconds != 30U) &&
        (target_seconds != 60U))
    {
        target_seconds = 5U;
    }

    target_samples = target_seconds * 48000U;

    if (ui.recorder_status.captured_samples >= target_samples)
    {
        remaining_ms = 0U;
        progress_percent = 100U;
    }
    else
    {
        uint32_t remaining_samples =
            target_samples - ui.recorder_status.captured_samples;

        remaining_ms = remaining_samples / 48U;
        progress_percent = (uint32_t)(
            ((uint64_t)ui.recorder_status.captured_samples * 100U) /
            target_samples);
    }

    progress_width = (progress_percent * 112U) / 100U;

    switch (ui.recorder_status.recorder_state)
""",
    "recorder state switch",
)

# Replace the right-side diagnostic block with duration/progress information.
old_right = """    Display_DrawString(188, 82, "SELECTED WAV", COLOR_WHITE, COLOR_BLACK);
    Display_DrawString(188, 102, "RMS", COLOR_WHITE, COLOR_BLACK);
    ui_draw_uint(230, 102,
                 ui.recorder_status.rms_counts,
                 COLOR_YELLOW);

    Display_DrawString(188, 122, "CLIP", COLOR_WHITE, COLOR_BLACK);
    Display_DrawString(230, 122,
                       ui.recorder_status.clipping ? "YES" : "NO",
                       ui.recorder_status.clipping ? COLOR_RED : COLOR_GREEN,
                       COLOR_BLACK);

    Display_DrawString(188, 142, "CLIP CNT", COLOR_WHITE, COLOR_BLACK);
    ui_draw_uint(248, 142,
                 ui.recorder_status.clip_count,
                 ui.recorder_status.clipping ? COLOR_RED : COLOR_YELLOW);


"""
new_right = """    Display_DrawString(188, 82, "TARGET", COLOR_WHITE, COLOR_BLACK);
    ui_draw_uint(246, 82, target_seconds, COLOR_CYAN);
    Display_DrawString(278, 82, "SEC", COLOR_GRAY, COLOR_BLACK);

    Display_DrawString(188, 102, "REMAIN", COLOR_WHITE, COLOR_BLACK);
    ui_draw_uint(246, 102, remaining_ms, COLOR_YELLOW);
    Display_DrawString(292, 102, "MS", COLOR_GRAY, COLOR_BLACK);

    Display_DrawString(188, 122, "PROGRESS", COLOR_WHITE, COLOR_BLACK);
    ui_draw_uint(260, 122, progress_percent, COLOR_GREEN);
    Display_DrawString(292, 122, "%", COLOR_GRAY, COLOR_BLACK);

    Display_DrawRect(188, 142, 116, 12, COLOR_WHITE);
    Display_FillRect(190, 144, 112, 8, COLOR_DARK_GRAY);
    if (progress_width != 0U)
    {
        Display_FillRect(190, 144, progress_width, 8, COLOR_GREEN);
    }

    Display_DrawString(188, 160, "RMS", COLOR_WHITE, COLOR_BLACK);
    ui_draw_uint(230, 160,
                 ui.recorder_status.rms_counts,
                 COLOR_YELLOW);
    Display_DrawString(266, 160,
                       ui.recorder_status.clipping ? "CLIP" : "OK",
                       ui.recorder_status.clipping ? COLOR_RED : COLOR_GREEN,
                       COLOR_BLACK);

"""
function = once(
    function,
    old_right,
    new_right,
    "recorder right-side diagnostic block",
)

text = text[:start] + function + text[end:]
text = "\n".join(line.rstrip() for line in text.splitlines()) + "\n"

if text.count("{") != text.count("}"):
    fail("Brace balance failed")
if text.count("/*") != text.count("*/"):
    fail("Comment balance failed")
if re.search(r"&(lt|gt|amp);|<br", text):
    fail("HTML artifact generated")
if b"\x00" in text.encode("utf-8"):
    fail("Binary NUL generated")

for symbol in (
    MARKER,
    "target_seconds",
    "remaining_ms",
    "progress_percent",
    '"PROGRESS"',
    "progress_width",
):
    if symbol not in text:
        fail("Missing generated symbol: " + symbol)

UI_C.write_text(text, encoding="utf-8")

print("REC-screen recording progress patch applied successfully.")
print("Modified: Core/Src/ui.c")
print("Added TARGET, REMAIN, percentage, progress bar, RMS, and clip status.")
print("Recorder DMA, queue, FatFs, and duration-selector logic were unchanged.")
print("Next: make clean && make -j4")
