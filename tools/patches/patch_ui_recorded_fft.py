#!/usr/bin/env python3
"""Add recorded CAPTURE.WAV FFT browsing to 429_DSP.

Run from the project root:

    python3 patch_ui_recorded_fft.py

Creates wav_fft_viewer.c/.h and updates Makefile, main.c, ui.c, and ui.h.
The viewer analyzes 2048-sample WAV segments with BACK, PREV, NEXT, FULL,
and PEAK touchscreen controls.
"""

from pathlib import Path
import re
import shutil

ROOT = Path.cwd()
MAKEFILE = ROOT / "Makefile"
MAIN_C = ROOT / "Core/Src/main.c"
UI_C = ROOT / "Core/Src/ui.c"
UI_H = ROOT / "Core/Inc/ui.h"
FFT_C = ROOT / "Core/Src/fft.c"
FFT_H = ROOT / "Core/Inc/fft.h"
VIEW_C = ROOT / "Core/Src/wav_fft_viewer.c"
VIEW_H = ROOT / "Core/Inc/wav_fft_viewer.h"
MARKER = "UI_RECORDED_FFT_PATCH"


def fail(message):
    raise SystemExit(message)


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        fail(f"Expected exactly one {label}, found {count}")
    return text.replace(old, new, 1)


def backup(path):
    target = path.with_name(path.name + ".before_recorded_fft")
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


for path in (MAKEFILE, MAIN_C, UI_C, UI_H, FFT_C, FFT_H):
    if not path.exists():
        fail(f"Missing required file: {path}")

makefile = MAKEFILE.read_text(encoding="utf-8")
main_c = MAIN_C.read_text(encoding="utf-8")
ui_c = UI_C.read_text(encoding="utf-8")
ui_h = UI_H.read_text(encoding="utf-8")
fft_c = FFT_C.read_text(encoding="utf-8")
fft_h = FFT_H.read_text(encoding="utf-8")

for path, text in (
    (MAKEFILE, makefile),
    (MAIN_C, main_c),
    (UI_C, ui_c),
    (UI_H, ui_h),
    (FFT_C, fft_c),
    (FFT_H, fft_h),
):
    if re.search(r"&(lt|gt|amp);|<br", text):
        fail(f"HTML artifact found in {path}")

if MARKER in main_c or MARKER in ui_c or VIEW_C.exists() or VIEW_H.exists():
    fail("Recorded FFT patch appears to be already installed")

for symbol in (
    "UI_MODE_WAVEFORM",
    "UI_BUTTON_WAVEFORM_RELOAD",
    "ui_draw_waveform_body",
):
    if symbol not in ui_c:
        fail(f"Missing waveform UI prerequisite: {symbol}")

for symbol in (
    "UI_ShowWaveform",
    "UI_IsWaveformScreen",
    "UI_ACTION_VIEW_SPECTRUM",
):
    if symbol not in ui_h:
        fail(f"Missing UI header prerequisite: {symbol}")

for symbol in (
    "UI_WAVE_VIEWER_PATCH",
    "case UI_ACTION_VIEW_SPECTRUM:",
    "UI recorded spectrum viewer not installed yet",
):
    if symbol not in main_c:
        fail(f"Missing main.c prerequisite: {symbol}")

for symbol in (
    "bool FFT_Process(void)",
    "bool FFT_SetSize",
    "uint32_t FFT_GetPeakFrequency",
):
    if symbol not in fft_h or symbol not in fft_c:
        fail(f"Missing FFT prerequisite: {symbol}")

for path in (MAKEFILE, MAIN_C, UI_C, UI_H):
    backup(path)

