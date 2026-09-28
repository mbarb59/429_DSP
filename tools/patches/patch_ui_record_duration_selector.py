#!/usr/bin/env python3
"""Add a touchscreen 5/10/30/60-second recording duration selector."""

from pathlib import Path
import re
import shutil

UI_H = Path("Core/Inc/ui.h")
UI_C = Path("Core/Src/ui.c")
MAIN_C = Path("Core/Src/main.c")
MARKER = "UI_RECORD_DURATION_SELECTOR_PATCH"


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


def backup(path):
    target = path.with_name(path.name + ".before_record_duration_selector")
    if not target.exists():
        shutil.copy2(path, target)


for path in (UI_H, UI_C, MAIN_C):
    if not path.exists():
        fail("Missing required file: " + str(path))
    if b"\x00" in path.read_bytes():
        fail("Binary NUL byte found in " + str(path))

ui_h = UI_H.read_text(encoding="utf-8")
ui_c = UI_C.read_text(encoding="utf-8")
main_c = MAIN_C.read_text(encoding="utf-8")

for path, text in ((UI_H, ui_h), (UI_C, ui_c), (MAIN_C, main_c)):
    if MARKER in text:
        fail("Duration selector already installed in " + str(path))
    if re.search(r"&(lt|gt|amp);|<br", text):
        fail("HTML artifact found in " + str(path))

for prerequisite in (
    "CONSOLE_REC10_PATCH",
    "CONSOLE_REC30_PATCH",
    "CONSOLE_REC60_PATCH",
    "UI_WAV_RENAME_PATCH",
    "Capture_StartRecorder(60U)",
):
    if prerequisite not in (ui_h + ui_c + main_c):
        fail("Missing prerequisite: " + prerequisite)

for path in (UI_H, UI_C, MAIN_C):
    backup(path)

# ---------------------------------------------------------------------------
# Public UI action and API.
# ---------------------------------------------------------------------------
ui_h = once(
    ui_h,
    """    UI_ACTION_FILE_RENAME_OPEN,
    UI_ACTION_FILE_RENAME_SAVE
} UI_Action;
""",
    """    UI_ACTION_FILE_RENAME_OPEN,
    UI_ACTION_FILE_RENAME_SAVE,
    UI_ACTION_RECORD_DURATION
} UI_Action;
""",
    "UI action enum tail",
)

ui_h = once(
    ui_h,
    "bool UI_IsRecorderScreen(void);\n",
    """bool UI_IsRecorderScreen(void);

/* UI_RECORD_DURATION_SELECTOR_PATCH */
void UI_ShowRecordDurationSelector(void);
bool UI_IsRecordDurationSelectorScreen(void);
uint32_t UI_GetRequestedRecordingSeconds(void);
""",
    "recorder screen public API",
)

# ---------------------------------------------------------------------------
# UI enums and state.
# ---------------------------------------------------------------------------
ui_c = once(
    ui_c,
    """    UI_BUTTON_RENAME_CANCEL,
    UI_BUTTON_RENAME_SAVE
} UI_Button;
""",
    """    UI_BUTTON_RENAME_CANCEL,
    UI_BUTTON_RENAME_SAVE,
    UI_BUTTON_DURATION_BACK,
    UI_BUTTON_DURATION_5,
    UI_BUTTON_DURATION_10,
    UI_BUTTON_DURATION_30,
    UI_BUTTON_DURATION_60
} UI_Button;
""",
    "rename button enum tail",
)

ui_c = once(
    ui_c,
    """    UI_MODE_FILE_DELETE_CONFIRM,
    UI_MODE_FILE_RENAME
} UI_Mode;
""",
    """    UI_MODE_FILE_DELETE_CONFIRM,
    UI_MODE_FILE_RENAME,
    UI_MODE_RECORD_DURATION
} UI_Mode;
""",
    "rename mode enum tail",
)

ui_c = once(
    ui_c,
    "    uint32_t browser_pending_row;\n",
    """    uint32_t browser_pending_row;
    uint32_t requested_recording_seconds;
""",
    "browser pending-row state",
)

ui_c = once(
    ui_c,
    "static void ui_draw_recorder_body(void);\n",
    """static void ui_draw_recorder_body(void);
static void ui_draw_record_duration_body(void);
""",
    "recorder body prototype",
)

start, end = function_range(ui_c, "void UI_Init(void)\n")
function = ui_c[start:end]
function = once(
    function,
    "    ui.browser_pending_row = 0U;\n",
    """    ui.browser_pending_row = 0U;
    ui.requested_recording_seconds = 5U;
""",
    "browser pending-row initialization",
)
ui_c = ui_c[:start] + function + ui_c[end:]

