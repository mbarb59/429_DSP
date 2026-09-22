#!/usr/bin/env python3
# Add live RMS and clipping indicators to the recorder UI.

from pathlib import Path
import re
import shutil

ROOT = Path.cwd()
CH = ROOT / "Core/Inc/capture.h"
CC = ROOT / "Core/Src/capture.c"
UH = ROOT / "Core/Inc/ui.h"
UC = ROOT / "Core/Src/ui.c"
MC = ROOT / "Core/Src/main.c"
MARKER = "RECORDER_RMS_CLIPPING_PATCH"


def fail(message):
    raise SystemExit(message)


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        fail(f"Expected exactly one {label}, found {count}")
    return text.replace(old, new, 1)


def backup(path):
    target = path.with_name(path.name + ".before_rms_clipping")
    if not target.exists():
        shutil.copy2(path, target)


for path in (CH, CC, UH, UC, MC):
    if not path.exists():
        fail(f"Missing required file: {path}")

capture_h = CH.read_text(encoding="utf-8")
capture_c = CC.read_text(encoding="utf-8")
ui_h = UH.read_text(encoding="utf-8")
ui_c = UC.read_text(encoding="utf-8")
main_c = MC.read_text(encoding="utf-8")

for path, text in ((CH, capture_h), (CC, capture_c),
                   (UH, ui_h), (UC, ui_c), (MC, main_c)):
    if MARKER in text:
        fail(f"Patch already installed in {path}")
    if re.search(r"&(lt|gt|amp);|<br", text):
        fail(f"HTML artifact found in {path}")

for path in (CH, CC, UH, UC, MC):
    backup(path)

capture_h = replace_once(
    capture_h,
    "uint32_t Capture_GetRecorderOverruns(void);\n",
    """uint32_t Capture_GetRecorderOverruns(void);

/* RECORDER_RMS_CLIPPING_PATCH */
uint32_t Capture_GetRecorderRms(void);
uint32_t Capture_GetRecorderClipCount(void);
bool Capture_IsRecorderClipping(void);
""",
    "capture statistics API anchor",
)

capture_c = replace_once(
    capture_c,
    "static volatile uint32_t recorder_dma_overruns;\n",
    """static volatile uint32_t recorder_dma_overruns;

/* RECORDER_RMS_CLIPPING_PATCH */
#define RECORDER_CLIP_LOW_THRESHOLD   16U
#define RECORDER_CLIP_HIGH_THRESHOLD  4079U
static volatile uint32_t recorder_rms_counts;
static volatile uint32_t recorder_clip_count;
static volatile uint8_t recorder_clipping;
""",
    "recorder overrun state",
)

statistics_code = r'''static uint32_t recorder_integer_sqrt(uint64_t value)
{
    uint64_t bit = (uint64_t)1U << 62U;
    uint64_t result = 0U;

    while (bit > value)
    {
        bit >>= 2U;
    }

    while (bit != 0U)
    {
        if (value >= (result + bit))
        {
            value -= result + bit;
            result = (result >> 1U) + bit;
        }
        else
        {
            result >>= 1U;
        }
        bit >>= 2U;
    }

    return (uint32_t)result;
}

static void recorder_update_signal_statistics(
    const uint16_t *samples,
    uint32_t sample_count)
{
    uint64_t square_sum = 0U;
    uint32_t clipped = 0U;

    if ((samples == NULL) || (sample_count == 0U))
    {
        recorder_rms_counts = 0U;
        return;
    }

    for (uint32_t index = 0U; index < sample_count; index++)
    {
        uint32_t adc_sample = samples[index] & 0x0FFFU;
        int32_t centered = (int32_t)adc_sample - 2048;

        square_sum +=
            (uint64_t)((int64_t)centered * (int64_t)centered);

        if ((adc_sample <= RECORDER_CLIP_LOW_THRESHOLD) ||
            (adc_sample >= RECORDER_CLIP_HIGH_THRESHOLD))
        {
            clipped++;
        }
    }

    recorder_rms_counts = recorder_integer_sqrt(
        square_sum / sample_count);

    if (clipped != 0U)
    {
        recorder_clip_count += clipped;
        recorder_clipping = 1U;
    }
}

'''

start_anchor = "bool Capture_StartRecorder(uint32_t duration_seconds)\n"
capture_c = replace_once(
    capture_c,
    start_anchor,
    statistics_code + start_anchor,
    "Capture_StartRecorder definition",
)

