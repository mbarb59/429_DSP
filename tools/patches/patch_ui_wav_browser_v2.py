#!/usr/bin/env python3
"""Add a stable, read-only, paged WAV browser to 429_DSP.

Designed for recorder-priority-v1 plus UI_TOUCH_STABILITY_PATCH.
Filesystem access remains in foreground action processing, never in touch code.
"""

from pathlib import Path
import re
import shutil

CAT_H = Path("Core/Inc/recording_catalog.h")
CAT_C = Path("Core/Src/recording_catalog.c")
UI_H = Path("Core/Inc/ui.h")
UI_C = Path("Core/Src/ui.c")
MAIN_C = Path("Core/Src/main.c")
MARKER = "UI_WAV_BROWSER_V2_PATCH"


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
    if brace < 0:
        fail("Missing opening brace: " + signature.strip())
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
    target = path.with_name(path.name + ".before_wav_browser_v2")
    if not target.exists():
        shutil.copy2(path, target)


paths = (CAT_H, CAT_C, UI_H, UI_C, MAIN_C)
for path in paths:
    if not path.exists():
        fail("Missing required file: " + str(path))
    if bytes([0]) in path.read_bytes():
        fail("NUL byte found in " + str(path))

cat_h = CAT_H.read_text(encoding="utf-8")
cat_c = CAT_C.read_text(encoding="utf-8")
ui_h = UI_H.read_text(encoding="utf-8")
ui_c = UI_C.read_text(encoding="utf-8")
main_c = MAIN_C.read_text(encoding="utf-8")

for path, text in zip(paths, (cat_h, cat_c, ui_h, ui_c, main_c)):
    if MARKER in text:
        fail("Browser v2 is already installed in " + str(path))
    if re.search(r"&(lt|gt|amp);|<br", text):
        fail("HTML artifact found in " + str(path))

for prerequisite in (
    "UI_TOUCH_STABILITY_PATCH",
    "UI_TOUCH_ACTION_LOCKOUT_MS",
    "pressed_button",
):
    if prerequisite not in ui_c:
        fail("Missing touch-stability prerequisite: " + prerequisite)

for path in paths:
    backup(path)

# ---------------------------------------------------------------------------
# Recording catalog: one page of six FAT 8.3 WAV entries.
# ---------------------------------------------------------------------------
cat_h = once(
    cat_h,
    "#define RECORDING_FILENAME_CAPACITY 13U\n",
    """#define RECORDING_FILENAME_CAPACITY 13U
#define RECORDING_BROWSER_PAGE_SIZE  6U

typedef struct
{
    char filename[RECORDING_FILENAME_CAPACITY];
    uint32_t file_size;
} RecordingCatalogEntry;
""",
    "recording filename capacity",
)
cat_h = once(
    cat_h,
    "bool RecordingCatalog_SetSelected(const char *filename);\n",
    """bool RecordingCatalog_SetSelected(const char *filename);

/* UI_WAV_BROWSER_V2_PATCH */
FRESULT RecordingCatalog_LoadPage(uint32_t page_index);
uint32_t RecordingCatalog_GetPageIndex(void);
uint32_t RecordingCatalog_GetPageCount(void);
uint32_t RecordingCatalog_GetPageEntryCount(void);
const RecordingCatalogEntry *RecordingCatalog_GetPageEntry(uint32_t index);
bool RecordingCatalog_SelectPageEntry(uint32_t index);
""",
    "selected filename API",
)

