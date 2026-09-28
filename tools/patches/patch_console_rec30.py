#!/usr/bin/env python3
"""Add a console-only 30-second numbered WAV recording command.

Prerequisite: the validated CONSOLE_REC10_PATCH must already be installed.
The script leaves REC5 and REC10 unchanged, validates all source anchors, and
writes Core/Src/main.c only after every integrity check succeeds.
"""

from pathlib import Path
import re
import shutil

MAIN = Path("Core/Src/main.c")
MARKER = "CONSOLE_REC30_PATCH"


def fail(message):
    raise SystemExit(message)


def once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        fail(f"Expected exactly one {label}, found {count}")
    return text.replace(old, new, 1)


if not MAIN.exists():
    fail(f"Missing required file: {MAIN}")

raw = MAIN.read_bytes()
if b"\x00" in raw:
    fail("Binary NUL byte found in Core/Src/main.c")

text = raw.decode("utf-8")

if MARKER in text:
    fail("Console rec30 patch is already installed")

if re.search(r"&(lt|gt|amp);|<br", text):
    fail("HTML artifact found in Core/Src/main.c")

for prerequisite in (
    "CONSOLE_REC10_PATCH",
    "Console_RunRec10",
    'strcmp(command, "rec10")',
    "Capture_StartRecorder(10U)",
    "queue_high_water_bytes",
    "RecordingCatalog_StartNext",
):
    if prerequisite not in text:
        fail("Missing prerequisite: " + prerequisite)

backup = MAIN.with_name(MAIN.name + ".before_console_rec30")
if not backup.exists():
    shutil.copy2(MAIN, backup)

# Add the REC30 prototype beside REC10.
text = once(
    text,
    "static void Console_RunRec10(void);\n",
    """static void Console_RunRec10(void);
static void Console_RunRec30(void);
""",
    "Console_RunRec10 prototype",
)

# Add a top-level feature marker after REC10.
text = once(
    text,
    "/* CONSOLE_REC10_PATCH */\n",
    """/* CONSOLE_REC10_PATCH */
/* CONSOLE_REC30_PATCH */
""",
    "REC10 feature marker",
)

# Add the parser branch directly after the validated REC10 branch.
parser_anchor = '''    else if (strcmp(command, "rec10") == 0)
    {
        if (Analyzer_RequireStopped())
        {
            Console_RunRec10();
        }
    }
'''
parser_replacement = parser_anchor + '''    else if (strcmp(command, "rec30") == 0)
    {
        if (Analyzer_RequireStopped())
        {
            Console_RunRec30();
        }
    }
'''
text = once(text, parser_anchor, parser_replacement, "rec10 parser branch")

# Add help text directly after the REC10 help line.
help_match = re.search(
    r'(?m)^(\s*)"rec10\s+-[^\n]*\\r\\n"\n',
    text,
)
if not help_match:
    fail("Missing rec10 help line")
help_line = help_match.group(0)
indent = help_match.group(1)
help_insert = (
    help_line
    + indent
    + '"rec30       - Record thirty seconds to numbered WAV\\r\\n"\n'
)
text = once(text, help_line, help_insert, "rec10 help line")

# Add a self-contained REC30 implementation immediately before REC10. This
# leaves both validated shorter-duration implementations untouched.
rec30_function = r'''static void Console_RunRec30(void)
{
    RecorderStatus status;
    FRESULT result;
    uint64_t target_samples_64;
    uint32_t target_samples;

    target_samples_64 =
        (uint64_t)Capture_GetSampleRate() * 30U;

    if (target_samples_64 > UINT32_MAX)
    {
        UART_SendString("Thirty-second target is too large\r\n");
        return;
    }

    target_samples = (uint32_t)target_samples_64;

    UART_SendString("Starting thirty-second WAV recording\r\n");
    UART_SendString("Target Samples = ");
    UART_SendDec(target_samples);
    UART_SendString("\r\n");

    if (Capture_IsRecorderActive())
    {
        UART_SendString("Recorder is already active\r\n");
        return;
    }

    Recorder_GetStatus(&status);
    if (status.state != RECORDER_STATE_IDLE)
    {
        UART_SendString("Recorder is not idle\r\n");
        return;
    }

    result = RecordingCatalog_StartNext();
    if (result != FR_OK)
    {
        UART_SendString("Recorder file open failed, FatFs = ");
        UART_SendDec((uint32_t)result);
        UART_SendString("\r\n");
        return;
    }

    if (!Capture_StartRecorder(30U))
    {
        UART_SendString("Thirty-second DMA start failed\r\n");
        (void)Recorder_StopRaw();
        return;
    }

    UART_SendString("Thirty-second recording started\r\n");
}

'''

definition_anchor = '''static void Console_RunRec10(void)
{
'''
text = once(
    text,
    definition_anchor,
    rec30_function + definition_anchor,
    "Console_RunRec10 definition",
)

# Normalize and validate before writing.
text = "\n".join(line.rstrip() for line in text.splitlines()) + "\n"

if text.count("{") != text.count("}"):
    fail("Brace balance failed")
if text.count("/*") != text.count("*/"):
    fail("Comment balance failed")
if re.search(r"&(lt|gt|amp);|<br", text):
    fail("HTML artifact generated")
if b"\x00" in text.encode("utf-8"):
    fail("Binary NUL byte generated")

for symbol in (
    MARKER,
    "Console_RunRec30",
    'strcmp(command, "rec30")',
    "Capture_StartRecorder(30U)",
    "Starting thirty-second WAV recording",
    "target_samples_64",
):
    if symbol not in text:
        fail("Missing generated symbol: " + symbol)

# Confirm the validated shorter commands remain exactly once.
for symbol in (
    'strcmp(command, "rec5")',
    'strcmp(command, "rec10")',
    "Capture_StartRecorder(10U)",
):
    if text.count(symbol) != 1:
        fail(f"Validated shorter-duration symbol changed or duplicated: {symbol}")

MAIN.write_text(text, encoding="utf-8")

print("Console rec30 patch applied successfully.")
print("Modified: Core/Src/main.c")
print("The validated rec5 and rec10 commands were left unchanged.")
print("Added rec30 with numbered WAV creation and 30-second capture.")
print("Next: make clean && make -j4")