view_h = r'''#ifndef WAV_FFT_VIEWER_H
#define WAV_FFT_VIEWER_H

#include "ff.h"

#include <stdbool.h>
#include <stdint.h>

#define WAV_FFT_VIEWER_SIZE  2048U

typedef struct
{
    bool active;
    FRESULT result;
    uint32_t sample_rate;
    uint32_t sample_count;
    uint32_t segment_count;
    uint32_t segment_index;
    uint32_t segment_start_sample;
    uint32_t segment_time_ms;
} WavFftViewerInfo;

void WavFftViewer_Init(void);
FRESULT WavFftViewer_Begin(const char *filename);
FRESULT WavFftViewer_LoadPrevious(void);
FRESULT WavFftViewer_LoadNext(void);
void WavFftViewer_End(void);
const WavFftViewerInfo *WavFftViewer_GetInfo(void);

#endif
'''

view_c = r'''#include "wav_fft_viewer.h"

#include "capture.h"
#include "fft.h"
#include "filesystem_service.h"

#include <stddef.h>
#include <string.h>

#define WAV_FFT_HEADER_SIZE   44U
#define WAV_FFT_READ_SAMPLES  256U

static WavFftViewerInfo info;
static char active_filename[32];
static uint8_t read_buffer[WAV_FFT_READ_SAMPLES * 2U];
static uint32_t previous_capture_count;
static uint32_t previous_fft_size;

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

static FRESULT load_segment(uint32_t segment_index)
{
    FIL file;
    FRESULT result;
    UINT bytes_read = 0U;
    uint32_t segment_start;
    uint32_t available_samples;
    uint32_t loaded_samples = 0U;

    if (!info.active || (segment_index >= info.segment_count))
    {
        info.result = FR_INVALID_OBJECT;
        return info.result;
    }

    result = f_open(&file, active_filename, FA_READ);
    if (result != FR_OK)
    {
        info.result = result;
        return result;
    }

    segment_start = segment_index * WAV_FFT_VIEWER_SIZE;
    available_samples = info.sample_count - segment_start;

    result = f_lseek(
        &file,
        WAV_FFT_HEADER_SIZE + (segment_start * 2U));

    if (result != FR_OK)
    {
        info.result = result;
        (void)f_close(&file);
        return result;
    }

    while ((loaded_samples < WAV_FFT_VIEWER_SIZE) &&
           (loaded_samples < available_samples))
    {
        uint32_t remaining_segment =
            WAV_FFT_VIEWER_SIZE - loaded_samples;
        uint32_t remaining_file =
            available_samples - loaded_samples;
        uint32_t request_samples = WAV_FFT_READ_SAMPLES;

        if (request_samples > remaining_segment)
        {
            request_samples = remaining_segment;
        }
        if (request_samples > remaining_file)
        {
            request_samples = remaining_file;
        }

        result = f_read(
            &file,
            read_buffer,
            request_samples * 2U,
            &bytes_read);

        if ((result != FR_OK) ||
            (bytes_read != (request_samples * 2U)))
        {
            info.result = (result != FR_OK) ? result : FR_DISK_ERR;
            (void)f_close(&file);
            return info.result;
        }

        for (uint32_t index = 0U; index < request_samples; index++)
        {
            int16_t pcm =
                (int16_t)read_le16(&read_buffer[index * 2U]);
            int32_t adc = 2048 + ((int32_t)pcm / 16);

            if (adc < 0)
            {
                adc = 0;
            }
            else if (adc > 4095)
            {
                adc = 4095;
            }

            sampleBuffer[loaded_samples + index] = (uint16_t)adc;
        }

        loaded_samples += request_samples;
    }

    while (loaded_samples < WAV_FFT_VIEWER_SIZE)
    {
        sampleBuffer[loaded_samples] = 2048U;
        loaded_samples++;
    }

    result = f_close(&file);
    if (result != FR_OK)
    {
        info.result = result;
        return result;
    }

    sampleCount = WAV_FFT_VIEWER_SIZE;
    sampleRate = info.sample_rate;
    captureComplete = 1;

    if (!FFT_Process())
    {
        info.result = FR_INT_ERR;
        return info.result;
    }

    info.segment_index = segment_index;
    info.segment_start_sample = segment_start;
    info.segment_time_ms = (uint32_t)(
        ((uint64_t)segment_start * 1000ULL) /
        info.sample_rate);
    info.result = FR_OK;
    return FR_OK;
}

void WavFftViewer_Init(void)
{
    memset(&info, 0, sizeof(info));
    memset(active_filename, 0, sizeof(active_filename));
    info.result = FR_OK;
}

const WavFftViewerInfo *WavFftViewer_GetInfo(void)
{
    return &info;
}

FRESULT WavFftViewer_Begin(const char *filename)
{
    FIL file;
    FRESULT result;
    UINT bytes_read = 0U;
    uint8_t header[WAV_FFT_HEADER_SIZE];
    uint32_t file_size;
    uint32_t data_bytes;

    WavFftViewer_Init();

    if ((filename == NULL) ||
        (filename[0] == '\0') ||
        (strlen(filename) >= sizeof(active_filename)))
    {
        info.result = FR_INVALID_NAME;
        return info.result;
    }

    result = FileSystem_Mount();
    if (result != FR_OK)
    {
        info.result = result;
        return result;
    }

    result = f_open(&file, filename, FA_READ);
    if (result != FR_OK)
    {
        info.result = result;
        return result;
    }

    file_size = (uint32_t)f_size(&file);
    result = f_read(&file, header, sizeof(header), &bytes_read);

    if ((result != FR_OK) || (bytes_read != sizeof(header)))
    {
        info.result = (result != FR_OK) ? result : FR_DISK_ERR;
        (void)f_close(&file);
        return info.result;
    }

    data_bytes = read_le32(&header[40]);

    if ((memcmp(&header[0], "RIFF", 4U) != 0) ||
        (memcmp(&header[8], "WAVE", 4U) != 0) ||
        (memcmp(&header[12], "fmt ", 4U) != 0) ||
        (memcmp(&header[36], "data", 4U) != 0) ||
        (read_le32(&header[16]) != 16U) ||
        (read_le16(&header[20]) != 1U) ||
        (read_le16(&header[22]) != 1U) ||
        (read_le16(&header[34]) != 16U) ||
        (read_le32(&header[24]) == 0U) ||
        (data_bytes == 0U) ||
        (file_size != (WAV_FFT_HEADER_SIZE + data_bytes)))
    {
        info.result = FR_INVALID_OBJECT;
        (void)f_close(&file);
        return info.result;
    }

    result = f_close(&file);
    if (result != FR_OK)
    {
        info.result = result;
        return result;
    }

    previous_capture_count = Capture_GetSampleCount();
    previous_fft_size = FFT_GetSize();

    if (!Capture_SetSampleCount(WAV_FFT_VIEWER_SIZE))
    {
        info.result = FR_INT_ERR;
        return info.result;
    }

    if (!FFT_SetSize(WAV_FFT_VIEWER_SIZE))
    {
        (void)Capture_SetSampleCount(previous_capture_count);
        info.result = FR_INT_ERR;
        return info.result;
    }

    strcpy(active_filename, filename);
    info.sample_rate = read_le32(&header[24]);
    info.sample_count = data_bytes / 2U;
    info.segment_count =
        (info.sample_count + WAV_FFT_VIEWER_SIZE - 1U) /
        WAV_FFT_VIEWER_SIZE;
    info.active = true;

    result = load_segment(0U);
    if (result != FR_OK)
    {
        WavFftViewer_End();
    }

    return result;
}

FRESULT WavFftViewer_LoadPrevious(void)
{
    if (!info.active)
    {
        return FR_INVALID_OBJECT;
    }

    if (info.segment_index == 0U)
    {
        return load_segment(0U);
    }

    return load_segment(info.segment_index - 1U);
}

FRESULT WavFftViewer_LoadNext(void)
{
    if (!info.active)
    {
        return FR_INVALID_OBJECT;
    }

    if ((info.segment_index + 1U) >= info.segment_count)
    {
        return load_segment(info.segment_index);
    }

    return load_segment(info.segment_index + 1U);
}

void WavFftViewer_End(void)
{
    if (!info.active)
    {
        return;
    }

    info.active = false;
    (void)Capture_SetSampleCount(previous_capture_count);
    (void)FFT_SetSize(previous_fft_size);
    sampleCount = 0U;
    captureComplete = 1;
    FFT_Invalidate();
}
'''