cat_c = once(
    cat_c,
    "static char selected_filename[RECORDING_FILENAME_CAPACITY];\n",
    """static char selected_filename[RECORDING_FILENAME_CAPACITY];

/* UI_WAV_BROWSER_V2_PATCH */
static RecordingCatalogEntry browser_entries[RECORDING_BROWSER_PAGE_SIZE];
static uint32_t browser_page_index;
static uint32_t browser_page_count;
static uint32_t browser_entry_count;
""",
    "selected filename storage",
)
cat_c = once(
    cat_c,
    """void RecordingCatalog_Init(void)
{
    strcpy(selected_filename, "CAPTURE.WAV");
}
""",
    """void RecordingCatalog_Init(void)
{
    strcpy(selected_filename, "CAPTURE.WAV");
    memset(browser_entries, 0, sizeof(browser_entries));
    browser_page_index = 0U;
    browser_page_count = 0U;
    browser_entry_count = 0U;
}
""",
    "catalog initialization",
)

catalog_browser_code = r'''static bool filename_is_wav(const char *filename)
{
    size_t length;

    if (filename == NULL)
    {
        return false;
    }

    length = strlen(filename);
    if (length < 4U)
    {
        return false;
    }

    return (filename[length - 4U] == '.') &&
           ((filename[length - 3U] == 'W') ||
            (filename[length - 3U] == 'w')) &&
           ((filename[length - 2U] == 'A') ||
            (filename[length - 2U] == 'a')) &&
           ((filename[length - 1U] == 'V') ||
            (filename[length - 1U] == 'v'));
}

FRESULT RecordingCatalog_LoadPage(uint32_t page_index)
{
    DIR directory;
    FILINFO information;
    FRESULT result;
    uint32_t wav_count = 0U;
    uint32_t first_entry = page_index * RECORDING_BROWSER_PAGE_SIZE;

    result = FileSystem_Mount();
    if (result != FR_OK)
    {
        return result;
    }

    result = f_opendir(&directory, "0:/");
    if (result != FR_OK)
    {
        return result;
    }

    memset(browser_entries, 0, sizeof(browser_entries));
    browser_entry_count = 0U;

    for (;;)
    {
        result = f_readdir(&directory, &information);
        if ((result != FR_OK) || (information.fname[0] == '\0'))
        {
            break;
        }

        if (((information.fattrib & AM_DIR) != 0U) ||
            !filename_is_wav(information.fname))
        {
            continue;
        }

        if ((wav_count >= first_entry) &&
            (browser_entry_count < RECORDING_BROWSER_PAGE_SIZE))
        {
            RecordingCatalogEntry *entry =
                &browser_entries[browser_entry_count];

            strncpy(entry->filename,
                    information.fname,
                    RECORDING_FILENAME_CAPACITY - 1U);
            entry->filename[RECORDING_FILENAME_CAPACITY - 1U] = '\0';
            entry->file_size = (uint32_t)information.fsize;
            browser_entry_count++;
        }

        wav_count++;
    }

    (void)f_closedir(&directory);
    if (result != FR_OK)
    {
        return result;
    }

    browser_page_count =
        (wav_count + RECORDING_BROWSER_PAGE_SIZE - 1U) /
        RECORDING_BROWSER_PAGE_SIZE;

    if (browser_page_count == 0U)
    {
        browser_page_index = 0U;
    }
    else if (page_index >= browser_page_count)
    {
        return RecordingCatalog_LoadPage(browser_page_count - 1U);
    }
    else
    {
        browser_page_index = page_index;
    }

    return FR_OK;
}

uint32_t RecordingCatalog_GetPageIndex(void)
{
    return browser_page_index;
}

uint32_t RecordingCatalog_GetPageCount(void)
{
    return browser_page_count;
}

uint32_t RecordingCatalog_GetPageEntryCount(void)
{
    return browser_entry_count;
}

const RecordingCatalogEntry *RecordingCatalog_GetPageEntry(uint32_t index)
{
    if (index >= browser_entry_count)
    {
        return NULL;
    }

    return &browser_entries[index];
}

bool RecordingCatalog_SelectPageEntry(uint32_t index)
{
    const RecordingCatalogEntry *entry =
        RecordingCatalog_GetPageEntry(index);

    if (entry == NULL)
    {
        return false;
    }

    return RecordingCatalog_SetSelected(entry->filename);
}

'''
cat_c = once(
    cat_c,
    "FRESULT RecordingCatalog_StartNext(void)\n",
    catalog_browser_code + "FRESULT RecordingCatalog_StartNext(void)\n",
    "catalog start-next definition",
)

