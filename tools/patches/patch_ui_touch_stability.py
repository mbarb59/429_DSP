#!/usr/bin/env python3
"""Add isolated touch debounce and same-control release validation."""

from pathlib import Path
import re
import shutil

UI_C = Path("Core/Src/ui.c")
BACKUP = Path("Core/Src/ui.c.before_touch_stability")
MARKER = "UI_TOUCH_STABILITY_PATCH"


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

source_bytes = UI_C.read_bytes()
if bytes([0]) in source_bytes:
    fail("NUL byte found in Core/Src/ui.c")

text = source_bytes.decode("utf-8")

if MARKER in text:
    fail("Touch-stability patch is already installed")

for prerequisite in (
    "UI_TOUCH_POLL_MS",
    "UI_TAP_MOVE_LIMIT",
    "UI_BUTTON_RECORD_5",
    "static void ui_handle_touch_up(void)",
):
    if prerequisite not in text:
        fail("Missing prerequisite: " + prerequisite)

if re.search(r"&(lt|gt|amp);|<br", text):
    fail("HTML artifact found in Core/Src/ui.c")

if not BACKUP.exists():
    shutil.copy2(UI_C, BACKUP)

# Debounce constants.
text = once(
    text,
    "#define UI_TOUCH_POLL_MS           10U\n",
    """#define UI_TOUCH_POLL_MS           10U

/* UI_TOUCH_STABILITY_PATCH */
#define UI_TOUCH_ACTION_LOCKOUT_MS  225U
#define UI_TOUCH_RELEASE_POLLS      2U
""",
    "touch polling constant",
)

# State required to rearm only after a stable release and lockout.
text = once(
    text,
    """    uint16_t previous_touch_x;
    uint16_t previous_touch_y;

    uint32_t last_touch_poll_ms;
""",
    """    uint16_t previous_touch_x;
    uint16_t previous_touch_y;

    uint32_t last_touch_poll_ms;
    uint32_t last_touch_action_ms;
    uint32_t released_poll_count;
    bool touch_armed;
    UI_Button pressed_button;
""",
    "touch state fields",
)

# Initialize stability state.
init_start, init_end = function_range(text, "void UI_Init(void)\n")
init_text = text[init_start:init_end]
init_text = once(
    init_text,
    "    ui.last_touch_poll_ms = HAL_GetTick();\n",
    """    ui.last_touch_poll_ms = HAL_GetTick();
    ui.last_touch_action_ms = 0U;
    ui.released_poll_count = UI_TOUCH_RELEASE_POLLS;
    ui.touch_armed = true;
    ui.pressed_button = UI_BUTTON_NONE;
""",
    "touch stability initialization",
)
text = text[:init_start] + init_text + text[init_end:]

# Replace the press/release dispatch with an armed state machine.
process_start, process_end = function_range(text, "void UI_ProcessTouch(void)\n")
process = text[process_start:process_end]
old_dispatch = """    if (touch.pressed)
    {
        if (!ui.touch_active)
        {
            ui_handle_touch_down(&touch);
        }
        else
        {
            ui_handle_touch_move(&touch);
        }
    }
    else if (ui.touch_active)
    {
        ui_handle_touch_up();
    }
"""
new_dispatch = """    if (touch.pressed)
    {
        ui.released_poll_count = 0U;

        if (!ui.touch_active)
        {
            if (ui.touch_armed &&
                ((now - ui.last_touch_action_ms) >=
                 UI_TOUCH_ACTION_LOCKOUT_MS))
            {
                ui_handle_touch_down(&touch);
            }
        }
        else
        {
            ui_handle_touch_move(&touch);
        }
    }
    else
    {
        if (ui.touch_active)
        {
            ui_handle_touch_up();
        }

        if (ui.released_poll_count < UI_TOUCH_RELEASE_POLLS)
        {
            ui.released_poll_count++;
        }

        if ((ui.released_poll_count >= UI_TOUCH_RELEASE_POLLS) &&
            ((now - ui.last_touch_action_ms) >=
             UI_TOUCH_ACTION_LOCKOUT_MS))
        {
            ui.touch_armed = true;
        }
    }
"""
process = once(
    process,
    old_dispatch,
    new_dispatch,
    "touch press/release dispatch",
)
text = text[:process_start] + process + text[process_end:]