# Add duration-screen transition API after recorder screen query.
anchor = r'''bool UI_IsRecorderScreen(void)
{
    return ui.mode == UI_MODE_RECORDER;
}

'''
replacement = anchor + r'''void UI_ShowRecordDurationSelector(void)
{
    ui.mode = UI_MODE_RECORD_DURATION;
    ui.running = false;
    ui.graph_dirty = true;
    ui.controls_dirty = true;
    Display_Clear(COLOR_BLACK);
}

bool UI_IsRecordDurationSelectorScreen(void)
{
    return ui.mode == UI_MODE_RECORD_DURATION;
}

uint32_t UI_GetRequestedRecordingSeconds(void)
{
    return ui.requested_recording_seconds;
}

'''
ui_c = once(ui_c, anchor, replacement, "recorder screen query")

# Draw the duration body before the recorder body.
start, end = function_range(ui_c, "void UI_DrawSpectrum(void)\n")
function = ui_c[start:end]
function = once(
    function,
    "    if (ui.mode == UI_MODE_FILE_RENAME)\n",
    """    if (ui.mode == UI_MODE_RECORD_DURATION)
    {
        ui_draw_record_duration_body();
        ui.graph_dirty = false;
        return;
    }

    if (ui.mode == UI_MODE_FILE_RENAME)
""",
    "rename draw branch",
)
ui_c = ui_c[:start] + function + ui_c[end:]

# Duration controls and rename REC5 label to RECORD.
start, end = function_range(ui_c, "static void ui_draw_controls(void)\n")
function = ui_c[start:end]
function = once(
    function,
    "    if (ui.mode == UI_MODE_FILE_RENAME)\n",
    r'''    if (ui.mode == UI_MODE_RECORD_DURATION)
    {
        Display_FillRect(0, UI_CONTROL_TOP, DISPLAY_WIDTH,
                         UI_CONTROL_HEIGHT, COLOR_BLACK);
        ui_draw_button(0, COLOR_BLUE, COLOR_WHITE);
        ui_draw_button(64, COLOR_CYAN, COLOR_WHITE);
        ui_draw_button(128, COLOR_GREEN, COLOR_WHITE);
        ui_draw_button(192, COLOR_YELLOW, COLOR_WHITE);
        ui_draw_button(256, COLOR_MAGENTA, COLOR_WHITE);
        Display_DrawString(22, 12, "BACK", COLOR_BLACK, COLOR_BLUE);
        Display_DrawString(78, 12, "5 SEC", COLOR_BLACK, COLOR_CYAN);
        Display_DrawString(138, 12, "10 SEC", COLOR_BLACK, COLOR_GREEN);
        Display_DrawString(202, 12, "30 SEC", COLOR_BLACK, COLOR_YELLOW);
        Display_DrawString(266, 12, "60 SEC", COLOR_BLACK, COLOR_MAGENTA);
        return;
    }

    if (ui.mode == UI_MODE_FILE_RENAME)
''',
    "rename control branch",
)
function = once(
    function,
    '        Display_DrawString(86, 12, "REC5", COLOR_BLACK, COLOR_GREEN);\n',
    '        Display_DrawString(72, 12, "RECORD", COLOR_BLACK, COLOR_GREEN);\n',
    "recorder REC5 label",
)
ui_c = ui_c[:start] + function + ui_c[end:]

# Duration selector body before recorder body.
duration_body = r'''static void ui_draw_record_duration_body(void)
{
    Display_FillRect(UI_GRAPH_LEFT, UI_GRAPH_TOP,
                     UI_GRAPH_WIDTH, UI_GRAPH_HEIGHT, COLOR_BLACK);
    Display_DrawRect(UI_GRAPH_LEFT, UI_GRAPH_TOP,
                     UI_GRAPH_WIDTH, UI_GRAPH_HEIGHT, COLOR_CYAN);

    Display_DrawString(50, 46, "SELECT RECORDING DURATION",
                       COLOR_WHITE, COLOR_BLACK);

    Display_DrawString(28, 82, "5 SEC", COLOR_CYAN, COLOR_BLACK);
    Display_DrawString(138, 82, "480044 BYTES", COLOR_GRAY, COLOR_BLACK);

    Display_DrawString(28, 112, "10 SEC", COLOR_GREEN, COLOR_BLACK);
    Display_DrawString(138, 112, "960044 BYTES", COLOR_GRAY, COLOR_BLACK);

    Display_DrawString(28, 142, "30 SEC", COLOR_YELLOW, COLOR_BLACK);
    Display_DrawString(126, 142, "2880044 BYTES", COLOR_GRAY, COLOR_BLACK);

    Display_DrawString(28, 172, "60 SEC", COLOR_MAGENTA, COLOR_BLACK);
    Display_DrawString(126, 172, "5760044 BYTES", COLOR_GRAY, COLOR_BLACK);

    Display_DrawString(42, 202, "TOUCH A PRESET TO START",
                       COLOR_WHITE, COLOR_BLACK);
}

'''
ui_c = once(
    ui_c,
    "static void ui_draw_recorder_body(void)\n",
    duration_body + "static void ui_draw_recorder_body(void)\n",
    "recorder body definition",
)

