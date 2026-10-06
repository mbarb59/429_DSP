#!/usr/bin/env python3
"""Add long-press EXPORT ALL to the recorded FFT TFT screen.

Short tap EXPORT keeps the existing single-segment RECxxxx.CSV export.
Holding EXPORT for at least 1200 ms invokes the scanned-event exporter and
creates RxxxxE1.CSV through RxxxxE5.CSV for the events currently retained.
"""

from pathlib import Path
import re
import shutil

UI_H = Path("Core/Inc/ui.h")
UI_C = Path("Core/Src/ui.c")
MAIN_C = Path("Core/Src/main.c")
MARKER = "TFT_FFT_EXPORT_ALL_LONG_PRESS_PATCH"


def fail(message):
    raise SystemExit(message)


def once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        fail(f"Expected exactly one {label}, found {count}")
    return text.replace(old, new, 1)


for path in (UI_H, UI_C, MAIN_C):
    if not path.exists():
        fail("Missing required file: " + str(path))
    raw = path.read_bytes()
    if b"\x00" in raw:
        fail("Binary NUL found in " + str(path))

ui_h = UI_H.read_text(encoding="utf-8")
ui_c = UI_C.read_text(encoding="utf-8")
main_c = MAIN_C.read_text(encoding="utf-8")
all_text = ui_h + ui_c + main_c

if MARKER in all_text:
    fail("TFT EXPORT ALL long-press support is already installed")

for path, text in ((UI_H, ui_h), (UI_C, ui_c), (MAIN_C, main_c)):
    if re.search(r"&(lt|gt|amp);|<br", text):
        fail("HTML artifact found in " + str(path))

for required in (
    "TFT_FFT_SCAN_EXPORT_PATCH",
    "FFTEXPORTEVENTS_UART_PATCH",
    "UI_ACTION_FFT_EXPORT",
    "UI_BUTTON_FFT_EXPORT",
    "Console_RunFftExportEvents",
    "ui.last_touch_action_ms",
    "static void ui_handle_touch_down",
    "static void ui_handle_touch_up(void)",
):
    if required not in all_text:
        fail("Missing prerequisite: " + required)

for path in (UI_H, UI_C, MAIN_C):
    backup = path.with_name(path.name + ".before_tft_export_all_long_press")
    if not backup.exists():
        shutil.copy2(path, backup)

# Public action consumed by main.c.
ui_h = once(
    ui_h,
    """    UI_ACTION_FFT_SCAN,
    UI_ACTION_FFT_EXPORT,
    UI_ACTION_FFT_LOAD_SEGMENT,
""",
    """    UI_ACTION_FFT_SCAN,
    UI_ACTION_FFT_EXPORT,
    UI_ACTION_FFT_EXPORT_ALL,
    UI_ACTION_FFT_LOAD_SEGMENT,
""",
    "recorded FFT public action sequence",
)

# Long-press threshold near the other touch timing constants.
match = re.search(
    r"(#define UI_TOUCH_ACTION_LOCKOUT_MS\s+\d+U\n)", ui_c)
if not match:
    fail("Could not find UI_TOUCH_ACTION_LOCKOUT_MS")
anchor = match.group(1)
ui_c = once(
    ui_c,
    anchor,
    anchor + "#define UI_FFT_EXPORT_ALL_HOLD_MS       1200U\n",
    "touch action lockout definition",
)

# Add press timestamp alongside existing touch timing state.
ui_c = once(
    ui_c,
    """    uint32_t last_touch_poll_ms;
    uint32_t last_touch_action_ms;
""",
    """    uint32_t last_touch_poll_ms;
    uint32_t last_touch_action_ms;
    uint32_t touch_down_ms;
""",
    "touch timing state",
)

ui_c = once(
    ui_c,
    """    ui.last_touch_poll_ms = HAL_GetTick();
    ui.last_touch_action_ms = 0U;
""",
    """    ui.last_touch_poll_ms = HAL_GetTick();
    ui.last_touch_action_ms = 0U;
    ui.touch_down_ms = 0U;
""",
    "touch timing initialization",
)

# Record the start time whenever a new accepted touch begins. The active source
# may format function signatures on one line or across several lines, so locate
# the implementations by name and then use the next touch-handler definition.
down_matches = list(re.finditer(
    r"^static void ui_handle_touch_down\s*\(",
    ui_c,
    re.MULTILINE))
move_matches = list(re.finditer(
    r"^static void ui_handle_touch_move\s*\(",
    ui_c,
    re.MULTILINE))

if not down_matches or not move_matches:
    fail("Could not locate touch-down or touch-move implementation")

down_start = down_matches[-1].start()
down_end = next(
    (match.start() for match in move_matches if match.start() > down_start),
    -1)