# Capture the button present at touch-down. Analyzer REC tab remains a
# separate body/status target and therefore stores UI_BUTTON_NONE.
down_start, down_end = function_range(
    text,
    "static void ui_handle_touch_down(\n",
)
down = text[down_start:down_end]
down = once(
    down,
    """    ui.previous_touch_x = touch->x;
    ui.previous_touch_y = touch->y;
""",
    """    ui.previous_touch_x = touch->x;
    ui.previous_touch_y = touch->y;

    ui.touch_armed = false;
    ui.pressed_button = ui_hit_test_button(touch->x, touch->y);
""",
    "touch-down coordinate storage",
)
text = text[:down_start] + down + text[down_end:]

# Replace touch-up handling. A top-row action is accepted only when the
# release remains in the same button as the press. Screen transitions are
# therefore protected from a lingering or sliding touch.
up_start, up_end = function_range(text, "static void ui_handle_touch_up(void)\n")
up = text[up_start:up_end]
old_up = """static void ui_handle_touch_up(void)
{
    if ((ui.mode == UI_MODE_ANALYZER) &&
        (ui.touch_start_y >= UI_STATUS_TOP) &&
        (ui.touch_start_y < (UI_STATUS_TOP + UI_STATUS_HEIGHT)) &&
        (ui.touch_start_x >= UI_REC_TAB_X))
    {
        ui.mode = UI_MODE_RECORDER;
        ui.running = false;
        ui.graph_dirty = true;
        ui.controls_dirty = true;
        Display_Clear(COLOR_BLACK);
        UART_SendString("UI RECORDER SCREEN\\r\\n");
    }
    else if (ui.touch_start_y < UI_CONTROL_HEIGHT)
    {
        UI_Button button = ui_hit_test_button(
            ui.touch_start_x,
            ui.touch_start_y);

        ui_execute_button(button);
    }

    ui.touch_active = false;
    ui.dragging = false;
}
"""
new_up = """static void ui_handle_touch_up(void)
{
    bool action_executed = false;

    if (!ui.dragging &&
        (ui.mode == UI_MODE_ANALYZER) &&
        (ui.touch_start_y >= UI_STATUS_TOP) &&
        (ui.touch_start_y < (UI_STATUS_TOP + UI_STATUS_HEIGHT)) &&
        (ui.touch_start_x >= UI_REC_TAB_X) &&
        (ui.previous_touch_y >= UI_STATUS_TOP) &&
        (ui.previous_touch_y < (UI_STATUS_TOP + UI_STATUS_HEIGHT)) &&
        (ui.previous_touch_x >= UI_REC_TAB_X))
    {
        ui.mode = UI_MODE_RECORDER;
        ui.running = false;
        ui.graph_dirty = true;
        ui.controls_dirty = true;
        Display_Clear(COLOR_BLACK);
        UART_SendString("UI RECORDER SCREEN\\r\\n");
        action_executed = true;
    }
    else if (!ui.dragging &&
             (ui.touch_start_y < UI_CONTROL_HEIGHT) &&
             (ui.previous_touch_y < UI_CONTROL_HEIGHT))
    {
        UI_Button released_button = ui_hit_test_button(
            ui.previous_touch_x,
            ui.previous_touch_y);

        if ((ui.pressed_button != UI_BUTTON_NONE) &&
            (released_button == ui.pressed_button))
        {
            ui_execute_button(ui.pressed_button);
            action_executed = true;
        }
    }

    ui.touch_active = false;
    ui.dragging = false;
    ui.pressed_button = UI_BUTTON_NONE;

    if (action_executed)
    {
        ui.last_touch_action_ms = HAL_GetTick();
    }
}
"""
up = once(up, old_up, new_up, "touch-up function")
text = text[:up_start] + up + text[up_end:]

# Validate all generated source before writing.
text = "\n".join(line.rstrip() for line in text.splitlines()) + "\n"

if text.count("{") != text.count("}"):
    fail("Brace balance failed in Core/Src/ui.c")
if text.count("/*") != text.count("*/"):
    fail("Comment balance failed in Core/Src/ui.c")
if re.search(r"&(lt|gt|amp);|<br", text):
    fail("HTML artifact generated in Core/Src/ui.c")
if bytes([0]) in text.encode("utf-8"):
    fail("NUL byte generated in Core/Src/ui.c")

for symbol in (
    MARKER,
    "UI_TOUCH_ACTION_LOCKOUT_MS",
    "UI_TOUCH_RELEASE_POLLS",
    "pressed_button",
    "released_poll_count",
):
    if symbol not in text:
        fail("Missing generated symbol: " + symbol)

UI_C.write_text(text, encoding="utf-8")

print("Touch-stability patch applied successfully.")
print("Modified: Core/Src/ui.c")
print("Backup:   Core/Src/ui.c.before_touch_stability")
print("Added 225 ms lockout, stable-release rearming, and")
print("same-button press/release validation.")
print("Next: make clean && make -j4")