# Status branch.
start, end = function_range(ui_c, "static void ui_draw_status(void)\n")
function = ui_c[start:end]
function = once(
    function,
    "    if (ui.mode == UI_MODE_FILE_RENAME)\n",
    r'''    if (ui.mode == UI_MODE_RECORD_DURATION)
    {
        Display_FillRect(0, UI_STATUS_TOP, DISPLAY_WIDTH,
                         UI_STATUS_HEIGHT, COLOR_BLACK);
        Display_DrawRect(0, UI_STATUS_TOP, DISPLAY_WIDTH,
                         UI_STATUS_HEIGHT, COLOR_CYAN);
        Display_DrawString(4, UI_STATUS_TOP + 8,
                           "RECORD DURATION", COLOR_WHITE, COLOR_BLACK);
        Display_DrawString(144, UI_STATUS_TOP + 8,
                           "5 / 10 / 30 / 60 SEC", COLOR_YELLOW, COLOR_BLACK);
        return;
    }

    if (ui.mode == UI_MODE_FILE_RENAME)
''',
    "rename status branch",
)
ui_c = ui_c[:start] + function + ui_c[end:]

# Hit testing.
start, end = function_range(
    ui_c,
    "static UI_Button ui_hit_test_button(uint16_t x, uint16_t y)\n",
)
function = ui_c[start:end]
function = once(
    function,
    "    if (ui.mode == UI_MODE_FILE_RENAME)\n",
    r'''    if (ui.mode == UI_MODE_RECORD_DURATION)
    {
        if (x < 64U) return UI_BUTTON_DURATION_BACK;
        if (x < 128U) return UI_BUTTON_DURATION_5;
        if (x < 192U) return UI_BUTTON_DURATION_10;
        if (x < 256U) return UI_BUTTON_DURATION_30;
        return UI_BUTTON_DURATION_60;
    }

    if (ui.mode == UI_MODE_FILE_RENAME)
''',
    "rename hit-test branch",
)
ui_c = ui_c[:start] + function + ui_c[end:]

# Button execution: REC5 now opens the selector. Duration buttons queue one
# common action and return to the recorder screen immediately.
start, end = function_range(ui_c, "static void ui_execute_button(UI_Button button)\n")
function = ui_c[start:end]
function = once(
    function,
    """        case UI_BUTTON_RECORD_5:
            ui.pending_action = UI_ACTION_RECORD_5;
            UART_SendString("UI RECORD 5 REQUEST\\r\\n");
            break;
""",
    """        case UI_BUTTON_RECORD_5:
            UI_ShowRecordDurationSelector();
            UART_SendString("UI RECORD DURATION SCREEN\\r\\n");
            break;

        case UI_BUTTON_DURATION_BACK:
            UI_ShowRecorder();
            UART_SendString("UI RECORD DURATION BACK\\r\\n");
            break;

        case UI_BUTTON_DURATION_5:
            ui.requested_recording_seconds = 5U;
            ui.pending_action = UI_ACTION_RECORD_DURATION;
            UI_ShowRecorder();
            UART_SendString("UI RECORD 5 REQUEST\\r\\n");
            break;

        case UI_BUTTON_DURATION_10:
            ui.requested_recording_seconds = 10U;
            ui.pending_action = UI_ACTION_RECORD_DURATION;
            UI_ShowRecorder();
            UART_SendString("UI RECORD 10 REQUEST\\r\\n");
            break;

        case UI_BUTTON_DURATION_30:
            ui.requested_recording_seconds = 30U;
            ui.pending_action = UI_ACTION_RECORD_DURATION;
            UI_ShowRecorder();
            UART_SendString("UI RECORD 30 REQUEST\\r\\n");
            break;

        case UI_BUTTON_DURATION_60:
            ui.requested_recording_seconds = 60U;
            ui.pending_action = UI_ACTION_RECORD_DURATION;
            UI_ShowRecorder();
            UART_SendString("UI RECORD 60 REQUEST\\r\\n");
            break;
""",
    "REC5 button execution case",
)
ui_c = ui_c[:start] + function + ui_c[end:]