# ---------------------------------------------------------------------------
# Public UI actions and browser API.
# ---------------------------------------------------------------------------
ui_h = once(
    ui_h,
    "    UI_ACTION_FFT_NEXT\n} UI_Action;\n",
    """    UI_ACTION_FFT_NEXT,
    UI_ACTION_BROWSER_OPEN,
    UI_ACTION_BROWSER_BACK,
    UI_ACTION_BROWSER_PREVIOUS,
    UI_ACTION_BROWSER_NEXT,
    UI_ACTION_BROWSER_REFRESH,
    UI_ACTION_BROWSER_SELECT_ROW
} UI_Action;
""",
    "UI action enum tail",
)
ui_h = once(
    ui_h,
    "bool UI_IsRecordedFftScreen(void);\n",
    """bool UI_IsRecordedFftScreen(void);
void UI_ShowFileBrowser(void);
bool UI_IsFileBrowserScreen(void);
void UI_RefreshFileBrowser(void);
uint32_t UI_GetBrowserPendingRow(void);
""",
    "recorded FFT public API",
)

# ---------------------------------------------------------------------------
# UI implementation.
# ---------------------------------------------------------------------------
ui_c = once(
    ui_c,
    '#include "display.h"\n',
    '#include "display.h"\n#include "recording_catalog.h"\n',
    "display include",
)
ui_c = once(
    ui_c,
    "#define UI_REC_STATUS_REFRESH_MS   100U\n",
    """#define UI_REC_STATUS_REFRESH_MS   100U

/* UI_WAV_BROWSER_V2_PATCH */
#define UI_FILES_X                 188U
#define UI_FILES_Y                 166U
#define UI_FILES_WIDTH             120U
#define UI_FILES_HEIGHT            35U
#define UI_BROWSER_ROW_TOP         42U
#define UI_BROWSER_ROW_HEIGHT      27U
""",
    "recorder status refresh constant",
)
ui_c = once(
    ui_c,
    "    UI_BUTTON_FFT_PEAK\n} UI_Button;\n",
    """    UI_BUTTON_FFT_PEAK,
    UI_BUTTON_BROWSER_BACK,
    UI_BUTTON_BROWSER_PREVIOUS,
    UI_BUTTON_BROWSER_NEXT,
    UI_BUTTON_BROWSER_SELECT,
    UI_BUTTON_BROWSER_REFRESH
} UI_Button;
""",
    "UI button enum tail",
)
ui_c = once(
    ui_c,
    "    UI_MODE_RECORDED_FFT\n} UI_Mode;\n",
    """    UI_MODE_RECORDED_FFT,
    UI_MODE_FILE_BROWSER
} UI_Mode;
""",
    "UI mode enum tail",
)
ui_c = once(
    ui_c,
    "    uint32_t last_recorder_draw_ms;\n",
    """    uint32_t last_recorder_draw_ms;
    uint32_t browser_row;
    uint32_t browser_pending_row;
""",
    "recorder state tail",
)
ui_c = once(
    ui_c,
    "static void ui_draw_recorder_body(void);\n",
    """static void ui_draw_recorder_body(void);
static void ui_draw_file_browser_body(void);
""",
    "recorder body prototype",
)

# UI_Init scoped initialization.
start, end = function_range(ui_c, "void UI_Init(void)\n")
function = ui_c[start:end]
function = once(
    function,
    "    ui.last_recorder_draw_ms = HAL_GetTick();\n",
    """    ui.last_recorder_draw_ms = HAL_GetTick();
    ui.browser_row = 0U;
    ui.browser_pending_row = 0U;
""",
    "browser state initialization",
)
ui_c = ui_c[:start] + function + ui_c[end:]

