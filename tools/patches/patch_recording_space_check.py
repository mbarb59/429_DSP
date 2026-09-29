#!/usr/bin/env python3
"""Add SD free-space protection to touchscreen timed WAV recording."""

from pathlib import Path
import re
import shutil

CAT_H = Path("Core/Inc/recording_catalog.h")
CAT_C = Path("Core/Src/recording_catalog.c")
MAIN = Path("Core/Src/main.c")
MARK = "RECORDING_SPACE_CHECK_PATCH"


def fail(message):
    raise SystemExit(message)


def once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        fail(f"Expected exactly one {label}, found {count}")
    return text.replace(old, new, 1)


def backup(path):
    target = path.with_name(path.name + ".before_recording_space_check")
    if not target.exists():
        shutil.copy2(path, target)


for path in (CAT_H, CAT_C, MAIN):
    if not path.exists():
        fail("Missing required file: " + str(path))
    if b"\x00" in path.read_bytes():
        fail("Binary NUL found in " + str(path))

h = CAT_H.read_text(encoding="utf-8")
c = CAT_C.read_text(encoding="utf-8")
m = MAIN.read_text(encoding="utf-8")

for path, text in ((CAT_H, h), (CAT_C, c), (MAIN, m)):
    if MARK in text:
        fail("Space-check patch already installed in " + str(path))
    if re.search(r"&(lt|gt|amp);|<br", text):
        fail("HTML artifact found in " + str(path))

all_text = h + c + m
for required in (
    "FileSystem_GetSpace",
    "UI_ACTION_RECORD_DURATION",
    "UI_GetRequestedRecordingSeconds",
    "Capture_StartRecorder(duration_seconds)",
):
    if required not in all_text:
        fail("Missing prerequisite: " + required)

for path in (CAT_H, CAT_C, MAIN):
    backup(path)

h = once(
    h,
    "#define RECORDING_BROWSER_PAGE_SIZE  6U\n",
    "#define RECORDING_BROWSER_PAGE_SIZE  6U\n\n"
    "/* RECORDING_SPACE_CHECK_PATCH */\n"
    "#define RECORDING_WAV_HEADER_BYTES    44U\n"
    "#define RECORDING_CHANNEL_COUNT        1U\n"
    "#define RECORDING_BITS_PER_SAMPLE     16U\n"
    "#define RECORDING_SPACE_RESERVE_KIB   64U\n",
    "browser page-size definition",
)

entry = """typedef struct
{
    char filename[RECORDING_FILENAME_CAPACITY];
    uint32_t file_size;
} RecordingCatalogEntry;
"""

entry_and_check = entry + """
typedef struct
{
    uint32_t duration_seconds;
    uint32_t sample_rate_hz;
    uint32_t wav_kib;
    uint32_t reserve_kib;
    uint32_t required_kib;
    uint32_t available_kib;
    FRESULT filesystem_result;
    bool sufficient;
} RecordingSpaceCheck;
"""

h = once(h, entry, entry_and_check, "RecordingCatalogEntry structure")

h = once(
    h,
    "FRESULT RecordingCatalog_StartNext(void);\n",
    "FRESULT RecordingCatalog_StartNext(void);\n"
    "FRESULT RecordingCatalog_CheckSpace(\n"
    "    uint32_t duration_seconds,\n"
    "    uint32_t sample_rate_hz,\n"
    "    RecordingSpaceCheck *check);\n",
    "RecordingCatalog_StartNext prototype",
)

space_function = r"""static bool recording_duration_is_valid(
    uint32_t duration_seconds)
{
    return (duration_seconds == 5U) ||
           (duration_seconds == 10U) ||
           (duration_seconds == 30U) ||
           (duration_seconds == 60U);
}

FRESULT RecordingCatalog_CheckSpace(
    uint32_t duration_seconds,
    uint32_t sample_rate_hz,
    RecordingSpaceCheck *check)
{
    uint32_t total_kib = 0U;
    uint32_t free_kib = 0U;
    uint64_t bits_per_second;
    uint64_t data_bytes;
    uint64_t wav_bytes;
    uint64_t wav_kib;
    uint64_t required_kib;
    FRESULT result;

    if (check == NULL)
    {
        return FR_INVALID_PARAMETER;
    }

    memset(check, 0, sizeof(*check));
    check->duration_seconds = duration_seconds;
    check->sample_rate_hz = sample_rate_hz;
    check->reserve_kib = RECORDING_SPACE_RESERVE_KIB;
    check->filesystem_result = FR_OK;
    check->sufficient = false;

    if (!recording_duration_is_valid(duration_seconds) ||
        (sample_rate_hz == 0U))
    {
        check->filesystem_result = FR_INVALID_PARAMETER;
        return FR_INVALID_PARAMETER;
    }

    bits_per_second =
        (uint64_t)sample_rate_hz *
        RECORDING_CHANNEL_COUNT *
        RECORDING_BITS_PER_SAMPLE;

    data_bytes =
        (bits_per_second / 8U) * duration_seconds;
    wav_bytes =
        (uint64_t)RECORDING_WAV_HEADER_BYTES + data_bytes;
    wav_kib = (wav_bytes + 1023U) / 1024U;
    required_kib = wav_kib + RECORDING_SPACE_RESERVE_KIB;

    if ((wav_kib > UINT32_MAX) ||
        (required_kib > UINT32_MAX))
    {
        check->filesystem_result = FR_INVALID_PARAMETER;
        return FR_INVALID_PARAMETER;
    }

    check->wav_kib = (uint32_t)wav_kib;
    check->required_kib = (uint32_t)required_kib;

    result = FileSystem_GetSpace(&total_kib, &free_kib);
    check->filesystem_result = result;

    if (result != FR_OK)
    {
        return result;
    }

    check->available_kib = free_kib;
    check->sufficient = free_kib >= check->required_kib;
    return FR_OK;
}

"""

