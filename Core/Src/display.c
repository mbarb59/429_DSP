#include "display.h"

#include <stddef.h>
#include <stdint.h>

#define FRAMEBUFFER_PHYSICAL_WIDTH   240U
#define FRAMEBUFFER_PHYSICAL_HEIGHT  320U

static volatile uint16_t *display_framebuffer;

/*
 * Return one vertical column of a compact 5 x 7 bitmap character.
 * Bit 0 is the top pixel and bit 6 is the bottom pixel.
 *
 * The initial character set supports frequency labels:
 * digits 0 through 9, space, uppercase H, and lowercase z.
 */
/*
 * DISPLAY_FULL_UPPERCASE_FONT_PATCH
 *
 * Return one vertical column of a compact 5 x 7 bitmap character.
 * Bit 0 is the top pixel and bit 6 is the bottom pixel.
 */
static uint8_t Display_FontColumn(char character, uint32_t column)
{
    static const uint8_t digit_font[10][DISPLAY_FONT_WIDTH] =
    {
        {0x3EU, 0x51U, 0x49U, 0x45U, 0x3EU}, /* 0 */
        {0x00U, 0x42U, 0x7FU, 0x40U, 0x00U}, /* 1 */
        {0x42U, 0x61U, 0x51U, 0x49U, 0x46U}, /* 2 */
        {0x21U, 0x41U, 0x45U, 0x4BU, 0x31U}, /* 3 */
        {0x18U, 0x14U, 0x12U, 0x7FU, 0x10U}, /* 4 */
        {0x27U, 0x45U, 0x45U, 0x45U, 0x39U}, /* 5 */
        {0x3CU, 0x4AU, 0x49U, 0x49U, 0x30U}, /* 6 */
        {0x01U, 0x71U, 0x09U, 0x05U, 0x03U}, /* 7 */
        {0x36U, 0x49U, 0x49U, 0x49U, 0x36U}, /* 8 */
        {0x06U, 0x49U, 0x49U, 0x29U, 0x1EU}  /* 9 */
    };

    static const uint8_t uppercase_font[26][DISPLAY_FONT_WIDTH] =
    {
        {0x7EU, 0x11U, 0x11U, 0x11U, 0x7EU}, /* A */
        {0x7FU, 0x49U, 0x49U, 0x49U, 0x36U}, /* B */
        {0x3EU, 0x41U, 0x41U, 0x41U, 0x22U}, /* C */
        {0x7FU, 0x41U, 0x41U, 0x22U, 0x1CU}, /* D */
        {0x7FU, 0x49U, 0x49U, 0x49U, 0x41U}, /* E */
        {0x7FU, 0x09U, 0x09U, 0x09U, 0x01U}, /* F */
        {0x3EU, 0x41U, 0x49U, 0x49U, 0x7AU}, /* G */
        {0x7FU, 0x08U, 0x08U, 0x08U, 0x7FU}, /* H */
        {0x00U, 0x41U, 0x7FU, 0x41U, 0x00U}, /* I */
        {0x20U, 0x40U, 0x41U, 0x3FU, 0x01U}, /* J */
        {0x7FU, 0x08U, 0x14U, 0x22U, 0x41U}, /* K */
        {0x7FU, 0x40U, 0x40U, 0x40U, 0x40U}, /* L */
        {0x7FU, 0x02U, 0x0CU, 0x02U, 0x7FU}, /* M */
        {0x7FU, 0x04U, 0x08U, 0x10U, 0x7FU}, /* N */
        {0x3EU, 0x41U, 0x41U, 0x41U, 0x3EU}, /* O */
        {0x7FU, 0x09U, 0x09U, 0x09U, 0x06U}, /* P */
        {0x3EU, 0x41U, 0x51U, 0x21U, 0x5EU}, /* Q */
        {0x7FU, 0x09U, 0x19U, 0x29U, 0x46U}, /* R */
        {0x46U, 0x49U, 0x49U, 0x49U, 0x31U}, /* S */
        {0x01U, 0x01U, 0x7FU, 0x01U, 0x01U}, /* T */
        {0x3FU, 0x40U, 0x40U, 0x40U, 0x3FU}, /* U */
        {0x1FU, 0x20U, 0x40U, 0x20U, 0x1FU}, /* V */
        {0x3FU, 0x40U, 0x38U, 0x40U, 0x3FU}, /* W */
        {0x63U, 0x14U, 0x08U, 0x14U, 0x63U}, /* X */
        {0x07U, 0x08U, 0x70U, 0x08U, 0x07U}, /* Y */
        {0x61U, 0x51U, 0x49U, 0x45U, 0x43U}  /* Z */
    };

    static const uint8_t lowercase_z[DISPLAY_FONT_WIDTH] =
    {
        0x44U, 0x64U, 0x54U, 0x4CU, 0x44U
    };

    static const uint8_t dash[DISPLAY_FONT_WIDTH] =
    {
        0x08U, 0x08U, 0x08U, 0x08U, 0x08U
    };

    static const uint8_t period[DISPLAY_FONT_WIDTH] =
    {
        0x00U, 0x60U, 0x60U, 0x00U, 0x00U
    };

    static const uint8_t colon[DISPLAY_FONT_WIDTH] =
    {
        0x00U, 0x36U, 0x36U, 0x00U, 0x00U
    };

    static const uint8_t slash[DISPLAY_FONT_WIDTH] =
    {
        0x20U, 0x10U, 0x08U, 0x04U, 0x02U
    };

    if (column >= DISPLAY_FONT_WIDTH)
    {
        return 0U;
    }

    if ((character >= '0') && (character <= '9'))
    {
        return digit_font[(uint32_t)(character - '0')][column];
    }

    if ((character >= 'A') && (character <= 'Z'))
    {
        return uppercase_font[(uint32_t)(character - 'A')][column];
    }

    if (character == 'z')
    {
        return lowercase_z[column];
    }

    if (character == '-')
    {
        return dash[column];
    }

    if (character == '.')
    {
        return period[column];
    }

    if (character == ':')
    {
        return colon[column];
    }

    if (character == '/')
    {
        return slash[column];
    }

    /* Spaces and unsupported characters are rendered blank. */
    return 0U;
}

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
    uint32_t pixel_index;

    if (display_framebuffer == NULL)
    {
        return;
    }

    /* Validate logical 320 x 240 landscape coordinates. */
    if ((x < 0) ||
        (y < 0) ||
        (x >= (int32_t)DISPLAY_WIDTH) ||
        (y >= (int32_t)DISPLAY_HEIGHT))
    {
        return;
    }

    /* Map logical landscape coordinates to the 240 x 320 framebuffer. */
    physical_x = y;
    physical_y = x;

    pixel_index =
        ((uint32_t)physical_y * FRAMEBUFFER_PHYSICAL_WIDTH) +
        (uint32_t)physical_x;

    display_framebuffer[pixel_index] = color;
}

