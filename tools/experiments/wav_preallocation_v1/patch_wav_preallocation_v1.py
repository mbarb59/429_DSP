#!/usr/bin/env python3
"""Add synchronous WAV preallocation and final truncation to 429_DSP.

Designed for the current recorder-drain, numbered-recording, browser, rename,
and delete-confirmation milestones.

Modified files:
  Core/Inc/recorder_framework.h
  Core/Src/recorder_framework.c
  Core/Inc/recording_catalog.h
  Core/Src/recording_catalog.c
  Core/Src/main.c

The script validates every source anchor and writes nothing unless all edits
and integrity checks succeed.
"""

from pathlib import Path
import re
import shutil

REC_H = Path("Core/Inc/recorder_framework.h")
REC_C = Path("Core/Src/recorder_framework.c")
CAT_H = Path("Core/Inc/recording_catalog.h")
CAT_C = Path("Core/Src/recording_catalog.c")
MAIN_C = Path("Core/Src/main.c")
MARKER = "WAV_PREALLOCATION_V1_PATCH"
HEADER_BYTES = 44
BYTES_PER_SAMPLE = 2


def fail(message):
    raise SystemExit(message)


def once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        fail(f"Expected exactly one {label}, found {count}")
    return text.replace(old, new, 1)


def backup(path):
    destination = path.with_name(path.name + ".before_wav_preallocation_v1")
    if not destination.exists():
        shutil.copy2(path, destination)


def function_range(text, signature):
    start = text.find(signature)
    if start < 0:
        fail("Missing function: " + signature.strip())
    brace = text.find("{", start)
    if brace < 0:
        fail("Missing opening brace for: " + signature.strip())
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                end = index + 1
                while end < len(text) and text[end] in " \t\r\n":
                    end += 1
                return start, end
    fail("Unbalanced function: " + signature.strip())


paths = (REC_H, REC_C, CAT_H, CAT_C, MAIN_C)
for path in paths:
    if not path.exists():
        fail("Missing required file: " + str(path))
    if bytes([0]) in path.read_bytes():
        fail("Binary NUL byte found in " + str(path))

rec_h = REC_H.read_text(encoding="utf-8")
rec_c = REC_C.read_text(encoding="utf-8")
cat_h = CAT_H.read_text(encoding="utf-8")
cat_c = CAT_C.read_text(encoding="utf-8")
main_c = MAIN_C.read_text(encoding="utf-8")

for path, text in zip(paths, (rec_h, rec_c, cat_h, cat_c, main_c)):
    if MARKER in text:
        fail("WAV preallocation v1 is already installed in " + str(path))
    if re.search(r"&(lt|gt|amp);|<br", text):
        fail("HTML artifact found in " + str(path))

for prerequisite in (
    "RECORDER_TWO_BLOCK_DRAIN",
    "NUMBERED_RECORDINGS_PATCH",
    "RecordingCatalog_StartNext",
    "Recorder_StartFile",
):
    if prerequisite not in (rec_h + rec_c + cat_h + cat_c + main_c):
        fail("Missing prerequisite: " + prerequisite)

for path in paths:
    backup(path)

# ---------------------------------------------------------------------------
# recorder_framework.h
# ---------------------------------------------------------------------------

# Extend RecorderStatus immediately after written_bytes. This anchor is kept
# deliberately small, but must occur only once inside the status structure.
rec_h = once(
    rec_h,
    "    uint32_t written_bytes;\n",
    """    uint32_t written_bytes;

    /* WAV_PREALLOCATION_V1_PATCH */
    uint32_t expected_samples;
    uint32_t expected_file_bytes;
    uint32_t preallocated_bytes;
    uint32_t final_file_bytes;
    FRESULT preallocation_result;
    FRESULT truncate_result;
    bool preallocated;
""",
    "RecorderStatus written_bytes field",
)

# Add new APIs beside Recorder_StartFile.
rec_h = once(
    rec_h,
    "FRESULT Recorder_StartFile(const char *filename);\n",
    """FRESULT Recorder_StartFile(const char *filename);
FRESULT Recorder_StartFilePreallocated(
    const char *filename,
    uint32_t expected_samples);
FRESULT Recorder_AbortPreparedFile(void);
""",
    "Recorder_StartFile prototype",
)

# ---------------------------------------------------------------------------
# recorder_framework.c
# ---------------------------------------------------------------------------

# RECORDER_WRITE_BLOCK_BYTES is declared in recorder_framework.h in this
# project. Add the WAV-format constants beside the local raw filename instead.
rec_c = once(
    rec_c,
    '#define RECORDER_RAW_FILENAME  "CAPTURE.WAV"\n',
    '#define RECORDER_RAW_FILENAME  "CAPTURE.WAV"\n'
    '#define RECORDER_WAV_HEADER_BYTES       44U\n'
    '#define RECORDER_PCM_BYTES_PER_SAMPLE  2U\n',
    "recorder raw filename definition",
)

