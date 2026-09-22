#!/usr/bin/env python3
"""Add numbered touchscreen recordings and selected-file review support."""

from pathlib import Path
import re
import shutil

MAKE = Path("Makefile")
RH = Path("Core/Inc/recorder_framework.h")
RC = Path("Core/Src/recorder_framework.c")
MC = Path("Core/Src/main.c")
UC = Path("Core/Src/ui.c")
CH = Path("Core/Inc/recording_catalog.h")
CC = Path("Core/Src/recording_catalog.c")
MARKER = "NUMBERED_RECORDINGS_PATCH"


def fail(message):
    raise SystemExit(message)


def once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        fail(f"Expected exactly one {label}, found {count}")
    return text.replace(old, new, 1)


def function_range(text, signature):
    start = text.find(signature)
    if start < 0:
        fail("Missing function: " + signature.strip())
    brace = text.find("{", start)
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


def backup(path):
    target = path.with_name(path.name + ".before_numbered_recordings")
    if not target.exists():
        shutil.copy2(path, target)


for path in (MAKE, RH, RC, MC, UC):
    if not path.exists():
        fail("Missing required file: " + str(path))

make = MAKE.read_text(encoding="utf-8")
rh = RH.read_text(encoding="utf-8")
rc = RC.read_text(encoding="utf-8")
mc = MC.read_text(encoding="utf-8")
uc = UC.read_text(encoding="utf-8")

for path, text in ((MAKE, make), (RH, rh), (RC, rc), (MC, mc), (UC, uc)):
    if re.search(r"&(lt|gt|amp);|<br", text):
        fail("HTML artifact found in " + str(path))

if MARKER in mc or CH.exists() or CC.exists():
    fail("Numbered-recordings patch is already installed")

for path in (MAKE, RH, RC, MC, UC):
    backup(path)

catalog_h = '''#ifndef RECORDING_CATALOG_H
#define RECORDING_CATALOG_H

#include "ff.h"

#include <stdbool.h>
#include <stdint.h>

#define RECORDING_FILENAME_CAPACITY 13U

void RecordingCatalog_Init(void);
FRESULT RecordingCatalog_StartNext(void);
const char *RecordingCatalog_GetSelected(void);
bool RecordingCatalog_SetSelected(const char *filename);

#endif
'''

