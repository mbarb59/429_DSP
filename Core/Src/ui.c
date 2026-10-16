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
#define UI_FFT_TIMELINE_X                 20
#define UI_FFT_TIMELINE_Y                218
#define UI_FFT_TIMELINE_WIDTH            280U
#define UI_FFT_TIMELINE_HEIGHT            20U
#define UI_FFT_TIMELINE_LINE_Y           228
#define UI_FFT_TIMELINE_TOUCH_MARGIN       4U
#define UI_FFT_TIMELINE_AUTO_HIDE_MS      3000U

/* FFT_TIMELINE_SEPARATOR_PATCH */
#define UI_FFT_TIMELINE_SEPARATOR_Y     (UI_FFT_TIMELINE_Y - 2)

/* RECORDED_FFT_TIMELINE_GEOMETRY_FIX */
/* FFT_COLLAPSIBLE_TIMELINE_READINGS_PATCH */
/* FFT_TIMELINE_UI_CLEANUP_PATCH */
/* FFT_DISPLAY_CSV_BROWSER_COMBINED_PATCH */
/* RECORDED_FFT_RECLAIM_BOTTOM_PATCH */
/* FFT_EVENT_SCAN_MARKERS_PATCH */
/* TFT_FFT_SCAN_EXPORT_PATCH */
/* TFT_FFT_EXPORT_ALL_LONG_PRESS_PATCH */
#define UI_RECORDED_FFT_GRAPH_BOTTOM     (UI_FFT_TIMELINE_SEPARATOR_Y - 1)
#define UI_RECORDED_FFT_GRAPH_HEIGHT     (UI_RECORDED_FFT_GRAPH_BOTTOM - UI_GRAPH_TOP + 1)

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
#define UI_FFT_EXPORT_ALL_HOLD_MS       1200U
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
#define UI_PROGRESS_PERIOD_MS          1000U
#define UI_PROGRESS_REMAIN_X           246
#define UI_PROGRESS_REMAIN_Y           102
#define UI_PROGRESS_REMAIN_W            46U
#define UI_PROGRESS_REMAIN_H            14U
#define UI_PROGRESS_PERCENT_X          260
#define UI_PROGRESS_PERCENT_Y          122
#define UI_PROGRESS_PERCENT_W           32U
#define UI_PROGRESS_PERCENT_H           14U
#define UI_PROGRESS_BAR_X              188
#define UI_PROGRESS_BAR_Y              142
#define UI_PROGRESS_BAR_W              116U
#define UI_PROGRESS_BAR_H               12U
#define UI_PROGRESS_BAR_INNER_X        190
#define UI_PROGRESS_BAR_INNER_Y        144
#define UI_PROGRESS_BAR_INNER_W        112U
#define UI_PROGRESS_BAR_INNER_H          8U
#define UI_MIC_RMS_VALUE_X              248
#define UI_MIC_RMS_VALUE_Y               62
#define UI_MIC_RMS_VALUE_W               40U
#define UI_MIC_RMS_VALUE_H               16U
#define UI_MIC_CLIP_VALUE_X              292
#define UI_MIC_CLIP_VALUE_Y               62
#define UI_MIC_CLIP_VALUE_W               24U
#define UI_MIC_CLIP_VALUE_H               16U
#define UI_BROWSER_ROW_TOP         42U
#define UI_BROWSER_ROW_HEIGHT      27U

/* UI_WAV_RENAME_PATCH */
/* RECORDED_FFT_DB_DISPLAY_V1_PATCH */
/* FFT_PEAK_FREQUENCY_UNITS_PATCH */
/* FFT_PEAK_UNITS_NO_STDIO_FIX */
/* RECORDED_FFT_TIMELINE_SCRUBBER_PATCH */
/* FFT_TIMELINE_SEPARATOR_PATCH */
/* UI_LIGHTWEIGHT_PROGRESS_V2_PATCH */
/* UI_RMS_1HZ_UPDATE_PATCH */
/* UI_SD_SPACE_LOW_MESSAGE_PATCH */
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
    UI_BUTTON_FFT_SCAN,
    UI_BUTTON_FFT_EXPORT,
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
    UI_BUTTON_RENAME_SAVE,
    UI_BUTTON_DURATION_BACK,
    UI_BUTTON_DURATION_5,
    UI_BUTTON_DURATION_10,
    UI_BUTTON_DURATION_30,
    UI_BUTTON_DURATION_60
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
    UI_MODE_FILE_RENAME,
    UI_MODE_RECORD_DURATION
} UI_Mode;

typedef struct
{
    bool initialized;
    bool running;
    bool touch_active;
    bool dragging;
    bool graph_dirty;
    bool controls_dirty;
    /* ACCEL_TOUCH_LIVE_UI_V4 */
    bool accel_live_screen;
    bool accel_live_running;
    uint32_t accel_live_frames;
    uint32_t accel_live_peak_hz;
    uint32_t accel_live_magnitude;
    uint32_t accel_live_rms;
    uint32_t accel_live_source; /* 0=X, 1=Y, 2=Z, 3=XYZ */
    uint32_t accel_xyz_view_axis;
    uint32_t accel_xyz_rms[3];
    uint32_t accel_xyz_peak_hz[3];
    uint32_t accel_xyz_peak_mag[3];
    uint8_t accel_xyz_clip[3];
    uint32_t accel_xyz_vector_rms;
    uint32_t accel_xyz_dominant_axis;
    bool xyz_save_page;
    bool source_selector_visible;
    uint32_t source_selector_value; /* 0=MIC, 1=X, 2=Y, 3=Z, 4=XYZ */
    char xyz_save_message[32];
    /* ACCEL_PEAK_AUTOSCALE_V1 */
    uint8_t accel_live_autoscale_down_frames;

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
    uint32_t requested_recording_seconds;
    bool recorder_progress_dirty;
    uint32_t last_progress_draw_ms;
    uint32_t last_progress_percent;
    uint32_t last_remaining_seconds;
    uint32_t last_displayed_rms;
    bool last_displayed_clipping;
    bool recorder_space_low;
    uint32_t recorder_required_kib;
    uint32_t recorder_available_kib;

    uint32_t first_bin;
    uint32_t visible_bins;
    uint32_t magnitude_max;
    bool fft_timeline_dragging;
    bool fft_timeline_visible;
    uint32_t fft_timeline_preview_segment;
    uint32_t requested_fft_segment;
    uint32_t fft_timeline_last_interaction_ms;

    uint16_t touch_start_x;
    uint16_t touch_start_y;
    uint16_t previous_touch_x;
    uint16_t previous_touch_y;

    uint32_t last_touch_poll_ms;
    uint32_t last_touch_action_ms;
    uint32_t touch_down_ms;
    uint32_t released_poll_count;
    bool touch_armed;
    UI_Button pressed_button;
} UI_State;

static UI_State ui;

static void ui_draw_controls(void);
static void ui_draw_status(void);
static void ui_draw_recorder_body(void);
static void ui_draw_recording_progress_static(void);
static void ui_draw_recording_progress_dynamic(bool force);
static uint32_t ui_recording_target_seconds(void);
static void ui_draw_record_duration_body(void);
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
static void ui_format_peak_frequency(uint32_t frequency_hz,
                                     char *buffer,
                                     uint32_t buffer_size);
static void ui_draw_grid(void);
static void ui_draw_db_grid(void);
static void ui_draw_db_spectrum_trace(void);
static void ui_draw_db_peak_marker(void);
static void ui_draw_fft_timeline(void);
static void ui_draw_fft_readings(void);
static int32_t ui_recorded_fft_graph_bottom(void);
static uint32_t ui_recorded_fft_graph_height(void);
static void ui_format_time_seconds(uint32_t time_ms,
                                   char *buffer,
                                   uint32_t buffer_size);