make_anchor = "Core/Src/wav_viewer.c \\\n"
makefile = replace_once(
    makefile,
    make_anchor,
    make_anchor + "Core/Src/wav_fft_viewer.c \\\n",
    "wav_viewer Makefile source",
)

action_anchor = "    UI_ACTION_VIEW_SPECTRUM\n} UI_Action;\n"
ui_h = replace_once(
    ui_h,
    action_anchor,
    "    UI_ACTION_VIEW_SPECTRUM,\n"
    "    UI_ACTION_FFT_BACK,\n"
    "    UI_ACTION_FFT_PREVIOUS,\n"
    "    UI_ACTION_FFT_NEXT\n"
    "} UI_Action;\n",
    "UI action enum tail",
)

api_anchor = "bool UI_IsWaveformScreen(void);\n"
ui_h = replace_once(
    ui_h,
    api_anchor,
    api_anchor +
    "void UI_ShowRecordedFft(void);\n"
    "bool UI_IsRecordedFftScreen(void);\n",
    "waveform screen API",
)

include_anchor = '#include "wav_viewer.h"\n'
ui_c = replace_once(
    ui_c,
    include_anchor,
    include_anchor + '#include "wav_fft_viewer.h"\n',
    "wav_viewer include",
)

button_anchor = "    UI_BUTTON_WAVEFORM_RELOAD\n} UI_Button;\n"
ui_c = replace_once(
    ui_c,
    button_anchor,
    "    UI_BUTTON_WAVEFORM_RELOAD,\n"
    "    UI_BUTTON_FFT_BACK,\n"
    "    UI_BUTTON_FFT_PREVIOUS,\n"
    "    UI_BUTTON_FFT_NEXT,\n"
    "    UI_BUTTON_FFT_FULL,\n"
    "    UI_BUTTON_FFT_PEAK\n"
    "} UI_Button;\n",
    "waveform button enum tail",
)