# Browser public functions after recorded FFT screen query.
api_anchor = r'''bool UI_IsRecordedFftScreen(void)
{
    return ui.mode == UI_MODE_RECORDED_FFT;
}

'''
api_replacement = api_anchor + r'''void UI_ShowFileBrowser(void)
{
    ui.mode = UI_MODE_FILE_BROWSER;
    ui.running = false;
    ui.browser_row = 0U;
    ui.browser_pending_row = 0U;
    ui.graph_dirty = true;
    ui.controls_dirty = true;
    Display_Clear(COLOR_BLACK);
}

bool UI_IsFileBrowserScreen(void)
{
    return ui.mode == UI_MODE_FILE_BROWSER;
}

void UI_RefreshFileBrowser(void)
{
    uint32_t count = RecordingCatalog_GetPageEntryCount();

    if (count == 0U)
    {
        ui.browser_row = 0U;
    }
    else if (ui.browser_row >= count)
    {
        ui.browser_row = count - 1U;
    }

    ui.browser_pending_row = ui.browser_row;
    ui.graph_dirty = true;
    ui.controls_dirty = true;
}

uint32_t UI_GetBrowserPendingRow(void)
{
    return ui.browser_pending_row;
}

'''
ui_c = once(
    ui_c,
    api_anchor,
    api_replacement,
    "recorded FFT screen query",
)

# Browser draw branch scoped to UI_DrawSpectrum.
start, end = function_range(ui_c, "void UI_DrawSpectrum(void)\n")
function = ui_c[start:end]
function = once(
    function,
    "    if (ui.mode == UI_MODE_WAVEFORM)\n",
    """    if (ui.mode == UI_MODE_FILE_BROWSER)
    {
        ui_draw_file_browser_body();
        ui.graph_dirty = false;
        return;
    }

    if (ui.mode == UI_MODE_WAVEFORM)
""",
    "file-browser draw branch",
)
ui_c = ui_c[:start] + function + ui_c[end:]

# Browser controls scoped to ui_draw_controls.
start, end = function_range(ui_c, "static void ui_draw_controls(void)\n")
function = ui_c[start:end]
function = once(
    function,
    "    if (ui.mode == UI_MODE_RECORDED_FFT)\n",
    r'''    if (ui.mode == UI_MODE_FILE_BROWSER)
    {
        Display_FillRect(0, UI_CONTROL_TOP, DISPLAY_WIDTH,
                         UI_CONTROL_HEIGHT, COLOR_BLACK);
        ui_draw_button(0, COLOR_BLUE, COLOR_WHITE);
        ui_draw_button(64, COLOR_CYAN, COLOR_WHITE);
        ui_draw_button(128, COLOR_GREEN, COLOR_WHITE);
        ui_draw_button(192, COLOR_YELLOW, COLOR_WHITE);
        ui_draw_button(256, COLOR_MAGENTA, COLOR_WHITE);
        Display_DrawString(22, 12, "BACK", COLOR_BLACK, COLOR_BLUE);
        Display_DrawString(80, 12, "PREV", COLOR_BLACK, COLOR_CYAN);
        Display_DrawString(144, 12, "NEXT", COLOR_BLACK, COLOR_GREEN);
        Display_DrawString(196, 12, "SELECT", COLOR_BLACK, COLOR_YELLOW);
        Display_DrawString(264, 12, "REFRESH", COLOR_BLACK, COLOR_MAGENTA);
        return;
    }

    if (ui.mode == UI_MODE_RECORDED_FFT)
''',
    "browser control branch",
)
ui_c = ui_c[:start] + function + ui_c[end:]

