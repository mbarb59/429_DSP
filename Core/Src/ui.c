/*
 * Touch-driven spectrum analyzer UI for STM32F429I-DISC1.
 *
 * Logical display: 320 x 240 landscape.
 *
 * Top control bar:
 *   RUN/STOP | FULL | ZOOM OUT | ZOOM IN | CENTER PEAK
 *
 * Gestures in graph area:
 *   horizontal drag = pan frequency viewport
 *   vertical drag   = change magnitude scale
 */

#include "ui.h"

#include "display.h"
#include "recording_catalog.h"
#include "fft.h"
#include "stmpe811.h"
#include "uart.h"
#include "wav_viewer.h"
#include "wav_fft_viewer.h"

#include <string.h>

#include "stm32f4xx_hal.h"

#include <stdbool.h>
#include <stdint.h>

#define UI_CONTROL_TOP             0
#define UI_CONTROL_HEIGHT          32
#define UI_BUTTON_WIDTH            64

#define UI_GRAPH_LEFT              0
#define UI_GRAPH_TOP               32
#define UI_GRAPH_RIGHT             ((int32_t)DISPLAY_WIDTH - 1)
#define UI_GRAPH_BOTTOM            215
#define UI_GRAPH_WIDTH             DISPLAY_WIDTH
#define UI_GRAPH_HEIGHT            (UI_GRAPH_BOTTOM - UI_GRAPH_TOP + 1)

#define UI_STATUS_TOP              216
#define UI_STATUS_HEIGHT           24

#define UI_RUN_X                   0
#define UI_FULL_X                  64
#define UI_ZOOM_OUT_X              128
#define UI_ZOOM_IN_X               192
#define UI_PEAK_X                  256

#define UI_FIRST_USABLE_BIN        1U
#define UI_MIN_VISIBLE_BINS        8U

#define UI_DEFAULT_MAGNITUDE_MAX   1600U
#define UI_MIN_MAGNITUDE_MAX       100U
#define UI_MAX_MAGNITUDE_MAX       65535U

#define UI_TAP_MOVE_LIMIT          16U
#define UI_PAN_PIXEL_THRESHOLD     3
#define UI_SCALE_PIXEL_THRESHOLD   4
#define UI_TOUCH_POLL_MS           10U

/* UI_TOUCH_STABILITY_PATCH */
#define UI_TOUCH_ACTION_LOCKOUT_MS  225U
#define UI_TOUCH_RELEASE_POLLS      2U

/* UI_RECORDER_LABEL_FIX */
#define UI_REC_TAB_X               256
#define UI_REC_TAB_WIDTH           64
#define UI_REC_STATUS_REFRESH_MS   100U

/* UI_WAV_BROWSER_V2_PATCH */
#define UI_FILES_X                 174U
#define UI_FILES_Y                 158U
#define UI_FILES_WIDTH             140U
#define UI_FILES_HEIGHT            48U
#define UI_FILES_RELEASE_MARGIN    12U
#define UI_BROWSER_ROW_TOP         42U
#define UI_BROWSER_ROW_HEIGHT      27U

/* UI_WAV_RENAME_PATCH */
#define UI_RENAME_BUTTON_X          188U
#define UI_RENAME_BUTTON_Y          132U
#define UI_RENAME_BUTTON_WIDTH      120U
#define UI_RENAME_BUTTON_HEIGHT     54U
#define UI_RENAME_KEY_TOP           86U
#define UI_RENAME_KEY_WIDTH         40U
#define UI_RENAME_KEY_HEIGHT        25U
#define UI_RENAME_KEY_COUNT         37U

typedef enum
{
    UI_BUTTON_NONE = 0,
    UI_BUTTON_RUN_STOP,
    UI_BUTTON_FULL,
    UI_BUTTON_ZOOM_OUT,
    UI_BUTTON_ZOOM_IN,
    UI_BUTTON_PEAK,
    UI_BUTTON_RECORDER_BACK,
    UI_BUTTON_RECORD_5,
    UI_BUTTON_RECORD_STOP,
    UI_BUTTON_VIEW_WAVEFORM,
    UI_BUTTON_VIEW_SPECTRUM,
    UI_BUTTON_WAVEFORM_BACK,
    UI_BUTTON_WAVEFORM_RELOAD,
    UI_BUTTON_FFT_BACK,
    UI_BUTTON_FFT_PREVIOUS,
    UI_BUTTON_FFT_NEXT,
    UI_BUTTON_FFT_FULL,
    UI_BUTTON_FFT_PEAK,
    UI_BUTTON_BROWSER_BACK,
    UI_BUTTON_BROWSER_PREVIOUS,
    UI_BUTTON_BROWSER_NEXT,
    UI_BUTTON_BROWSER_SELECT,
    UI_BUTTON_BROWSER_REFRESH,
    UI_BUTTON_FILE_ACTIONS_BACK,
    UI_BUTTON_FILE_ACTIONS_WAVE,
    UI_BUTTON_FILE_ACTIONS_FFT,
    UI_BUTTON_FILE_ACTIONS_INFO,
    UI_BUTTON_FILE_ACTIONS_DELETE,
    UI_BUTTON_WAV_INFO_BACK,
    UI_BUTTON_DELETE_BACK,
    UI_BUTTON_DELETE_CANCEL,
    UI_BUTTON_DELETE_CONFIRM,
    UI_BUTTON_RENAME_BACK,
    UI_BUTTON_RENAME_CLEAR,
    UI_BUTTON_RENAME_BACKSPACE,
    UI_BUTTON_RENAME_CANCEL,
    UI_BUTTON_RENAME_SAVE
} UI_Button;

typedef enum
{
    UI_MODE_ANALYZER = 0,
    UI_MODE_RECORDER,
    UI_MODE_WAVEFORM,
    UI_MODE_RECORDED_FFT,
    UI_MODE_FILE_BROWSER,
    UI_MODE_FILE_ACTIONS,
    UI_MODE_WAV_INFO,
    UI_MODE_FILE_DELETE_CONFIRM,
    UI_MODE_FILE_RENAME
} UI_Mode;

typedef struct
{
    bool initialized;
    bool running;
    bool touch_active;
    bool dragging;
    bool graph_dirty;
    bool controls_dirty;

    UI_Mode mode;
    UI_Action pending_action;
    UI_RecorderStatus recorder_status;
    UI_WavInfo wav_info;
    char delete_filename[RECORDING_FILENAME_CAPACITY];
    char rename_source[RECORDING_FILENAME_CAPACITY];
    char rename_base[9];
    char rename_target[RECORDING_FILENAME_CAPACITY];
    uint32_t rename_length;
    int32_t pressed_rename_key;
    uint32_t last_recorder_draw_ms;
    uint32_t browser_row;
    uint32_t browser_pending_row;

    uint32_t first_bin;
    uint32_t visible_bins;
    uint32_t magnitude_max;

    uint16_t touch_start_x;
    uint16_t touch_start_y;
    uint16_t previous_touch_x;
    uint16_t previous_touch_y;

    uint32_t last_touch_poll_ms;
    uint32_t last_touch_action_ms;
    uint32_t released_poll_count;
    bool touch_armed;
    UI_Button pressed_button;
} UI_State;

static UI_State ui;

static void ui_draw_controls(void);
static void ui_draw_status(void);
static void ui_draw_recorder_body(void);
static void ui_draw_file_browser_body(void);
static void ui_draw_file_actions_body(void);
static void ui_draw_wav_info_body(void);
static void ui_draw_delete_confirmation_body(void);
static void ui_draw_rename_editor_body(void);
static int32_t ui_rename_key_at(uint16_t x, uint16_t y);
static void ui_rename_append_key(uint32_t key_index);
static void ui_draw_waveform_body(void);
static void ui_draw_uint(
    int32_t x,
    int32_t y,
    uint32_t value,
    uint16_t color);
static void ui_draw_grid(void);
static void ui_draw_spectrum_trace(void);
static void ui_draw_peak_marker(void);
static void ui_draw_button(int32_t x, uint16_t fill_color, uint16_t border_color);

static UI_Button ui_hit_test_button(uint16_t x, uint16_t y);
static void ui_execute_button(UI_Button button);
static void ui_handle_touch_down(const STMPE811_TouchData *touch);
static void ui_handle_touch_move(const STMPE811_TouchData *touch);
static void ui_handle_touch_up(void);

static void ui_pan_pixels(int32_t pixel_delta);
static void ui_scale_pixels(int32_t pixel_delta);
static void ui_apply_full_span(void);
static void ui_zoom_in(void);
static void ui_zoom_out(void);
static void ui_center_on_peak(void);
static void ui_clamp_view(void);

static int32_t ui_bin_to_x(uint32_t bin);
static int32_t ui_magnitude_to_y(uint32_t magnitude);
static uint32_t ui_last_usable_bin(void);
static uint32_t ui_full_visible_bins(void);
static uint32_t ui_last_visible_bin(void);
static uint32_t ui_absolute_difference(uint16_t a, uint16_t b);