mode_anchor = "    UI_MODE_WAVEFORM\n} UI_Mode;\n"
ui_c = replace_once(
    ui_c,
    mode_anchor,
    "    UI_MODE_WAVEFORM,\n    UI_MODE_RECORDED_FFT\n} UI_Mode;\n",
    "waveform mode enum tail",
)

api_function_anchor = r'''bool UI_IsWaveformScreen(void)
{
    return ui.mode == UI_MODE_WAVEFORM;
}

'''
api_function_replacement = r'''bool UI_IsWaveformScreen(void)
{
    return ui.mode == UI_MODE_WAVEFORM;
}

void UI_ShowRecordedFft(void)
{
    uint32_t largest = 0U;

    ui.mode = UI_MODE_RECORDED_FFT;
    ui.running = false;
    ui_apply_full_span();

    for (uint32_t bin = 1U; bin < FFT_GetBinCount(); bin++)
    {
        uint32_t magnitude = FFT_GetMagnitude(bin);

        if (magnitude > largest)
        {
            largest = magnitude;
        }
    }

    if (largest < UI_MIN_MAGNITUDE_MAX)
    {
        largest = UI_MIN_MAGNITUDE_MAX;
    }
    else if (largest < UI_MAX_MAGNITUDE_MAX)
    {
        largest += largest / 4U;
    }

    if (largest > UI_MAX_MAGNITUDE_MAX)
    {
        largest = UI_MAX_MAGNITUDE_MAX;
    }

    ui.magnitude_max = largest;
    ui.graph_dirty = true;
    ui.controls_dirty = true;
    Display_Clear(COLOR_BLACK);
}

bool UI_IsRecordedFftScreen(void)
{
    return ui.mode == UI_MODE_RECORDED_FFT;
}

'''
ui_c = replace_once(
    ui_c,
    api_function_anchor,
    api_function_replacement,
    "waveform screen function",
)

