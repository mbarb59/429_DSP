#ifndef F429_ANALYZER_UI_H
#define F429_ANALYZER_UI_H

#include <stdbool.h>
#include <stdint.h>

/* UI_RECORDER_MODE_PATCH */
typedef enum
{
    UI_ACTION_NONE = 0,
    UI_ACTION_RECORD_5,
    UI_ACTION_RECORD_STOP,
    UI_ACTION_VIEW_WAVEFORM,
    UI_ACTION_VIEW_SPECTRUM,
    UI_ACTION_FFT_BACK,
    UI_ACTION_FFT_PREVIOUS,
    UI_ACTION_FFT_NEXT,
    UI_ACTION_BROWSER_OPEN,
    UI_ACTION_BROWSER_BACK,
    UI_ACTION_BROWSER_PREVIOUS,
    UI_ACTION_BROWSER_NEXT,
    UI_ACTION_BROWSER_REFRESH,
    UI_ACTION_BROWSER_SELECT_ROW,
    UI_ACTION_FILE_ACTIONS_OPEN,
    UI_ACTION_FILE_ACTIONS_BACK,
    UI_ACTION_FILE_INFO,
    UI_ACTION_FILE_DELETE
} UI_Action;

typedef struct
{
    uint32_t recorder_state;
    uint32_t captured_samples;
    uint32_t queued_bytes;
    uint32_t dropped_samples;
    uint32_t write_errors;
    uint32_t dma_overruns;
    uint32_t rms_counts;
    uint32_t clip_count;
    bool clipping;
    bool capture_active;
} UI_RecorderStatus;

void UI_Init(void);
void UI_Draw(void);
void UI_DrawSpectrum(void);
void UI_ProcessTouch(void);
void UI_NotifySpectrumChanged(void);

UI_Action UI_GetPendingAction(void);
void UI_SetRecorderStatus(
    const UI_RecorderStatus *status);
bool UI_IsRecorderScreen(void);
void UI_ShowRecorder(void);
void UI_ShowWaveform(void);
bool UI_IsWaveformScreen(void);
void UI_ShowRecordedFft(void);
bool UI_IsRecordedFftScreen(void);
void UI_ShowFileBrowser(void);
bool UI_IsFileBrowserScreen(void);
void UI_RefreshFileBrowser(void);
uint32_t UI_GetBrowserPendingRow(void);

/* UI_FILE_ACTION_SCREEN_PATCH */
void UI_ShowFileActions(void);
void UI_ReturnToFileBrowser(void);
bool UI_IsFileActionsScreen(void);


void UI_SetRunning(bool running);
bool UI_IsRunning(void);

void UI_FullSpan(void);
void UI_OnFFTConfigurationChanged(void);
void UI_ZoomIn(void);
void UI_ZoomOut(void);
void UI_CenterOnPeak(void);

uint32_t UI_GetFirstBin(void);
uint32_t UI_GetVisibleBins(void);
uint32_t UI_GetMagnitudeMax(void);

#endif /* F429_ANALYZER_UI_H */