void UI_Init(void)
{
    ui.initialized = true;
    ui.running = false;
    ui.touch_active = false;
    ui.dragging = false;
    ui.graph_dirty = true;
    ui.controls_dirty = true;
    ui.mode = UI_MODE_ANALYZER;
    ui.pending_action = UI_ACTION_NONE;
    memset(&ui.wav_info, 0, sizeof(ui.wav_info));
    ui.delete_filename[0] = '\0';
    ui.rename_source[0] = '\0';
    ui.rename_base[0] = '\0';
    ui.rename_target[0] = '\0';
    ui.rename_length = 0U;
    ui.pressed_rename_key = -1;
    ui.recorder_status.recorder_state = 0U;
    ui.recorder_status.captured_samples = 0U;
    ui.recorder_status.queued_bytes = 0U;
    ui.recorder_status.dropped_samples = 0U;
    ui.recorder_status.write_errors = 0U;
    ui.recorder_status.dma_overruns = 0U;
    ui.recorder_status.rms_counts = 0U;
    ui.recorder_status.clip_count = 0U;
    ui.recorder_status.clipping = false;
    ui.recorder_status.capture_active = false;
    ui.last_recorder_draw_ms = HAL_GetTick();
    ui.browser_row = 0U;
    ui.browser_pending_row = 0U;

    ui.first_bin = UI_FIRST_USABLE_BIN;
    ui.visible_bins = ui_full_visible_bins();
    ui.magnitude_max = UI_DEFAULT_MAGNITUDE_MAX;

    ui.touch_start_x = 0U;
    ui.touch_start_y = 0U;
    ui.previous_touch_x = 0U;
    ui.previous_touch_y = 0U;
    ui.last_touch_poll_ms = HAL_GetTick();
    ui.last_touch_action_ms = 0U;
    ui.released_poll_count = UI_TOUCH_RELEASE_POLLS;
    ui.touch_armed = true;
    ui.pressed_button = UI_BUTTON_NONE;

    Display_Clear(COLOR_BLACK);
    UI_Draw();
}

void UI_Draw(void)
{
    if (!ui.initialized)
    {
        return;
    }

    ui_draw_controls();
    UI_DrawSpectrum();
    ui_draw_status();

    ui.graph_dirty = false;
    ui.controls_dirty = false;
}

void UI_DrawSpectrum(void)
{
    if (!ui.initialized)
    {
        return;
    }

    if (ui.mode == UI_MODE_FILE_RENAME)
    {
        ui_draw_rename_editor_body();
        ui.graph_dirty = false;
        return;
    }

    if (ui.mode == UI_MODE_FILE_DELETE_CONFIRM)
    {
        ui_draw_delete_confirmation_body();
        ui.graph_dirty = false;
        return;
    }

    if (ui.mode == UI_MODE_WAV_INFO)
    {
        ui_draw_wav_info_body();
        ui.graph_dirty = false;
        return;
    }

    if (ui.mode == UI_MODE_FILE_ACTIONS)
    {
        ui_draw_file_actions_body();
        ui.graph_dirty = false;
        return;
    }

    if (ui.mode == UI_MODE_FILE_BROWSER)
    {
        ui_draw_file_browser_body();
        ui.graph_dirty = false;
        return;
    }

    if (ui.mode == UI_MODE_WAVEFORM)
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

    Display_FillRect(UI_GRAPH_LEFT,
                     UI_GRAPH_TOP,
                     UI_GRAPH_WIDTH,
                     UI_GRAPH_HEIGHT,
                     COLOR_BLACK);

    ui_draw_grid();

    if (FFT_IsValid())
    {
        ui_draw_spectrum_trace();
        ui_draw_peak_marker();
    }

    ui.graph_dirty = false;
}

void UI_ProcessTouch(void)
{
    STMPE811_TouchData touch = {0};
    uint32_t now;

    if (!ui.initialized)
    {
        return;
    }

    now = HAL_GetTick();

    if ((now - ui.last_touch_poll_ms) < UI_TOUCH_POLL_MS)
    {
        return;
    }

    ui.last_touch_poll_ms = now;

    if (STMPE811_ReadTouch(&touch) != HAL_OK)
    {
        return;
    }

    /* UI_TOUCH_X_MIRROR_PATCH
     *
     * The calibrated touchscreen X axis is reversed relative to the
     * 320-pixel landscape display. Convert it only in the UI layer.
     */
    if (touch.x < DISPLAY_WIDTH)
    {
        touch.x = (uint16_t)((DISPLAY_WIDTH - 1U) - touch.x);
    }

    if (touch.pressed)
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

    if (ui.controls_dirty)
    {
        ui_draw_controls();
        ui_draw_status();
        ui.controls_dirty = false;
    }

    if (ui.graph_dirty)
    {
        UI_DrawSpectrum();
        ui_draw_status();
    }
}

void UI_NotifySpectrumChanged(void)
{
    ui.graph_dirty = true;
}

UI_Action UI_GetPendingAction(void)
{
    UI_Action action = ui.pending_action;
    ui.pending_action = UI_ACTION_NONE;
    return action;
}

void UI_SetRecorderStatus(
    const UI_RecorderStatus *status)
{
    bool changed;

    if (status == NULL)
    {
        return;
    }

    /*
     * UI_REC_SCREEN_FLICKER_POLISH
     * Compare actual fields rather than structure padding.
     */
    changed =
        (ui.recorder_status.recorder_state !=
         status->recorder_state) ||
        (ui.recorder_status.captured_samples !=
         status->captured_samples) ||
        (ui.recorder_status.queued_bytes !=
         status->queued_bytes) ||
        (ui.recorder_status.dropped_samples !=
         status->dropped_samples) ||
        (ui.recorder_status.write_errors !=
         status->write_errors) ||
        (ui.recorder_status.dma_overruns !=
         status->dma_overruns) ||
        (ui.recorder_status.rms_counts !=
         status->rms_counts) ||
        (ui.recorder_status.clip_count !=
         status->clip_count) ||
        (ui.recorder_status.clipping !=
         status->clipping) ||
        (ui.recorder_status.capture_active !=
         status->capture_active);

    if (!changed)
    {
        return;
    }

    ui.recorder_status = *status;

    if (ui.mode == UI_MODE_RECORDER)
    {
        ui.graph_dirty = true;
    }
}
bool UI_IsRecorderScreen(void)
{
    return ui.mode == UI_MODE_RECORDER;
}