static void ui_format_dbfs_tenths(int16_t value, char *buffer, uint32_t size);
static bool ui_fft_timeline_contains(uint16_t x, uint16_t y);
static uint32_t ui_fft_timeline_segment_from_x(uint16_t x);
static int32_t ui_db_tenths_to_y(int16_t db_tenths);
static void ui_draw_signed_db(int32_t x,
                              int32_t y,
                              int16_t db_tenths,
                              uint16_t color);
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
    ui.accel_live_screen = false;
    ui.accel_live_running = false;
    ui.accel_live_frames = 0U;
    ui.accel_live_peak_hz = 0U;
    ui.accel_live_magnitude = 0U;
    ui.accel_live_rms = 0U;
    ui.accel_live_source = 0U;
    ui.accel_xyz_view_axis = 0U;
    memset(ui.accel_xyz_rms, 0, sizeof(ui.accel_xyz_rms));
    memset(ui.accel_xyz_peak_hz, 0, sizeof(ui.accel_xyz_peak_hz));
    memset(ui.accel_xyz_peak_mag, 0, sizeof(ui.accel_xyz_peak_mag));
    memset(ui.accel_xyz_clip, 0, sizeof(ui.accel_xyz_clip));
    ui.accel_xyz_vector_rms = 0U;
    ui.accel_xyz_dominant_axis = 0U;
    ui.xyz_save_page = false;
    ui.source_selector_visible = false;
    ui.source_selector_value = 0U;
    ui.xyz_save_message[0] = '\0';
    ui.accel_live_autoscale_down_frames = 0U;
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
    ui.requested_recording_seconds = 5U;
    ui.recorder_progress_dirty = false;
    ui.last_progress_draw_ms = HAL_GetTick();
    ui.last_progress_percent = 0U;
    ui.last_remaining_seconds = 0U;
    ui.last_displayed_rms = UINT32_MAX;
    ui.last_displayed_clipping = false;
    ui.recorder_space_low = false;
    ui.recorder_required_kib = 0U;
    ui.recorder_available_kib = 0U;

    ui.first_bin = UI_FIRST_USABLE_BIN;
    ui.visible_bins = ui_full_visible_bins();
    ui.magnitude_max = UI_DEFAULT_MAGNITUDE_MAX;
    ui.fft_timeline_dragging = false;
    ui.fft_timeline_visible = false;
    ui.fft_timeline_preview_segment = 0U;
    ui.requested_fft_segment = 0U;
    ui.fft_timeline_last_interaction_ms = 0U;

    ui.touch_start_x = 0U;
    ui.touch_start_y = 0U;
    ui.previous_touch_x = 0U;
    ui.previous_touch_y = 0U;
    ui.last_touch_poll_ms = HAL_GetTick();
    ui.last_touch_action_ms = 0U;
    ui.touch_down_ms = 0U;
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

    if (ui.mode == UI_MODE_RECORD_DURATION)
    {
        ui_draw_record_duration_body();
        ui.graph_dirty = false;
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

    if (ui.mode == UI_MODE_RECORDED_FFT)
    {
        Display_FillRect(UI_GRAPH_LEFT,
                         UI_GRAPH_TOP,
                         UI_GRAPH_WIDTH,
                         ui_recorded_fft_graph_height(),
                         COLOR_BLACK);
        ui_draw_db_grid();

        if (FFT_IsValid())
        {
            ui_draw_db_spectrum_trace();
            ui_draw_db_peak_marker();
            ui_draw_fft_readings();
            ui_draw_fft_timeline();
        }
    }
    else
    {
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


    if ((ui.mode == UI_MODE_RECORDED_FFT) &&
        ui.fft_timeline_visible &&
        !ui.touch_active &&
        !ui.fft_timeline_dragging &&
        ((now - ui.fft_timeline_last_interaction_ms) >=
         UI_FFT_TIMELINE_AUTO_HIDE_MS))
    {
        ui.fft_timeline_visible = false;
        ui.graph_dirty = true;
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

uint32_t UI_GetRequestedFftSegment(void)
{
    return ui.requested_fft_segment;
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

    {
        bool was_active = ui.recorder_status.capture_active;

        ui.recorder_status = *status;

        if (ui.mode == UI_MODE_RECORDER)
        {
            if (status->capture_active)
            {
                ui.recorder_progress_dirty = true;
            }
            else
            {
                ui.recorder_progress_dirty = false;
                ui.graph_dirty = true;
            }

            if (!was_active && status->capture_active)
            {
                ui.last_progress_draw_ms = HAL_GetTick();
                ui.last_progress_percent = 0U;
                ui.last_remaining_seconds = ui_recording_target_seconds();
                ui.recorder_progress_dirty = true;
            }
        }
    }
}
bool UI_IsRecorderScreen(void)
{
    return ui.mode == UI_MODE_RECORDER;
}

void UI_ShowSdSpaceLow(
    uint32_t required_kib,
    uint32_t available_kib)
{
    ui.recorder_space_low = true;
    ui.recorder_required_kib = required_kib;
    ui.recorder_available_kib = available_kib;
    ui.mode = UI_MODE_RECORDER;
    ui.graph_dirty = true;
    ui.controls_dirty = true;
}

void UI_ClearRecorderMessage(void)
{
    ui.recorder_space_low = false;
    ui.recorder_required_kib = 0U;
    ui.recorder_available_kib = 0U;
}

void UI_ShowRecordDurationSelector(void)
{
    UI_ClearRecorderMessage();
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
    ui.fft_timeline_dragging = false;
    ui.fft_timeline_preview_segment =
        WavFftViewer_GetInfo()->segment_index;
    ui.requested_fft_segment =
        ui.fft_timeline_preview_segment;
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

void UI_ShowAccelLive(void)
{
    ui.mode = UI_MODE_ANALYZER;
    ui.running = false;
    ui.accel_live_screen = true;
    ui.accel_live_running = false;
    ui.accel_live_frames = 0U;
    ui.accel_live_peak_hz = 0U;
    ui.accel_live_magnitude = 0U;
    ui.accel_live_rms = 0U;
    ui.accel_live_source = 0U;
    ui.accel_xyz_view_axis = 0U;
    memset(ui.accel_xyz_rms, 0, sizeof(ui.accel_xyz_rms));
    memset(ui.accel_xyz_peak_hz, 0, sizeof(ui.accel_xyz_peak_hz));
    memset(ui.accel_xyz_peak_mag, 0, sizeof(ui.accel_xyz_peak_mag));
    memset(ui.accel_xyz_clip, 0, sizeof(ui.accel_xyz_clip));
    ui.accel_xyz_vector_rms = 0U;
    ui.accel_xyz_dominant_axis = 0U;
    ui.xyz_save_page = false;
    ui.source_selector_visible = false;
    ui.source_selector_value = 0U;
    ui.xyz_save_message[0] = '\0';
    ui.accel_live_autoscale_down_frames = 0U;
    ui_apply_full_span();
    ui.graph_dirty = true;
    ui.controls_dirty = true;
    Display_Clear(COLOR_BLACK);
}

bool UI_IsAccelLiveScreen(void)
{
    return ui.accel_live_screen && (ui.mode == UI_MODE_ANALYZER);
}

void UI_ShowSourceSelector(uint32_t source)
{
    if (source > 4U) source = 0U;
    ui.source_selector_value = source;
    ui.source_selector_visible = true;
    ui.xyz_save_page = false;
    ui.controls_dirty = true;
}

void UI_HideSourceSelector(void)
{
    ui.source_selector_visible = false;
    ui.controls_dirty = true;
}

bool UI_IsSourceSelectorVisible(void)
{
    return UI_IsAccelLiveScreen() && ui.source_selector_visible;
}

void UI_SetSourceSelectorValue(uint32_t source)
{
    if (source > 4U) source = 0U;
    ui.source_selector_value = source;
    if (UI_IsSourceSelectorVisible()) ui.controls_dirty = true;
}

void UI_ShowXYZSavePage(void)
{
    if (!UI_IsAccelLiveScreen() || ui.accel_live_running ||
        (ui.accel_live_source != 3U)) return;
    ui.xyz_save_page = true;
    ui.xyz_save_message[0] = '\0';
    ui.controls_dirty = true;
}

void UI_HideXYZSavePage(void)
{
    ui.xyz_save_page = false;
    ui.controls_dirty = true;
}

bool UI_IsXYZSavePage(void)
{
    return UI_IsAccelLiveScreen() && ui.xyz_save_page;
}

void UI_SetXYZSaveMessage(const char *message)
{
    uint32_t i=0U;
    if(message==NULL) message="";
    while((message[i]!='\0')&&(i<(sizeof(ui.xyz_save_message)-1U)))
    { ui.xyz_save_message[i]=message[i]; i++; }
    ui.xyz_save_message[i]='\0';
    if(UI_IsXYZSavePage()) ui.controls_dirty=true;
}

void UI_SetAccelSource(uint32_t source)
{
    if (source > 3U)
    {
        source = 0U;
    }
    ui.accel_live_source = source;
    if (UI_IsAccelLiveScreen())
    {
        ui.controls_dirty = true;
    }
}

uint32_t UI_GetAccelSource(void)
{
    return ui.accel_live_source;
}

void UI_SetAccelXYZSummary(bool running,
                           uint32_t frames,
                           uint32_t view_axis,
                           const uint32_t rms[3],
                           const uint32_t peak_hz[3],
                           const uint32_t peak_mag[3],
                           const uint8_t clip_flags[3],
                           uint32_t vector_rms,
                           uint32_t dominant_axis)
{
    uint32_t axis;
    if (view_axis > 2U) view_axis = 0U;
    ui.accel_live_source = 3U;
    ui.accel_xyz_view_axis = view_axis;
    ui.accel_live_running = running;
    ui.accel_live_frames = frames;
    ui.accel_xyz_vector_rms = vector_rms;
    ui.accel_xyz_dominant_axis = dominant_axis;
    for (axis = 0U; axis < 3U; axis++)
    {
        ui.accel_xyz_rms[axis] = rms[axis];
        ui.accel_xyz_peak_hz[axis] = peak_hz[axis];
        ui.accel_xyz_peak_mag[axis] = peak_mag[axis];
        ui.accel_xyz_clip[axis] = clip_flags[axis];
    }
    ui.accel_live_peak_hz = peak_hz[view_axis];
    ui.accel_live_magnitude = peak_mag[view_axis];
    ui.accel_live_rms = rms[view_axis];
    if (UI_IsAccelLiveScreen()) ui.controls_dirty = true;
}

void UI_SetAccelLiveStatus(bool running, uint32_t frames,
                           uint32_t peak_hz, uint32_t magnitude,
                           uint32_t rms)
{
    ui.accel_live_running = running;
    ui.accel_live_frames = frames;
    ui.accel_live_peak_hz = peak_hz;
    ui.accel_live_magnitude = magnitude;
    ui.accel_live_rms = rms;

    if (running && UI_IsAccelLiveScreen())
    {
        uint32_t target_scale;
        uint32_t rounded_scale;

        /* Four times the peak keeps a small signal visible with headroom. */
        target_scale = magnitude * 4U;

        if (target_scale < 8U)
        {
            target_scale = 8U;
        }

        if (target_scale > UI_MAX_MAGNITUDE_MAX)
        {
            target_scale = UI_MAX_MAGNITUDE_MAX;
        }

        /* Round upward to a stable 1, 2, 5 x power-of-ten scale. */
        rounded_scale = 1U;
        while ((rounded_scale < target_scale) &&
               (rounded_scale <= (UI_MAX_MAGNITUDE_MAX / 10U)))
        {
            rounded_scale *= 10U;
        }

        if ((rounded_scale / 5U) >= target_scale)
        {
            rounded_scale /= 5U;
        }
        else if ((rounded_scale / 2U) >= target_scale)
        {
            rounded_scale /= 2U;
        }

        if (rounded_scale < 8U)
        {
            rounded_scale = 8U;
        }

        if (rounded_scale > ui.magnitude_max)
        {
            /* Expand immediately so a sudden strong source is not clipped. */
            ui.magnitude_max = rounded_scale;
            ui.accel_live_autoscale_down_frames = 0U;
            ui.graph_dirty = true;
        }
        else if (rounded_scale < ui.magnitude_max)
        {
            /* Require three quiet frames before reducing the scale. */
            ui.accel_live_autoscale_down_frames++;

            if (ui.accel_live_autoscale_down_frames >= 3U)
            {
                ui.magnitude_max = rounded_scale;
                ui.accel_live_autoscale_down_frames = 0U;
                ui.graph_dirty = true;
            }
        }
        else
        {
            ui.accel_live_autoscale_down_frames = 0U;
        }
    }

    if (UI_IsAccelLiveScreen())
    {
        ui.controls_dirty = true;
    }
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
    if (UI_IsSourceSelectorVisible())
    {
        uint16_t c0=(ui.source_selector_value==0U)?COLOR_GREEN:COLOR_BLUE;
        uint16_t c1=(ui.source_selector_value==1U)?COLOR_GREEN:COLOR_BLUE;
        uint16_t c2=(ui.source_selector_value==2U)?COLOR_GREEN:COLOR_BLUE;
        uint16_t c3=(ui.source_selector_value==3U)?COLOR_GREEN:COLOR_BLUE;
        uint16_t c4=(ui.source_selector_value==4U)?COLOR_GREEN:COLOR_MAGENTA;
        Display_FillRect(0,UI_CONTROL_TOP,DISPLAY_WIDTH,UI_CONTROL_HEIGHT,COLOR_BLACK);
        ui_draw_button(0,c0,COLOR_WHITE); ui_draw_button(64,c1,COLOR_WHITE);
        ui_draw_button(128,c2,COLOR_WHITE); ui_draw_button(192,c3,COLOR_WHITE);
        ui_draw_button(256,c4,COLOR_WHITE);
        Display_DrawString(18,12,"MIC",COLOR_WHITE,c0);
        Display_DrawString(92,12,"X",COLOR_WHITE,c1);
        Display_DrawString(156,12,"Y",COLOR_WHITE,c2);
        Display_DrawString(220,12,"Z",COLOR_WHITE,c3);
        Display_DrawString(274,12,"XYZ",COLOR_WHITE,c4);
        return;
    }

    if (UI_IsXYZSavePage())
    {
        Display_FillRect(0, UI_CONTROL_TOP, DISPLAY_WIDTH,
                         UI_CONTROL_HEIGHT, COLOR_BLACK);
        ui_draw_button(0, COLOR_BLUE, COLOR_WHITE);
        ui_draw_button(64, COLOR_CYAN, COLOR_WHITE);
        ui_draw_button(128, COLOR_YELLOW, COLOR_WHITE);
        ui_draw_button(192, COLOR_GREEN, COLOR_WHITE);
        ui_draw_button(256, COLOR_MAGENTA, COLOR_WHITE);
        Display_DrawString(12,12,"BACK",COLOR_WHITE,COLOR_BLUE);
        Display_DrawString(78,12,"TIME",COLOR_BLACK,COLOR_CYAN);
        Display_DrawString(148,12,"FFT",COLOR_BLACK,COLOR_YELLOW);
        Display_DrawString(208,12,"BOTH",COLOR_BLACK,COLOR_GREEN);
        Display_DrawString(270,12,"FILES",COLOR_WHITE,COLOR_MAGENTA);
        return;
    }

    if (UI_IsAccelLiveScreen())
    {
        uint16_t run_color = ui.accel_live_running ? COLOR_RED : COLOR_GREEN;
        uint16_t x_color = ((ui.accel_live_source == 0U) ||
            ((ui.accel_live_source == 3U) && (ui.accel_xyz_view_axis == 0U))) ? COLOR_GREEN : COLOR_BLUE;
        uint16_t y_color = ((ui.accel_live_source == 1U) ||
            ((ui.accel_live_source == 3U) && (ui.accel_xyz_view_axis == 1U))) ? COLOR_GREEN : COLOR_BLUE;
        uint16_t z_color = ((ui.accel_live_source == 2U) ||
            ((ui.accel_live_source == 3U) && (ui.accel_xyz_view_axis == 2U))) ? COLOR_GREEN : COLOR_BLUE;
        uint16_t xyz_color = (ui.accel_live_source == 3U) ? COLOR_GREEN : COLOR_MAGENTA;
        Display_FillRect(0, UI_CONTROL_TOP, DISPLAY_WIDTH,
                         UI_CONTROL_HEIGHT, COLOR_BLACK);
        ui_draw_button(0, run_color, COLOR_WHITE);
        ui_draw_button(64, x_color, COLOR_WHITE);
        ui_draw_button(128, y_color, COLOR_WHITE);
        ui_draw_button(192, z_color, COLOR_WHITE);
        ui_draw_button(256, xyz_color, COLOR_WHITE);
        Display_DrawString(8, 12,
            ui.accel_live_running ? "STOP" : "START", COLOR_BLACK, run_color);
        Display_DrawString(92, 12, "X", COLOR_WHITE, x_color);
        Display_DrawString(156, 12, "Y", COLOR_WHITE, y_color);
        Display_DrawString(220, 12, "Z", COLOR_WHITE, z_color);
        Display_DrawString(272, 12, "XYZ", COLOR_WHITE, xyz_color);
        return;
    }
    uint16_t run_color = ui.running ? COLOR_GREEN : COLOR_RED;

    if (ui.mode == UI_MODE_RECORD_DURATION)
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
        Display_DrawString(208, 12, "SCAN", COLOR_BLACK, COLOR_YELLOW);
        Display_DrawString(266, 12, "EXPORT", COLOR_BLACK, COLOR_MAGENTA);
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
        Display_DrawString(72, 12, "RECORD", COLOR_BLACK, COLOR_GREEN);
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

static uint32_t ui_append_uint_text(char *buffer,
                                    uint32_t buffer_size,
                                    uint32_t position,
                                    uint32_t value,
                                    uint32_t minimum_digits)
{
    char reverse[10];
    uint32_t count = 0U;

    do
    {
        reverse[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    while ((value != 0U) && (count < sizeof(reverse)));

    while ((count < minimum_digits) &&
           (count < sizeof(reverse)))
    {
        reverse[count++] = '0';
    }

    while ((count != 0U) &&
           ((position + 1U) < buffer_size))
    {
        buffer[position++] = reverse[--count];
    }

    buffer[position] = '\0';
    return position;
}

static uint32_t ui_append_text(char *buffer,
                               uint32_t buffer_size,
                               uint32_t position,
                               const char *text)
{
    while ((*text != '\0') &&
           ((position + 1U) < buffer_size))
    {
        buffer[position++] = *text++;
    }

    buffer[position] = '\0';
    return position;
}

static void ui_format_peak_frequency(uint32_t frequency_hz,
                                     char *buffer,
                                     uint32_t buffer_size)
{
    uint32_t position = 0U;

    if ((buffer == NULL) || (buffer_size == 0U))
    {
        return;
    }

    buffer[0] = '\0';

    if (frequency_hz < 1000U)
    {
        position = ui_append_uint_text(buffer,
                                       buffer_size,
                                       position,
                                       frequency_hz,
                                       1U);
        (void)ui_append_text(buffer,
                            buffer_size,
                            position,
                            " Hz");
    }
    else if (frequency_hz < 10000U)
    {
        uint32_t rounded_hundredths =
            (frequency_hz + 5U) / 10U;
        uint32_t whole_khz = rounded_hundredths / 100U;
        uint32_t fraction = rounded_hundredths % 100U;

        position = ui_append_uint_text(buffer,
                                       buffer_size,
                                       position,
                                       whole_khz,
                                       1U);
        position = ui_append_text(buffer,
                                  buffer_size,
                                  position,
                                  ".");
        position = ui_append_uint_text(buffer,
                                       buffer_size,
                                       position,
                                       fraction,
                                       2U);
        (void)ui_append_text(buffer,
                            buffer_size,
                            position,
                            " kHz");
    }
    else
    {
        uint32_t rounded_tenths =
            (frequency_hz + 50U) / 100U;
        uint32_t whole_khz = rounded_tenths / 10U;
        uint32_t fraction = rounded_tenths % 10U;

        position = ui_append_uint_text(buffer,
                                       buffer_size,
                                       position,
                                       whole_khz,
                                       1U);
        position = ui_append_text(buffer,
                                  buffer_size,
                                  position,
                                  ".");
        position = ui_append_uint_text(buffer,
                                       buffer_size,
                                       position,
                                       fraction,
                                       1U);
        (void)ui_append_text(buffer,
                            buffer_size,
                            position,
                            " kHz");
    }
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

static void ui_draw_record_duration_body(void)
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

static uint32_t ui_recording_target_seconds(void)
{
    uint32_t seconds = ui.requested_recording_seconds;
    if ((seconds != 5U) && (seconds != 10U) &&
        (seconds != 30U) && (seconds != 60U)) seconds = 5U;
    return seconds;
}

static void ui_draw_recording_progress_dynamic(bool force)
{
    uint32_t target_seconds = ui_recording_target_seconds();
    uint64_t target_samples = (uint64_t)target_seconds * 48000U;
    uint32_t captured = ui.recorder_status.captured_samples;
    uint32_t elapsed_seconds = captured / 48000U;
    uint32_t remaining_seconds;
    uint32_t percent;
    uint32_t old_width;
    uint32_t new_width;

    if ((uint64_t)captured >= target_samples)
    {
        remaining_seconds = 0U;
        percent = 100U;
    }
    else
    {
        remaining_seconds = target_seconds - elapsed_seconds;
        percent = (uint32_t)(((uint64_t)captured * 100U) / target_samples);
    }

    if (force || (remaining_seconds != ui.last_remaining_seconds))
    {
        Display_FillRect(UI_PROGRESS_REMAIN_X, UI_PROGRESS_REMAIN_Y,
                         UI_PROGRESS_REMAIN_W, UI_PROGRESS_REMAIN_H, COLOR_BLACK);
        ui_draw_uint(UI_PROGRESS_REMAIN_X, UI_PROGRESS_REMAIN_Y,
                     remaining_seconds, COLOR_YELLOW);
        ui.last_remaining_seconds = remaining_seconds;
    }

    if (force || (percent != ui.last_progress_percent))
    {
        Display_FillRect(UI_PROGRESS_PERCENT_X, UI_PROGRESS_PERCENT_Y,
                         UI_PROGRESS_PERCENT_W, UI_PROGRESS_PERCENT_H, COLOR_BLACK);
        ui_draw_uint(UI_PROGRESS_PERCENT_X, UI_PROGRESS_PERCENT_Y,
                     percent, COLOR_GREEN);
        old_width = (ui.last_progress_percent * UI_PROGRESS_BAR_INNER_W) / 100U;
        new_width = (percent * UI_PROGRESS_BAR_INNER_W) / 100U;
        if (new_width > old_width)
        {
            Display_FillRect(UI_PROGRESS_BAR_INNER_X + (int32_t)old_width,
                             UI_PROGRESS_BAR_INNER_Y, new_width - old_width,
                             UI_PROGRESS_BAR_INNER_H, COLOR_GREEN);
        }
        ui.last_progress_percent = percent;
    }

    /* UI_RMS_1HZ_UPDATE_PATCH
     * This function is gated by UI_PROGRESS_PERIOD_MS, so MIC RMS and the
     * clip indicator update at the same one-second rate as progress.
     */
    if (force ||
        (ui.recorder_status.rms_counts != ui.last_displayed_rms))
    {
        Display_FillRect(UI_MIC_RMS_VALUE_X,
                         UI_MIC_RMS_VALUE_Y,
                         UI_MIC_RMS_VALUE_W,
                         UI_MIC_RMS_VALUE_H,
                         COLOR_BLACK);
        ui_draw_uint(UI_MIC_RMS_VALUE_X,
                     UI_MIC_RMS_VALUE_Y,
                     ui.recorder_status.rms_counts,
                     COLOR_YELLOW);
        ui.last_displayed_rms = ui.recorder_status.rms_counts;
    }

    if (force ||
        (ui.recorder_status.clipping != ui.last_displayed_clipping))
    {
        Display_FillRect(UI_MIC_CLIP_VALUE_X,
                         UI_MIC_CLIP_VALUE_Y,
                         UI_MIC_CLIP_VALUE_W,
                         UI_MIC_CLIP_VALUE_H,
                         COLOR_BLACK);
        Display_DrawString(UI_MIC_CLIP_VALUE_X,
                           UI_MIC_CLIP_VALUE_Y,
                           ui.recorder_status.clipping ? "!" : "OK",
                           ui.recorder_status.clipping ? COLOR_RED : COLOR_GREEN,
                           COLOR_BLACK);
        ui.last_displayed_clipping = ui.recorder_status.clipping;
    }
}

static void ui_draw_recording_progress_static(void)
{
    uint32_t target = ui_recording_target_seconds();
    Display_DrawString(188, 82, "TARGET", COLOR_WHITE, COLOR_BLACK);
    ui_draw_uint(246, 82, target, COLOR_CYAN);
    Display_DrawString(278, 82, "SEC", COLOR_GRAY, COLOR_BLACK);
    Display_DrawString(188, 102, "REMAIN", COLOR_WHITE, COLOR_BLACK);
    Display_DrawString(292, 102, "SEC", COLOR_GRAY, COLOR_BLACK);
    Display_DrawString(188, 122, "PROGRESS", COLOR_WHITE, COLOR_BLACK);
    Display_DrawString(292, 122, "%", COLOR_GRAY, COLOR_BLACK);
    Display_DrawRect(UI_PROGRESS_BAR_X, UI_PROGRESS_BAR_Y,
                     UI_PROGRESS_BAR_W, UI_PROGRESS_BAR_H, COLOR_WHITE);
    Display_FillRect(UI_PROGRESS_BAR_INNER_X, UI_PROGRESS_BAR_INNER_Y,
                     UI_PROGRESS_BAR_INNER_W, UI_PROGRESS_BAR_INNER_H,
                     COLOR_DARK_GRAY);
    ui.last_progress_percent = 0U;
    ui.last_remaining_seconds = target;
    ui_draw_recording_progress_dynamic(true);
}

void UI_ResetRecordingProgress(void)
{
    uint32_t target = ui_recording_target_seconds();

    /* UI_LIGHTWEIGHT_PROGRESS_RESET_FIX
     * Reset only the tiny dynamic regions before DMA capture starts.
     */
    Display_FillRect(UI_PROGRESS_REMAIN_X,
                     UI_PROGRESS_REMAIN_Y,
                     UI_PROGRESS_REMAIN_W,
                     UI_PROGRESS_REMAIN_H,
                     COLOR_BLACK);
    ui_draw_uint(UI_PROGRESS_REMAIN_X,
                 UI_PROGRESS_REMAIN_Y,
                 target,
                 COLOR_YELLOW);

    Display_FillRect(UI_PROGRESS_PERCENT_X,
                     UI_PROGRESS_PERCENT_Y,
                     UI_PROGRESS_PERCENT_W,
                     UI_PROGRESS_PERCENT_H,
                     COLOR_BLACK);
    ui_draw_uint(UI_PROGRESS_PERCENT_X,
                 UI_PROGRESS_PERCENT_Y,
                 0U,
                 COLOR_GREEN);

    Display_FillRect(UI_PROGRESS_BAR_INNER_X,
                     UI_PROGRESS_BAR_INNER_Y,
                     UI_PROGRESS_BAR_INNER_W,
                     UI_PROGRESS_BAR_INNER_H,
                     COLOR_DARK_GRAY);

    ui.last_progress_percent = 0U;
    ui.last_remaining_seconds = target;
    ui.last_displayed_rms = UINT32_MAX;
    ui.last_displayed_clipping = !ui.recorder_status.clipping;
    ui.last_progress_draw_ms = HAL_GetTick();
    ui.recorder_progress_dirty = false;
}

void UI_ProcessRecorderDisplay(void)
{
    uint32_t now;
    if (!ui.initialized || (ui.mode != UI_MODE_RECORDER) ||
        !ui.recorder_status.capture_active || !ui.recorder_progress_dirty) return;
    now = HAL_GetTick();
    if ((now - ui.last_progress_draw_ms) < UI_PROGRESS_PERIOD_MS) return;
    ui.last_progress_draw_ms = now;
    ui.recorder_progress_dirty = false;
    ui_draw_recording_progress_dynamic(false);
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

    /* UI_PROGRESS_FIELD_OVERLAP_FIX
     * Keep MIC RMS on its own row. The legacy SELECTED WAV, RMS, CLIP,
     * and CLIP CNT rows overlapped TARGET, REMAIN, PROGRESS, and the bar.
     */
    Display_DrawString(188, 62, "MIC RMS", COLOR_WHITE, COLOR_BLACK);
    ui_draw_uint(248, 62,
                 ui.recorder_status.rms_counts,
                 COLOR_YELLOW);
    Display_DrawString(292, 62,
                       ui.recorder_status.clipping ? "!" : "OK",
                       ui.recorder_status.clipping ? COLOR_RED : COLOR_GREEN,
                       COLOR_BLACK);

    ui_draw_recording_progress_static();

    if (ui.recorder_space_low &&
        !ui.recorder_status.capture_active)
    {
        Display_FillRect(176, 58, 136, 100, COLOR_BLACK);
        Display_DrawRect(176, 58, 136, 100, COLOR_RED);
        Display_DrawString(188, 68, "SD SPACE LOW", COLOR_RED, COLOR_BLACK);
        Display_DrawString(184, 94, "REQUIRED", COLOR_WHITE, COLOR_BLACK);
        ui_draw_uint(250, 94, ui.recorder_required_kib, COLOR_YELLOW);
        Display_DrawString(292, 94, "K", COLOR_GRAY, COLOR_BLACK);
        Display_DrawString(184, 118, "AVAILABLE", COLOR_WHITE, COLOR_BLACK);
        ui_draw_uint(250, 118, ui.recorder_available_kib, COLOR_YELLOW);
        Display_DrawString(292, 118, "K", COLOR_GRAY, COLOR_BLACK);
        Display_DrawString(188, 142, "DELETE FILES", COLOR_WHITE, COLOR_BLACK);
    }

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
    const char *selected=RecordingCatalog_GetSelected();
    bool csv=RecordingCatalog_SelectedIsCsv();
    FILINFO fi; FRESULT r;
    Display_FillRect(UI_GRAPH_LEFT,UI_GRAPH_TOP,UI_GRAPH_WIDTH,UI_GRAPH_HEIGHT,COLOR_BLACK);
    Display_DrawRect(UI_GRAPH_LEFT,UI_GRAPH_TOP,UI_GRAPH_WIDTH,UI_GRAPH_HEIGHT,COLOR_GRAY);
    Display_DrawString(10,44,csv?"SELECTED FFT EXPORT":"SELECTED WAV",COLOR_CYAN,COLOR_BLACK);
    Display_DrawString(10,68,selected,COLOR_GREEN,COLOR_BLACK);
    r=f_stat(selected,&fi);
    if(r==FR_OK){ Display_DrawString(10,96,"SIZE",COLOR_WHITE,COLOR_BLACK); ui_draw_uint(58,96,(uint32_t)fi.fsize,COLOR_YELLOW); Display_DrawString(140,96,"BYTES",COLOR_GRAY,COLOR_BLACK); }
    else Display_DrawString(10,96,"FILE NOT FOUND",COLOR_RED,COLOR_BLACK);
    if(csv){ Display_DrawString(10,128,"FFT CSV EXPORT",COLOR_CYAN,COLOR_BLACK); Display_DrawString(10,150,"INFO: FILE AND SIZE",COLOR_YELLOW,COLOR_BLACK); Display_DrawString(10,194,"DELETE",COLOR_RED,COLOR_BLACK); Display_DrawString(66,194,"CONFIRM",COLOR_GRAY,COLOR_BLACK); Display_DrawString(202,151,"WAV ACTIONS",COLOR_DARK_GRAY,COLOR_BLACK); Display_DrawString(202,171,"DISABLED",COLOR_DARK_GRAY,COLOR_BLACK); return; }
    Display_DrawString(10,128,"WAVE",COLOR_CYAN,COLOR_BLACK); Display_DrawString(10,150,"FFT",COLOR_GREEN,COLOR_BLACK); Display_DrawString(10,172,"INFO",COLOR_YELLOW,COLOR_BLACK); Display_DrawString(10,194,"DELETE",COLOR_RED,COLOR_BLACK); Display_DrawString(66,194,"CONFIRM",COLOR_GRAY,COLOR_BLACK);
    Display_FillRect(UI_RENAME_BUTTON_X,UI_RENAME_BUTTON_Y,UI_RENAME_BUTTON_WIDTH,UI_RENAME_BUTTON_HEIGHT,COLOR_MAGENTA); Display_DrawRect(UI_RENAME_BUTTON_X,UI_RENAME_BUTTON_Y,UI_RENAME_BUTTON_WIDTH,UI_RENAME_BUTTON_HEIGHT,COLOR_WHITE); Display_DrawString(220,151,"RENAME",COLOR_BLACK,COLOR_MAGENTA); Display_DrawString(202,171,"EDIT WAV NAME",COLOR_BLACK,COLOR_MAGENTA);
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
            Display_DrawString(118, y + 9,
                entry->file_type == RECORDING_FILE_TYPE_CSV ? "FFT" : "WAV",
                entry->file_type == RECORDING_FILE_TYPE_CSV ? COLOR_CYAN : COLOR_GREEN,
                background);
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

static int32_t ui_recorded_fft_graph_bottom(void)
{
    return ui.fft_timeline_visible ?
        UI_RECORDED_FFT_GRAPH_BOTTOM :
        ((int32_t)DISPLAY_HEIGHT - 1);
}

static uint32_t ui_recorded_fft_graph_height(void)
{
    return (uint32_t)(ui_recorded_fft_graph_bottom() -
                      UI_GRAPH_TOP + 1);
}

static int32_t ui_db_tenths_to_y(int16_t db_tenths)
{
    int32_t value = db_tenths;
    uint32_t usable_height =
        ui_recorded_fft_graph_height() - 2U;

    if (value > 0)
    {
        value = 0;
    }
    else if (value < -900)
    {
        value = -900;
    }

    return UI_GRAPH_TOP + 1 +
           (int32_t)(((-value) * usable_height) / 900);
}

static void ui_draw_signed_db(int32_t x,
                              int32_t y,
                              int16_t db_tenths,
                              uint16_t color)
{
    int32_t whole_db = db_tenths / 10;

    if (whole_db < 0)
    {
        Display_DrawString(x, y, "-", color, COLOR_BLACK);
        ui_draw_uint(x + 8, y, (uint32_t)(-whole_db), color);
    }
    else
    {
        ui_draw_uint(x, y, (uint32_t)whole_db, color);
    }
}

static void ui_format_time_seconds(uint32_t time_ms,
                                   char *buffer,
                                   uint32_t buffer_size)
{
    uint32_t position = 0U;
    uint32_t rounded_hundredths;
    uint32_t whole_seconds;
    uint32_t fraction;

    if ((buffer == NULL) || (buffer_size == 0U))
    {
        return;
    }

    buffer[0] = '\0';
    rounded_hundredths = (time_ms + 5U) / 10U;
    whole_seconds = rounded_hundredths / 100U;
    fraction = rounded_hundredths % 100U;

    position = ui_append_uint_text(buffer,
                                   buffer_size,
                                   position,
                                   whole_seconds,
                                   1U);
    position = ui_append_text(buffer,
                              buffer_size,
                              position,
                              ".");
    position = ui_append_uint_text(buffer,
                                   buffer_size,
                                   position,
                                   fraction,
                                   2U);
    (void)ui_append_text(buffer,
                        buffer_size,
                        position,
                        " s");
}

static void ui_format_dbfs_tenths(int16_t value, char *buffer, uint32_t size)
{
    uint32_t p=0U, mag;
    if(buffer==NULL||size==0U) return;
    buffer[0]='\0';
    if(value<0){ p=ui_append_text(buffer,size,p,"-"); mag=(uint32_t)(-(value+1))+1U; }
    else mag=(uint32_t)value;
    p=ui_append_uint_text(buffer,size,p,mag/10U,1U);
    p=ui_append_text(buffer,size,p,".");
    p=ui_append_uint_text(buffer,size,p,mag%10U,1U);
    (void)ui_append_text(buffer,size,p," dBFS");
}

static void ui_draw_fft_readings(void)
{
    const WavFftViewerInfo *info=WavFftViewer_GetInfo();
    char freq[16],time[16],level[20];
    if(info==NULL||!info->active) return;
    Display_FillRect(34,34,112,18,COLOR_BLACK); Display_DrawString(36,36,"SEG",COLOR_WHITE,COLOR_BLACK); ui_draw_uint(62,36,info->segment_index+1U,COLOR_YELLOW); Display_DrawString(92,36,"/",COLOR_GRAY,COLOR_BLACK); ui_draw_uint(102,36,info->segment_count,COLOR_YELLOW);
    ui_format_time_seconds(info->segment_time_ms,time,sizeof(time)); Display_FillRect(148,34,170,18,COLOR_BLACK); Display_DrawString(150,36,"TIME",COLOR_WHITE,COLOR_BLACK); Display_DrawString(190,36,time,COLOR_CYAN,COLOR_BLACK);
    ui_format_peak_frequency(FFT_GetPeakFrequency(),freq,sizeof(freq)); ui_format_dbfs_tenths(FFT_GetPeakDbTenths(),level,sizeof(level)); Display_FillRect(34,54,284,18,COLOR_BLACK); Display_DrawString(36,56,"PEAK",COLOR_WHITE,COLOR_BLACK); Display_DrawString(74,56,freq,COLOR_GREEN,COLOR_BLACK); Display_DrawString(170,56,"LEVEL",COLOR_WHITE,COLOR_BLACK); Display_DrawString(218,56,level,COLOR_YELLOW,COLOR_BLACK);
}

static bool ui_fft_timeline_contains(uint16_t x, uint16_t y)
{
    return ui.fft_timeline_visible &&
           (x >= UI_FFT_TIMELINE_X) &&
           (x < (UI_FFT_TIMELINE_X + UI_FFT_TIMELINE_WIDTH)) &&
           ((uint32_t)y + UI_FFT_TIMELINE_TOUCH_MARGIN >=
            UI_FFT_TIMELINE_Y) &&
           (y < (UI_FFT_TIMELINE_Y + UI_FFT_TIMELINE_HEIGHT +
                 UI_FFT_TIMELINE_TOUCH_MARGIN));
}

static uint32_t ui_fft_timeline_segment_from_x(uint16_t x)
{
    const WavFftViewerInfo *info = WavFftViewer_GetInfo();
    uint32_t relative_x;

    if ((info == NULL) || !info->active ||
        (info->segment_count <= 1U))
    {
        return 0U;
    }

    if (x <= UI_FFT_TIMELINE_X)
    {
        return 0U;
    }

    if (x >= (UI_FFT_TIMELINE_X + UI_FFT_TIMELINE_WIDTH - 1U))
    {
        return info->segment_count - 1U;
    }

    relative_x = x - UI_FFT_TIMELINE_X;

    return (uint32_t)(
        ((uint64_t)relative_x * (info->segment_count - 1U)) /
        (UI_FFT_TIMELINE_WIDTH - 1U));
}

static void ui_draw_fft_timeline(void)
{
    const WavFftViewerInfo *info = WavFftViewer_GetInfo();
    uint32_t segment_index;
    int32_t cursor_x;

    if (!ui.fft_timeline_visible ||
        (info == NULL) || !info->active ||
        (info->segment_count == 0U))
    {
        return;
    }

    segment_index = ui.fft_timeline_dragging ?
        ui.fft_timeline_preview_segment : info->segment_index;

    if (info->segment_count <= 1U)
    {
        cursor_x = UI_FFT_TIMELINE_X;
    }
    else
    {
        cursor_x = UI_FFT_TIMELINE_X + (int32_t)(
            ((uint64_t)segment_index *
             (UI_FFT_TIMELINE_WIDTH - 1U)) /
            (info->segment_count - 1U));
    }

    Display_FillRect(UI_FFT_TIMELINE_X - 2,
                     UI_FFT_TIMELINE_Y,
                     UI_FFT_TIMELINE_WIDTH + 4U,
                     UI_FFT_TIMELINE_HEIGHT,
                     COLOR_BLACK);

    /* Separate the FFT plot from the timeline without changing touch logic. */
    Display_DrawLine(UI_GRAPH_LEFT,
                     UI_FFT_TIMELINE_SEPARATOR_Y,
                     UI_GRAPH_RIGHT,
                     UI_FFT_TIMELINE_SEPARATOR_Y,
                     COLOR_CYAN);
    Display_DrawLine(UI_GRAPH_LEFT,
                     UI_FFT_TIMELINE_SEPARATOR_Y + 1,
                     UI_GRAPH_RIGHT,
                     UI_FFT_TIMELINE_SEPARATOR_Y + 1,
                     COLOR_DARK_GRAY);


    for (uint32_t event_index = 0U;
         event_index < WavFftViewer_GetEventCount();
         event_index++)
    {
        const WavFftPeakEvent *event =
            WavFftViewer_GetEvent(event_index);
        int32_t event_x;
        uint16_t event_color;

        if ((event == NULL) || !event->valid)
        {
            continue;
        }

        if (info->segment_count <= 1U)
        {
            event_x = UI_FFT_TIMELINE_X;
        }
        else
        {
            event_x = UI_FFT_TIMELINE_X + (int32_t)(
                ((uint64_t)event->segment_index *
                 (UI_FFT_TIMELINE_WIDTH - 1U)) /
                (info->segment_count - 1U));
        }

        event_color = (event_index == 0U) ? COLOR_RED : COLOR_YELLOW;
        Display_DrawLine(event_x,
                         UI_FFT_TIMELINE_Y + 4,
                         event_x,
                         UI_FFT_TIMELINE_LINE_Y - 2,
                         event_color);
        Display_DrawLine(event_x - 2,
                         UI_FFT_TIMELINE_Y + 4,
                         event_x + 2,
                         UI_FFT_TIMELINE_Y + 4,
                         event_color);
    }

    Display_DrawLine(UI_FFT_TIMELINE_X,
                     UI_FFT_TIMELINE_LINE_Y,
                     UI_FFT_TIMELINE_X +
                         (int32_t)UI_FFT_TIMELINE_WIDTH - 1,
                     UI_FFT_TIMELINE_LINE_Y,
                     COLOR_GRAY);
    Display_DrawLine(cursor_x,
                     UI_FFT_TIMELINE_Y + 2,
                     cursor_x,
                     UI_FFT_TIMELINE_Y +
                         (int32_t)UI_FFT_TIMELINE_HEIGHT - 3,
                     ui.fft_timeline_dragging ? COLOR_YELLOW : COLOR_WHITE);
    Display_DrawLine(cursor_x - 2,
                     UI_FFT_TIMELINE_Y + 2,
                     cursor_x + 2,
                     UI_FFT_TIMELINE_Y + 2,
                     ui.fft_timeline_dragging ? COLOR_YELLOW : COLOR_WHITE);
}

static void ui_draw_db_grid(void)
{
    static const int16_t levels[] =
    {
        0,
        -200,
        -400,
        -600,
        -800
    };
    int32_t quarter_width = (int32_t)UI_GRAPH_WIDTH / 4;

    Display_DrawRect(UI_GRAPH_LEFT,
                     UI_GRAPH_TOP,
                     UI_GRAPH_WIDTH,
                     ui_recorded_fft_graph_height(),
                     COLOR_GRAY);

    for (uint32_t index = 0U;
         index < (sizeof(levels) / sizeof(levels[0]));
         index++)
    {
        int32_t y = ui_db_tenths_to_y(levels[index]);

        Display_DrawLine(UI_GRAPH_LEFT,
                         y,
                         UI_GRAPH_RIGHT,
                         y,
                         COLOR_DARK_GRAY);
        ui_draw_signed_db(3, y + 1, levels[index], COLOR_GRAY);
    }

    for (int32_t x = UI_GRAPH_LEFT + quarter_width;
         x < UI_GRAPH_RIGHT;
         x += quarter_width)
    {
        Display_DrawLine(x,
                         UI_GRAPH_TOP,
                         x,
                         ui_recorded_fft_graph_bottom(),
                         COLOR_DARK_GRAY);
    }

}

static void ui_draw_db_spectrum_trace(void)
{
    uint32_t first_bin = ui.first_bin;
    uint32_t last_bin = ui_last_visible_bin();

    if (last_bin <= first_bin)
    {
        return;
    }

    for (uint32_t bin = first_bin + 1U;
         bin <= last_bin;
         bin++)
    {
        int32_t x0 = ui_bin_to_x(bin - 1U);
        int32_t y0 = ui_db_tenths_to_y(
            FFT_GetDbTenths(bin - 1U));
        int32_t x1 = ui_bin_to_x(bin);
        int32_t y1 = ui_db_tenths_to_y(
            FFT_GetDbTenths(bin));

        Display_DrawLine(x0, y0, x1, y1, COLOR_GREEN);
    }
}

static void ui_draw_db_peak_marker(void)
{
    uint32_t peak_bin = FFT_GetPeakBin();
    uint32_t last_bin = ui_last_visible_bin();
    int32_t peak_x;
    int32_t peak_y;
    int32_t marker_top;

    if ((peak_bin < ui.first_bin) ||
        (peak_bin > last_bin))
    {
        return;
    }

    peak_x = ui_bin_to_x(peak_bin);
    peak_y = ui_db_tenths_to_y(
        FFT_GetPeakDbTenths());

    if (peak_y > ui_recorded_fft_graph_bottom())
    {
        peak_y = ui_recorded_fft_graph_bottom();
    }

    marker_top = peak_y - 10;

    if (marker_top < UI_GRAPH_TOP)
    {
        marker_top = UI_GRAPH_TOP;
    }

    Display_DrawLine(peak_x,
                     marker_top,
                     peak_x,
                     peak_y,
                     COLOR_RED);
    Display_DrawLine(peak_x - 3,
                     marker_top,
                     peak_x + 3,
                     marker_top,
                     COLOR_RED);
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
    if (UI_IsSourceSelectorVisible())
    {
        const char *names[5]={"MIC","X","Y","Z","XYZ"};
        Display_FillRect(0,UI_STATUS_TOP,DISPLAY_WIDTH,UI_STATUS_HEIGHT,COLOR_BLACK);
        Display_DrawRect(0,UI_STATUS_TOP,DISPLAY_WIDTH,UI_STATUS_HEIGHT,COLOR_CYAN);
        Display_DrawString(4,UI_STATUS_TOP+8,"SELECT SOURCE",COLOR_CYAN,COLOR_BLACK);
        Display_DrawString(118,UI_STATUS_TOP+8,names[ui.source_selector_value],COLOR_GREEN,COLOR_BLACK);
        Display_DrawString(170,UI_STATUS_TOP+8,
            (ui.source_selector_value==0U)?"MIC UART MODE":"TAP TO APPLY",
            COLOR_YELLOW,COLOR_BLACK);
        return;
    }

    if (UI_IsXYZSavePage())
    {
        Display_FillRect(0,UI_STATUS_TOP,DISPLAY_WIDTH,UI_STATUS_HEIGHT,COLOR_BLACK);
        Display_DrawRect(0,UI_STATUS_TOP,DISPLAY_WIDTH,UI_STATUS_HEIGHT,COLOR_CYAN);
        Display_DrawString(4,UI_STATUS_TOP+8,"XYZ CSV SAVE",COLOR_CYAN,COLOR_BLACK);
        Display_DrawString(112,UI_STATUS_TOP+8,
            (ui.xyz_save_message[0]!='\0') ? ui.xyz_save_message : "SELECT FORMAT",
            COLOR_YELLOW,COLOR_BLACK);
        return;
    }

    if (UI_IsAccelLiveScreen() && (ui.accel_live_source == 3U))
    {
        const char *axis_name[3] = {"X", "Y", "Z"};
        bool any_clip = (ui.accel_xyz_clip[0] | ui.accel_xyz_clip[1] |
                         ui.accel_xyz_clip[2]) != 0U;
        Display_FillRect(0, UI_STATUS_TOP, DISPLAY_WIDTH,
                         UI_STATUS_HEIGHT, COLOR_BLACK);
        Display_DrawRect(0, UI_STATUS_TOP, DISPLAY_WIDTH,
                         UI_STATUS_HEIGHT,
                         any_clip ? COLOR_RED :
                         (ui.accel_live_running ? COLOR_GREEN : COLOR_YELLOW));
        Display_DrawString(4, UI_STATUS_TOP + 8, "XYZ", COLOR_CYAN, COLOR_BLACK);
        Display_DrawString(30, UI_STATUS_TOP + 8,
            ui.accel_live_running ? "L" : "S",
            ui.accel_live_running ? COLOR_GREEN : COLOR_YELLOW, COLOR_BLACK);
        Display_DrawString(42, UI_STATUS_TOP + 8, "X", COLOR_WHITE, COLOR_BLACK);
        ui_draw_uint(52, UI_STATUS_TOP + 8, ui.accel_xyz_rms[0], COLOR_CYAN);
        Display_DrawString(92, UI_STATUS_TOP + 8, "Y", COLOR_WHITE, COLOR_BLACK);
        ui_draw_uint(102, UI_STATUS_TOP + 8, ui.accel_xyz_rms[1], COLOR_CYAN);
        Display_DrawString(142, UI_STATUS_TOP + 8, "Z", COLOR_WHITE, COLOR_BLACK);
        ui_draw_uint(152, UI_STATUS_TOP + 8, ui.accel_xyz_rms[2], COLOR_CYAN);
        Display_DrawString(192, UI_STATUS_TOP + 8, "V", COLOR_WHITE, COLOR_BLACK);
        ui_draw_uint(202, UI_STATUS_TOP + 8, ui.accel_xyz_vector_rms, COLOR_YELLOW);
        Display_DrawString(252, UI_STATUS_TOP + 8, "D", COLOR_WHITE, COLOR_BLACK);
        Display_DrawString(264, UI_STATUS_TOP + 8,
            axis_name[ui.accel_xyz_dominant_axis], COLOR_GREEN, COLOR_BLACK);
        if (any_clip) Display_DrawString(280, UI_STATUS_TOP + 8, "CLIP", COLOR_RED, COLOR_BLACK);
        return;
    }

    if (UI_IsAccelLiveScreen())
    {
        char frequency[16];
        ui_format_peak_frequency(ui.accel_live_peak_hz,
                                 frequency, sizeof(frequency));
        Display_FillRect(0, UI_STATUS_TOP, DISPLAY_WIDTH,
                         UI_STATUS_HEIGHT, COLOR_BLACK);
        Display_DrawRect(0, UI_STATUS_TOP, DISPLAY_WIDTH,
                         UI_STATUS_HEIGHT,
                         ui.accel_live_running ? COLOR_GREEN : COLOR_YELLOW);
        Display_DrawString(4, UI_STATUS_TOP + 8,
            (ui.accel_live_source == 0U) ? "X" :
            (ui.accel_live_source == 1U) ? "Y" :
            (ui.accel_live_source == 2U) ? "Z" : "3",
            COLOR_CYAN, COLOR_BLACK);
        Display_DrawString(16, UI_STATUS_TOP + 8,
            ui.accel_live_running ? "LIVE" : "STOP",
            ui.accel_live_running ? COLOR_GREEN : COLOR_YELLOW, COLOR_BLACK);
        Display_DrawString(46, UI_STATUS_TOP + 8, "A", COLOR_MAGENTA, COLOR_BLACK);
        Display_DrawString(58, UI_STATUS_TOP + 8, "PK", COLOR_WHITE, COLOR_BLACK);
        Display_DrawString(78, UI_STATUS_TOP + 8, frequency, COLOR_GREEN, COLOR_BLACK);
        Display_DrawString(144, UI_STATUS_TOP + 8, "M", COLOR_WHITE, COLOR_BLACK);
        ui_draw_uint(156, UI_STATUS_TOP + 8, ui.accel_live_magnitude, COLOR_YELLOW);
        Display_DrawString(196, UI_STATUS_TOP + 8, "R", COLOR_WHITE, COLOR_BLACK);
        ui_draw_uint(208, UI_STATUS_TOP + 8, ui.accel_live_rms, COLOR_CYAN);
        Display_DrawString(246, UI_STATUS_TOP + 8, "F", COLOR_WHITE, COLOR_BLACK);
        ui_draw_uint(258, UI_STATUS_TOP + 8, ui.accel_live_frames, COLOR_YELLOW);
        return;
    }
    uint32_t first_bin = ui.first_bin;

    if (ui.mode == UI_MODE_RECORD_DURATION)
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
        if (!ui.fft_timeline_visible)
        {
            /* The FFT graph itself now owns rows 216 through 239. */
            return;
        }

        /* Timeline and separator already occupy the former status area. */
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

    if (ui.mode == UI_MODE_RECORD_DURATION)
    {
        if (x < 64U) return UI_BUTTON_DURATION_BACK;
        if (x < 128U) return UI_BUTTON_DURATION_5;
        if (x < 192U) return UI_BUTTON_DURATION_10;
        if (x < 256U) return UI_BUTTON_DURATION_30;
        return UI_BUTTON_DURATION_60;
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
            return UI_BUTTON_FFT_SCAN;
        }
        if (x < DISPLAY_WIDTH)
        {
            return UI_BUTTON_FFT_EXPORT;
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
    if (UI_IsSourceSelectorVisible())
    {
        switch(button)
        {
            case UI_BUTTON_RUN_STOP: ui.pending_action=UI_ACTION_SOURCE_MIC; break;
            case UI_BUTTON_FULL: ui.pending_action=UI_ACTION_SOURCE_X; break;
            case UI_BUTTON_ZOOM_OUT: ui.pending_action=UI_ACTION_SOURCE_Y; break;
            case UI_BUTTON_ZOOM_IN: ui.pending_action=UI_ACTION_SOURCE_Z; break;
            case UI_BUTTON_PEAK: ui.pending_action=UI_ACTION_SOURCE_XYZ; break;
            default: break;
        }
        return;
    }

    if (UI_IsXYZSavePage())
    {
        switch(button)
        {
            case UI_BUTTON_RUN_STOP: ui.pending_action=UI_ACTION_XYZ_SAVE_BACK; break;
            case UI_BUTTON_FULL: ui.pending_action=UI_ACTION_XYZ_SAVE_TIME; break;
            case UI_BUTTON_ZOOM_OUT: ui.pending_action=UI_ACTION_XYZ_SAVE_FFT; break;
            case UI_BUTTON_ZOOM_IN: ui.pending_action=UI_ACTION_XYZ_SAVE_BOTH; break;
            case UI_BUTTON_PEAK: ui.pending_action=UI_ACTION_XYZ_SAVE_FILES; break;
            default: break;
        }
        return;
    }

    switch (button)
    {
        case UI_BUTTON_RUN_STOP:
            if (UI_IsAccelLiveScreen())
            {
                ui.pending_action = ui.accel_live_running ?
                    UI_ACTION_ACCEL_STOP : UI_ACTION_ACCEL_START;
                UART_SendString(ui.accel_live_running ?
                    "UI ACCEL STOP REQUEST\r\n" :
                    "UI ACCEL START REQUEST\r\n");
            }
            else
            {
                UI_SetRunning(!ui.running);
                UART_SendString(ui.running ? "UI RUN\r\n" : "UI STOP\r\n");
            }
            break;

        case UI_BUTTON_FULL:
            if (UI_IsAccelLiveScreen())
            {
                ui.pending_action = UI_ACTION_ACCEL_X;
                break;
            }
            ui_apply_full_span();
            ui.graph_dirty = true;
            UART_SendString("UI FULL SPAN\r\n");
            break;

        case UI_BUTTON_ZOOM_OUT:
            if (UI_IsAccelLiveScreen())
            {
                ui.pending_action = UI_ACTION_ACCEL_Y;
                break;
            }
            ui_zoom_out();
            ui.graph_dirty = true;
            UART_SendString("UI ZOOM OUT\r\n");
            break;

        case UI_BUTTON_ZOOM_IN:
            if (UI_IsAccelLiveScreen())
            {
                ui.pending_action = UI_ACTION_ACCEL_Z;
                break;
            }
            ui_zoom_in();
            ui.graph_dirty = true;
            UART_SendString("UI ZOOM IN\r\n");
            break;

        case UI_BUTTON_PEAK:
            if (UI_IsAccelLiveScreen())
            {
                ui.pending_action = UI_ACTION_ACCEL_XYZ;
                break;
            }
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
            UI_ShowRecordDurationSelector();
            UART_SendString("UI RECORD DURATION SCREEN\r\n");
            break;

        case UI_BUTTON_DURATION_BACK:
            UI_ShowRecorder();
            UART_SendString("UI RECORD DURATION BACK\r\n");
            break;

        case UI_BUTTON_DURATION_5:
            ui.requested_recording_seconds = 5U;
            ui.pending_action = UI_ACTION_RECORD_DURATION;
            UI_ShowRecorder();
            UART_SendString("UI RECORD 5 REQUEST\r\n");
            break;

        case UI_BUTTON_DURATION_10:
            ui.requested_recording_seconds = 10U;
            ui.pending_action = UI_ACTION_RECORD_DURATION;
            UI_ShowRecorder();
            UART_SendString("UI RECORD 10 REQUEST\r\n");
            break;

        case UI_BUTTON_DURATION_30:
            ui.requested_recording_seconds = 30U;
            ui.pending_action = UI_ACTION_RECORD_DURATION;
            UI_ShowRecorder();
            UART_SendString("UI RECORD 30 REQUEST\r\n");
            break;

        case UI_BUTTON_DURATION_60:
            ui.requested_recording_seconds = 60U;
            ui.pending_action = UI_ACTION_RECORD_DURATION;
            UI_ShowRecorder();
            UART_SendString("UI RECORD 60 REQUEST\r\n");
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
            ui.fft_timeline_visible = false;
            ui.fft_timeline_dragging = false;
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

        case UI_BUTTON_FFT_SCAN:
            ui.pending_action = UI_ACTION_FFT_SCAN;
            UART_SendString("UI RECORDED FFT SCAN REQUEST\r\n");
            break;

        case UI_BUTTON_FFT_EXPORT:
            ui.pending_action = UI_ACTION_FFT_EXPORT;
            UART_SendString("UI RECORDED FFT EXPORT REQUEST\r\n");
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
    ui.touch_down_ms = HAL_GetTick();

    ui.previous_touch_x = touch->x;
    ui.previous_touch_y = touch->y;

    ui.touch_armed = false;

    if ((ui.mode == UI_MODE_RECORDED_FFT) &&
        ui_fft_timeline_contains(touch->x, touch->y))
    {
        ui.fft_timeline_dragging = true;
        ui.fft_timeline_preview_segment =
            ui_fft_timeline_segment_from_x(touch->x);
        ui.requested_fft_segment =
            ui.fft_timeline_preview_segment;
        ui.fft_timeline_last_interaction_ms = HAL_GetTick();
        ui.graph_dirty = true;
        ui.pressed_button = UI_BUTTON_NONE;
    }
    else
    {
        ui.pressed_button = ui_hit_test_button(touch->x, touch->y);
    }
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

    if ((ui.mode == UI_MODE_RECORDED_FFT) &&
        ui.fft_timeline_visible &&
        ui.fft_timeline_dragging)
    {
        ui.fft_timeline_last_interaction_ms = HAL_GetTick();
    }

    if ((ui.mode == UI_MODE_RECORDED_FFT) &&
        ui.fft_timeline_dragging)
    {
        ui.fft_timeline_preview_segment =
            ui_fft_timeline_segment_from_x(touch->x);
        ui.requested_fft_segment =
            ui.fft_timeline_preview_segment;
        ui.fft_timeline_last_interaction_ms = HAL_GetTick();
        ui.graph_dirty = true;
    }
    else if ((ui.mode == UI_MODE_ANALYZER) &&
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

    if ((ui.mode == UI_MODE_RECORDED_FFT) &&
        ui.fft_timeline_dragging)
    {
        ui.requested_fft_segment =
            ui.fft_timeline_preview_segment;
        ui.pending_action = UI_ACTION_FFT_LOAD_SEGMENT;
        ui.fft_timeline_dragging = false;
        ui.fft_timeline_visible = true;
        ui.fft_timeline_last_interaction_ms = HAL_GetTick();
        UART_SendString("UI RECORDED FFT TIMELINE REQUEST\r\n");
        action_executed = true;
    }
    /* UI_FILES_TOUCH_FIX
     * Require the press to begin inside FILES, but allow the release
     * to drift slightly because this is a resistive touchscreen.
     */
    else if (!ui.dragging &&
             (ui.mode == UI_MODE_RECORDED_FFT) &&
             (ui.touch_start_y >= UI_GRAPH_TOP) &&
             (ui.touch_start_y <= UI_GRAPH_BOTTOM) &&
             (ui.previous_touch_y >= UI_GRAPH_TOP) &&
             (ui.previous_touch_y <= UI_GRAPH_BOTTOM))
    {
        ui.fft_timeline_visible = !ui.fft_timeline_visible;
        ui.fft_timeline_last_interaction_ms = HAL_GetTick();
        ui.graph_dirty = true;
        UART_SendString(ui.fft_timeline_visible ?
            "UI FFT TIMELINE SHOW\r\n" :
            "UI FFT TIMELINE HIDE\r\n");
        action_executed = true;
    }
    else if ((ui.mode == UI_MODE_RECORDER) &&
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
            if ((ui.pressed_button == UI_BUTTON_FFT_EXPORT) &&
                ((HAL_GetTick() - ui.touch_down_ms) >=
                 UI_FFT_EXPORT_ALL_HOLD_MS))
            {
                ui.pending_action = UI_ACTION_FFT_EXPORT_ALL;
                UART_SendString(
                    "UI RECORDED FFT EXPORT ALL REQUEST\r\n");
            }
            else
            {
                ui_execute_button(ui.pressed_button);
            }
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
