#!/usr/bin/env python3
"""Add an on-screen CAPTURE.WAV waveform viewer to 429_DSP.

Run from the project root:

    python3 patch_ui_wave_viewer.py

The patch creates wav_viewer.c/.h and updates the Makefile, UI, and main.c.
"""

from pathlib import Path
import re
import shutil

ROOT = Path.cwd()
MAKEFILE = ROOT / "Makefile"
MAIN_C = ROOT / "Core/Src/main.c"
UI_C = ROOT / "Core/Src/ui.c"
UI_H = ROOT / "Core/Inc/ui.h"
VIEW_C = ROOT / "Core/Src/wav_viewer.c"
VIEW_H = ROOT / "Core/Inc/wav_viewer.h"
MARKER = "UI_WAVE_VIEWER_PATCH"


def fail(message):
    raise SystemExit(message)


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        fail(f"Expected exactly one {label}, found {count}")
    return text.replace(old, new, 1)


def backup(path, suffix):
    target = path.with_name(path.name + suffix)
    if not target.exists():
        shutil.copy2(path, target)


def function_range(text, signature):
    start = text.find(signature)
    if start < 0:
        fail("Could not find function: " + signature.strip())

    brace = text.find("{", start)
    if brace < 0:
        fail("Could not find opening brace: " + signature.strip())

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

    fail("Could not find closing brace: " + signature.strip())


for path in (MAKEFILE, MAIN_C, UI_C, UI_H):
    if not path.exists():
        fail(f"Missing required file: {path}")

makefile = MAKEFILE.read_text(encoding="utf-8")
main_c = MAIN_C.read_text(encoding="utf-8")
ui_c = UI_C.read_text(encoding="utf-8")
ui_h = UI_H.read_text(encoding="utf-8")

for path, text in ((MAKEFILE, makefile), (MAIN_C, main_c), (UI_C, ui_c), (UI_H, ui_h)):
    if re.search(r"&(lt|gt|amp);|<br", text):
        fail(f"HTML artifact found in {path}")

if MARKER in main_c or MARKER in ui_c or VIEW_C.exists() or VIEW_H.exists():
    fail("Wave viewer patch appears to be already installed")

for symbol in (
    "UI_RECORDER_MODE_PATCH",
    "UI_ACTION_VIEW_WAVEFORM",
    "UI_GetPendingAction",
):
    if symbol not in ui_h:
        fail(f"Missing UI prerequisite: {symbol}")

for symbol in (
    "UI_MODE_RECORDER",
    "UI_BUTTON_VIEW_WAVEFORM",
    "ui_draw_recorder_body",
    "UI_RECORDER_LABEL_FIX",
):
    if symbol not in ui_c:
        fail(f"Missing ui.c prerequisite: {symbol}")

for symbol in (
    "UI_RECORDER_CONTROLLER_PATCH",
    "case UI_ACTION_VIEW_WAVEFORM:",
    "UI waveform viewer not installed yet",
):
    if symbol not in main_c:
        fail(f"Missing main.c prerequisite: {symbol}")

backup(MAKEFILE, ".before_wave_viewer")
backup(MAIN_C, ".before_wave_viewer")
backup(UI_C, ".before_wave_viewer")
backup(UI_H, ".before_wave_viewer")

view_h = r"""#ifndef WAV_VIEWER_H
#define WAV_VIEWER_H

#include "ff.h"

#include <stdbool.h>
#include <stdint.h>

#define WAV_VIEWER_COLUMN_COUNT  320U

typedef struct
{
    bool valid;
    FRESULT result;
    uint32_t file_size;
    uint32_t sample_rate;
    uint32_t sample_count;
    uint32_t duration_ms;
    uint32_t data_bytes;
    uint32_t peak_absolute;
    int16_t minimum[WAV_VIEWER_COLUMN_COUNT];
    int16_t maximum[WAV_VIEWER_COLUMN_COUNT];
} WavViewerData;

void WavViewer_Init(void);
FRESULT WavViewer_Load(const char *filename);
const WavViewerData *WavViewer_GetData(void);

#endif
"""

