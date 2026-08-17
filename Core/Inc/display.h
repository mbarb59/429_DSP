#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

#define DISPLAY_WIDTH   320U
#define DISPLAY_HEIGHT  240U

#define COLOR_BLACK      0x0000U
#define COLOR_WHITE      0xFFFFU
#define COLOR_RED        0xF800U
#define COLOR_GREEN      0x07E0U
#define COLOR_BLUE       0x001FU
#define COLOR_YELLOW     0xFFE0U
#define COLOR_CYAN       0x07FFU
#define COLOR_MAGENTA    0xF81FU
#define COLOR_GRAY       0x8410U
#define COLOR_DARK_GRAY  0x4208U

void Display_Init(uint32_t framebuffer_address);
void Display_Clear(uint16_t color);
void Display_DrawPixel(int32_t x, int32_t y, uint16_t color);
void Display_DrawLine(int32_t x0,
                      int32_t y0,
                      int32_t x1,
                      int32_t y1,
                      uint16_t color);
void Display_FillRect(int32_t x,
                      int32_t y,
                      int32_t width,
                      int32_t height,
                      uint16_t color);
void Display_DrawRect(int32_t x,
                      int32_t y,
                      int32_t width,
                      int32_t height,
                      uint16_t color);

#endif /* DISPLAY_H */
