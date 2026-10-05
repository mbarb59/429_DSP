#!/usr/bin/env python3
"""Add UART command fftexportevents to export every scanned FFT event."""

from pathlib import Path
import re
import shutil

MAIN = Path("Core/Src/main.c")
MARK = "FFTEXPORTEVENTS_UART_PATCH"


def stop(message):
    raise SystemExit(message)


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        stop(f"Expected exactly one {label}, found {count}")
    return text.replace(old, new, 1)


if not MAIN.exists():
    stop("Run this script from the ~/429_DSP project root")

data = MAIN.read_bytes()
if b"\x00" in data:
    stop("Binary NUL found in Core/Src/main.c")

text = data.decode("utf-8")
if MARK in text:
    stop("fftexportevents is already installed")
if re.search(r"&(lt|gt|amp);|<br", text):
    stop("HTML artifact found in Core/Src/main.c")

for item in (
    "FFTEXPORT_CSV_PATCH",
    "FFT_EVENT_SCAN_MARKERS_PATCH",
    "static void Console_RunFftExport(void);",
    "static void Console_RunFftScan(void);",
    "FftExport_WriteKeyUint",
    "FftExport_WriteKeyInt",
    "FftExport_AppendUint",
    "WavFftViewer_GetEventCount",
    "WavFftViewer_LoadSegment",
    'strcmp(command, "fftexport")',
):
    if item not in text:
        stop("Missing prerequisite: " + item)

backup = MAIN.with_name(MAIN.name + ".before_fftexportevents_uart")
if not backup.exists():
    shutil.copy2(MAIN, backup)

text = replace_once(
    text,
    "static void Console_RunFftExport(void);\n",
    """static void Console_RunFftExport(void);
static void Console_RunFftExportEvents(void);
""",
    "FFT export declaration",
)

text = replace_once(
    text,
    "/* FFTEXPORT_CSV_PATCH */\n",
    """/* FFTEXPORT_CSV_PATCH */
/* FFTEXPORTEVENTS_UART_PATCH */
""",
    "FFT export marker",
)