catalog_c = '''#include "recording_catalog.h"

#include "filesystem_service.h"
#include "recorder_framework.h"

#include <stddef.h>
#include <string.h>

static char selected_filename[RECORDING_FILENAME_CAPACITY];

static void build_filename(uint32_t number, char *filename)
{
    filename[0] = 'R';
    filename[1] = 'E';
    filename[2] = 'C';
    filename[3] = (char)('0' + ((number / 1000U) % 10U));
    filename[4] = (char)('0' + ((number / 100U) % 10U));
    filename[5] = (char)('0' + ((number / 10U) % 10U));
    filename[6] = (char)('0' + (number % 10U));
    filename[7] = '.';
    filename[8] = 'W';
    filename[9] = 'A';
    filename[10] = 'V';
    filename[11] = '\\0';
}

void RecordingCatalog_Init(void)
{
    strcpy(selected_filename, "CAPTURE.WAV");
}

const char *RecordingCatalog_GetSelected(void)
{
    return selected_filename;
}

bool RecordingCatalog_SetSelected(const char *filename)
{
    size_t length;

    if (filename == NULL)
    {
        return false;
    }

    length = strlen(filename);
    if ((length == 0U) || (length >= sizeof(selected_filename)))
    {
        return false;
    }

    strcpy(selected_filename, filename);
    return true;
}

FRESULT RecordingCatalog_StartNext(void)
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
            result = Recorder_StartFile(candidate);
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

rh = once(
    rh,
    "FRESULT Recorder_StartRaw(void);\n",
    "/* NUMBERED_RECORDINGS_PATCH */\n"
    "FRESULT Recorder_StartFile(const char *filename);\n"
    "FRESULT Recorder_StartRaw(void);\n",
    "Recorder_StartRaw declaration",
)

rc = once(
    rc,
    "FRESULT Recorder_StartRaw(void)\n",
    "/* NUMBERED_RECORDINGS_PATCH */\n"
    "FRESULT Recorder_StartFile(const char *filename)\n",
    "Recorder_StartRaw definition",
)

rc = once(
    rc,
    """FRESULT Recorder_StartFile(const char *filename)
{
    FRESULT result;

""",
    """FRESULT Recorder_StartFile(const char *filename)
{
    FRESULT result;

    if ((filename == NULL) || (filename[0] == '\\0'))
    {
        return FR_INVALID_NAME;
    }

""",
    "Recorder_StartFile opening",
)

rc = once(
    rc,
    """        &recorder_file,
        RECORDER_RAW_FILENAME,
        FA_CREATE_ALWAYS | FA_WRITE);
""",
    """        &recorder_file,
        filename,
        FA_CREATE_ALWAYS | FA_WRITE);
""",
    "recorder filename argument",
)

rc = once(
    rc,
    "FRESULT Recorder_StopRaw(void)\n",
    """FRESULT Recorder_StartRaw(void)
{
    return Recorder_StartFile(RECORDER_RAW_FILENAME);
}

FRESULT Recorder_StopRaw(void)
""",
    "Recorder_StopRaw definition",
)

make = once(
    make,
    "Core/Src/recorder_framework.c \\\n",
    "Core/Src/recorder_framework.c \\\n"
    "Core/Src/recording_catalog.c \\\n",
    "recorder framework Makefile entry",
)

mc = once(
    mc,
    '#include "recorder_framework.h"\n',
    '#include "recorder_framework.h"\n#include "recording_catalog.h"\n',
    "recorder framework include",
)

mc = once(
    mc,
    "    Recorder_FrameworkInit();\n",
    "    Recorder_FrameworkInit();\n    RecordingCatalog_Init();\n",
    "recorder framework initialization",
)

# Only touchscreen REC5 becomes numbered in this first stage.
start, end = function_range(mc, "static void UI_ProcessRecorderActions(void)\n")
controller = mc[start:end]
controller = once(
    controller,
    "result = Recorder_StartRaw();",
    "result = RecordingCatalog_StartNext();",
    "touchscreen recorder start",
)
mc = mc[:start] + controller + mc[end:]

mc = once(
    mc,
    'WavViewer_Load("CAPTURE.WAV")',
    "WavViewer_Load(RecordingCatalog_GetSelected())",
    "waveform selected filename",
)
mc = once(
    mc,
    'WavFftViewer_Begin("CAPTURE.WAV")',
    "WavFftViewer_Begin(RecordingCatalog_GetSelected())",
    "FFT selected filename",
)

mc = once(
    mc,
    "/* UI_RECORDER_CONTROLLER_PATCH */\n",
    "/* UI_RECORDER_CONTROLLER_PATCH */\n"
    "/* NUMBERED_RECORDINGS_PATCH */\n",
    "recorder controller marker",
)

uc = uc.replace(
    'Display_DrawString(188, 82, "CAPTURE.WAV", COLOR_WHITE, COLOR_BLACK);',
    'Display_DrawString(188, 82, "SELECTED WAV", COLOR_WHITE, COLOR_BLACK);',
)
uc = uc.replace(
    'Display_DrawString(8, 38, "CAPTURE.WAV", COLOR_CYAN, COLOR_BLACK);',
    'Display_DrawString(8, 38, "SELECTED WAV", COLOR_CYAN, COLOR_BLACK);',
)

for path, text in ((RH, rh), (RC, rc), (MC, mc), (UC, uc)):
    if text.count("{") != text.count("}"):
        fail("Brace balance failed in " + str(path))
    if text.count("/*") != text.count("*/"):
        fail("Comment balance failed in " + str(path))
    if re.search(r"&(lt|gt|amp);|<br", text):
        fail("HTML artifact after patching " + str(path))

if make.count("Core/Src/recording_catalog.c") != 1:
    fail("recording_catalog.c Makefile entry is invalid")

combined = rh + rc + mc + uc + catalog_h + catalog_c
for symbol in (MARKER, "Recorder_StartFile", "RecordingCatalog_StartNext",
               "RecordingCatalog_GetSelected"):
    if symbol not in combined:
        fail("Missing patched symbol: " + symbol)

MAKE.write_text(make, encoding="utf-8")
RH.write_text("\n".join(line.rstrip() for line in rh.splitlines()) + "\n", encoding="utf-8")
RC.write_text("\n".join(line.rstrip() for line in rc.splitlines()) + "\n", encoding="utf-8")
MC.write_text("\n".join(line.rstrip() for line in mc.splitlines()) + "\n", encoding="utf-8")
UC.write_text("\n".join(line.rstrip() for line in uc.splitlines()) + "\n", encoding="utf-8")
CH.write_text(catalog_h, encoding="utf-8")
CC.write_text(catalog_c, encoding="utf-8")

print("Numbered recordings patch applied successfully.")
print("Created: Core/Inc/recording_catalog.h")
print("Created: Core/Src/recording_catalog.c")
print("Touchscreen REC5 now creates REC0001.WAV through REC9999.WAV.")
print("WAVE and FFT use the newly selected recording.")
print("Console recorder commands remain compatible with CAPTURE.WAV.")
print("Next: make clean && make -j4")