# Add FILES button to recorder body.
files_anchor = """    Display_DrawString(188, 142, "CLIP CNT", COLOR_WHITE, COLOR_BLACK);
    ui_draw_uint(248, 142,
                 ui.recorder_status.clip_count,
                 ui.recorder_status.clipping ? COLOR_RED : COLOR_YELLOW);
"""
files_replacement = files_anchor + r'''

    Display_FillRect(UI_FILES_X, UI_FILES_Y,
                     UI_FILES_WIDTH, UI_FILES_HEIGHT, COLOR_BLUE);
    Display_DrawRect(UI_FILES_X, UI_FILES_Y,
                     UI_FILES_WIDTH, UI_FILES_HEIGHT, COLOR_WHITE);
    Display_DrawString(226, 180, "FILES", COLOR_BLACK, COLOR_BLUE);
'''
ui_c = once(
    ui_c,
    files_anchor,
    files_replacement,
    "recorder clipping counter",
)

# Browser body before waveform body.
browser_body = r'''static void ui_draw_file_browser_body(void)
{
    uint32_t entry_count = RecordingCatalog_GetPageEntryCount();

    Display_FillRect(UI_GRAPH_LEFT, UI_GRAPH_TOP,
                     UI_GRAPH_WIDTH, UI_GRAPH_HEIGHT, COLOR_BLACK);
    Display_DrawRect(UI_GRAPH_LEFT, UI_GRAPH_TOP,
                     UI_GRAPH_WIDTH, UI_GRAPH_HEIGHT, COLOR_GRAY);

    for (uint32_t row = 0U; row < RECORDING_BROWSER_PAGE_SIZE; row++)
    {
        int32_t y = (int32_t)UI_BROWSER_ROW_TOP +
                    ((int32_t)row * (int32_t)UI_BROWSER_ROW_HEIGHT);
        uint16_t background =
            (row == ui.browser_row) ? COLOR_BLUE : COLOR_BLACK;
        uint16_t border =
            (row == ui.browser_row) ? COLOR_WHITE : COLOR_DARK_GRAY;
        const RecordingCatalogEntry *entry =
            RecordingCatalog_GetPageEntry(row);

        Display_FillRect(4, y, 312, UI_BROWSER_ROW_HEIGHT - 2U,
                         background);
        Display_DrawRect(4, y, 312, UI_BROWSER_ROW_HEIGHT - 2U,
                         border);

        if ((row < entry_count) && (entry != NULL))
        {
            Display_DrawString(12, y + 9, entry->filename,
                               COLOR_WHITE, background);
            ui_draw_uint(160, y + 9, entry->file_size, COLOR_YELLOW);
            Display_DrawString(240, y + 9, "BYTES",
                               COLOR_GRAY, background);
        }
    }
}

'''
ui_c = once(
    ui_c,
    "static void ui_draw_waveform_body(void)\n",
    browser_body + "static void ui_draw_waveform_body(void)\n",
    "waveform body definition",
)

# Browser status branch scoped to ui_draw_status.
start, end = function_range(ui_c, "static void ui_draw_status(void)\n")
function = ui_c[start:end]
function = once(
    function,
    "    if (ui.mode == UI_MODE_RECORDED_FFT)\n",
    r'''    if (ui.mode == UI_MODE_FILE_BROWSER)
    {
        Display_FillRect(0, UI_STATUS_TOP, DISPLAY_WIDTH,
                         UI_STATUS_HEIGHT, COLOR_BLACK);
        Display_DrawRect(0, UI_STATUS_TOP, DISPLAY_WIDTH,
                         UI_STATUS_HEIGHT, COLOR_GRAY);
        Display_DrawString(4, UI_STATUS_TOP + 8,
                           "PAGE", COLOR_WHITE, COLOR_BLACK);
        ui_draw_uint(40, UI_STATUS_TOP + 8,
                     RecordingCatalog_GetPageIndex() + 1U, COLOR_YELLOW);
        Display_DrawString(60, UI_STATUS_TOP + 8,
                           "/", COLOR_WHITE, COLOR_BLACK);
        ui_draw_uint(70, UI_STATUS_TOP + 8,
                     RecordingCatalog_GetPageCount(), COLOR_YELLOW);
        Display_DrawString(110, UI_STATUS_TOP + 8,
                           "SELECTED", COLOR_WHITE, COLOR_BLACK);
        Display_DrawString(176, UI_STATUS_TOP + 8,
                           RecordingCatalog_GetSelected(),
                           COLOR_GREEN, COLOR_BLACK);
        return;
    }

    if (ui.mode == UI_MODE_RECORDED_FFT)
''',
    "browser status branch",
)
ui_c = ui_c[:start] + function + ui_c[end:]