view_c = r"""#include "wav_viewer.h"

#include "filesystem_service.h"

#include <stddef.h>
#include <string.h>

#define WAV_VIEWER_HEADER_SIZE       44U
#define WAV_VIEWER_READ_SAMPLES      256U

static WavViewerData viewer;
static uint8_t read_buffer[WAV_VIEWER_READ_SAMPLES * 2U];

static uint16_t read_le16(const uint8_t *data)
{
    return (uint16_t)data[0] |
           ((uint16_t)data[1] << 8U);
}

static uint32_t read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) |
           ((uint32_t)data[3] << 24U);
}

void WavViewer_Init(void)
{
    memset(&viewer, 0, sizeof(viewer));
    viewer.result = FR_OK;
}

const WavViewerData *WavViewer_GetData(void)
{
    return &viewer;
}

FRESULT WavViewer_Load(const char *filename)
{
    FIL file;
    FRESULT result;
    UINT bytes_read = 0U;
    uint8_t header[WAV_VIEWER_HEADER_SIZE];
    uint32_t sample_index = 0U;
    uint32_t column = 0U;
    uint32_t column_end = 0U;

    WavViewer_Init();

    if ((filename == NULL) || (filename[0] == '\0'))
    {
        viewer.result = FR_INVALID_NAME;
        return viewer.result;
    }

    result = FileSystem_Mount();
    if (result != FR_OK)
    {
        viewer.result = result;
        return result;
    }

    result = f_open(&file, filename, FA_READ);
    if (result != FR_OK)
    {
        viewer.result = result;
        return result;
    }

    viewer.file_size = (uint32_t)f_size(&file);

    result = f_read(&file, header, sizeof(header), &bytes_read);
    if ((result != FR_OK) || (bytes_read != sizeof(header)))
    {
        viewer.result = (result != FR_OK) ? result : FR_DISK_ERR;
        (void)f_close(&file);
        return viewer.result;
    }

    if ((memcmp(&header[0], "RIFF", 4U) != 0) ||
        (memcmp(&header[8], "WAVE", 4U) != 0) ||
        (memcmp(&header[12], "fmt ", 4U) != 0) ||
        (memcmp(&header[36], "data", 4U) != 0) ||
        (read_le32(&header[16]) != 16U) ||
        (read_le16(&header[20]) != 1U) ||
        (read_le16(&header[22]) != 1U) ||
        (read_le16(&header[34]) != 16U))
    {
        viewer.result = FR_INVALID_OBJECT;
        (void)f_close(&file);
        return viewer.result;
    }

    viewer.sample_rate = read_le32(&header[24]);
    viewer.data_bytes = read_le32(&header[40]);
    viewer.sample_count = viewer.data_bytes / 2U;

    if ((viewer.sample_rate == 0U) ||
        (viewer.sample_count == 0U) ||
        (viewer.file_size != (WAV_VIEWER_HEADER_SIZE + viewer.data_bytes)))
    {
        viewer.result = FR_INVALID_OBJECT;
        (void)f_close(&file);
        return viewer.result;
    }

    viewer.duration_ms = (uint32_t)(
        ((uint64_t)viewer.sample_count * 1000ULL) /
        viewer.sample_rate);

    for (column = 0U; column < WAV_VIEWER_COLUMN_COUNT; column++)
    {
        viewer.minimum[column] = INT16_MAX;
        viewer.maximum[column] = INT16_MIN;
    }

    column = 0U;
    column_end = (uint32_t)(
        ((uint64_t)(column + 1U) * viewer.sample_count) /
        WAV_VIEWER_COLUMN_COUNT);

    while (sample_index < viewer.sample_count)
    {
        uint32_t remaining = viewer.sample_count - sample_index;
        uint32_t request_samples = WAV_VIEWER_READ_SAMPLES;

        if (request_samples > remaining)
        {
            request_samples = remaining;
        }

        result = f_read(
            &file,
            read_buffer,
            request_samples * 2U,
            &bytes_read);

        if ((result != FR_OK) ||
            (bytes_read != (request_samples * 2U)))
        {
            viewer.result = (result != FR_OK) ? result : FR_DISK_ERR;
            (void)f_close(&file);
            return viewer.result;
        }

        for (uint32_t index = 0U; index < request_samples; index++)
        {
            int16_t sample = (int16_t)read_le16(&read_buffer[index * 2U]);
            uint32_t absolute = (sample < 0) ?
                (uint32_t)(-(int32_t)sample) : (uint32_t)sample;

            while ((sample_index >= column_end) &&
                   (column < (WAV_VIEWER_COLUMN_COUNT - 1U)))
            {
                column++;
                column_end = (uint32_t)(
                    ((uint64_t)(column + 1U) * viewer.sample_count) /
                    WAV_VIEWER_COLUMN_COUNT);
            }

            if (sample < viewer.minimum[column])
            {
                viewer.minimum[column] = sample;
            }

            if (sample > viewer.maximum[column])
            {
                viewer.maximum[column] = sample;
            }

            if (absolute > viewer.peak_absolute)
            {
                viewer.peak_absolute = absolute;
            }

            sample_index++;
        }
    }

    result = f_close(&file);
    viewer.result = result;
    viewer.valid = (result == FR_OK);
    return result;
}
"""