controls_start, controls_end = function_range(
    ui_c,
    "static void ui_draw_controls(void)\n",
)
controls = ui_c[controls_start:controls_end]
controls_anchor = """    if (ui.mode == UI_MODE_WAVEFORM)
    {
"""
controls_replacement = r'''    if (ui.mode == UI_MODE_RECORDED_FFT)
    {
        Display_FillRect(0, UI_CONTROL_TOP, DISPLAY_WIDTH,
                         UI_CONTROL_HEIGHT, COLOR_BLACK);
        ui_draw_button(0, COLOR_BLUE, COLOR_WHITE);
        ui_draw_button(64, COLOR_CYAN, COLOR_WHITE);
        ui_draw_button(128, COLOR_GREEN, COLOR_WHITE);
        ui_draw_button(192, COLOR_YELLOW, COLOR_WHITE);
        ui_draw_button(256, COLOR_MAGENTA, COLOR_WHITE);
        Display_DrawString(22, 12, "BACK", COLOR_BLACK, COLOR_BLUE);
        Display_DrawString(80, 12, "PREV", COLOR_BLACK, COLOR_CYAN);
        Display_DrawString(144, 12, "NEXT", COLOR_BLACK, COLOR_GREEN);
        Display_DrawString(208, 12, "FULL", COLOR_BLACK, COLOR_YELLOW);
        Display_DrawString(278, 12, "PEAK", COLOR_BLACK, COLOR_MAGENTA);
        return;
    }

    if (ui.mode == UI_MODE_WAVEFORM)
    {
'''
controls = replace_once(
    controls,
    controls_anchor,
    controls_replacement,
    "waveform controls branch",
)
ui_c = ui_c[:controls_start] + controls + ui_c[controls_end:]

hit_start, hit_end = function_range(
    ui_c,
    "static UI_Button ui_hit_test_button(uint16_t x, uint16_t y)\n",
)
hit = ui_c[hit_start:hit_end]
hit_anchor = """    if (ui.mode == UI_MODE_WAVEFORM)
    {
"""
hit_replacement = r'''    if (ui.mode == UI_MODE_RECORDED_FFT)
    {
        if (x < 64U)
        {
            return UI_BUTTON_FFT_BACK;
        }
        if (x < 128U)
        {
            return UI_BUTTON_FFT_PREVIOUS;
        }
        if (x < 192U)
        {
            return UI_BUTTON_FFT_NEXT;
        }
        if (x < 256U)
        {
            return UI_BUTTON_FFT_FULL;
        }
        if (x < DISPLAY_WIDTH)
        {
            return UI_BUTTON_FFT_PEAK;
        }
        return UI_BUTTON_NONE;
    }

    if (ui.mode == UI_MODE_WAVEFORM)
    {
'''
hit = replace_once(
    hit,
    hit_anchor,
    hit_replacement,
    "waveform hit-test branch",
)
ui_c = ui_c[:hit_start] + hit + ui_c[hit_end:]