# Browser top-button hit testing.
start, end = function_range(
    ui_c,
    "static UI_Button ui_hit_test_button(uint16_t x, uint16_t y)\n",
)
function = ui_c[start:end]
function = once(
    function,
    "    if (ui.mode == UI_MODE_RECORDED_FFT)\n",
    r'''    if (ui.mode == UI_MODE_FILE_BROWSER)
    {
        if (x < 64U) return UI_BUTTON_BROWSER_BACK;
        if (x < 128U) return UI_BUTTON_BROWSER_PREVIOUS;
        if (x < 192U) return UI_BUTTON_BROWSER_NEXT;
        if (x < 256U) return UI_BUTTON_BROWSER_SELECT;
        if (x < DISPLAY_WIDTH) return UI_BUTTON_BROWSER_REFRESH;
        return UI_BUTTON_NONE;
    }

    if (ui.mode == UI_MODE_RECORDED_FFT)
''',
    "browser hit-test branch",
)
ui_c = ui_c[:start] + function + ui_c[end:]

# Browser button actions only queue foreground actions.
action_anchor = "        case UI_BUTTON_FFT_BACK:\n"
action_code = r'''        case UI_BUTTON_BROWSER_BACK:
            ui.pending_action = UI_ACTION_BROWSER_BACK;
            UART_SendString("UI FILE BROWSER BACK REQUEST\r\n");
            break;

        case UI_BUTTON_BROWSER_PREVIOUS:
            ui.pending_action = UI_ACTION_BROWSER_PREVIOUS;
            UART_SendString("UI FILES PREV REQUEST\r\n");
            break;

        case UI_BUTTON_BROWSER_NEXT:
            ui.pending_action = UI_ACTION_BROWSER_NEXT;
            UART_SendString("UI FILES NEXT REQUEST\r\n");
            break;

        case UI_BUTTON_BROWSER_SELECT:
            ui.browser_pending_row = ui.browser_row;
            ui.pending_action = UI_ACTION_BROWSER_SELECT_ROW;
            UART_SendString("UI FILE SELECT REQUEST\r\n");
            break;

        case UI_BUTTON_BROWSER_REFRESH:
            ui.pending_action = UI_ACTION_BROWSER_REFRESH;
            UART_SendString("UI FILES REFRESH REQUEST\r\n");
            break;

        case UI_BUTTON_FFT_BACK:
'''
ui_c = once(
    ui_c,
    action_anchor,
    action_code,
    "FFT-back action",
)

