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
    UI_ACTION_FFT_SCAN,
    UI_ACTION_FFT_EXPORT,
    UI_ACTION_FFT_EXPORT_ALL,
    UI_ACTION_FFT_LOAD_SEGMENT,
    UI_ACTION_BROWSER_OPEN,
    UI_ACTION_BROWSER_BACK,
    UI_ACTION_BROWSER_PREVIOUS,
    UI_ACTION_BROWSER_NEXT,
    UI_ACTION_BROWSER_REFRESH,
    UI_ACTION_BROWSER_SELECT_ROW,
    UI_ACTION_FILE_ACTIONS_OPEN,
    UI_ACTION_FILE_ACTIONS_BACK,
    UI_ACTION_FILE_INFO,
    UI_ACTION_FILE_DELETE,
    UI_ACTION_FILE_DELETE_CANCEL,
    UI_ACTION_FILE_DELETE_CONFIRM,
    UI_ACTION_FILE_RENAME_OPEN,
    UI_ACTION_FILE_RENAME_SAVE,
    UI_ACTION_RECORD_DURATION,
    UI_ACTION_ACCEL_START,
    UI_ACTION_ACCEL_STOP,
    UI_ACTION_ACCEL_X,
    UI_ACTION_ACCEL_Y,
    UI_ACTION_ACCEL_Z,
    UI_ACTION_ACCEL_XYZ,
    UI_ACTION_XYZ_SAVE_BACK,
    UI_ACTION_XYZ_SAVE_TIME,
    UI_ACTION_XYZ_SAVE_FFT,
    UI_ACTION_XYZ_SAVE_BOTH,
    UI_ACTION_XYZ_SAVE_FILES
} UI_Action;

/* UI_WAV_INFO_SCREEN_PATCH */
typedef struct
{
    char filename[13];
    uint32_t file_size;
    uint32_t sample_rate;
    uint32_t data_bytes;
    uint32_t duration_ms;
    uint16_t channels;
    uint16_t bits_per_sample;
    bool valid;
} UI_WavInfo;

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

/* UI_LIGHTWEIGHT_PROGRESS_V2_PATCH */
void UI_ProcessRecorderDisplay(void);
void UI_ResetRecordingProgress(void);
void UI_NotifySpectrumChanged(void);

/* ACCEL_TOUCH_LIVE_UI_V4 */
void UI_ShowAccelLive(void);
bool UI_IsAccelLiveScreen(void);
void UI_SetAccelLiveStatus(bool running, uint32_t frames,
                           uint32_t peak_hz, uint32_t magnitude,
                           uint32_t rms);
void UI_SetAccelSource(uint32_t source);
uint32_t UI_GetAccelSource(void);
void UI_ShowXYZSavePage(void);
void UI_HideXYZSavePage(void);
bool UI_IsXYZSavePage(void);
void UI_SetXYZSaveMessage(const char *message);
void UI_SetAccelXYZSummary(bool running,
                           uint32_t frames,
                           uint32_t view_axis,
                           const uint32_t rms[3],
                           const uint32_t peak_hz[3],
                           const uint32_t peak_mag[3],
                           const uint8_t clip_flags[3],
                           uint32_t vector_rms,
                           uint32_t dominant_axis);

UI_Action UI_GetPendingAction(void);

/* RECORDED_FFT_TIMELINE_SCRUBBER_PATCH */
uint32_t UI_GetRequestedFftSegment(void);
void UI_SetRecorderStatus(
    const UI_RecorderStatus *status);
bool UI_IsRecorderScreen(void);

/* UI_RECORD_DURATION_SELECTOR_PATCH */
void UI_ShowRecordDurationSelector(void);
bool UI_IsRecordDurationSelectorScreen(void);
uint32_t UI_GetRequestedRecordingSeconds(void);

/* UI_SD_SPACE_LOW_MESSAGE_PATCH */
void UI_ShowSdSpaceLow(
    uint32_t required_kib,
    uint32_t available_kib);
void UI_ClearRecorderMessage(void);
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
void UI_ShowWavInfo(const UI_WavInfo *info);
bool UI_IsWavInfoScreen(void);


/* UI_DELETE_CONFIRMATION_PATCH */
void UI_ShowDeleteConfirmation(const char *filename);
void UI_ReturnToFileActions(void);
bool UI_IsDeleteConfirmationScreen(void);
const char *UI_GetDeleteFilename(void);

/* UI_WAV_RENAME_PATCH */
void UI_ShowRenameEditor(const char *filename);
bool UI_IsRenameEditorScreen(void);
const char *UI_GetRenameSource(void);
const char *UI_GetRenameTarget(void);


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