action_anchor = """        case UI_BUTTON_WAVEFORM_BACK:
"""
action_addition = r'''        case UI_BUTTON_FFT_BACK:
            ui.mode = UI_MODE_RECORDER;
            ui.graph_dirty = true;
            ui.controls_dirty = true;
            ui.pending_action = UI_ACTION_FFT_BACK;
            Display_Clear(COLOR_BLACK);
            UART_SendString("UI RECORDED FFT BACK REQUEST\r\n");
            break;

        case UI_BUTTON_FFT_PREVIOUS:
            ui.pending_action = UI_ACTION_FFT_PREVIOUS;
            UART_SendString("UI RECORDED FFT PREV REQUEST\r\n");
            break;

        case UI_BUTTON_FFT_NEXT:
            ui.pending_action = UI_ACTION_FFT_NEXT;
            UART_SendString("UI RECORDED FFT NEXT REQUEST\r\n");
            break;

        case UI_BUTTON_FFT_FULL:
            ui_apply_full_span();
            ui.graph_dirty = true;
            UART_SendString("UI RECORDED FFT FULL\r\n");
            break;

        case UI_BUTTON_FFT_PEAK:
            ui_center_on_peak();
            ui.graph_dirty = true;
            UART_SendString("UI RECORDED FFT PEAK\r\n");
            break;

        case UI_BUTTON_WAVEFORM_BACK:
'''
ui_c = replace_once(
    ui_c,
    action_anchor,
    action_addition,
    "waveform back action",
)

status_start, status_end = function_range(
    ui_c,
    "static void ui_draw_status(void)\n",
)
status = ui_c[status_start:status_end]
status_anchor = """    if (ui.mode == UI_MODE_WAVEFORM)
    {
"""
status_replacement = r'''    if (ui.mode == UI_MODE_RECORDED_FFT)
    {
        const WavFftViewerInfo *info = WavFftViewer_GetInfo();

        Display_FillRect(0, UI_STATUS_TOP, DISPLAY_WIDTH,
                         UI_STATUS_HEIGHT, COLOR_BLACK);
        Display_DrawRect(0, UI_STATUS_TOP, DISPLAY_WIDTH,
                         UI_STATUS_HEIGHT, COLOR_GRAY);
        Display_DrawString(4, UI_STATUS_TOP + 8,
                           "SEG", COLOR_WHITE, COLOR_BLACK);

        if ((info != NULL) && info->active)
        {
            ui_draw_uint(28, UI_STATUS_TOP + 8,
                         info->segment_index + 1U, COLOR_YELLOW);
            Display_DrawString(58, UI_STATUS_TOP + 8,
                               "/", COLOR_WHITE, COLOR_BLACK);
            ui_draw_uint(68, UI_STATUS_TOP + 8,
                         info->segment_count, COLOR_YELLOW);
            Display_DrawString(118, UI_STATUS_TOP + 8,
                               "MS", COLOR_WHITE, COLOR_BLACK);
            ui_draw_uint(138, UI_STATUS_TOP + 8,
                         info->segment_time_ms, COLOR_YELLOW);
            Display_DrawString(210, UI_STATUS_TOP + 8,
                               "PEAK", COLOR_WHITE, COLOR_BLACK);
            ui_draw_uint(246, UI_STATUS_TOP + 8,
                         FFT_GetPeakFrequency(), COLOR_GREEN);
        }
        return;
    }

    if (ui.mode == UI_MODE_WAVEFORM)
    {
'''
status = replace_once(
    status,
    status_anchor,
    status_replacement,
    "waveform status branch",
)
ui_c = ui_c[:status_start] + status + ui_c[status_end:]

include_anchor = '#include "wav_viewer.h"\n'
main_c = replace_once(
    main_c,
    include_anchor,
    include_anchor + '#include "wav_fft_viewer.h"\n',
    "wav_viewer main include",
)

init_anchor = "    WavViewer_Init();\n"
main_c = replace_once(
    main_c,
    init_anchor,
    init_anchor + "    WavFftViewer_Init();\n",
    "wav viewer initialization",
)