# Recorder FILES and browser row touches, scoped to stable touch-up function.
start, end = function_range(ui_c, "static void ui_handle_touch_up(void)\n")
function = ui_c[start:end]
function = once(
    function,
    """    if (!ui.dragging &&
        (ui.mode == UI_MODE_ANALYZER) &&
""",
    """    if (!ui.dragging &&
        (ui.mode == UI_MODE_RECORDER) &&
        (ui.touch_start_x >= UI_FILES_X) &&
        (ui.touch_start_x < (UI_FILES_X + UI_FILES_WIDTH)) &&
        (ui.touch_start_y >= UI_FILES_Y) &&
        (ui.touch_start_y < (UI_FILES_Y + UI_FILES_HEIGHT)) &&
        (ui.previous_touch_x >= UI_FILES_X) &&
        (ui.previous_touch_x < (UI_FILES_X + UI_FILES_WIDTH)) &&
        (ui.previous_touch_y >= UI_FILES_Y) &&
        (ui.previous_touch_y < (UI_FILES_Y + UI_FILES_HEIGHT)))
    {
        ui.pending_action = UI_ACTION_BROWSER_OPEN;
        UART_SendString("UI FILE BROWSER REQUEST\\r\\n");
        action_executed = true;
    }
    else if (!ui.dragging &&
             (ui.mode == UI_MODE_FILE_BROWSER) &&
             (ui.touch_start_y >= UI_BROWSER_ROW_TOP) &&
             (ui.touch_start_y < (UI_BROWSER_ROW_TOP +
              (RECORDING_BROWSER_PAGE_SIZE * UI_BROWSER_ROW_HEIGHT))) &&
             (ui.previous_touch_y >= UI_BROWSER_ROW_TOP) &&
             (ui.previous_touch_y < (UI_BROWSER_ROW_TOP +
              (RECORDING_BROWSER_PAGE_SIZE * UI_BROWSER_ROW_HEIGHT))))
    {
        uint32_t start_row =
            (ui.touch_start_y - UI_BROWSER_ROW_TOP) /
            UI_BROWSER_ROW_HEIGHT;
        uint32_t release_row =
            (ui.previous_touch_y - UI_BROWSER_ROW_TOP) /
            UI_BROWSER_ROW_HEIGHT;

        if ((start_row == release_row) &&
            (start_row < RecordingCatalog_GetPageEntryCount()))
        {
            ui.browser_row = start_row;
            ui.browser_pending_row = start_row;
            ui.pending_action = UI_ACTION_BROWSER_SELECT_ROW;
            ui.graph_dirty = true;
            UART_SendString("UI FILE ROW SELECT REQUEST\\r\\n");
            action_executed = true;
        }
    }
    else if (!ui.dragging &&
        (ui.mode == UI_MODE_ANALYZER) &&
""",
    "analyzer REC-tab touch branch",
)
ui_c = ui_c[:start] + function + ui_c[end:]

# ---------------------------------------------------------------------------
# Foreground action processing in main.c.
# ---------------------------------------------------------------------------
main_c = once(
    main_c,
    "/* UI_RECORDED_FFT_PATCH */\n",
    "/* UI_RECORDED_FFT_PATCH */\n/* UI_WAV_BROWSER_V2_PATCH */\n",
    "recorded FFT marker",
)

main_action_anchor = """        case UI_ACTION_FFT_BACK:
            WavFftViewer_End();
            UART_SendString("UI recorded FFT closed\\r\\n");
            break;

        case UI_ACTION_NONE:
"""
main_action_code = r'''        case UI_ACTION_FFT_BACK:
            WavFftViewer_End();
            UART_SendString("UI recorded FFT closed\r\n");
            break;

        case UI_ACTION_BROWSER_OPEN:
            if (Capture_IsRecorderActive())
            {
                UART_SendString("Stop recording before file browser\r\n");
                break;
            }

            Recorder_GetStatus(&status);
            if (status.state != RECORDER_STATE_IDLE)
            {
                UART_SendString("Wait for recorder IDLE before file browser\r\n");
                break;
            }

            result = RecordingCatalog_LoadPage(0U);
            if (result == FR_OK)
            {
                UI_ShowFileBrowser();
                UART_SendString("UI file browser loaded\r\n");
            }
            else
            {
                UART_SendString("UI file browser load failed, FatFs = ");
                UART_SendDec((uint32_t)result);
                UART_SendString("\r\n");
            }
            break;

        case UI_ACTION_BROWSER_BACK:
            UI_ShowRecorder();
            UART_SendString("UI RECORDER SCREEN\r\n");
            break;

        case UI_ACTION_BROWSER_PREVIOUS:
        {
            uint32_t page = RecordingCatalog_GetPageIndex();
            if (page > 0U)
            {
                page--;
            }
            result = RecordingCatalog_LoadPage(page);
            if (result == FR_OK)
            {
                UI_RefreshFileBrowser();
            }
            break;
        }

        case UI_ACTION_BROWSER_NEXT:
        {
            uint32_t page = RecordingCatalog_GetPageIndex();
            uint32_t page_count = RecordingCatalog_GetPageCount();
            if ((page + 1U) < page_count)
            {
                page++;
            }
            result = RecordingCatalog_LoadPage(page);
            if (result == FR_OK)
            {
                UI_RefreshFileBrowser();
            }
            break;
        }

        case UI_ACTION_BROWSER_REFRESH:
            result = RecordingCatalog_LoadPage(
                RecordingCatalog_GetPageIndex());
            if (result == FR_OK)
            {
                UI_RefreshFileBrowser();
            }
            break;

        case UI_ACTION_BROWSER_SELECT_ROW:
            if (RecordingCatalog_SelectPageEntry(
                    UI_GetBrowserPendingRow()))
            {
                UART_SendString("UI selected WAV: ");
                UART_SendString(RecordingCatalog_GetSelected());
                UART_SendString("\r\n");
                UI_RefreshFileBrowser();
            }
            else
            {
                UART_SendString("UI file selection failed\r\n");
            }
            break;

        case UI_ACTION_NONE:
'''
main_c = once(
    main_c,
    main_action_anchor,
    main_action_code,
    "main recorder action tail",
)

