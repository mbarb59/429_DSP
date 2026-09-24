#!/usr/bin/env python3
"""Make the recorder FILES button larger and tolerant of touch drift."""

from pathlib import Path
import re
import shutil

PATH = Path("Core/Src/ui.c")
BACKUP = Path("Core/Src/ui.c.before_files_touch_fix")
MARKER = "UI_FILES_TOUCH_FIX"


def fail(message):
    raise SystemExit(message)


def once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        fail(f"Expected exactly one {label}, found {count}")
    return text.replace(old, new, 1)


if not PATH.exists():
    fail(f"Missing required file: {PATH}")

raw = PATH.read_bytes()
if bytes([0]) in raw:
    fail("NUL byte found in Core/Src/ui.c")

text = raw.decode("utf-8")

if MARKER in text:
    fail("FILES touch fix is already installed")

for prerequisite in (
    "UI_WAV_BROWSER_V2_PATCH",
    "UI_TOUCH_STABILITY_PATCH",
    "UI_ACTION_BROWSER_OPEN",
):
    if prerequisite not in text:
        fail("Missing prerequisite: " + prerequisite)

if re.search(r"&(lt|gt|amp);|<br", text):
    fail("HTML artifact found in Core/Src/ui.c")

if not BACKUP.exists():
    shutil.copy2(PATH, BACKUP)

text = once(
    text,
    """#define UI_FILES_X                 188U
#define UI_FILES_Y                 166U
#define UI_FILES_WIDTH             120U
#define UI_FILES_HEIGHT            35U
""",
    """#define UI_FILES_X                 174U
#define UI_FILES_Y                 158U
#define UI_FILES_WIDTH             140U
#define UI_FILES_HEIGHT            48U
#define UI_FILES_RELEASE_MARGIN    12U
""",
    "FILES geometry constants",
)

text = once(
    text,
    '    Display_DrawString(226, 180, "FILES", COLOR_BLACK, COLOR_BLUE);\n',
    '    Display_DrawString(218, 178, "FILES", COLOR_BLACK, COLOR_BLUE);\n',
    "FILES label position",
)

old = """    if (!ui.dragging &&
        (ui.mode == UI_MODE_RECORDER) &&
        (ui.touch_start_x >= UI_FILES_X) &&
        (ui.touch_start_x < (UI_FILES_X + UI_FILES_WIDTH)) &&
        (ui.touch_start_y >= UI_FILES_Y) &&
        (ui.touch_start_y < (UI_FILES_Y + UI_FILES_HEIGHT)) &&
        (ui.previous_touch_x >= UI_FILES_X) &&
        (ui.previous_touch_x < (UI_FILES_X + UI_FILES_WIDTH)) &&
        (ui.previous_touch_y >= UI_FILES_Y) &&
        (ui.previous_touch_y < (UI_FILES_Y + UI_FILES_HEIGHT)))
    {
        ui.pending_action = UI_ACTION_BROWSER_OPEN;
        UART_SendString("UI FILE BROWSER REQUEST\\r\\n");
        action_executed = true;
    }
"""

new = """    /* UI_FILES_TOUCH_FIX
     * Require the press to begin inside FILES, but allow the release
     * to drift slightly because this is a resistive touchscreen.
     */
    if ((ui.mode == UI_MODE_RECORDER) &&
        (ui.touch_start_x >= UI_FILES_X) &&
        (ui.touch_start_x < (UI_FILES_X + UI_FILES_WIDTH)) &&
        (ui.touch_start_y >= UI_FILES_Y) &&
        (ui.touch_start_y < (UI_FILES_Y + UI_FILES_HEIGHT)) &&
        ((uint32_t)ui.previous_touch_x + UI_FILES_RELEASE_MARGIN >=
         UI_FILES_X) &&
        (ui.previous_touch_x < (UI_FILES_X + UI_FILES_WIDTH +
                                UI_FILES_RELEASE_MARGIN)) &&
        ((uint32_t)ui.previous_touch_y + UI_FILES_RELEASE_MARGIN >=
         UI_FILES_Y) &&
        (ui.previous_touch_y < (UI_FILES_Y + UI_FILES_HEIGHT +
                                UI_FILES_RELEASE_MARGIN)))
    {
        ui.pending_action = UI_ACTION_BROWSER_OPEN;
        UART_SendString("UI FILE BROWSER REQUEST\\r\\n");
        action_executed = true;
    }
"""

text = once(text, old, new, "strict FILES touch branch")
text = "\n".join(line.rstrip() for line in text.splitlines()) + "\n"

if text.count("{") != text.count("}"):
    fail("Brace balance failed")
if text.count("/*") != text.count("*/"):
    fail("Comment balance failed")
if re.search(r"&(lt|gt|amp);|<br", text):
    fail("HTML artifact generated")
if bytes([0]) in text.encode("utf-8"):
    fail("NUL byte generated")
if MARKER not in text:
    fail("Patch marker missing")

PATH.write_text(text, encoding="utf-8")

print("FILES touch fix applied successfully.")
print("Modified: Core/Src/ui.c")
print("Backup:   Core/Src/ui.c.before_files_touch_fix")
print("FILES is larger and allows 12 pixels of release drift.")
print("Next: make clean && make -j4")