# Add new module to Makefile.
make_anchor = "Core/Src/recorder_framework.c \\\n"
makefile = replace_once(
    makefile,
    make_anchor,
    make_anchor + "Core/Src/wav_viewer.c \\\n",
    "recorder_framework Makefile source",
)

# Public UI API.
ui_h_anchor = "bool UI_IsRecorderScreen(void);\n"
ui_h = replace_once(
    ui_h,
    ui_h_anchor,
    ui_h_anchor + "void UI_ShowWaveform(void);\nbool UI_IsWaveformScreen(void);\n",
    "UI_IsRecorderScreen declaration",
)

# Include viewer in UI.
ui_include_anchor = '#include "uart.h"\n'
ui_c = replace_once(
    ui_c,
    ui_include_anchor,
    ui_include_anchor + '#include "wav_viewer.h"\n',
    "ui.c uart include",
)

# Add waveform buttons.
button_anchor = "    UI_BUTTON_VIEW_SPECTRUM\n} UI_Button;\n"
ui_c = replace_once(
    ui_c,
    button_anchor,
    "    UI_BUTTON_VIEW_SPECTRUM,\n"
    "    UI_BUTTON_WAVEFORM_BACK,\n"
    "    UI_BUTTON_WAVEFORM_RELOAD\n"
    "} UI_Button;\n",
    "recorder button enum tail",
)

# Add waveform mode.
mode_anchor = "    UI_MODE_RECORDER\n} UI_Mode;\n"
ui_c = replace_once(
    ui_c,
    mode_anchor,
    "    UI_MODE_RECORDER,\n    UI_MODE_WAVEFORM\n} UI_Mode;\n",
    "UI mode enum tail",
)

# Add drawing prototype.
prototype_anchor = "static void ui_draw_recorder_body(void);\n"
ui_c = replace_once(
    ui_c,
    prototype_anchor,
    prototype_anchor + "static void ui_draw_waveform_body(void);\n",
    "recorder body prototype",
)

# Public APIs after UI_IsRecorderScreen.
api_anchor = """bool UI_IsRecorderScreen(void)
{
    return ui.mode == UI_MODE_RECORDER;
}

"""
api_addition = r"""bool UI_IsRecorderScreen(void)
{
    return ui.mode == UI_MODE_RECORDER;
}

void UI_ShowWaveform(void)
{
    ui.mode = UI_MODE_WAVEFORM;
    ui.running = false;
    ui.graph_dirty = true;
    ui.controls_dirty = true;
    Display_Clear(COLOR_BLACK);
}

bool UI_IsWaveformScreen(void)
{
    return ui.mode == UI_MODE_WAVEFORM;
}

"""
ui_c = replace_once(ui_c, api_anchor, api_addition, "UI_IsRecorderScreen function")