# Preserve the prepared filename so a DMA-start failure can close and unlink
# the preallocated file cleanly.
file_match = re.search(r"(?m)^static\s+FIL\s+recorder_file;\n", rec_c)
if not file_match:
    fail("Missing static recorder_file declaration")
file_line = file_match.group(0)
rec_c = once(
    rec_c,
    file_line,
    file_line +
    "static char recorder_prepared_filename[64];\n",
    "recorder_file declaration",
)

# Initialize the prepared filename alongside the recorder framework state.
init_start, init_end = function_range(rec_c, "void Recorder_FrameworkInit(void)\n")
init_fn = rec_c[init_start:init_end]
init_fn = once(
    init_fn,
    "    recorder_write_index = 0U;\n",
    "    recorder_write_index = 0U;\n"
    "    recorder_prepared_filename[0] = '\\0';\n",
    "recorder framework write-index initialization",
)
rec_c = rec_c[:init_start] + init_fn + rec_c[init_end:]

# Replace Recorder_StartFile with a wrapper plus preallocating implementation.
start, end = function_range(rec_c, "FRESULT Recorder_StartFile(const char *filename)\n")
old_start_file = rec_c[start:end]

new_start_file = r'''FRESULT Recorder_StartFile(const char *filename)
{
    return Recorder_StartFilePreallocated(filename, 0U);
}

FRESULT Recorder_StartFilePreallocated(
    const char *filename,
    uint32_t expected_samples)
{
    FRESULT result;
    UINT bytes_written = 0U;
    uint8_t zero = 0U;
    uint64_t expected_data_bytes;
    uint64_t expected_file_bytes;

    if ((filename == NULL) || (filename[0] == '\0'))
    {
        return FR_INVALID_NAME;
    }

    if (recorder_status.state != RECORDER_STATE_IDLE)
    {
        return FR_LOCKED;
    }

    expected_data_bytes =
        (uint64_t)expected_samples * RECORDER_PCM_BYTES_PER_SAMPLE;
    expected_file_bytes =
        RECORDER_WAV_HEADER_BYTES + expected_data_bytes;

    if (expected_file_bytes > UINT32_MAX)
    {
        return FR_INVALID_PARAMETER;
    }

    result = FileSystem_Mount();
    if (result != FR_OK)
    {
        recorder_status.last_result = result;
        recorder_status.state = RECORDER_STATE_ERROR;
        return result;
    }

    result = f_open(
        &recorder_file,
        filename,
        FA_CREATE_ALWAYS | FA_WRITE);

    if (result != FR_OK)
    {
        recorder_status.last_result = result;
        recorder_status.state = RECORDER_STATE_ERROR;
        return result;
    }

    strncpy(recorder_prepared_filename,
            filename,
            sizeof(recorder_prepared_filename) - 1U);
    recorder_prepared_filename[
        sizeof(recorder_prepared_filename) - 1U] = '\0';

    result = Recorder_WriteWavHeader(&recorder_file, 0U);
    if (result != FR_OK)
    {
        (void)f_close(&recorder_file);
        (void)f_unlink(filename);
        recorder_prepared_filename[0] = '\0';
        recorder_status.last_result = result;
        recorder_status.state = RECORDER_STATE_ERROR;
        return result;
    }

    recorder_status.captured_samples = 0U;
    recorder_status.queued_bytes = 0U;
    recorder_status.queue_high_water_bytes = 0U;
    recorder_status.written_bytes = 0U;
    recorder_status.dropped_samples = 0U;
    recorder_status.write_errors = 0U;
    recorder_status.expected_samples = expected_samples;
    recorder_status.expected_file_bytes = (uint32_t)expected_file_bytes;
    recorder_status.preallocated_bytes = 0U;
    recorder_status.final_file_bytes = 0U;
    recorder_status.preallocation_result = FR_OK;
    recorder_status.truncate_result = FR_OK;
    recorder_status.preallocated = false;
    recorder_status.last_result = FR_OK;
    recorder_read_index = 0U;
    recorder_write_index = 0U;

    if (expected_samples != 0U)
    {
        result = f_lseek(
            &recorder_file,
            (FSIZE_t)((uint32_t)expected_file_bytes - 1U));

        if (result == FR_OK)
        {
            result = f_write(
                &recorder_file,
                &zero,
                1U,
                &bytes_written);

            if ((result == FR_OK) && (bytes_written != 1U))
            {
                result = FR_DISK_ERR;
            }
        }

        if (result == FR_OK)
        {
            result = f_sync(&recorder_file);
        }

        if (result == FR_OK)
        {
            result = f_lseek(
                &recorder_file,
                RECORDER_WAV_HEADER_BYTES);
        }

        recorder_status.preallocation_result = result;

        if (result != FR_OK)
        {
            (void)f_close(&recorder_file);
            (void)f_unlink(filename);
            recorder_status.write_errors++;
            recorder_status.last_result = result;
            recorder_status.state = RECORDER_STATE_ERROR;
            return result;
        }

        recorder_status.preallocated_bytes =
            (uint32_t)expected_file_bytes;
        recorder_status.preallocated = true;
    }
    else
    {
        result = f_lseek(
            &recorder_file,
            RECORDER_WAV_HEADER_BYTES);

        if (result != FR_OK)
        {
            (void)f_close(&recorder_file);
            (void)f_unlink(filename);
            recorder_status.last_result = result;
            recorder_status.state = RECORDER_STATE_ERROR;
            return result;
        }
    }

    recorder_status.state = RECORDER_STATE_RECORDING;
    return FR_OK;
}

FRESULT Recorder_AbortPreparedFile(void)
{
    FRESULT close_result = FR_OK;
    FRESULT unlink_result = FR_OK;

    if (recorder_status.state == RECORDER_STATE_RECORDING)
    {
        close_result = f_close(&recorder_file);
    }

    if (recorder_prepared_filename[0] != '\0')
    {
        unlink_result = f_unlink(recorder_prepared_filename);
    }

    recorder_prepared_filename[0] = '\0';
    recorder_status.state = RECORDER_STATE_IDLE;
    recorder_status.queued_bytes = 0U;
    recorder_read_index = 0U;
    recorder_write_index = 0U;

    return (close_result != FR_OK) ? close_result : unlink_result;
}

'''
rec_c = rec_c[:start] + new_start_file + rec_c[end:]

