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
#include "fft.h"
#include "stmpe811.h"
#include "uart.h"
#include "wav_viewer.h"
#include "wav_fft_viewer.h"

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
    UI_BUTTON_FFT_PEAK
} UI_Button;

typedef enum
{
    UI_MODE_ANALYZER = 0,
    UI_MODE_RECORDER,
    UI_MODE_WAVEFORM,
    UI_MODE_RECORDED_FFT
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
    uint32_t last_recorder_draw_ms;

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
    if (status == NULL)
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