# Draw waveform mode before recorder mode.
draw_anchor = """    if (ui.mode == UI_MODE_RECORDER)
    {
        ui_draw_recorder_body();
        ui.graph_dirty = false;
        return;
    }
"""
draw_replacement = r"""    if (ui.mode == UI_MODE_WAVEFORM)
    {
        ui_draw_waveform_body();
        ui.graph_dirty = false;
        return;
    }

    if (ui.mode == UI_MODE_RECORDER)
    {
        ui_draw_recorder_body();
        ui.graph_dirty = false;
        return;
    }
"""
ui_c = replace_once(ui_c, draw_anchor, draw_replacement, "recorder draw branch")

# Waveform controls before recorder controls, scoped to ui_draw_controls().
controls_start, controls_end = function_range(
    ui_c,
    "static void ui_draw_controls(void)\n",
)
controls_text = ui_c[controls_start:controls_end]
controls_anchor = """    if (ui.mode == UI_MODE_RECORDER)
    {
"""
controls_replacement = r"""    if (ui.mode == UI_MODE_WAVEFORM)
    {
        Display_FillRect(0, UI_CONTROL_TOP, DISPLAY_WIDTH,
                         UI_CONTROL_HEIGHT, COLOR_BLACK);
        ui_draw_button(0, COLOR_BLUE, COLOR_WHITE);
        ui_draw_button(64, COLOR_GREEN, COLOR_WHITE);
        ui_draw_button(128, COLOR_DARK_GRAY, COLOR_GRAY);
        ui_draw_button(192, COLOR_DARK_GRAY, COLOR_GRAY);
        ui_draw_button(256, COLOR_DARK_GRAY, COLOR_GRAY);
        Display_DrawString(22, 12, "BACK", COLOR_BLACK, COLOR_BLUE);
        Display_DrawString(76, 12, "RELOAD", COLOR_BLACK, COLOR_GREEN);
        return;
    }

    if (ui.mode == UI_MODE_RECORDER)
    {
"""
controls_text = replace_once(
    controls_text,
    controls_anchor,
    controls_replacement,
    "recorder controls branch inside ui_draw_controls",
)
ui_c = ui_c[:controls_start] + controls_text + ui_c[controls_end:]

# Waveform hit testing, scoped to ui_hit_test_button().
hit_start, hit_end = function_range(
    ui_c,
    "static UI_Button ui_hit_test_button(uint16_t x, uint16_t y)\n",
)
hit_text = ui_c[hit_start:hit_end]
hit_anchor = """    if (ui.mode == UI_MODE_RECORDER)
    {
"""
hit_replacement = r"""    if (ui.mode == UI_MODE_WAVEFORM)
    {
        if (x < 64U)
        {
            return UI_BUTTON_WAVEFORM_BACK;
        }
        if (x < 128U)
        {
            return UI_BUTTON_WAVEFORM_RELOAD;
        }
        return UI_BUTTON_NONE;
    }

    if (ui.mode == UI_MODE_RECORDER)
    {
"""
hit_text = replace_once(
    hit_text,
    hit_anchor,
    hit_replacement,
    "recorder branch inside ui_hit_test_button",
)
ui_c = ui_c[:hit_start] + hit_text + ui_c[hit_end:]

# Button actions before NONE.
action_anchor = """        case UI_BUTTON_NONE:
        default:
"""
action_addition = r"""        case UI_BUTTON_WAVEFORM_BACK:
            ui.mode = UI_MODE_RECORDER;
            ui.graph_dirty = true;
            ui.controls_dirty = true;
            Display_Clear(COLOR_BLACK);
            UART_SendString("UI RECORDER SCREEN\r\n");
            break;

        case UI_BUTTON_WAVEFORM_RELOAD:
            ui.pending_action = UI_ACTION_VIEW_WAVEFORM;
            UART_SendString("UI WAVEFORM RELOAD REQUEST\r\n");
            break;

        case UI_BUTTON_NONE:
        default:
"""
ui_c = replace_once(ui_c, action_anchor, action_addition, "button action default")