# ---------------------------------------------------------------------------
# main.c foreground duration action.
# ---------------------------------------------------------------------------
main_c = once(
    main_c,
    "/* CONSOLE_REC60_PATCH */\n",
    """/* CONSOLE_REC60_PATCH */
/* UI_RECORD_DURATION_SELECTOR_PATCH */
""",
    "REC60 feature marker",
)

# Insert the common UI duration case immediately before the proven REC5 case.
duration_case = r'''        case UI_ACTION_RECORD_DURATION:
        {
            uint32_t duration_seconds =
                UI_GetRequestedRecordingSeconds();

            if ((duration_seconds != 5U) &&
                (duration_seconds != 10U) &&
                (duration_seconds != 30U) &&
                (duration_seconds != 60U))
            {
                UART_SendString("UI invalid recording duration\r\n");
                break;
            }

            if (!Analyzer_RequireStopped())
            {
                UART_SendString("UI timed recording blocked by analyzer\r\n");
                break;
            }

            if (Capture_IsRecorderActive())
            {
                UART_SendString("UI recorder is already active\r\n");
                break;
            }

            Recorder_GetStatus(&status);
            if (status.state != RECORDER_STATE_IDLE)
            {
                UART_SendString("UI recorder is not idle\r\n");
                break;
            }

            result = RecordingCatalog_StartNext();
            if (result != FR_OK)
            {
                UART_SendString("UI recorder file open failed, FatFs = ");
                UART_SendDec((uint32_t)result);
                UART_SendString("\r\n");
                break;
            }

            if (!Capture_StartRecorder(duration_seconds))
            {
                UART_SendString("UI timed recorder DMA start failed\r\n");
                (void)Recorder_StopRaw();
                break;
            }

            UART_SendString("UI timed recording started: ");
            UART_SendDec(duration_seconds);
            UART_SendString(" seconds\r\n");
            break;
        }

'''
main_c = once(
    main_c,
    "        case UI_ACTION_RECORD_5:\n",
    duration_case + "        case UI_ACTION_RECORD_5:\n",
    "UI_ACTION_RECORD_5 case",
)

# Validate all generated sources before writing.
outputs = []
for path, text in ((UI_H, ui_h), (UI_C, ui_c), (MAIN_C, main_c)):
    text = "\n".join(line.rstrip() for line in text.splitlines()) + "\n"
    if text.count("{") != text.count("}"):
        fail("Brace balance failed in " + str(path))
    if text.count("/*") != text.count("*/"):
        fail("Comment balance failed in " + str(path))
    if re.search(r"&(lt|gt|amp);|<br", text):
        fail("HTML artifact generated in " + str(path))
    if b"\x00" in text.encode("utf-8"):
        fail("Binary NUL generated in " + str(path))
    outputs.append((path, text))

combined = "\n".join(text for _, text in outputs)
for symbol in (
    MARKER,
    "UI_MODE_RECORD_DURATION",
    "UI_ACTION_RECORD_DURATION",
    "UI_GetRequestedRecordingSeconds",
    "UI_BUTTON_DURATION_60",
    "UI timed recording started",
):
    if symbol not in combined:
        fail("Missing generated symbol: " + symbol)

# Ensure console duration commands remain unchanged and present.
for symbol in (
    'strcmp(command, "rec10")',
    'strcmp(command, "rec30")',
    'strcmp(command, "rec60")',
    "Capture_StartRecorder(10U)",
    "Capture_StartRecorder(30U)",
    "Capture_StartRecorder(60U)",
):
    if combined.count(symbol) != 1:
        fail("Console duration symbol missing or duplicated: " + symbol)

for path, text in outputs:
    path.write_text(text, encoding="utf-8")

print("Touchscreen duration selector patch applied successfully.")
print("Modified: Core/Inc/ui.h, Core/Src/ui.c, Core/Src/main.c")
print("REC screen now opens 5/10/30/60-second selector.")
print("Console rec5/rec10/rec30/rec60 paths remain available.")
print("Next: make clean && make -j4")