implementation = r'''static bool FftExport_MakeEventFilename(const char *wav_name,
                                        uint32_t event_rank,
                                        char *output_name,
                                        uint32_t capacity)
{
    uint32_t recording_number = 0U;

    if ((wav_name == NULL) || (output_name == NULL) ||
        (capacity < 12U) || (event_rank == 0U) ||
        (event_rank > WAV_FFT_SCAN_MAX_EVENTS))
    {
        return false;
    }

    if ((wav_name[0] != 'R') || (wav_name[1] != 'E') ||
        (wav_name[2] != 'C'))
    {
        return false;
    }

    for (uint32_t index = 3U; index <= 6U; index++)
    {
        if ((wav_name[index] < '0') || (wav_name[index] > '9'))
        {
            return false;
        }
        recording_number = recording_number * 10U +
                           (uint32_t)(wav_name[index] - '0');
    }

    /* Seven-character 8.3 base: R0001E1.CSV. */
    output_name[0] = 'R';
    output_name[1] = (char)('0' + ((recording_number / 1000U) % 10U));
    output_name[2] = (char)('0' + ((recording_number / 100U) % 10U));
    output_name[3] = (char)('0' + ((recording_number / 10U) % 10U));
    output_name[4] = (char)('0' + (recording_number % 10U));
    output_name[5] = 'E';
    output_name[6] = (char)('0' + event_rank);
    output_name[7] = '.';
    output_name[8] = 'C';
    output_name[9] = 'S';
    output_name[10] = 'V';
    output_name[11] = '\0';
    return true;
}

static FRESULT FftExport_WriteEventFile(const char *output_name,
                                        const char *source_name,
                                        uint32_t event_rank,
                                        uint32_t *bins_written)
{
    const WavFftViewerInfo *info = WavFftViewer_GetInfo();
    FIL file;
    FRESULT result;
    char line[FFTEXPORT_TEXT_CAPACITY];
    bool success;
    uint32_t bin_count;
    uint32_t bin_width_millihz;

    if ((output_name == NULL) || (source_name == NULL) ||
        (bins_written == NULL) || (info == NULL) ||
        !info->active || !FFT_IsValid())
    {
        return FR_INVALID_OBJECT;
    }

    *bins_written = 0U;
    result = f_open(&file, output_name, FA_CREATE_ALWAYS | FA_WRITE);
    if (result != FR_OK)
    {
        return result;
    }

    success = FftExport_Write(&file, "429_DSP FFT EVENT EXPORT\r\n", &result);

    if (success)
    {
        uint32_t position = 0U;
        line[0] = '\0';
        position = FftExport_AppendText(line, sizeof(line), position,
                                        "Source File,");
        position = FftExport_AppendText(line, sizeof(line), position,
                                        source_name);
        (void)FftExport_AppendText(line, sizeof(line), position, "\r\n");
        success = FftExport_Write(&file, line, &result);
    }

    if (success) success = FftExport_WriteKeyUint(
        &file, "Event Rank", event_rank, &result);
    if (success) success = FftExport_WriteKeyUint(
        &file, "Segment", info->segment_index + 1U, &result);
    if (success) success = FftExport_WriteKeyUint(
        &file, "Segment Count", info->segment_count, &result);
    if (success) success = FftExport_WriteKeyUint(
        &file, "Segment Time ms", info->segment_time_ms, &result);
    if (success) success = FftExport_WriteKeyUint(
        &file, "Sample Rate Hz", info->sample_rate, &result);
    if (success) success = FftExport_WriteKeyUint(
        &file, "FFT Size", FFT_GetSize(), &result);
    if (success) success = FftExport_WriteKeyUint(
        &file, "Bin Width mHz", FFT_GetBinWidthMilliHz(), &result);
    if (success) success = FftExport_WriteKeyUint(
        &file, "Peak Frequency Hz", FFT_GetPeakFrequency(), &result);
    if (success) success = FftExport_WriteKeyInt(
        &file, "Peak Level dBFS x10", FFT_GetPeakDbTenths(), &result);
    if (success) success = FftExport_Write(
        &file,
        "\r\nBin,Frequency mHz,Magnitude Counts,Level dBFS x10\r\n",
        &result);

    bin_count = FFT_GetBinCount();
    bin_width_millihz = FFT_GetBinWidthMilliHz();

    for (uint32_t bin = 1U; success && (bin < bin_count); bin++)
    {
        uint32_t position = 0U;
        uint32_t frequency_millihz =
            (uint32_t)((uint64_t)bin * bin_width_millihz);

        line[0] = '\0';
        position = FftExport_AppendUint(line, sizeof(line), position, bin);
        position = FftExport_AppendText(line, sizeof(line), position, ",");
        position = FftExport_AppendUint(line, sizeof(line), position,
                                        frequency_millihz);
        position = FftExport_AppendText(line, sizeof(line), position, ",");
        position = FftExport_AppendUint(line, sizeof(line), position,
                                        FFT_GetMagnitude(bin));
        position = FftExport_AppendText(line, sizeof(line), position, ",");
        position = FftExport_AppendInt(line, sizeof(line), position,
                                       FFT_GetDbTenths(bin));
        (void)FftExport_AppendText(line, sizeof(line), position, "\r\n");

        success = FftExport_Write(&file, line, &result);
        if (success)
        {
            (*bins_written)++;
        }
    }

    if (success)
    {
        result = f_sync(&file);
        success = (result == FR_OK);
    }

    {
        FRESULT close_result = f_close(&file);
        if (success && (close_result != FR_OK))
        {
            result = close_result;
            success = false;
        }
    }

    if (!success)
    {
        (void)f_unlink(output_name);
    }

    return success ? FR_OK : result;
}

static void Console_RunFftExportEvents(void)
{
    const WavFftViewerInfo *info = WavFftViewer_GetInfo();
    const char *source_name = RecordingCatalog_GetSelected();
    uint32_t event_count;
    uint32_t files_written = 0U;
    FRESULT overall_result = FR_OK;

    UART_SendString("FFT Event CSV Export\r\n");
    UART_SendString("--------------------\r\n");

    if (Capture_IsRecorderActive())
    {
        UART_SendString("Event export blocked: recorder active\r\n");
        return;
    }

    if ((info == NULL) || !info->active || !FFT_IsValid())
    {
        UART_SendString(
            "Event export blocked: open a recorded WAV FFT first\r\n");
        return;
    }

    event_count = WavFftViewer_GetEventCount();
    if (event_count == 0U)
    {
        UART_SendString("Event export blocked: run fftscan first\r\n");
        return;
    }

    UART_SendString("Source File    = ");
    UART_SendString(source_name);
    UART_SendString("\r\nEvents         = ");
    UART_SendDec(event_count);
    UART_SendString("\r\n");

    for (uint32_t index = 0U; index < event_count; index++)
    {
        const WavFftPeakEvent *event = WavFftViewer_GetEvent(index);
        char output_name[13];
        uint32_t bins_written = 0U;
        FRESULT result;

        if ((event == NULL) || !event->valid ||
            !FftExport_MakeEventFilename(source_name,
                                         index + 1U,
                                         output_name,
                                         sizeof(output_name)))
        {
            overall_result = FR_INVALID_NAME;
            break;
        }

        result = WavFftViewer_LoadSegment(event->segment_index);
        if (result == FR_OK)
        {
            result = FftExport_WriteEventFile(output_name,
                                              source_name,
                                              index + 1U,
                                              &bins_written);
        }

        UART_SendString("Event ");
        UART_SendDec(index + 1U);
        UART_SendString(" File=");
        UART_SendString(output_name);
        UART_SendString(" Segment=");
        UART_SendDec(event->segment_index + 1U);
        UART_SendString(" Bins=");
        UART_SendDec(bins_written);
        UART_SendString(" Result=");
        UART_SendDec((uint32_t)result);
        UART_SendString((result == FR_OK) ? " PASS\r\n" : " FAIL\r\n");

        if (result != FR_OK)
        {
            overall_result = result;
            break;
        }

        files_written++;
    }

    if (event_count != 0U)
    {
        const WavFftPeakEvent *strongest = WavFftViewer_GetEvent(0U);
        if (strongest != NULL)
        {
            FRESULT restore_result =
                WavFftViewer_LoadSegment(strongest->segment_index);
            if ((overall_result == FR_OK) && (restore_result != FR_OK))
            {
                overall_result = restore_result;
            }
        }
    }

    UI_ShowRecordedFft();
    UART_SendString("Files Written  = ");
    UART_SendDec(files_written);
    UART_SendString("\r\nExport Result  = ");
    UART_SendDec((uint32_t)overall_result);
    UART_SendString((overall_result == FR_OK) ?
        " PASS\r\n" : " FAIL\r\n");
}

'''