# Waveform status before recorder status.
status_anchor = """    if (ui.mode == UI_MODE_RECORDER)
    {
"""
status_replacement = r"""    if (ui.mode == UI_MODE_WAVEFORM)
    {
        Display_FillRect(0, UI_STATUS_TOP, DISPLAY_WIDTH,
                         UI_STATUS_HEIGHT, COLOR_BLACK);
        Display_DrawRect(0, UI_STATUS_TOP, DISPLAY_WIDTH,
                         UI_STATUS_HEIGHT, COLOR_GRAY);
        Display_DrawString(4, UI_STATUS_TOP + 8,
                           "CAPTURE.WAV WAVEFORM",
                           COLOR_WHITE, COLOR_BLACK);
        return;
    }

    if (ui.mode == UI_MODE_RECORDER)
    {
"""
# Locate status function and replace only within it.
status_start = ui_c.find("static void ui_draw_status(void)\n")
if status_start < 0:
    fail("Could not find ui_draw_status")
status_end = ui_c.find("\nstatic ", status_start + 1)
if status_end < 0:
    fail("Could not find end of ui_draw_status")
status_text = ui_c[status_start:status_end]
status_text = replace_once(status_text, status_anchor, status_replacement, "status recorder branch")
ui_c = ui_c[:status_start] + status_text + ui_c[status_end:]

# Waveform body before grid.
body_anchor = "static void ui_draw_grid(void)\n"
body = r"""static void ui_draw_waveform_body(void)
{
    const WavViewerData *data = WavViewer_GetData();
    const int32_t wave_top = 88;
    const int32_t wave_bottom = 204;
    const int32_t center_y = (wave_top + wave_bottom) / 2;
    const int32_t half_height = (wave_bottom - wave_top) / 2;
    uint32_t peak;

    Display_FillRect(UI_GRAPH_LEFT, UI_GRAPH_TOP,
                     UI_GRAPH_WIDTH, UI_GRAPH_HEIGHT, COLOR_BLACK);
    Display_DrawRect(UI_GRAPH_LEFT, UI_GRAPH_TOP,
                     UI_GRAPH_WIDTH, UI_GRAPH_HEIGHT, COLOR_GRAY);
    Display_DrawString(8, 38, "CAPTURE.WAV", COLOR_CYAN, COLOR_BLACK);

    if ((data == NULL) || !data->valid)
    {
        Display_DrawString(8, 60, "NO VALID WAV", COLOR_RED, COLOR_BLACK);
        return;
    }

    Display_DrawString(8, 52, "RATE", COLOR_WHITE, COLOR_BLACK);
    ui_draw_uint(44, 52, data->sample_rate, COLOR_YELLOW);
    Display_DrawString(110, 52, "TIME MS", COLOR_WHITE, COLOR_BLACK);
    ui_draw_uint(164, 52, data->duration_ms, COLOR_YELLOW);
    Display_DrawString(230, 52, "PEAK", COLOR_WHITE, COLOR_BLACK);
    ui_draw_uint(266, 52, data->peak_absolute, COLOR_YELLOW);

    Display_DrawLine(0, center_y, DISPLAY_WIDTH - 1,
                     center_y, COLOR_DARK_GRAY);
    Display_DrawRect(0, wave_top, DISPLAY_WIDTH,
                     wave_bottom - wave_top + 1, COLOR_GRAY);

    peak = data->peak_absolute;
    if (peak == 0U)
    {
        peak = 1U;
    }

    for (uint32_t x = 0U; x < WAV_VIEWER_COLUMN_COUNT; x++)
    {
        int32_t minimum = data->minimum[x];
        int32_t maximum = data->maximum[x];
        int32_t y_min;
        int32_t y_max;

        if (minimum == INT16_MAX)
        {
            minimum = 0;
        }
        if (maximum == INT16_MIN)
        {
            maximum = 0;
        }

        y_min = center_y - (maximum * half_height) / (int32_t)peak;
        y_max = center_y - (minimum * half_height) / (int32_t)peak;

        if (y_min < wave_top)
        {
            y_min = wave_top;
        }
        if (y_max > wave_bottom)
        {
            y_max = wave_bottom;
        }

        Display_DrawLine((int32_t)x, y_min,
                         (int32_t)x, y_max, COLOR_GREEN);
    }
}

"""
ui_c = replace_once(ui_c, body_anchor, body + body_anchor, "ui_draw_grid definition")