if down_end < 0:
    fail("Could not isolate ui_handle_touch_down implementation")

down = ui_c[down_start:down_end]

# The coordinate assignment is a stable location after entering a touch.
down = once(
    down,
    """    ui.touch_start_x = touch->x;
    ui.touch_start_y = touch->y;
""",
    """    ui.touch_start_x = touch->x;
    ui.touch_start_y = touch->y;
    ui.touch_down_ms = HAL_GetTick();
""",
    "touch-down coordinate initialization",
)
ui_c = ui_c[:down_start] + down + ui_c[down_end:]

# Replace the generic release execution block with an EXPORT-specific duration
# decision. All other buttons retain their existing execution path.
release_old = '''        if ((ui.pressed_button != UI_BUTTON_NONE) &&
            (released_button == ui.pressed_button))
        {
            ui_execute_button(ui.pressed_button);
            action_executed = true;
        }
'''
release_new = '''        if ((ui.pressed_button != UI_BUTTON_NONE) &&
            (released_button == ui.pressed_button))
        {
            if ((ui.pressed_button == UI_BUTTON_FFT_EXPORT) &&
                ((HAL_GetTick() - ui.touch_down_ms) >=
                 UI_FFT_EXPORT_ALL_HOLD_MS))
            {
                ui.pending_action = UI_ACTION_FFT_EXPORT_ALL;
                UART_SendString(
                    "UI RECORDED FFT EXPORT ALL REQUEST\\r\\n");
            }
            else
            {
                ui_execute_button(ui.pressed_button);
            }
            action_executed = true;
        }
'''
ui_c = once(ui_c, release_old, release_new,
            "top-control release execution block")

# Marker in UI source.
ui_c = once(
    ui_c,
    "/* TFT_FFT_SCAN_EXPORT_PATCH */\n",
    """/* TFT_FFT_SCAN_EXPORT_PATCH */
/* TFT_FFT_EXPORT_ALL_LONG_PRESS_PATCH */
""",
    "TFT SCAN/EXPORT marker",
)

# Route the new action to the already validated UART event-export function.
main_c = once(
    main_c,
    """        case UI_ACTION_FFT_EXPORT:
            if (!RecordingCatalog_SelectedIsWav())
            {
                UART_SendString("FFT export blocked: select a WAV file\\r\\n");
                break;
            }
            Console_RunFftExport();
            break;

""",
    """        case UI_ACTION_FFT_EXPORT:
            if (!RecordingCatalog_SelectedIsWav())
            {
                UART_SendString("FFT export blocked: select a WAV file\\r\\n");
                break;
            }
            Console_RunFftExport();
            break;

        case UI_ACTION_FFT_EXPORT_ALL:
            if (!RecordingCatalog_SelectedIsWav())
            {
                UART_SendString(
                    "FFT event export blocked: select a WAV file\\r\\n");
                break;
            }
            Console_RunFftExportEvents();
            break;

""",
    "TFT single export action case",
)

main_c = once(
    main_c,
    "/* TFT_FFT_SCAN_EXPORT_PATCH */\n",
    """/* TFT_FFT_SCAN_EXPORT_PATCH */
/* TFT_FFT_EXPORT_ALL_LONG_PRESS_PATCH */
""",
    "TFT SCAN/EXPORT marker in main",
)

outputs = []
for path, text in ((UI_H, ui_h), (UI_C, ui_c), (MAIN_C, main_c)):
    text = "\n".join(line.rstrip() for line in text.splitlines()) + "\n"
    if text.count("{") != text.count("}"):
        fail("Brace mismatch in " + str(path))
    if text.count("/*") != text.count("*/"):
        fail("Comment mismatch in " + str(path))
    if re.search(r"&(lt|gt|amp);|<br", text):
        fail("HTML artifact generated in " + str(path))
    if b"\x00" in text.encode("utf-8"):
        fail("Binary NUL generated in " + str(path))
    outputs.append((path, text))

joined = "\n".join(text for _, text in outputs)
for required in (
    MARKER,
    "UI_ACTION_FFT_EXPORT_ALL",
    "UI_FFT_EXPORT_ALL_HOLD_MS",
    "touch_down_ms",
    "UI RECORDED FFT EXPORT ALL REQUEST",
    "Console_RunFftExportEvents();",
):
    if required not in joined:
        fail("Missing generated item: " + required)

for path, text in outputs:
    path.write_text(text, encoding="utf-8")

print("TFT EXPORT ALL long-press patch applied successfully.")
print("Short tap EXPORT keeps the current-segment RECxxxx.CSV export.")
print("Hold EXPORT for at least 1200 ms to export all scanned events.")
print("EXPORT ALL uses RxxxxE1.CSV through RxxxxE5.CSV.")
print("Next: make clean && make -j4")