# UI_ShowRecorder is a small foreground transition API. Add declaration.
ui_h = once(
    ui_h,
    "bool UI_IsRecorderScreen(void);\n",
    """bool UI_IsRecorderScreen(void);
void UI_ShowRecorder(void);
""",
    "recorder screen API",
)

# Add implementation beside recorder screen query.
recorder_api_anchor = r'''bool UI_IsRecorderScreen(void)
{
    return ui.mode == UI_MODE_RECORDER;
}

'''
recorder_api_replacement = recorder_api_anchor + r'''void UI_ShowRecorder(void)
{
    ui.mode = UI_MODE_RECORDER;
    ui.running = false;
    ui.graph_dirty = true;
    ui.controls_dirty = true;
    Display_Clear(COLOR_BLACK);
}

'''
ui_c = once(
    ui_c,
    recorder_api_anchor,
    recorder_api_replacement,
    "recorder screen query",
)

# ---------------------------------------------------------------------------
# Validate every output before writing.
# ---------------------------------------------------------------------------
outputs = []
for path, text in (
    (CAT_H, cat_h),
    (CAT_C, cat_c),
    (UI_H, ui_h),
    (UI_C, ui_c),
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
        fail("NUL byte generated in " + str(path))
    outputs.append((path, text))

combined = "\n".join(text for _, text in outputs)
for symbol in (
    MARKER,
    "RecordingCatalog_LoadPage",
    "UI_MODE_FILE_BROWSER",
    "UI_ACTION_BROWSER_SELECT_ROW",
    "UI_ShowFileBrowser",
    "UI_ShowRecorder",
):
    if symbol not in combined:
        fail("Missing generated symbol: " + symbol)

# Ensure recorder-priority loop remains present and unchanged in intent.
for required_call in (
    "Capture_ProcessRecorder();",
    "Recorder_Process();",
    "UI_ProcessTouch();",
    "UI_ProcessRecorderActions();",
    "Console_Poll();",
):
    if required_call not in main_c:
        fail("Recorder-priority loop prerequisite missing: " + required_call)

for path, text in outputs:
    path.write_text(text, encoding="utf-8")

print("Stable WAV browser v2 patch applied successfully.")
print("Modified: recording_catalog.h/.c, ui.h/.c, and main.c")
print("Added read-only six-file paging with filename and byte size.")
print("Filesystem scans run only in foreground action processing.")
print("Touch stability and recorder-priority scheduling are preserved.")
print("Next: make clean && make -j4")