# Replace STOPPING finalization with truncate-before-header logic while
# retaining the existing queue drain loop.
start, end = function_range(rec_c, "void Recorder_Process(void)\n")
process_fn = rec_c[start:end]
old_finalize = r'''        result =
            Recorder_WriteWavHeader(
                &recorder_file,
                recorder_status.captured_samples);

        if (result == FR_OK)
        {
            result = f_sync(&recorder_file);
        }

        if (result == FR_OK)
        {
            result = f_close(&recorder_file);
        }
'''
new_finalize = r'''        /* WAV_PREALLOCATION_V1_PATCH
         * Remove unused reserved space before rewriting the final header.
         * written_bytes is authoritative because it counts successful PCM
         * writes to the file.
         */
        recorder_status.final_file_bytes =
            RECORDER_WAV_HEADER_BYTES + recorder_status.written_bytes;

        result = f_lseek(
            &recorder_file,
            (FSIZE_t)recorder_status.final_file_bytes);

        if (result == FR_OK)
        {
            result = f_truncate(&recorder_file);
        }

        recorder_status.truncate_result = result;

        if (result == FR_OK)
        {
            result = Recorder_WriteWavHeader(
                &recorder_file,
                recorder_status.written_bytes /
                RECORDER_PCM_BYTES_PER_SAMPLE);
        }

        if (result == FR_OK)
        {
            result = f_sync(&recorder_file);
        }

        if (result == FR_OK)
        {
            result = f_close(&recorder_file);
        }
'''
process_fn = once(
    process_fn,
    old_finalize,
    new_finalize,
    "recorder STOPPING finalization block",
)
rec_c = rec_c[:start] + process_fn + rec_c[end:]

# ---------------------------------------------------------------------------
# recording_catalog.h/.c
# ---------------------------------------------------------------------------
cat_h = once(
    cat_h,
    "FRESULT RecordingCatalog_StartNext(void);\n",
    """FRESULT RecordingCatalog_StartNext(void);
FRESULT RecordingCatalog_StartNextForSamples(uint32_t expected_samples);
""",
    "RecordingCatalog_StartNext prototype",
)

start, end = function_range(cat_c, "FRESULT RecordingCatalog_StartNext(void)\n")
old_catalog_start = cat_c[start:end]
new_catalog_start = r'''FRESULT RecordingCatalog_StartNext(void)
{
    return RecordingCatalog_StartNextForSamples(0U);
}

FRESULT RecordingCatalog_StartNextForSamples(uint32_t expected_samples)
{
    FILINFO info;
    FRESULT result;
    char candidate[RECORDING_FILENAME_CAPACITY];

    result = FileSystem_Mount();
    if (result != FR_OK)
    {
        return result;
    }

    for (uint32_t number = 1U; number <= 9999U; number++)
    {
        build_filename(number, candidate);
        result = f_stat(candidate, &info);

        if (result == FR_NO_FILE)
        {
            result = Recorder_StartFilePreallocated(
                candidate,
                expected_samples);

            if (result == FR_OK)
            {
                strcpy(selected_filename, candidate);
            }
            return result;
        }

        if (result != FR_OK)
        {
            return result;
        }
    }

    return FR_DENIED;
}

'''
cat_c = cat_c[:start] + new_catalog_start + cat_c[end:]