c = once(
    c,
    "void RecordingCatalog_Init(void)\n",
    space_function + "void RecordingCatalog_Init(void)\n",
    "RecordingCatalog_Init definition",
)

m = once(
    m,
    "/* UI_RECORD_DURATION_SELECTOR_PATCH */\n",
    "/* UI_RECORD_DURATION_SELECTOR_PATCH */\n"
    "/* RECORDING_SPACE_CHECK_PATCH */\n",
    "duration-selector feature marker",
)

old = r"""            Recorder_GetStatus(&status);
            if (status.state != RECORDER_STATE_IDLE)
            {
                UART_SendString("UI recorder is not idle\r\n");
                break;
            }

            result = RecordingCatalog_StartNext();
"""

new = r"""            Recorder_GetStatus(&status);
            if (status.state != RECORDER_STATE_IDLE)
            {
                UART_SendString("UI recorder is not idle\r\n");
                break;
            }

            {
                RecordingSpaceCheck space_check;

                result = RecordingCatalog_CheckSpace(
                    duration_seconds,
                    Capture_GetSampleRate(),
                    &space_check);

                UART_SendString("UI SD SPACE CHECK\r\n");

                if (result != FR_OK)
                {
                    UART_SendString("Space FatFs    = ");
                    UART_SendDec((uint32_t)result);
                    UART_SendString("\r\n");
                    UART_SendString(
                        "UI recording blocked: SD space check failed\r\n");
                    break;
                }

                UART_SendString("Duration Sec   = ");
                UART_SendDec(space_check.duration_seconds);
                UART_SendString("\r\n");
                UART_SendString("WAV KiB        = ");
                UART_SendDec(space_check.wav_kib);
                UART_SendString("\r\n");
                UART_SendString("Reserve KiB    = ");
                UART_SendDec(space_check.reserve_kib);
                UART_SendString("\r\n");
                UART_SendString("Required KiB   = ");
                UART_SendDec(space_check.required_kib);
                UART_SendString("\r\n");
                UART_SendString("Available KiB  = ");
                UART_SendDec(space_check.available_kib);
                UART_SendString("\r\n");
                UART_SendString("Space Result   = ");
                UART_SendString(
                    space_check.sufficient ?
                    "PASS\r\n" :
                    "FAIL\r\n");

                if (!space_check.sufficient)
                {
                    UART_SendString(
                        "UI recording blocked: SD space low\r\n");
                    break;
                }
            }

            result = RecordingCatalog_StartNext();
"""

case_start = m.find("        case UI_ACTION_RECORD_DURATION:\n")
case_end = m.find("        case UI_ACTION_RECORD_5:\n", case_start)
if case_start < 0 or case_end < 0:
    fail("Could not isolate UI_ACTION_RECORD_DURATION case")

case_text = m[case_start:case_end]
case_text = once(case_text, old, new, "duration-action IDLE block")
m = m[:case_start] + case_text + m[case_end:]

outputs = []
for path, text in ((CAT_H, h), (CAT_C, c), (MAIN, m)):
    text = "\n".join(line.rstrip() for line in text.splitlines()) + "\n"
    if text.count("{") != text.count("}"):
        fail("Brace balance failed in " + str(path))
    if text.count("/*") != text.count("*/"):
        fail("Comment balance failed in " + str(path))
    if re.search(r"&(lt|gt|amp);|<br", text):
        fail("HTML artifact generated in " + str(path))
    if b"\x00" in text.encode("utf-8"):
        fail("Binary NUL generated in " + str(path))
    outputs.append((path, text))

combined = "\n".join(text for _, text in outputs)
for symbol in (
    MARK,
    "RecordingSpaceCheck",
    "RecordingCatalog_CheckSpace",
    "RECORDING_SPACE_RESERVE_KIB",
    "UI SD SPACE CHECK",
    "UI recording blocked: SD space low",
):
    if symbol not in combined:
        fail("Missing generated symbol: " + symbol)

updated_start = m.find("        case UI_ACTION_RECORD_DURATION:\n")
updated_end = m.find("        case UI_ACTION_RECORD_5:\n", updated_start)
updated_case = m[updated_start:updated_end]
if updated_case.find("RecordingCatalog_CheckSpace") > updated_case.find(
        "RecordingCatalog_StartNext"):
    fail("Space check was not placed before file creation")

for path, text in outputs:
    path.write_text(text, encoding="utf-8")

print("Recording SD free-space protection patch applied successfully.")
print("Modified: recording_catalog.h/.c and main.c")
print("Timed touchscreen recording checks capacity before file creation.")
print("Uses existing FileSystem_GetSpace() and a 64 KiB reserve.")
print("DMA, queue, WAV writing, and finalization were unchanged.")
print("Next: make clean && make -j4")