void UI_ShowRecorder(void)
{
    ui.mode = UI_MODE_RECORDER;
    ui.running = false;
    ui.graph_dirty = true;
    ui.controls_dirty = true;
    Display_Clear(COLOR_BLACK);
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

void UI_ShowFileBrowser(void)
{
    ui.mode = UI_MODE_FILE_BROWSER;
    ui.running = false;
    ui.browser_row = 0U;
    ui.browser_pending_row = 0U;
    ui.graph_dirty = true;
    ui.controls_dirty = true;
    Display_Clear(COLOR_BLACK);
}

bool UI_IsFileBrowserScreen(void)
{
    return ui.mode == UI_MODE_FILE_BROWSER;
}

void UI_ShowFileActions(void)
{
    ui.mode = UI_MODE_FILE_ACTIONS;
    ui.running = false;
    ui.graph_dirty = true;
    ui.controls_dirty = true;
    Display_Clear(COLOR_BLACK);
}

void UI_ReturnToFileBrowser(void)
{
    ui.mode = UI_MODE_FILE_BROWSER;
    ui.running = false;
    ui.graph_dirty = true;
    ui.controls_dirty = true;
    Display_Clear(COLOR_BLACK);
}

bool UI_IsFileActionsScreen(void)
{
    return ui.mode == UI_MODE_FILE_ACTIONS;
}

void UI_ShowWavInfo(const UI_WavInfo *info)
{
    if (info == NULL)
    {
        return;
    }

    ui.wav_info = *info;
    ui.mode = UI_MODE_WAV_INFO;
    ui.running = false;
    ui.graph_dirty = true;
    ui.controls_dirty = true;
    Display_Clear(COLOR_BLACK);
}

bool UI_IsWavInfoScreen(void)
{
    return ui.mode == UI_MODE_WAV_INFO;
}

void UI_ShowDeleteConfirmation(const char *filename)
{
    if (filename == NULL)
    {
        return;
    }

    strncpy(ui.delete_filename,
            filename,
            sizeof(ui.delete_filename) - 1U);
    ui.delete_filename[sizeof(ui.delete_filename) - 1U] = '\0';
    ui.mode = UI_MODE_FILE_DELETE_CONFIRM;
    ui.running = false;
    ui.graph_dirty = true;
    ui.controls_dirty = true;
    Display_Clear(COLOR_BLACK);
}

void UI_ReturnToFileActions(void)
{
    UI_ShowFileActions();
}

bool UI_IsDeleteConfirmationScreen(void)
{
    return ui.mode == UI_MODE_FILE_DELETE_CONFIRM;
}

const char *UI_GetDeleteFilename(void)
{
    return ui.delete_filename;
}

void UI_ShowRenameEditor(const char *filename)
{
    const char *dot;
    size_t base_length;

    if (filename == NULL) return;

    strncpy(ui.rename_source, filename,
            sizeof(ui.rename_source) - 1U);
    ui.rename_source[sizeof(ui.rename_source) - 1U] = '\0';

    dot = strchr(filename, '.');
    base_length = (dot != NULL) ? (size_t)(dot - filename) : strlen(filename);
    if (base_length > 8U) base_length = 8U;
    memcpy(ui.rename_base, filename, base_length);
    ui.rename_base[base_length] = '\0';
    ui.rename_length = (uint32_t)base_length;
    ui.rename_target[0] = '\0';
    ui.pressed_rename_key = -1;
    ui.mode = UI_MODE_FILE_RENAME;
    ui.running = false;
    ui.graph_dirty = true;
    ui.controls_dirty = true;
    Display_Clear(COLOR_BLACK);
}

bool UI_IsRenameEditorScreen(void)
{
    return ui.mode == UI_MODE_FILE_RENAME;
}

const char *UI_GetRenameSource(void)
{
    return ui.rename_source;
}

const char *UI_GetRenameTarget(void)
{
    if (ui.rename_length == 0U)
    {
        ui.rename_target[0] = '\0';
        return ui.rename_target;
    }

    memcpy(ui.rename_target, ui.rename_base, ui.rename_length);
    strcpy(&ui.rename_target[ui.rename_length], ".WAV");
    return ui.rename_target;
}

void UI_RefreshFileBrowser(void)
{
    uint32_t count = RecordingCatalog_GetPageEntryCount();

    if (count == 0U)
    {
        ui.browser_row = 0U;
    }
    else if (ui.browser_row >= count)
    {
        ui.browser_row = count - 1U;
    }

    ui.browser_pending_row = ui.browser_row;
    ui.graph_dirty = true;
    ui.controls_dirty = true;
}

uint32_t UI_GetBrowserPendingRow(void)
{
    return ui.browser_pending_row;
}

void UI_SetRunning(bool running)
{
    if (ui.running != running)
    {
        ui.running = running;
        ui.controls_dirty = true;
    }
}

bool UI_IsRunning(void)
{
    return ui.running;
}

void UI_FullSpan(void)
{
    ui_apply_full_span();
    ui.graph_dirty = true;
}

void UI_OnFFTConfigurationChanged(void)
{
    ui.first_bin = UI_FIRST_USABLE_BIN;
    ui.visible_bins = ui_full_visible_bins();
    ui_clamp_view();
    ui.graph_dirty = true;
    ui.controls_dirty = true;
}

void UI_ZoomIn(void)
{
    ui_zoom_in();
    ui.graph_dirty = true;
}

void UI_ZoomOut(void)
{
    ui_zoom_out();
    ui.graph_dirty = true;
}

void UI_CenterOnPeak(void)
{
    ui_center_on_peak();
    ui.graph_dirty = true;
}

uint32_t UI_GetFirstBin(void)
{
    return ui.first_bin;
}

uint32_t UI_GetVisibleBins(void)
{
    return ui.visible_bins;
}

uint32_t UI_GetMagnitudeMax(void)
{
    return ui.magnitude_max;
}

static void ui_draw_controls(void)
{
    uint16_t run_color = ui.running ? COLOR_GREEN : COLOR_RED;

    if (ui.mode == UI_MODE_FILE_RENAME)
    {
        Display_FillRect(0, UI_CONTROL_TOP, DISPLAY_WIDTH,
                         UI_CONTROL_HEIGHT, COLOR_BLACK);
        ui_draw_button(0, COLOR_BLUE, COLOR_WHITE);
        ui_draw_button(64, COLOR_CYAN, COLOR_WHITE);
        ui_draw_button(128, COLOR_YELLOW, COLOR_WHITE);
        ui_draw_button(192, COLOR_MAGENTA, COLOR_WHITE);
        ui_draw_button(256, COLOR_GREEN, COLOR_WHITE);
        Display_DrawString(22, 12, "BACK", COLOR_BLACK, COLOR_BLUE);
        Display_DrawString(74, 12, "CLEAR", COLOR_BLACK, COLOR_CYAN);
        Display_DrawString(148, 12, "BKSP", COLOR_BLACK, COLOR_YELLOW);
        Display_DrawString(200, 12, "CANCEL", COLOR_BLACK, COLOR_MAGENTA);
        Display_DrawString(274, 12, "SAVE", COLOR_BLACK, COLOR_GREEN);
        return;
    }

    if (ui.mode == UI_MODE_FILE_DELETE_CONFIRM)
    {
        Display_FillRect(0, UI_CONTROL_TOP, DISPLAY_WIDTH,
                         UI_CONTROL_HEIGHT, COLOR_BLACK);
        ui_draw_button(0, COLOR_BLUE, COLOR_WHITE);
        ui_draw_button(64, COLOR_CYAN, COLOR_WHITE);
        ui_draw_button(128, COLOR_DARK_GRAY, COLOR_GRAY);
        ui_draw_button(192, COLOR_DARK_GRAY, COLOR_GRAY);
        ui_draw_button(256, COLOR_RED, COLOR_WHITE);
        Display_DrawString(22, 12, "BACK", COLOR_BLACK, COLOR_BLUE);
        Display_DrawString(72, 12, "CANCEL", COLOR_BLACK, COLOR_CYAN);
        Display_DrawString(260, 12, "CONFIRM", COLOR_WHITE, COLOR_RED);
        return;
    }

    if (ui.mode == UI_MODE_WAV_INFO)
    {
        Display_FillRect(0, UI_CONTROL_TOP, DISPLAY_WIDTH,
                         UI_CONTROL_HEIGHT, COLOR_BLACK);
        ui_draw_button(0, COLOR_BLUE, COLOR_WHITE);
        ui_draw_button(64, COLOR_DARK_GRAY, COLOR_GRAY);
        ui_draw_button(128, COLOR_DARK_GRAY, COLOR_GRAY);
        ui_draw_button(192, COLOR_DARK_GRAY, COLOR_GRAY);
        ui_draw_button(256, COLOR_DARK_GRAY, COLOR_GRAY);
        Display_DrawString(22, 12, "BACK", COLOR_BLACK, COLOR_BLUE);
        Display_DrawString(92, 12, "WAV FILE INFO", COLOR_WHITE, COLOR_BLACK);
        return;
    }

    if (ui.mode == UI_MODE_FILE_ACTIONS)
    {
        Display_FillRect(0, UI_CONTROL_TOP, DISPLAY_WIDTH,
                         UI_CONTROL_HEIGHT, COLOR_BLACK);
        ui_draw_button(0, COLOR_BLUE, COLOR_WHITE);
        ui_draw_button(64, COLOR_CYAN, COLOR_WHITE);
        ui_draw_button(128, COLOR_GREEN, COLOR_WHITE);
        ui_draw_button(192, COLOR_YELLOW, COLOR_WHITE);
        ui_draw_button(256, COLOR_RED, COLOR_WHITE);
        Display_DrawString(22, 12, "BACK", COLOR_BLACK, COLOR_BLUE);
        Display_DrawString(80, 12, "WAVE", COLOR_BLACK, COLOR_CYAN);
        Display_DrawString(150, 12, "FFT", COLOR_BLACK, COLOR_GREEN);
        Display_DrawString(208, 12, "INFO", COLOR_BLACK, COLOR_YELLOW);
        Display_DrawString(270, 12, "DELETE", COLOR_BLACK, COLOR_RED);
        return;
    }

    if (ui.mode == UI_MODE_FILE_BROWSER)
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
        Display_DrawString(208, 12, "OPEN", COLOR_BLACK, COLOR_YELLOW);
        Display_DrawString(264, 12, "REFRESH", COLOR_BLACK, COLOR_MAGENTA);
        return;
    }

    if (ui.mode == UI_MODE_RECORDED_FFT)
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
        Display_FillRect(0,
                         UI_CONTROL_TOP,
                         DISPLAY_WIDTH,
                         UI_CONTROL_HEIGHT,
                         COLOR_BLACK);

        ui_draw_button(0, COLOR_BLUE, COLOR_WHITE);
        ui_draw_button(64, COLOR_GREEN, COLOR_WHITE);
        ui_draw_button(128, COLOR_RED, COLOR_WHITE);
        ui_draw_button(192, COLOR_CYAN, COLOR_WHITE);
        ui_draw_button(256, COLOR_MAGENTA, COLOR_WHITE);

        Display_DrawString(22, 12, "BACK", COLOR_BLACK, COLOR_BLUE);
        Display_DrawString(86, 12, "REC5", COLOR_BLACK, COLOR_GREEN);
        Display_DrawString(150, 12, "STOP", COLOR_BLACK, COLOR_RED);
        Display_DrawString(214, 12, "WAVE", COLOR_BLACK, COLOR_CYAN);
        Display_DrawString(278, 12, "FFT", COLOR_BLACK, COLOR_MAGENTA);
        return;
    }

    Display_FillRect(0,
                     UI_CONTROL_TOP,
                     DISPLAY_WIDTH,
                     UI_CONTROL_HEIGHT,
                     COLOR_BLACK);

    ui_draw_button(UI_RUN_X, run_color, COLOR_WHITE);
    ui_draw_button(UI_FULL_X, COLOR_CYAN, COLOR_WHITE);
    ui_draw_button(UI_ZOOM_OUT_X, COLOR_BLUE, COLOR_WHITE);
    ui_draw_button(UI_ZOOM_IN_X, COLOR_YELLOW, COLOR_WHITE);
    ui_draw_button(UI_PEAK_X, COLOR_MAGENTA, COLOR_WHITE);

    if (ui.running)
    {
        Display_FillRect(UI_RUN_X + 24, 8, 6, 16, COLOR_BLACK);
        Display_FillRect(UI_RUN_X + 34, 8, 6, 16, COLOR_BLACK);
    }
    else
    {
        Display_FillRect(UI_RUN_X + 24, 8, 16, 16, COLOR_BLACK);
    }

    Display_DrawRect(UI_FULL_X + 19, 7, 26, 18, COLOR_BLACK);

    Display_FillRect(UI_ZOOM_OUT_X + 21, 14, 22, 4, COLOR_WHITE);

    Display_FillRect(UI_ZOOM_IN_X + 21, 14, 22, 4, COLOR_BLACK);
    Display_FillRect(UI_ZOOM_IN_X + 30, 5, 4, 22, COLOR_BLACK);

    Display_DrawLine(UI_PEAK_X + 19, 23,
                     UI_PEAK_X + 31, 7,
                     COLOR_WHITE);
    Display_DrawLine(UI_PEAK_X + 31, 7,
                     UI_PEAK_X + 44, 23,
                     COLOR_WHITE);
}

