#include "display.h"

#include <stddef.h>
#include <stdint.h>
#define FRAMEBUFFER_PHYSICAL_WIDTH   240U
#define FRAMEBUFFER_PHYSICAL_HEIGHT  320U

static volatile uint16_t *display_framebuffer;

void Display_Init(uint32_t framebuffer_address)
{
    display_framebuffer = (volatile uint16_t *)framebuffer_address;
}

void Display_DrawPixel(int32_t x,
                       int32_t y,
                       uint16_t color)
{
    int32_t physical_x;
    int32_t physical_y;

    if (display_framebuffer == NULL)
    {
        return;
    }

    /* Check logical 320 x 240 landscape coordinates. */
    if ((x < 0) ||
        (y < 0) ||
        (x >= (int32_t)DISPLAY_WIDTH) ||
        (y >= (int32_t)DISPLAY_HEIGHT))
    {
        return;
    }

    /*
     * Rotate logical coordinates 90 degrees clockwise into the
     * physical 240 x 320 framebuffer.
     */
    physical_x =
        (int32_t)FRAMEBUFFER_PHYSICAL_WIDTH - 1 - y;

    physical_y = x;

    display_framebuffer[
        ((uint32_t)physical_y *
         FRAMEBUFFER_PHYSICAL_WIDTH) +
        (uint32_t)physical_x
    ] = color;
}
void Display_Clear(uint16_t color)
{
    if (display_framebuffer == NULL)
    {
        return;
    }

    for (uint32_t i = 0U;
         i < (FRAMEBUFFER_PHYSICAL_WIDTH *
              FRAMEBUFFER_PHYSICAL_HEIGHT);
         i++)
    {
        display_framebuffer[i] = color;
    }
}

void Display_DrawLine(int32_t x0,
                      int32_t y0,
                      int32_t x1,
                      int32_t y1,
                      uint16_t color)
{
    int32_t dx = x1 - x0;
    int32_t dy = y1 - y0;
    int32_t step_x;
    int32_t step_y;
    int32_t error;

    if (dx < 0)
    {
        dx = -dx;
    }

    if (dy < 0)
    {
        dy = -dy;
    }

    step_x = (x0 < x1) ? 1 : -1;
    step_y = (y0 < y1) ? 1 : -1;
    error = dx - dy;

    while (1)
    {
        int32_t doubled_error;

        Display_DrawPixel(x0, y0, color);

        if ((x0 == x1) && (y0 == y1))
        {
            break;
        }

        doubled_error = error * 2;

        if (doubled_error > -dy)
        {
            error -= dy;
            x0 += step_x;
        }

        if (doubled_error < dx)
        {
            error += dx;
            y0 += step_y;
        }
    }
}

void Display_FillRect(int32_t x,
                      int32_t y,
                      int32_t width,
                      int32_t height,
                      uint16_t color)
{
    if ((width <= 0) || (height <= 0))
    {
        return;
    }

    for (int32_t row = 0; row < height; row++)
    {
        for (int32_t column = 0; column < width; column++)
        {
            Display_DrawPixel(x + column, y + row, color);
        }
    }
}

void Display_DrawRect(int32_t x,
                      int32_t y,
                      int32_t width,
                      int32_t height,
                      uint16_t color)
{
    if ((width <= 0) || (height <= 0))
    {
        return;
    }

    Display_DrawLine(x, y, x + width - 1, y, color);
    Display_DrawLine(x,
                     y + height - 1,
                     x + width - 1,
                     y + height - 1,
                     color);
    Display_DrawLine(x, y, x, y + height - 1, color);
    Display_DrawLine(x + width - 1,
                     y,
                     x + width - 1,
                     y + height - 1,
                     color);
}
