#!/usr/bin/env python3
"""Add a console-only 10-second numbered WAV recording command."""

from pathlib import Path
import re
import shutil

MAIN = Path("Core/Src/main.c")
MARKER = "CONSOLE_REC10_PATCH"


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
    fail("Console rec10 patch is already installed")

if re.search(r"&(lt|gt|amp);|<br", text):
    fail("HTML artifact found in Core/Src/main.c")

for prerequisite in (
    "NUMBERED_RECORDINGS_PATCH",
    "queue_high_water_bytes",
    "Console_RunRec5",
    "Capture_StartRecorder",
    "RecordingCatalog_StartNext",
):
    if prerequisite not in text:
        fail("Missing prerequisite: " + prerequisite)

backup = MAIN.with_name(MAIN.name + ".before_console_rec10")
if not backup.exists():
    shutil.copy2(MAIN, backup)

# Add the prototype beside the existing REC5 prototype.
text = once(
    text,
    "static void Console_RunRec5(void);\n",
    """static void Console_RunRec5(void);
static void Console_RunRec10(void);
""",
    "Console_RunRec5 prototype",
)

# Add a top-level feature marker beside the numbered-recording marker.
text = once(
    text,
    "/* NUMBERED_RECORDINGS_PATCH */\n",
    """/* NUMBERED_RECORDINGS_PATCH */
/* CONSOLE_REC10_PATCH */
""",
    "numbered-recordings marker",
)

# Add the parser branch directly after rec5.
parser_anchor = '''    else if (strcmp(command, "rec5") == 0)
    {
        if (Analyzer_RequireStopped())
        {
            Console_RunRec5();
        }
    }
'''
parser_replacement = parser_anchor + '''    else if (strcmp(command, "rec10") == 0)
    {
        if (Analyzer_RequireStopped())
        {
            Console_RunRec10();
        }
    }
'''
text = once(text, parser_anchor, parser_replacement, "rec5 parser branch")

# Add help text directly after rec5. Match the active source exactly rather
# than relying on indentation.
help_match = re.search(
    r'(?m)^(\s*)"rec5\s+-[^\n]*\\r\\n"\n',
    text,
)
if not help_match:
    fail("Missing rec5 help line")
help_line = help_match.group(0)
indent = help_match.group(1)
help_insert = help_line + indent + '"rec10       - Record ten seconds to numbered WAV\\r\\n"\n'
text = once(text, help_line, help_insert, "rec5 help line")

# Insert a self-contained REC10 implementation immediately before REC5.
# This intentionally leaves the proven REC5 implementation unchanged.
rec10_function = r'''static void Console_RunRec10(void)
{
    RecorderStatus status;
    FRESULT result;

    UART_SendString("Starting ten-second WAV recording\r\n");
    UART_SendString("Target Samples = ");
    UART_SendDec(Capture_GetSampleRate() * 10U);
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

    if (!Capture_StartRecorder(10U))
    {
        UART_SendString("Ten-second DMA start failed\r\n");
        (void)Recorder_StopRaw();
        return;
    }

    UART_SendString("Ten-second recording started\r\n");
}

'''

# Scope the insertion to the actual definition, not the prototype.
definition_anchor = '''static void Console_RunRec5(void)
{
'''
text = once(
    text,
    definition_anchor,
    rec10_function + definition_anchor,
    "Console_RunRec5 definition",
)

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
    "Console_RunRec10",
    'strcmp(command, "rec10")',
    "Capture_StartRecorder(10U)",
    "Starting ten-second WAV recording",
):
    if symbol not in text:
        fail("Missing generated symbol: " + symbol)

MAIN.write_text(text, encoding="utf-8")

print("Console rec10 patch applied successfully.")
print("Modified: Core/Src/main.c")
print("The proven rec5 command was left unchanged.")
print("Added rec10 with numbered WAV creation and ten-second capture.")
print("Next: make clean && make -j4")