static void ui_draw_button(int32_t x,
                           uint16_t fill_color,
                           uint16_t border_color)
{
    Display_FillRect(x + 1,
                     UI_CONTROL_TOP + 1,
                     UI_BUTTON_WIDTH - 2,
                     UI_CONTROL_HEIGHT - 2,
                     fill_color);

    Display_DrawRect(x,
                     UI_CONTROL_TOP,
                     UI_BUTTON_WIDTH,
                     UI_CONTROL_HEIGHT,
                     border_color);
}

static void ui_draw_uint(
    int32_t x,
    int32_t y,
    uint32_t value,
    uint16_t color)
{
    char buffer[11];
    uint32_t index = sizeof(buffer) - 1U;

    buffer[index] = '\0';

    do
    {
        index--;
        buffer[index] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    while ((value != 0U) && (index != 0U));

    Display_DrawString(x,
                       y,
                       &buffer[index],
                       color,
                       COLOR_BLACK);
}

static void ui_draw_recorder_body(void)
{
    const char *state_text = "UNKNOWN";
    uint32_t elapsed_ms =
        ui.recorder_status.captured_samples / 48U;

    switch (ui.recorder_status.recorder_state)
    {
        case 0U:
            state_text = "IDLE";
            break;
        case 1U:
            state_text = "RECORDING";
            break;
        case 2U:
            state_text = "STOPPING";
            break;
        case 3U:
            state_text = "ERROR";
            break;
        default:
            break;
    }

    Display_FillRect(UI_GRAPH_LEFT,
                     UI_GRAPH_TOP,
                     UI_GRAPH_WIDTH,
                     UI_GRAPH_HEIGHT,
                     COLOR_BLACK);
    Display_DrawRect(UI_GRAPH_LEFT,
                     UI_GRAPH_TOP,
                     UI_GRAPH_WIDTH,
                     UI_GRAPH_HEIGHT,
                     COLOR_GRAY);

    Display_DrawString(8, 42, "WAV RECORDER", COLOR_CYAN, COLOR_BLACK);
    Display_DrawString(8, 62, "STATE", COLOR_WHITE, COLOR_BLACK);
    Display_DrawString(92, 62, state_text, COLOR_GREEN, COLOR_BLACK);

    Display_DrawString(8, 82, "ELAPSED MS", COLOR_WHITE, COLOR_BLACK);
    ui_draw_uint(92, 82, elapsed_ms, COLOR_YELLOW);

    Display_DrawString(8, 102, "SAMPLES", COLOR_WHITE, COLOR_BLACK);
    ui_draw_uint(92, 102,
                 ui.recorder_status.captured_samples,
                 COLOR_YELLOW);

    Display_DrawString(8, 122, "QUEUE", COLOR_WHITE, COLOR_BLACK);
    ui_draw_uint(92, 122,
                 ui.recorder_status.queued_bytes,
                 COLOR_YELLOW);

    Display_DrawString(8, 142, "DROPPED", COLOR_WHITE, COLOR_BLACK);
    ui_draw_uint(92, 142,
                 ui.recorder_status.dropped_samples,
                 COLOR_YELLOW);

    Display_DrawString(8, 162, "DMA OVR", COLOR_WHITE, COLOR_BLACK);
    ui_draw_uint(92, 162,
                 ui.recorder_status.dma_overruns,
                 COLOR_YELLOW);

    Display_DrawString(8, 182, "WRITE ERR", COLOR_WHITE, COLOR_BLACK);
    ui_draw_uint(92, 182,
                 ui.recorder_status.write_errors,
                 COLOR_YELLOW);

    Display_DrawString(188, 62,
                       ui.recorder_status.capture_active ?
                       "CAPTURE ON" : "CAPTURE OFF",
                       ui.recorder_status.capture_active ?
                       COLOR_RED : COLOR_GREEN,
                       COLOR_BLACK);

    Display_DrawString(188, 82, "SELECTED WAV", COLOR_WHITE, COLOR_BLACK);
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


    Display_FillRect(UI_FILES_X, UI_FILES_Y,
                     UI_FILES_WIDTH, UI_FILES_HEIGHT, COLOR_BLUE);
    Display_DrawRect(UI_FILES_X, UI_FILES_Y,
                     UI_FILES_WIDTH, UI_FILES_HEIGHT, COLOR_WHITE);
    Display_DrawString(218, 178, "FILES", COLOR_BLACK, COLOR_BLUE);
}

static void ui_draw_delete_confirmation_body(void)
{
    FILINFO information;
    FRESULT result;

    Display_FillRect(UI_GRAPH_LEFT, UI_GRAPH_TOP,
                     UI_GRAPH_WIDTH, UI_GRAPH_HEIGHT, COLOR_BLACK);
    Display_DrawRect(UI_GRAPH_LEFT, UI_GRAPH_TOP,
                     UI_GRAPH_WIDTH, UI_GRAPH_HEIGHT, COLOR_RED);

    Display_DrawString(66, 48, "DELETE WAV FILE?",
                       COLOR_RED, COLOR_BLACK);
    Display_DrawString(94, 82, ui.delete_filename,
                       COLOR_GREEN, COLOR_BLACK);

    result = f_stat(ui.delete_filename, &information);
    if (result == FR_OK)
    {
        ui_draw_uint(92, 110, (uint32_t)information.fsize, COLOR_YELLOW);
        Display_DrawString(176, 110, "BYTES", COLOR_GRAY, COLOR_BLACK);
    }
    else
    {
        Display_DrawString(82, 110, "FILE NOT FOUND",
                           COLOR_RED, COLOR_BLACK);
    }

    Display_DrawString(54, 148, "THIS CANNOT BE UNDONE",
                       COLOR_YELLOW, COLOR_BLACK);
    Display_DrawString(44, 184, "PRESS CONFIRM TO DELETE",
                       COLOR_WHITE, COLOR_BLACK);
}

static void ui_draw_wav_info_body(void)
{
    Display_FillRect(UI_GRAPH_LEFT, UI_GRAPH_TOP,
                     UI_GRAPH_WIDTH, UI_GRAPH_HEIGHT, COLOR_BLACK);
    Display_DrawRect(UI_GRAPH_LEFT, UI_GRAPH_TOP,
                     UI_GRAPH_WIDTH, UI_GRAPH_HEIGHT, COLOR_GRAY);

    Display_DrawString(8, 40, "FILE", COLOR_WHITE, COLOR_BLACK);
    Display_DrawString(86, 40, ui.wav_info.filename,
                       COLOR_GREEN, COLOR_BLACK);
    Display_DrawString(8, 62, "SIZE", COLOR_WHITE, COLOR_BLACK);
    ui_draw_uint(86, 62, ui.wav_info.file_size, COLOR_YELLOW);
    Display_DrawString(174, 62, "BYTES", COLOR_GRAY, COLOR_BLACK);
    Display_DrawString(8, 84, "RATE", COLOR_WHITE, COLOR_BLACK);
    ui_draw_uint(86, 84, ui.wav_info.sample_rate, COLOR_YELLOW);
    Display_DrawString(174, 84, "HZ", COLOR_GRAY, COLOR_BLACK);
    Display_DrawString(8, 106, "CHANNELS", COLOR_WHITE, COLOR_BLACK);
    ui_draw_uint(86, 106, ui.wav_info.channels, COLOR_YELLOW);
    Display_DrawString(8, 128, "BITS", COLOR_WHITE, COLOR_BLACK);
    ui_draw_uint(86, 128, ui.wav_info.bits_per_sample, COLOR_YELLOW);
    Display_DrawString(8, 150, "DATA", COLOR_WHITE, COLOR_BLACK);
    ui_draw_uint(86, 150, ui.wav_info.data_bytes, COLOR_YELLOW);
    Display_DrawString(174, 150, "BYTES", COLOR_GRAY, COLOR_BLACK);
    Display_DrawString(8, 172, "DURATION", COLOR_WHITE, COLOR_BLACK);
    ui_draw_uint(86, 172, ui.wav_info.duration_ms, COLOR_YELLOW);
    Display_DrawString(174, 172, "MS", COLOR_GRAY, COLOR_BLACK);
    Display_DrawString(8, 194, "FORMAT", COLOR_WHITE, COLOR_BLACK);
    Display_DrawString(86, 194,
                       ui.wav_info.valid ? "VALID PCM WAV" : "INVALID WAV",
                       ui.wav_info.valid ? COLOR_GREEN : COLOR_RED,
                       COLOR_BLACK);
}

static int32_t ui_rename_key_at(uint16_t x, uint16_t y)
{
    uint32_t row;
    uint32_t column;
    uint32_t index;

    if ((y < UI_RENAME_KEY_TOP) ||
        (y >= (UI_RENAME_KEY_TOP + (5U * UI_RENAME_KEY_HEIGHT))))
    {
        return -1;
    }

    row = (y - UI_RENAME_KEY_TOP) / UI_RENAME_KEY_HEIGHT;
    column = x / UI_RENAME_KEY_WIDTH;
    index = (row * 8U) + column;
    return (index < UI_RENAME_KEY_COUNT) ? (int32_t)index : -1;
}

static void ui_rename_append_key(uint32_t key_index)
{
    static const char keys[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_";

    if ((key_index >= UI_RENAME_KEY_COUNT) ||
        (ui.rename_length >= 8U))
    {
        return;
    }

    ui.rename_base[ui.rename_length++] = keys[key_index];
    ui.rename_base[ui.rename_length] = '\0';
    ui.graph_dirty = true;
}

static void ui_draw_rename_editor_body(void)
{
    static const char keys[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_";
    char display_name[RECORDING_FILENAME_CAPACITY];

    Display_FillRect(UI_GRAPH_LEFT, UI_GRAPH_TOP,
                     UI_GRAPH_WIDTH, UI_GRAPH_HEIGHT, COLOR_BLACK);
    Display_DrawRect(UI_GRAPH_LEFT, UI_GRAPH_TOP,
                     UI_GRAPH_WIDTH, UI_GRAPH_HEIGHT, COLOR_MAGENTA);

    Display_DrawString(6, 38, "CURRENT", COLOR_WHITE, COLOR_BLACK);
    Display_DrawString(76, 38, ui.rename_source, COLOR_GREEN, COLOR_BLACK);
    Display_DrawString(6, 60, "NEW", COLOR_WHITE, COLOR_BLACK);
    if (ui.rename_length != 0U)
    {
        memcpy(display_name, ui.rename_base, ui.rename_length);
        strcpy(&display_name[ui.rename_length], ".WAV");
    }
    else
    {
        strcpy(display_name, "_.WAV");
    }
    Display_DrawString(76, 60, display_name, COLOR_YELLOW, COLOR_BLACK);

    for (uint32_t index = 0U; index < UI_RENAME_KEY_COUNT; index++)
    {
        uint32_t row = index / 8U;
        uint32_t column = index % 8U;
        int32_t x = (int32_t)(column * UI_RENAME_KEY_WIDTH);
        int32_t y = (int32_t)(UI_RENAME_KEY_TOP +
                              (row * UI_RENAME_KEY_HEIGHT));
        char label[2] = { keys[index], '\0' };

        Display_FillRect(x + 1, y + 1,
                         UI_RENAME_KEY_WIDTH - 2U,
                         UI_RENAME_KEY_HEIGHT - 2U,
                         COLOR_DARK_GRAY);
        Display_DrawRect(x, y,
                         UI_RENAME_KEY_WIDTH,
                         UI_RENAME_KEY_HEIGHT,
                         COLOR_WHITE);
        Display_DrawString(x + 16, y + 9, label,
                           COLOR_WHITE, COLOR_DARK_GRAY);
    }
}

static void ui_draw_file_actions_body(void)
{
    const char *selected = RecordingCatalog_GetSelected();
    FILINFO information;
    FRESULT result;

    Display_FillRect(UI_GRAPH_LEFT, UI_GRAPH_TOP,
                     UI_GRAPH_WIDTH, UI_GRAPH_HEIGHT, COLOR_BLACK);
    Display_DrawRect(UI_GRAPH_LEFT, UI_GRAPH_TOP,
                     UI_GRAPH_WIDTH, UI_GRAPH_HEIGHT, COLOR_GRAY);
    Display_DrawString(10, 44, "SELECTED WAV", COLOR_CYAN, COLOR_BLACK);
    Display_DrawString(10, 68, selected, COLOR_GREEN, COLOR_BLACK);

    result = f_stat(selected, &information);
    if (result == FR_OK)
    {
        Display_DrawString(10, 96, "SIZE", COLOR_WHITE, COLOR_BLACK);
        ui_draw_uint(58, 96, (uint32_t)information.fsize, COLOR_YELLOW);
        Display_DrawString(140, 96, "BYTES", COLOR_GRAY, COLOR_BLACK);
    }
    else
    {
        Display_DrawString(10, 96, "FILE NOT FOUND", COLOR_RED, COLOR_BLACK);
    }

    Display_DrawString(10, 128, "WAVE", COLOR_CYAN, COLOR_BLACK);
    Display_DrawString(10, 150, "FFT", COLOR_GREEN, COLOR_BLACK);
    Display_DrawString(10, 172, "INFO", COLOR_YELLOW, COLOR_BLACK);
    Display_DrawString(10, 194, "DELETE", COLOR_RED, COLOR_BLACK);
    Display_DrawString(66, 194, "CONFIRM", COLOR_GRAY, COLOR_BLACK);

    Display_FillRect(UI_RENAME_BUTTON_X, UI_RENAME_BUTTON_Y,
                     UI_RENAME_BUTTON_WIDTH, UI_RENAME_BUTTON_HEIGHT,
                     COLOR_MAGENTA);
    Display_DrawRect(UI_RENAME_BUTTON_X, UI_RENAME_BUTTON_Y,
                     UI_RENAME_BUTTON_WIDTH, UI_RENAME_BUTTON_HEIGHT,
                     COLOR_WHITE);
    Display_DrawString(220, 151, "RENAME", COLOR_BLACK, COLOR_MAGENTA);
    Display_DrawString(202, 171, "EDIT WAV NAME", COLOR_BLACK, COLOR_MAGENTA);
}

static void ui_draw_file_browser_body(void)
{
    uint32_t entry_count = RecordingCatalog_GetPageEntryCount();

    Display_FillRect(UI_GRAPH_LEFT, UI_GRAPH_TOP,
                     UI_GRAPH_WIDTH, UI_GRAPH_HEIGHT, COLOR_BLACK);
    Display_DrawRect(UI_GRAPH_LEFT, UI_GRAPH_TOP,
                     UI_GRAPH_WIDTH, UI_GRAPH_HEIGHT, COLOR_GRAY);

    for (uint32_t row = 0U; row < RECORDING_BROWSER_PAGE_SIZE; row++)
    {
        int32_t y = (int32_t)UI_BROWSER_ROW_TOP +
                    ((int32_t)row * (int32_t)UI_BROWSER_ROW_HEIGHT);
        uint16_t background =
            (row == ui.browser_row) ? COLOR_BLUE : COLOR_BLACK;
        uint16_t border =
            (row == ui.browser_row) ? COLOR_WHITE : COLOR_DARK_GRAY;
        const RecordingCatalogEntry *entry =
            RecordingCatalog_GetPageEntry(row);

        Display_FillRect(4, y, 312, UI_BROWSER_ROW_HEIGHT - 2U,
                         background);
        Display_DrawRect(4, y, 312, UI_BROWSER_ROW_HEIGHT - 2U,
                         border);

        if ((row < entry_count) && (entry != NULL))
        {
            Display_DrawString(12, y + 9, entry->filename,
                               COLOR_WHITE, background);
            ui_draw_uint(160, y + 9, entry->file_size, COLOR_YELLOW);
            Display_DrawString(240, y + 9, "BYTES",
                               COLOR_GRAY, background);
        }
    }
}

static void ui_draw_waveform_body(void)
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
    Display_DrawString(8, 38, "SELECTED WAV", COLOR_CYAN, COLOR_BLACK);

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

static void ui_draw_grid(void)
{
    int32_t quarter_height = UI_GRAPH_HEIGHT / 4;
    int32_t quarter_width = (int32_t)UI_GRAPH_WIDTH / 4;

    Display_DrawRect(UI_GRAPH_LEFT,
                     UI_GRAPH_TOP,
                     UI_GRAPH_WIDTH,
                     UI_GRAPH_HEIGHT,
                     COLOR_GRAY);

    for (int32_t y = UI_GRAPH_TOP + quarter_height;
         y < UI_GRAPH_BOTTOM;
         y += quarter_height)
    {
        Display_DrawLine(UI_GRAPH_LEFT, y,
                         UI_GRAPH_RIGHT, y,
                         COLOR_DARK_GRAY);
    }

    for (int32_t x = UI_GRAPH_LEFT + quarter_width;
         x < UI_GRAPH_RIGHT;
         x += quarter_width)
    {
        Display_DrawLine(x, UI_GRAPH_TOP,
                         x, UI_GRAPH_BOTTOM,
                         COLOR_DARK_GRAY);
    }
}

static void ui_draw_spectrum_trace(void)
{
    uint32_t first_bin = ui.first_bin;
    uint32_t last_bin = ui_last_visible_bin();

    if (last_bin <= first_bin)
    {
        return;
    }

    for (uint32_t bin = first_bin + 1U; bin <= last_bin; bin++)
    {
        int32_t x0 = ui_bin_to_x(bin - 1U);
        int32_t y0 = ui_magnitude_to_y(FFT_GetMagnitude(bin - 1U));
        int32_t x1 = ui_bin_to_x(bin);
        int32_t y1 = ui_magnitude_to_y(FFT_GetMagnitude(bin));

        Display_DrawLine(x0, y0, x1, y1, COLOR_GREEN);
    }
}

static void ui_draw_peak_marker(void)
{
    uint32_t peak_bin = FFT_GetPeakBin();
    uint32_t last_bin = ui_last_visible_bin();
    int32_t peak_x;
    int32_t peak_y;
    int32_t marker_top;

    if ((peak_bin < ui.first_bin) || (peak_bin > last_bin))
    {
        return;
    }

    peak_x = ui_bin_to_x(peak_bin);
    peak_y = ui_magnitude_to_y(FFT_GetMagnitude(peak_bin));
    marker_top = peak_y - 10;

    if (marker_top < UI_GRAPH_TOP)
    {
        marker_top = UI_GRAPH_TOP;
    }

    Display_DrawLine(peak_x, marker_top,
                     peak_x, peak_y,
                     COLOR_RED);
    Display_DrawLine(peak_x - 3, marker_top,
                     peak_x + 3, marker_top,
                     COLOR_RED);
}

static void ui_draw_status(void)
{
    uint32_t first_bin = ui.first_bin;

    if (ui.mode == UI_MODE_FILE_RENAME)
    {
        Display_FillRect(0, UI_STATUS_TOP, DISPLAY_WIDTH,
                         UI_STATUS_HEIGHT, COLOR_BLACK);
        Display_DrawRect(0, UI_STATUS_TOP, DISPLAY_WIDTH,
                         UI_STATUS_HEIGHT, COLOR_MAGENTA);
        Display_DrawString(4, UI_STATUS_TOP + 8,
                           "RENAME", COLOR_WHITE, COLOR_BLACK);
        Display_DrawString(68, UI_STATUS_TOP + 8,
                           "MAX 8 CHARACTERS", COLOR_YELLOW, COLOR_BLACK);
        return;
    }

    if (ui.mode == UI_MODE_FILE_DELETE_CONFIRM)
    {
        Display_FillRect(0, UI_STATUS_TOP, DISPLAY_WIDTH,
                         UI_STATUS_HEIGHT, COLOR_BLACK);
        Display_DrawRect(0, UI_STATUS_TOP, DISPLAY_WIDTH,
                         UI_STATUS_HEIGHT, COLOR_RED);
        Display_DrawString(4, UI_STATUS_TOP + 8,
                           "DELETE TARGET", COLOR_WHITE, COLOR_BLACK);
        Display_DrawString(112, UI_STATUS_TOP + 8,
                           ui.delete_filename, COLOR_RED, COLOR_BLACK);
        return;
    }

    if (ui.mode == UI_MODE_WAV_INFO)
    {
        Display_FillRect(0, UI_STATUS_TOP, DISPLAY_WIDTH,
                         UI_STATUS_HEIGHT, COLOR_BLACK);
        Display_DrawRect(0, UI_STATUS_TOP, DISPLAY_WIDTH,
                         UI_STATUS_HEIGHT, COLOR_GRAY);
        Display_DrawString(4, UI_STATUS_TOP + 8,
                           "WAV INFO", COLOR_WHITE, COLOR_BLACK);
        Display_DrawString(88, UI_STATUS_TOP + 8,
                           ui.wav_info.valid ? "VALID" : "INVALID",
                           ui.wav_info.valid ? COLOR_GREEN : COLOR_RED,
                           COLOR_BLACK);
        return;
    }

    if (ui.mode == UI_MODE_FILE_ACTIONS)
    {
        Display_FillRect(0, UI_STATUS_TOP, DISPLAY_WIDTH,
                         UI_STATUS_HEIGHT, COLOR_BLACK);
        Display_DrawRect(0, UI_STATUS_TOP, DISPLAY_WIDTH,
                         UI_STATUS_HEIGHT, COLOR_GRAY);
        Display_DrawString(4, UI_STATUS_TOP + 8,
                           "SELECTED", COLOR_WHITE, COLOR_BLACK);
        Display_DrawString(70, UI_STATUS_TOP + 8,
                           RecordingCatalog_GetSelected(),
                           COLOR_GREEN, COLOR_BLACK);
        return;
    }

    if (ui.mode == UI_MODE_FILE_BROWSER)
    {
        Display_FillRect(0, UI_STATUS_TOP, DISPLAY_WIDTH,
                         UI_STATUS_HEIGHT, COLOR_BLACK);
        Display_DrawRect(0, UI_STATUS_TOP, DISPLAY_WIDTH,
                         UI_STATUS_HEIGHT, COLOR_GRAY);
        Display_DrawString(4, UI_STATUS_TOP + 8,
                           "PAGE", COLOR_WHITE, COLOR_BLACK);
        ui_draw_uint(40, UI_STATUS_TOP + 8,
                     RecordingCatalog_GetPageIndex() + 1U, COLOR_YELLOW);
        Display_DrawString(60, UI_STATUS_TOP + 8,
                           "/", COLOR_WHITE, COLOR_BLACK);
        ui_draw_uint(70, UI_STATUS_TOP + 8,
                     RecordingCatalog_GetPageCount(), COLOR_YELLOW);
        Display_DrawString(110, UI_STATUS_TOP + 8,
                           "SELECTED", COLOR_WHITE, COLOR_BLACK);
        Display_DrawString(176, UI_STATUS_TOP + 8,
                           RecordingCatalog_GetSelected(),
                           COLOR_GREEN, COLOR_BLACK);
        return;
    }

    if (ui.mode == UI_MODE_RECORDED_FFT)
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
        Display_FillRect(0,
                         UI_STATUS_TOP,
                         DISPLAY_WIDTH,
                         UI_STATUS_HEIGHT,
                         COLOR_BLACK);
        Display_DrawRect(0,
                         UI_STATUS_TOP,
                         DISPLAY_WIDTH,
                         UI_STATUS_HEIGHT,
                         COLOR_GRAY);
        Display_DrawString(4,
                           UI_STATUS_TOP + 8,
                           "RECORDER MODE",
                           COLOR_WHITE,
                           COLOR_BLACK);
        return;
    }
    uint32_t last_bin = ui_last_visible_bin();
    int32_t view_x0;
    int32_t view_x1;

    Display_FillRect(0,
                     UI_STATUS_TOP,
                     DISPLAY_WIDTH,
                     UI_STATUS_HEIGHT,
                     COLOR_BLACK);

    Display_DrawRect(0,
                     UI_STATUS_TOP,
                     DISPLAY_WIDTH,
                     UI_STATUS_HEIGHT,
                     COLOR_GRAY);

    view_x0 = (int32_t)(((first_bin - UI_FIRST_USABLE_BIN) *
                         (DISPLAY_WIDTH - 1U)) /
                        (ui_full_visible_bins() - 1U));

    view_x1 = (int32_t)(((last_bin - UI_FIRST_USABLE_BIN) *
                         (DISPLAY_WIDTH - 1U)) /
                        (ui_full_visible_bins() - 1U));

    Display_FillRect(view_x0,
                     UI_STATUS_TOP + 8,
                     (view_x1 - view_x0) + 1,
                     8,
                     COLOR_WHITE);

    if (FFT_IsValid())
    {
        uint32_t peak_bin = FFT_GetPeakBin();

        if ((peak_bin >= UI_FIRST_USABLE_BIN) &&
            (peak_bin <= ui_last_usable_bin()))
        {
            int32_t peak_x = (int32_t)(
                ((peak_bin - UI_FIRST_USABLE_BIN) *
                 (DISPLAY_WIDTH - 1U)) /
                (ui_full_visible_bins() - 1U));

            Display_FillRect(peak_x - 1,
                             UI_STATUS_TOP + 5,
                             3,
                             14,
                             COLOR_GREEN);
        }
    }

    Display_FillRect(UI_REC_TAB_X,
                     UI_STATUS_TOP + 1,
                     UI_REC_TAB_WIDTH - 1,
                     UI_STATUS_HEIGHT - 2,
                     COLOR_RED);
    Display_DrawRect(UI_REC_TAB_X,
                     UI_STATUS_TOP,
                     UI_REC_TAB_WIDTH,
                     UI_STATUS_HEIGHT,
                     COLOR_WHITE);
    Display_DrawString(UI_REC_TAB_X + 22,
                       UI_STATUS_TOP + 8,
                       "REC",
                       COLOR_WHITE,
                       COLOR_RED);
}

static UI_Button ui_hit_test_button(uint16_t x, uint16_t y)
{
    if (y >= UI_CONTROL_HEIGHT)
    {
        return UI_BUTTON_NONE;
    }

    if (ui.mode == UI_MODE_FILE_RENAME)
    {
        if (x < 64U) return UI_BUTTON_RENAME_BACK;
        if (x < 128U) return UI_BUTTON_RENAME_CLEAR;
        if (x < 192U) return UI_BUTTON_RENAME_BACKSPACE;
        if (x < 256U) return UI_BUTTON_RENAME_CANCEL;
        return UI_BUTTON_RENAME_SAVE;
    }

    if (ui.mode == UI_MODE_FILE_DELETE_CONFIRM)
    {
        if (x < 64U) return UI_BUTTON_DELETE_BACK;
        if (x < 128U) return UI_BUTTON_DELETE_CANCEL;
        if (x >= 256U) return UI_BUTTON_DELETE_CONFIRM;
        return UI_BUTTON_NONE;
    }

    if (ui.mode == UI_MODE_WAV_INFO)
    {
        if (x < 64U)
        {
            return UI_BUTTON_WAV_INFO_BACK;
        }
        return UI_BUTTON_NONE;
    }

    if (ui.mode == UI_MODE_FILE_ACTIONS)
    {
        if (x < 64U) return UI_BUTTON_FILE_ACTIONS_BACK;
        if (x < 128U) return UI_BUTTON_FILE_ACTIONS_WAVE;
        if (x < 192U) return UI_BUTTON_FILE_ACTIONS_FFT;
        if (x < 256U) return UI_BUTTON_FILE_ACTIONS_INFO;
        if (x < DISPLAY_WIDTH) return UI_BUTTON_FILE_ACTIONS_DELETE;
        return UI_BUTTON_NONE;
    }

    if (ui.mode == UI_MODE_FILE_BROWSER)
    {
        if (x < 64U) return UI_BUTTON_BROWSER_BACK;
        if (x < 128U) return UI_BUTTON_BROWSER_PREVIOUS;
        if (x < 192U) return UI_BUTTON_BROWSER_NEXT;
        if (x < 256U) return UI_BUTTON_BROWSER_SELECT;
        if (x < DISPLAY_WIDTH) return UI_BUTTON_BROWSER_REFRESH;
        return UI_BUTTON_NONE;
    }

    if (ui.mode == UI_MODE_RECORDED_FFT)
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
        if (x < 64U)
        {
            return UI_BUTTON_RECORDER_BACK;
        }
        if (x < 128U)
        {
            return UI_BUTTON_RECORD_5;
        }
        if (x < 192U)
        {
            return UI_BUTTON_RECORD_STOP;
        }
        if (x < 256U)
        {
            return UI_BUTTON_VIEW_WAVEFORM;
        }
        if (x < DISPLAY_WIDTH)
        {
            return UI_BUTTON_VIEW_SPECTRUM;
        }
        return UI_BUTTON_NONE;
    }

    if (x < 64U)
    {
        return UI_BUTTON_RUN_STOP;
    }
    if (x < 128U)
    {
        return UI_BUTTON_FULL;
    }
    if (x < 192U)
    {
        return UI_BUTTON_ZOOM_OUT;
    }
    if (x < 256U)
    {
        return UI_BUTTON_ZOOM_IN;
    }
    if (x < DISPLAY_WIDTH)
    {
        return UI_BUTTON_PEAK;
    }

    return UI_BUTTON_NONE;
}

static void ui_execute_button(UI_Button button)
{
    switch (button)
    {
        case UI_BUTTON_RUN_STOP:
            UI_SetRunning(!ui.running);
            UART_SendString(ui.running ? "UI RUN\r\n" : "UI STOP\r\n");
            break;

        case UI_BUTTON_FULL:
            ui_apply_full_span();
            ui.graph_dirty = true;
            UART_SendString("UI FULL SPAN\r\n");
            break;

        case UI_BUTTON_ZOOM_OUT:
            ui_zoom_out();
            ui.graph_dirty = true;
            UART_SendString("UI ZOOM OUT\r\n");
            break;

        case UI_BUTTON_ZOOM_IN:
            ui_zoom_in();
            ui.graph_dirty = true;
            UART_SendString("UI ZOOM IN\r\n");
            break;

        case UI_BUTTON_PEAK:
            ui_center_on_peak();
            ui.graph_dirty = true;
            UART_SendString("UI CENTER PEAK\r\n");
            break;

        case UI_BUTTON_RECORDER_BACK:
            ui.mode = UI_MODE_ANALYZER;
            ui.graph_dirty = true;
            ui.controls_dirty = true;
            Display_Clear(COLOR_BLACK);
            UART_SendString("UI ANALYZER SCREEN\r\n");
            break;

        case UI_BUTTON_RECORD_5:
            ui.pending_action = UI_ACTION_RECORD_5;
            UART_SendString("UI RECORD 5 REQUEST\r\n");
            break;

        case UI_BUTTON_RECORD_STOP:
            ui.pending_action = UI_ACTION_RECORD_STOP;
            UART_SendString("UI RECORD STOP REQUEST\r\n");
            break;

        case UI_BUTTON_VIEW_WAVEFORM:
            ui.pending_action = UI_ACTION_VIEW_WAVEFORM;
            UART_SendString("UI WAVEFORM REQUEST\r\n");
            break;

        case UI_BUTTON_VIEW_SPECTRUM:
            ui.pending_action = UI_ACTION_VIEW_SPECTRUM;
            UART_SendString("UI RECORDED SPECTRUM REQUEST\r\n");
            break;

        case UI_BUTTON_BROWSER_BACK:
            ui.pending_action = UI_ACTION_BROWSER_BACK;
            UART_SendString("UI FILE BROWSER BACK REQUEST\r\n");
            break;

        case UI_BUTTON_BROWSER_PREVIOUS:
            ui.pending_action = UI_ACTION_BROWSER_PREVIOUS;
            UART_SendString("UI FILES PREV REQUEST\r\n");
            break;

        case UI_BUTTON_BROWSER_NEXT:
            ui.pending_action = UI_ACTION_BROWSER_NEXT;
            UART_SendString("UI FILES NEXT REQUEST\r\n");
            break;

        case UI_BUTTON_BROWSER_SELECT:
            ui.pending_action = UI_ACTION_FILE_ACTIONS_OPEN;
            UART_SendString("UI FILE ACTIONS REQUEST\r\n");
            break;

        case UI_BUTTON_BROWSER_REFRESH:
            ui.pending_action = UI_ACTION_BROWSER_REFRESH;
            UART_SendString("UI FILES REFRESH REQUEST\r\n");
            break;

        case UI_BUTTON_WAV_INFO_BACK:
            UI_ShowFileActions();
            UART_SendString("UI WAV INFO BACK\r\n");
            break;

        case UI_BUTTON_FILE_ACTIONS_BACK:
            ui.pending_action = UI_ACTION_FILE_ACTIONS_BACK;
            UART_SendString("UI FILE ACTIONS BACK REQUEST\r\n");
            break;

        case UI_BUTTON_FILE_ACTIONS_WAVE:
            ui.pending_action = UI_ACTION_VIEW_WAVEFORM;
            UART_SendString("UI FILE ACTIONS WAVE REQUEST\r\n");
            break;

        case UI_BUTTON_FILE_ACTIONS_FFT:
            ui.pending_action = UI_ACTION_VIEW_SPECTRUM;
            UART_SendString("UI FILE ACTIONS FFT REQUEST\r\n");
            break;

        case UI_BUTTON_FILE_ACTIONS_INFO:
            ui.pending_action = UI_ACTION_FILE_INFO;
            UART_SendString("UI FILE INFO REQUEST\r\n");
            break;

        case UI_BUTTON_FILE_ACTIONS_DELETE:
            ui.pending_action = UI_ACTION_FILE_DELETE;
            UART_SendString("UI FILE DELETE REQUEST\r\n");
            break;

        case UI_BUTTON_RENAME_BACK:
        case UI_BUTTON_RENAME_CANCEL:
            UI_ReturnToFileActions();
            UART_SendString("UI RENAME CANCELLED\r\n");
            break;

        case UI_BUTTON_RENAME_CLEAR:
            ui.rename_length = 0U;
            ui.rename_base[0] = '\0';
            ui.graph_dirty = true;
            UART_SendString("UI RENAME CLEAR\r\n");
            break;

        case UI_BUTTON_RENAME_BACKSPACE:
            if (ui.rename_length != 0U)
            {
                ui.rename_length--;
                ui.rename_base[ui.rename_length] = '\0';
                ui.graph_dirty = true;
            }
            UART_SendString("UI RENAME BKSP\r\n");
            break;

        case UI_BUTTON_RENAME_SAVE:
            ui.pending_action = UI_ACTION_FILE_RENAME_SAVE;
            UART_SendString("UI RENAME SAVE REQUEST\r\n");
            break;

        case UI_BUTTON_DELETE_BACK:
        case UI_BUTTON_DELETE_CANCEL:
            ui.pending_action = UI_ACTION_FILE_DELETE_CANCEL;
            UART_SendString("UI DELETE CANCEL REQUEST\r\n");
            break;

        case UI_BUTTON_DELETE_CONFIRM:
            ui.pending_action = UI_ACTION_FILE_DELETE_CONFIRM;
            UART_SendString("UI DELETE CONFIRM REQUEST\r\n");
            break;

        case UI_BUTTON_FFT_BACK:
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
            break;
    }
}

static void ui_handle_touch_down(
    const STMPE811_TouchData *touch)
{
    ui.touch_active = true;
    ui.dragging = false;

    ui.touch_start_x = touch->x;
    ui.touch_start_y = touch->y;

    ui.previous_touch_x = touch->x;
    ui.previous_touch_y = touch->y;

    ui.touch_armed = false;
    ui.pressed_button = ui_hit_test_button(touch->x, touch->y);
    ui.pressed_rename_key =
        (ui.mode == UI_MODE_FILE_RENAME) ?
        ui_rename_key_at(touch->x, touch->y) : -1;
}

static void ui_handle_touch_move(const STMPE811_TouchData *touch)
{
    int32_t delta_x = (int32_t)touch->x - (int32_t)ui.previous_touch_x;
    int32_t delta_y = (int32_t)touch->y - (int32_t)ui.previous_touch_y;
    uint32_t total_x = ui_absolute_difference(touch->x, ui.touch_start_x);
    uint32_t total_y = ui_absolute_difference(touch->y, ui.touch_start_y);

    if ((total_x > UI_TAP_MOVE_LIMIT) ||
        (total_y > UI_TAP_MOVE_LIMIT))
    {
        ui.dragging = true;
    }

    if ((ui.mode == UI_MODE_ANALYZER) &&
        (ui.touch_start_y >= UI_GRAPH_TOP) &&
        (ui.touch_start_y <= UI_GRAPH_BOTTOM))
    {
        if ((delta_x >= UI_PAN_PIXEL_THRESHOLD) ||
            (delta_x <= -UI_PAN_PIXEL_THRESHOLD))
        {
            ui_pan_pixels(delta_x);
            ui.graph_dirty = true;
        }

        if ((delta_y >= UI_SCALE_PIXEL_THRESHOLD) ||
            (delta_y <= -UI_SCALE_PIXEL_THRESHOLD))
        {
            ui_scale_pixels(delta_y);
            ui.graph_dirty = true;
        }
    }

    ui.previous_touch_x = touch->x;
    ui.previous_touch_y = touch->y;
}

static void ui_handle_touch_up(void)
{
    bool action_executed = false;

    /* UI_FILES_TOUCH_FIX
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
        UART_SendString("UI FILE BROWSER REQUEST\r\n");
        action_executed = true;
    }
    else if (!ui.dragging &&
             (ui.mode == UI_MODE_FILE_BROWSER) &&
             (ui.touch_start_y >= UI_BROWSER_ROW_TOP) &&
             (ui.touch_start_y < (UI_BROWSER_ROW_TOP +
              (RECORDING_BROWSER_PAGE_SIZE * UI_BROWSER_ROW_HEIGHT))) &&
             (ui.previous_touch_y >= UI_BROWSER_ROW_TOP) &&
             (ui.previous_touch_y < (UI_BROWSER_ROW_TOP +
              (RECORDING_BROWSER_PAGE_SIZE * UI_BROWSER_ROW_HEIGHT))))
    {
        uint32_t start_row =
            (ui.touch_start_y - UI_BROWSER_ROW_TOP) /
            UI_BROWSER_ROW_HEIGHT;
        uint32_t release_row =
            (ui.previous_touch_y - UI_BROWSER_ROW_TOP) /
            UI_BROWSER_ROW_HEIGHT;

        if ((start_row == release_row) &&
            (start_row < RecordingCatalog_GetPageEntryCount()))
        {
            ui.browser_row = start_row;
            ui.browser_pending_row = start_row;
            ui.pending_action = UI_ACTION_BROWSER_SELECT_ROW;
            ui.graph_dirty = true;
            UART_SendString("UI FILE ROW SELECT REQUEST\r\n");
            action_executed = true;
        }
    }
    else if (!ui.dragging &&
             (ui.mode == UI_MODE_FILE_ACTIONS) &&
             (ui.touch_start_x >= UI_RENAME_BUTTON_X) &&
             (ui.touch_start_x < (UI_RENAME_BUTTON_X + UI_RENAME_BUTTON_WIDTH)) &&
             (ui.touch_start_y >= UI_RENAME_BUTTON_Y) &&
             (ui.touch_start_y < (UI_RENAME_BUTTON_Y + UI_RENAME_BUTTON_HEIGHT)) &&
             (ui.previous_touch_x >= UI_RENAME_BUTTON_X) &&
             (ui.previous_touch_x < (UI_RENAME_BUTTON_X + UI_RENAME_BUTTON_WIDTH)) &&
             (ui.previous_touch_y >= UI_RENAME_BUTTON_Y) &&
             (ui.previous_touch_y < (UI_RENAME_BUTTON_Y + UI_RENAME_BUTTON_HEIGHT)))
    {
        ui.pending_action = UI_ACTION_FILE_RENAME_OPEN;
        UART_SendString("UI RENAME REQUEST\r\n");
        action_executed = true;
    }
    else if (!ui.dragging &&
             (ui.mode == UI_MODE_FILE_RENAME) &&
             (ui.pressed_rename_key >= 0))
    {
        int32_t released_key = ui_rename_key_at(
            ui.previous_touch_x, ui.previous_touch_y);
        if (released_key == ui.pressed_rename_key)
        {
            ui_rename_append_key((uint32_t)released_key);
            UART_SendString("UI RENAME KEY\r\n");
            action_executed = true;
        }
    }
    else if (!ui.dragging &&
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
        UART_SendString("UI RECORDER SCREEN\r\n");
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
    ui.pressed_rename_key = -1;

    if (action_executed)
    {
        ui.last_touch_action_ms = HAL_GetTick();
    }
}

static void ui_pan_pixels(int32_t pixel_delta)
{
    int32_t bin_delta;
    int32_t new_first_bin;

    if (ui.visible_bins >= ui_full_visible_bins())
    {
        ui.first_bin = UI_FIRST_USABLE_BIN;
        return;
    }

    bin_delta = (pixel_delta * (int32_t)ui.visible_bins) /
                (int32_t)UI_GRAPH_WIDTH;

    if (bin_delta == 0)
    {
        bin_delta = (pixel_delta > 0) ? 1 : -1;
    }

    new_first_bin = (int32_t)ui.first_bin - bin_delta;

    if (new_first_bin < (int32_t)UI_FIRST_USABLE_BIN)
    {
        new_first_bin = (int32_t)UI_FIRST_USABLE_BIN;
    }

    ui.first_bin = (uint32_t)new_first_bin;
    ui_clamp_view();
}

static void ui_scale_pixels(int32_t pixel_delta)
{
    uint32_t absolute_delta = (uint32_t)(pixel_delta < 0 ?
                                         -pixel_delta : pixel_delta);
    uint32_t change = (absolute_delta * ui.magnitude_max) /
                      (uint32_t)UI_GRAPH_HEIGHT;

    if (change == 0U)
    {
        change = 1U;
    }

    if (pixel_delta < 0)
    {
        if (ui.magnitude_max > (UI_MIN_MAGNITUDE_MAX + change))
        {
            ui.magnitude_max -= change;
        }
        else
        {
            ui.magnitude_max = UI_MIN_MAGNITUDE_MAX;
        }
    }
    else
    {
        if (ui.magnitude_max < (UI_MAX_MAGNITUDE_MAX - change))
        {
            ui.magnitude_max += change;
        }
        else
        {
            ui.magnitude_max = UI_MAX_MAGNITUDE_MAX;
        }
    }

    ui.controls_dirty = true;
}

static void ui_apply_full_span(void)
{
    ui.first_bin = UI_FIRST_USABLE_BIN;
    ui.visible_bins = ui_full_visible_bins();
    ui.magnitude_max = UI_DEFAULT_MAGNITUDE_MAX;
    ui_clamp_view();
    ui.controls_dirty = true;
}

static void ui_zoom_in(void)
{
    uint32_t center_bin;
    uint32_t new_visible_bins;

    if (ui.visible_bins <= UI_MIN_VISIBLE_BINS)
    {
        return;
    }

    center_bin = ui.first_bin + ((ui.visible_bins - 1U) / 2U);
    new_visible_bins = ui.visible_bins / 2U;

    if (new_visible_bins < UI_MIN_VISIBLE_BINS)
    {
        new_visible_bins = UI_MIN_VISIBLE_BINS;
    }

    ui.visible_bins = new_visible_bins;

    if (center_bin > ((ui.visible_bins - 1U) / 2U))
    {
        ui.first_bin = center_bin - ((ui.visible_bins - 1U) / 2U);
    }
    else
    {
        ui.first_bin = UI_FIRST_USABLE_BIN;
    }

    ui_clamp_view();
    ui.controls_dirty = true;
}

static void ui_zoom_out(void)
{
    uint32_t center_bin;
    uint32_t new_visible_bins;

    if (ui.visible_bins >= ui_full_visible_bins())
    {
        ui_apply_full_span();
        return;
    }

    center_bin = ui.first_bin + ((ui.visible_bins - 1U) / 2U);
    new_visible_bins = ui.visible_bins * 2U;

    if (new_visible_bins > ui_full_visible_bins())
    {
        new_visible_bins = ui_full_visible_bins();
    }

    ui.visible_bins = new_visible_bins;

    if (center_bin > ((ui.visible_bins - 1U) / 2U))
    {
        ui.first_bin = center_bin - ((ui.visible_bins - 1U) / 2U);
    }
    else
    {
        ui.first_bin = UI_FIRST_USABLE_BIN;
    }

    ui_clamp_view();
    ui.controls_dirty = true;
}

static void ui_center_on_peak(void)
{
    uint32_t peak_bin;
    uint32_t half_span;

    if (!FFT_IsValid())
    {
        return;
    }

    peak_bin = FFT_GetPeakBin();
    half_span = (ui.visible_bins - 1U) / 2U;

    if (peak_bin > half_span)
    {
        ui.first_bin = peak_bin - half_span;
    }
    else
    {
        ui.first_bin = UI_FIRST_USABLE_BIN;
    }

    ui_clamp_view();
    ui.controls_dirty = true;
}

static void ui_clamp_view(void)
{
    uint32_t maximum_first_bin;

    if (ui.visible_bins < UI_MIN_VISIBLE_BINS)
    {
        ui.visible_bins = UI_MIN_VISIBLE_BINS;
    }

    if (ui.visible_bins > ui_full_visible_bins())
    {
        ui.visible_bins = ui_full_visible_bins();
    }

    if (ui.first_bin < UI_FIRST_USABLE_BIN)
    {
        ui.first_bin = UI_FIRST_USABLE_BIN;
    }

    maximum_first_bin = ui_last_usable_bin() - ui.visible_bins + 1U;

    if (ui.first_bin > maximum_first_bin)
    {
        ui.first_bin = maximum_first_bin;
    }
}

static int32_t ui_bin_to_x(uint32_t bin)
{
    uint32_t relative_bin;

    if (bin <= ui.first_bin)
    {
        return UI_GRAPH_LEFT;
    }

    if (bin >= ui_last_visible_bin())
    {
        return UI_GRAPH_RIGHT;
    }

    relative_bin = bin - ui.first_bin;

    return UI_GRAPH_LEFT +
           (int32_t)((relative_bin * (UI_GRAPH_WIDTH - 1U)) /
                     (ui.visible_bins - 1U));
}

static int32_t ui_magnitude_to_y(uint32_t magnitude)
{
    uint32_t usable_height;

    if (magnitude > ui.magnitude_max)
    {
        magnitude = ui.magnitude_max;
    }

    usable_height = (uint32_t)UI_GRAPH_HEIGHT - 2U;

    return UI_GRAPH_BOTTOM - 1 -
           (int32_t)((magnitude * usable_height) /
                     ui.magnitude_max);
}

static uint32_t ui_last_usable_bin(void)
{
    uint32_t bin_count = FFT_GetBinCount();

    if (bin_count <= UI_FIRST_USABLE_BIN)
    {
        return UI_FIRST_USABLE_BIN;
    }

    return bin_count - 1U;
}

static uint32_t ui_full_visible_bins(void)
{
    return ui_last_usable_bin() - UI_FIRST_USABLE_BIN + 1U;
}

static uint32_t ui_last_visible_bin(void)
{
    uint32_t last_bin = ui.first_bin + ui.visible_bins - 1U;

    if (last_bin > ui_last_usable_bin())
    {
        last_bin = ui_last_usable_bin();
    }

    return last_bin;
}

static uint32_t ui_absolute_difference(uint16_t a, uint16_t b)
{
    return (a >= b) ? (uint32_t)(a - b) : (uint32_t)(b - a);
}