capture_c = replace_once(
    capture_c,
    """    recorder_dma_overruns = 0U;
    recorder_capture_active = 0U;
""",
    """    recorder_dma_overruns = 0U;
    recorder_rms_counts = 0U;
    recorder_clip_count = 0U;
    recorder_clipping = 0U;
    recorder_capture_active = 0U;
""",
    "recorder start reset block",
)

capture_c = replace_once(
    capture_c,
    """    if (!Recorder_SubmitAdcSamples(completed_samples, completed_count))
""",
    """    recorder_update_signal_statistics(
        completed_samples,
        completed_count);

    if (!Recorder_SubmitAdcSamples(completed_samples, completed_count))
""",
    "recorder sample submission",
)

capture_c = replace_once(
    capture_c,
    """uint32_t Capture_GetRecorderOverruns(void)
{
    return recorder_dma_overruns;
}
""",
    r'''uint32_t Capture_GetRecorderOverruns(void)
{
    return recorder_dma_overruns;
}

uint32_t Capture_GetRecorderRms(void)
{
    return recorder_rms_counts;
}

uint32_t Capture_GetRecorderClipCount(void)
{
    return recorder_clip_count;
}

bool Capture_IsRecorderClipping(void)
{
    return recorder_clipping != 0U;
}
''',
    "recorder overrun getter",
)

ui_h = replace_once(
    ui_h,
    """    uint32_t dma_overruns;
    bool capture_active;
""",
    """    uint32_t dma_overruns;
    uint32_t rms_counts;
    uint32_t clip_count;
    bool clipping;
    bool capture_active;
""",
    "UI recorder status fields",
)

ui_c = replace_once(
    ui_c,
    """    ui.recorder_status.dma_overruns = 0U;
    ui.recorder_status.capture_active = false;
""",
    """    ui.recorder_status.dma_overruns = 0U;
    ui.recorder_status.rms_counts = 0U;
    ui.recorder_status.clip_count = 0U;
    ui.recorder_status.clipping = false;
    ui.recorder_status.capture_active = false;
""",
    "UI recorder status initialization",
)

ui_c = replace_once(
    ui_c,
    """    Display_DrawString(188, 102, "WAVE/FFT", COLOR_GRAY, COLOR_BLACK);
    Display_DrawString(188, 116, "NEXT PATCH", COLOR_GRAY, COLOR_BLACK);
""",
    r'''    Display_DrawString(188, 102, "RMS", COLOR_WHITE, COLOR_BLACK);
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
''',
    "recorder-screen placeholder labels",
)

main_c = replace_once(
    main_c,
    """    ui_status.dma_overruns =
        Capture_GetRecorderOverruns();
    ui_status.capture_active =
""",
    """    ui_status.dma_overruns =
        Capture_GetRecorderOverruns();
    ui_status.rms_counts =
        Capture_GetRecorderRms();
    ui_status.clip_count =
        Capture_GetRecorderClipCount();
    ui_status.clipping =
        Capture_IsRecorderClipping();
    ui_status.capture_active =
""",
    "UI recorder status publication",
)

outputs = []
for path, text in ((CH, capture_h), (CC, capture_c),
                   (UH, ui_h), (UC, ui_c), (MC, main_c)):
    text = "\n".join(line.rstrip() for line in text.splitlines()) + "\n"
    if text.count("{") != text.count("}"):
        fail(f"Brace balance failed in {path}")
    if text.count("/*") != text.count("*/"):
        fail(f"Comment balance failed in {path}")
    if re.search(r"&(lt|gt|amp);|<br", text):
        fail(f"HTML artifact found after patching {path}")
    outputs.append((path, text))

combined = "\n".join(text for _, text in outputs)
for symbol in (MARKER, "Capture_GetRecorderRms",
               "Capture_GetRecorderClipCount",
               "Capture_IsRecorderClipping",
               "recorder_update_signal_statistics",
               "CLIP CNT"):
    if symbol not in combined:
        fail(f"Missing patched symbol: {symbol}")

for path, text in outputs:
    path.write_text(text, encoding="utf-8")

print("RMS and clipping UI patch applied successfully.")
print("Modified: capture.h, capture.c, ui.h, ui.c, and main.c")
print("Backups:  *.before_rms_clipping")
print("Clipping thresholds: ADC <= 16 or ADC >= 4079")
print("Next: make clean && make -j4")
