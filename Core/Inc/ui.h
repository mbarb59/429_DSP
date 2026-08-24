#ifndef F429_ANALYZER_UI_H
#define F429_ANALYZER_UI_H

#include <stdbool.h>
#include <stdint.h>

void UI_Init(void);
void UI_Draw(void);
void UI_DrawSpectrum(void);
void UI_ProcessTouch(void);
void UI_NotifySpectrumChanged(void);

void UI_SetRunning(bool running);
bool UI_IsRunning(void);

void UI_FullSpan(void);
void UI_ZoomIn(void);
void UI_ZoomOut(void);
void UI_CenterOnPeak(void);

uint32_t UI_GetFirstBin(void);
uint32_t UI_GetVisibleBins(void);
uint32_t UI_GetMagnitudeMax(void);

#endif /* F429_ANALYZER_UI_H */