# ---------------------------------------------------------------------------
# main.c: REC5 integration and recstatus telemetry
# ---------------------------------------------------------------------------
main_c = once(
    main_c,
    "/* UI_WAV_RENAME_PATCH */\n",
    """/* UI_WAV_RENAME_PATCH */
/* WAV_PREALLOCATION_V1_PATCH */
""",
    "touchscreen rename feature marker",
)

# Update the UI REC5 path.
main_c = once(
    main_c,
    "            result = RecordingCatalog_StartNext();\n",
    """            UART_SendString("UI preparing preallocated WAV\\r\\n");
            result = RecordingCatalog_StartNextForSamples(
                Capture_GetSampleRate() * 5U);
""",
    "UI REC5 catalog start",
)

# Abort the prepared file if DMA capture cannot start.
main_c = once(
    main_c,
    """                UART_SendString("UI continuous DMA start failed\\r\\n");
                (void)Recorder_StopRaw();
                break;
""",
    """                UART_SendString("UI continuous DMA start failed\\r\\n");
                (void)Recorder_AbortPreparedFile();
                break;
""",
    "UI REC5 capture-start failure cleanup",
)

# Add detailed telemetry after Written Bytes in Console_RunRecStatus.
status_anchor = r'''    UART_SendString("Written Bytes   = ");
    UART_SendDec(status.written_bytes);
    UART_SendString("\r\n");
'''
status_insert = status_anchor + r'''
    UART_SendString("Expected Samples= ");
    UART_SendDec(status.expected_samples);
    UART_SendString("\r\n");

    UART_SendString("Expected File   = ");
    UART_SendDec(status.expected_file_bytes);
    UART_SendString("\r\n");

    UART_SendString("Prealloc Bytes  = ");
    UART_SendDec(status.preallocated_bytes);
    UART_SendString("\r\n");

    UART_SendString("Final File Bytes= ");
    UART_SendDec(status.final_file_bytes);
    UART_SendString("\r\n");

    UART_SendString("Prealloc Result = ");
    UART_SendDec((uint32_t)status.preallocation_result);
    UART_SendString("\r\n");

    UART_SendString("Truncate Result = ");
    UART_SendDec((uint32_t)status.truncate_result);
    UART_SendString("\r\n");
'''
main_c = once(
    main_c,
    status_anchor,
    status_insert,
    "recstatus Written Bytes output",
)

# Normalize accidental literal NULs from nested Python strings.
for name, value in (
    ("rec_h", rec_h),
    ("rec_c", rec_c),
    ("cat_h", cat_h),
    ("cat_c", cat_c),
    ("main_c", main_c),
):
    value = value.replace("\x00", r"\0")
    if name == "rec_h": rec_h = value
    elif name == "rec_c": rec_c = value
    elif name == "cat_h": cat_h = value
    elif name == "cat_c": cat_c = value
    else: main_c = value

outputs = []
for path, text in (
    (REC_H, rec_h),
    (REC_C, rec_c),
    (CAT_H, cat_h),
    (CAT_C, cat_c),
    (MAIN_C, main_c),
):
    text = "\n".join(line.rstrip() for line in text.splitlines()) + "\n"
    if text.count("{") != text.count("}"):
        fail("Brace balance failed in " + str(path))
    if text.count("/*") != text.count("*/"):
        fail("Comment balance failed in " + str(path))
    if re.search(r"&(lt|gt|amp);|<br", text):
        fail("HTML artifact generated in " + str(path))
    if bytes([0]) in text.encode("utf-8"):
        fail("Binary NUL generated in " + str(path))
    outputs.append((path, text))

combined = "\n".join(text for _, text in outputs)
for symbol in (
    MARKER,
    "Recorder_StartFilePreallocated",
    "Recorder_AbortPreparedFile",
    "RecordingCatalog_StartNextForSamples",
    "f_truncate",
    "Prealloc Bytes",
):
    if symbol not in combined:
        fail("Missing generated symbol: " + symbol)

for path, text in outputs:
    path.write_text(text, encoding="utf-8")

print("WAV preallocation v1 patch applied successfully.")
print("Modified: recorder_framework.h/.c, recording_catalog.h/.c, main.c")
print("REC5 now reserves 480044 bytes before DMA capture at 48 kHz.")
print("Finalization truncates to 44 + successfully written PCM bytes.")
print("Next: make clean && make -j4")