old_fft_action = r'''        case UI_ACTION_VIEW_SPECTRUM:
            UART_SendString("UI recorded spectrum viewer not installed yet\r\n");
            break;
'''
new_fft_action = r'''        case UI_ACTION_VIEW_SPECTRUM:
            if (Capture_IsRecorderActive())
            {
                UART_SendString("Stop recording before FFT view\r\n");
                break;
            }

            Recorder_GetStatus(&status);
            if (status.state != RECORDER_STATE_IDLE)
            {
                UART_SendString("Wait for recorder IDLE before FFT view\r\n");
                break;
            }

            result = WavFftViewer_Begin("CAPTURE.WAV");
            if (result == FR_OK)
            {
                UI_ShowRecordedFft();
                UART_SendString("UI recorded FFT loaded\r\n");
            }
            else
            {
                UART_SendString("UI recorded FFT load failed, FatFs = ");
                UART_SendDec((uint32_t)result);
                UART_SendString("\r\n");
            }
            break;

        case UI_ACTION_FFT_PREVIOUS:
            result = WavFftViewer_LoadPrevious();
            if (result == FR_OK)
            {
                UI_ShowRecordedFft();
            }
            else
            {
                UART_SendString("UI recorded FFT previous failed\r\n");
            }
            break;

        case UI_ACTION_FFT_NEXT:
            result = WavFftViewer_LoadNext();
            if (result == FR_OK)
            {
                UI_ShowRecordedFft();
            }
            else
            {
                UART_SendString("UI recorded FFT next failed\r\n");
            }
            break;

        case UI_ACTION_FFT_BACK:
            WavFftViewer_End();
            UART_SendString("UI recorded FFT closed\r\n");
            break;
'''
main_c = replace_once(
    main_c,
    old_fft_action,
    new_fft_action,
    "recorded FFT placeholder",
)

marker_anchor = "/* UI_WAVE_VIEWER_PATCH */\n"
main_c = replace_once(
    main_c,
    marker_anchor,
    marker_anchor + "/* UI_RECORDED_FFT_PATCH */\n",
    "wave viewer marker",
)

for path, text in ((MAIN_C, main_c), (UI_C, ui_c), (UI_H, ui_h)):
    if text.count("{") != text.count("}"):
        fail(f"Brace balance failed in {path}")
    if text.count("/*") != text.count("*/"):
        fail(f"Comment balance failed in {path}")
    if re.search(r"&(lt|gt|amp);|<br", text):
        fail(f"HTML artifact found after patching {path}")

if re.search(r"&(lt|gt|amp);|<br", makefile):
    fail("HTML artifact found after patching Makefile")
if makefile.count("Core/Src/wav_fft_viewer.c") != 1:
    fail("wav_fft_viewer.c Makefile source count is not one")

for symbol in (
    "WavFftViewer_Begin",
    "UI_ShowRecordedFft",
    "UI_MODE_RECORDED_FFT",
    "UI_ACTION_FFT_NEXT",
    "UI_BUTTON_FFT_PEAK",
):
    if symbol not in (view_h + view_c + main_c + ui_c + ui_h):
        fail(f"Missing recorded FFT symbol: {symbol}")

MAKEFILE.write_text(makefile, encoding="utf-8")
MAIN_C.write_text(
    "\n".join(line.rstrip() for line in main_c.splitlines()) + "\n",
    encoding="utf-8",
)
UI_C.write_text(
    "\n".join(line.rstrip() for line in ui_c.splitlines()) + "\n",
    encoding="utf-8",
)
UI_H.write_text(
    "\n".join(line.rstrip() for line in ui_h.splitlines()) + "\n",
    encoding="utf-8",
)
VIEW_C.write_text(view_c, encoding="utf-8")
VIEW_H.write_text(view_h, encoding="utf-8")

print("Recorded FFT viewer patch applied successfully.")
print("Created: Core/Inc/wav_fft_viewer.h")
print("Created: Core/Src/wav_fft_viewer.c")
print("Modified: Makefile, main.c, ui.c, and ui.h")
print("Next: make clean && make -j4")