text = replace_once(
    text,
    "static void Console_RunFftExport(void)\n",
    implementation + "static void Console_RunFftExport(void)\n",
    "FFT export implementation",
)

text = replace_once(
    text,
    """    else if (strcmp(command, "fftexport") == 0)
    {
        Console_RunFftExport();
    }
""",
    """    else if (strcmp(command, "fftexport") == 0)
    {
        Console_RunFftExport();
    }
    else if (strcmp(command, "fftexportevents") == 0)
    {
        Console_RunFftExportEvents();
    }
""",
    "fftexport command block",
)

text = "\n".join(line.rstrip() for line in text.splitlines()) + "\n"

if text.count("{") != text.count("}"):
    stop("Brace balance failed before write")
if text.count("/*") != text.count("*/"):
    stop("Comment balance failed before write")
if re.search(r"&(lt|gt|amp);|<br", text):
    stop("HTML artifact generated")
if b"\x00" in text.encode("utf-8"):
    stop("Binary NUL generated")

for item in (
    MARK,
    "Console_RunFftExportEvents",
    'strcmp(command, "fftexportevents")',
    "FftExport_WriteEventFile",
    "FftExport_MakeEventFilename",
    "R0001E1.CSV",
    "run fftscan first",
):
    if item not in text:
        stop("Missing generated item: " + item)

MAIN.write_text(text, encoding="utf-8")

print("UART FFT event export patch applied successfully.")
print("Command: fftexportevents")
print("Event files use R0001E1.CSV through R0001E5.CSV.")
print("Each file contains metadata and 1023 positive-frequency bins.")
print("The strongest event is restored after export.")
print("Next: make clean && make -j4")