void Display_Clear(uint16_t color)
{
    if (display_framebuffer == NULL)
    {
        return;
    }

    for (uint32_t i = 0U;
         i < (FRAMEBUFFER_PHYSICAL_WIDTH * FRAMEBUFFER_PHYSICAL_HEIGHT);
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

void Display_DrawChar(int32_t x,
                      int32_t y,
                      char character,
                      uint16_t foreground,
                      uint16_t background)
{
    for (uint32_t column = 0U;
         column < DISPLAY_FONT_WIDTH;
         column++)
    {
        uint8_t column_bits = Display_FontColumn(character, column);

        for (uint32_t row = 0U;
             row < DISPLAY_FONT_HEIGHT;
             row++)
        {
            uint16_t color = background;

            if ((column_bits & (uint8_t)(1U << row)) != 0U)
            {
                color = foreground;
            }

            Display_DrawPixel(x + (int32_t)column,
                              y + (int32_t)row,
                              color);
        }
    }

    /* Draw one blank column between adjacent characters. */
    for (uint32_t row = 0U;
         row < DISPLAY_FONT_HEIGHT;
         row++)
    {
        Display_DrawPixel(x + (int32_t)DISPLAY_FONT_WIDTH,
                          y + (int32_t)row,
                          background);
    }
}

void Display_DrawString(int32_t x,
                        int32_t y,
                        const char *text,
                        uint16_t foreground,
                        uint16_t background)
{
    int32_t character_x = x;

    if (text == NULL)
    {
        return;
    }

    while (*text != '\0')
    {
        Display_DrawChar(character_x,
                         y,
                         *text,
                         foreground,
                         background);

        character_x += (int32_t)(DISPLAY_FONT_WIDTH + 1U);
        text++;
    }
}
