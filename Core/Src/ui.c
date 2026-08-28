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

typedef enum
{
    UI_BUTTON_NONE = 0,
    UI_BUTTON_RUN_STOP,
    UI_BUTTON_FULL,
    UI_BUTTON_ZOOM_OUT,
    UI_BUTTON_ZOOM_IN,
    UI_BUTTON_PEAK
} UI_Button;

typedef struct
{
    bool initialized;
    bool running;
    bool touch_active;
    bool dragging;
    bool graph_dirty;
    bool controls_dirty;

    uint32_t first_bin;
    uint32_t visible_bins;
    uint32_t magnitude_max;

    uint16_t touch_start_x;
    uint16_t touch_start_y;
    uint16_t previous_touch_x;
    uint16_t previous_touch_y;

    uint32_t last_touch_poll_ms;
} UI_State;

static UI_State ui;

static void ui_draw_controls(void);
static void ui_draw_status(void);
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

    ui.first_bin = UI_FIRST_USABLE_BIN;
    ui.visible_bins = ui_full_visible_bins();
    ui.magnitude_max = UI_DEFAULT_MAGNITUDE_MAX;

    ui.touch_start_x = 0U;
    ui.touch_start_y = 0U;
    ui.previous_touch_x = 0U;
    ui.previous_touch_y = 0U;
    ui.last_touch_poll_ms = HAL_GetTick();

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

    if (touch.pressed)
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
}

static UI_Button ui_hit_test_button(uint16_t x, uint16_t y)
{
    if (y >= UI_CONTROL_HEIGHT)
    {
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

    if ((ui.touch_start_y >= UI_GRAPH_TOP) &&
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
    if (ui.touch_start_y < UI_CONTROL_HEIGHT)
    {
        UI_Button button;

        button = ui_hit_test_button(
            ui.touch_start_x,
            ui.touch_start_y);

        ui_execute_button(button);
    }

    ui.touch_active = false;
    ui.dragging = false;
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
