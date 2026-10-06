#!/usr/bin/env python3
"""Reclaim the bottom 24 pixels for the recorded FFT display.

Behavior:
- Timeline hidden: recorded FFT graph extends through display row 239.
- Timeline visible: graph ends at row 215, separator uses rows 216-217,
  and the timeline occupies rows 218-237.
- The live analyzer and all non-recorded-FFT screens keep their geometry.
"""

from pathlib import Path
import re
import shutil

UI_C = Path("Core/Src/ui.c")
MARKER = "RECORDED_FFT_RECLAIM_BOTTOM_PATCH"


def fail(message):
    raise SystemExit(message)


def once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        fail(f"Expected exactly one {label}, found {count}")
    return text.replace(old, new, 1)


if not UI_C.exists():
    fail("Run this script from the ~/429_DSP project root")

raw = UI_C.read_bytes()
if b"\x00" in raw:
    fail("Binary NUL found in Core/Src/ui.c")

text = raw.decode("utf-8")

if MARKER in text:
    fail("Recorded FFT bottom-area repair is already installed")

if re.search(r"&(lt|gt|amp);|<br", text):
    fail("HTML artifact found in Core/Src/ui.c")

for prerequisite in (
    "FFT_DISPLAY_CSV_BROWSER_COMBINED_PATCH",
    "TFT_FFT_SCAN_EXPORT_PATCH",
    "UI_FFT_TIMELINE_Y",
    "UI_FFT_TIMELINE_SEPARATOR_Y",
    "ui_recorded_fft_graph_bottom",
    "ui_recorded_fft_graph_height",
    "static void ui_draw_fft_timeline(void)",
):
    if prerequisite not in text:
        fail("Missing prerequisite: " + prerequisite)

backup = UI_C.with_name(UI_C.name + ".before_recorded_fft_reclaim_bottom")
if not backup.exists():
    shutil.copy2(UI_C, backup)

# Move the timeline into the former 216-239 status region.
text = once(
    text,
    """#define UI_FFT_TIMELINE_Y                196
#define UI_FFT_TIMELINE_WIDTH            280U
#define UI_FFT_TIMELINE_HEIGHT            18U
#define UI_FFT_TIMELINE_LINE_Y           205
""",
    """#define UI_FFT_TIMELINE_Y                218
#define UI_FFT_TIMELINE_WIDTH            280U
#define UI_FFT_TIMELINE_HEIGHT            20U
#define UI_FFT_TIMELINE_LINE_Y           228
""",
    "timeline geometry constants",
)

# The existing separator expression remains Y-2, now resolving to row 216.
# The recorded FFT graph with timeline visible ends at row 215.

# Expand the hidden-timeline plot to the physical bottom of the display.
text = once(
    text,
    """static int32_t ui_recorded_fft_graph_bottom(void)
{
    return ui.fft_timeline_visible ?
        UI_RECORDED_FFT_GRAPH_BOTTOM : UI_GRAPH_BOTTOM;
}
""",
    """static int32_t ui_recorded_fft_graph_bottom(void)
{
    return ui.fft_timeline_visible ?
        UI_RECORDED_FFT_GRAPH_BOTTOM :
        ((int32_t)DISPLAY_HEIGHT - 1);
}
""",
    "dynamic recorded FFT graph bottom",
)

# Clear the same dynamic region that the recorded FFT is about to draw.
text = once(
    text,
    """    Display_FillRect(UI_GRAPH_LEFT,
                     UI_GRAPH_TOP,
                     UI_GRAPH_WIDTH,
                     UI_GRAPH_HEIGHT,
                     COLOR_BLACK);

    if (ui.mode == UI_MODE_RECORDED_FFT)
    {
""",
    """    if (ui.mode == UI_MODE_RECORDED_FFT)
    {
        Display_FillRect(UI_GRAPH_LEFT,
                         UI_GRAPH_TOP,
                         UI_GRAPH_WIDTH,
                         ui_recorded_fft_graph_height(),
                         COLOR_BLACK);
""",
    "recorded FFT graph clear dispatch",
)

# Restore the normal fixed graph clear for all other graph modes.
text = once(
    text,
    """    }
    else
    {
        ui_draw_grid();
""",
    """    }
    else
    {
        Display_FillRect(UI_GRAPH_LEFT,
                         UI_GRAPH_TOP,
                         UI_GRAPH_WIDTH,
                         UI_GRAPH_HEIGHT,
                         COLOR_BLACK);
        ui_draw_grid();
""",
    "non-recorded graph clear branch",
)

# When the timeline is visible, it has already been drawn into the former
# status region. Do not clear it afterward. When hidden, clear that region so
# the expanded FFT graph can use rows 216-239 without stale status content.
old_status = '''    if (ui.mode == UI_MODE_RECORDED_FFT)
    {
        Display_FillRect(0,
                         UI_STATUS_TOP,
                         DISPLAY_WIDTH,
                         UI_STATUS_HEIGHT,
                         COLOR_BLACK);
        return;
    }
'''
new_status = '''    if (ui.mode == UI_MODE_RECORDED_FFT)
    {
        if (!ui.fft_timeline_visible)
        {
            /* The FFT graph itself now owns rows 216 through 239. */
            return;
        }

        /* Timeline and separator already occupy the former status area. */
        return;
    }
'''
text = once(text, old_status, new_status,
            "recorded FFT blank status branch")

# Add source marker beside the combined display marker.
text = once(
    text,
    "/* FFT_DISPLAY_CSV_BROWSER_COMBINED_PATCH */\n",
    """/* FFT_DISPLAY_CSV_BROWSER_COMBINED_PATCH */
/* RECORDED_FFT_RECLAIM_BOTTOM_PATCH */
""",
    "combined display marker",
)

text = "\n".join(line.rstrip() for line in text.splitlines()) + "\n"

if text.count("{") != text.count("}"):
    fail("Brace balance failed before write")
if text.count("/*") != text.count("*/"):
    fail("Comment balance failed before write")
if re.search(r"&(lt|gt|amp);|<br", text):
    fail("HTML artifact generated")
if b"\x00" in text.encode("utf-8"):
    fail("Binary NUL generated")

for required in (
    MARKER,
    "UI_FFT_TIMELINE_Y                218",
    "UI_FFT_TIMELINE_HEIGHT            20U",
    "UI_FFT_TIMELINE_LINE_Y           228",
    "((int32_t)DISPLAY_HEIGHT - 1)",
    "ui_recorded_fft_graph_height(),",
):
    if required not in text:
        fail("Missing generated item: " + required)

UI_C.write_text(text, encoding="utf-8")

print("Recorded FFT bottom-area repair applied successfully.")
print("Timeline hidden: FFT graph extends through Y=239.")
print("Timeline visible: FFT ends at Y=215 and timeline uses Y=218..237.")
print("Live analyzer and other screens retain their original geometry.")
print("SCAN, EXPORT, event markers, and timeline touch behavior are unchanged.")
print("Next: make clean && make -j4")