# Add include and loader action in main.c.
include_anchor = '#include "recorder_framework.h"\n'
main_c = replace_once(
    main_c,
    include_anchor,
    include_anchor + '#include "wav_viewer.h"\n',
    "recorder framework include",
)

# Initialize viewer after recorder framework.
init_anchor = "    Recorder_FrameworkInit();\n"
main_c = replace_once(
    main_c,
    init_anchor,
    init_anchor + "    WavViewer_Init();\n",
    "recorder framework initialization",
)

old_action = r"""        case UI_ACTION_VIEW_WAVEFORM:
            UART_SendString("UI waveform viewer not installed yet\r\n");
            break;
"""
new_action = r"""        case UI_ACTION_VIEW_WAVEFORM:
            if (Capture_IsRecorderActive())
            {
                UART_SendString("Stop recording before waveform view\r\n");
                break;
            }

            Recorder_GetStatus(&status);
            if (status.state != RECORDER_STATE_IDLE)
            {
                UART_SendString("Wait for recorder IDLE before waveform view\r\n");
                break;
            }

            result = WavViewer_Load("CAPTURE.WAV");
            if (result == FR_OK)
            {
                UI_ShowWaveform();
                UART_SendString("UI waveform loaded\r\n");
            }
            else
            {
                UART_SendString("UI waveform load failed, FatFs = ");
                UART_SendDec((uint32_t)result);
                UART_SendString("\r\n");
            }
            break;
"""
main_c = replace_once(main_c, old_action, new_action, "waveform placeholder action")

# Add marker.
main_marker_anchor = "/* UI_RECORDER_CONTROLLER_PATCH */\n"
main_c = replace_once(
    main_c,
    main_marker_anchor,
    main_marker_anchor + "/* UI_WAVE_VIEWER_PATCH */\n",
    "UI recorder controller marker",
)

# Validate outputs. C brace/comment checks apply only to C and header files.
# The Makefile legitimately contains shell wildcards such as build/*, which
# must not be interpreted as the opening of a C comment.
for path, text in ((MAIN_C, main_c), (UI_C, ui_c), (UI_H, ui_h)):
    if text.count("{") != text.count("}"):
        fail(f"Brace balance failed in {path}")
    if text.count("/*") != text.count("*/"):
        fail(f"Comment balance failed in {path}")
    if re.search(r"&(lt|gt|amp);|<br", text):
        fail(f"HTML artifact found after patching {path}")

if re.search(r"&(lt|gt|amp);|<br", makefile):
    fail(f"HTML artifact found after patching {MAKEFILE}")

if makefile.count("Core/Src/wav_viewer.c") != 1:
    fail("wav_viewer.c Makefile source count is not one")

for symbol in (
    "WavViewer_Load",
    "UI_ShowWaveform",
    "UI_MODE_WAVEFORM",
    "ui_draw_waveform_body",
    "UI_BUTTON_WAVEFORM_RELOAD",
):
    if symbol not in (view_h + view_c + main_c + ui_c + ui_h):
        fail(f"Missing wave-viewer symbol: {symbol}")

MAKEFILE.write_text(makefile, encoding="utf-8")
MAIN_C.write_text("\n".join(line.rstrip() for line in main_c.splitlines()) + "\n", encoding="utf-8")
UI_C.write_text("\n".join(line.rstrip() for line in ui_c.splitlines()) + "\n", encoding="utf-8")
UI_H.write_text("\n".join(line.rstrip() for line in ui_h.splitlines()) + "\n", encoding="utf-8")
VIEW_C.write_text(view_c, encoding="utf-8")
VIEW_H.write_text(view_h, encoding="utf-8")

print("Waveform viewer patch applied successfully.")
print("Created: Core/Inc/wav_viewer.h")
print("Created: Core/Src/wav_viewer.c")
print("Modified: Makefile, main.c, ui.c, and ui.h")
print("Next: make clean && make -j4")
