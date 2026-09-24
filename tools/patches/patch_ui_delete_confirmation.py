#!/usr/bin/env python3
"""Add safe, confirmed touchscreen deletion for selected WAV files."""

from pathlib import Path
import re
import shutil

CAT_H = Path("Core/Inc/recording_catalog.h")
CAT_C = Path("Core/Src/recording_catalog.c")
UI_H = Path("Core/Inc/ui.h")
UI_C = Path("Core/Src/ui.c")
MAIN_C = Path("Core/Src/main.c")
MARKER = "UI_DELETE_CONFIRMATION_PATCH"


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
    target = path.with_name(path.name + ".before_delete_confirmation")
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
        fail("Delete confirmation is already installed in " + str(path))
    if re.search(r"&(lt|gt|amp);|<br", text):
        fail("HTML artifact found in " + str(path))

for required in (
    "UI_FILE_ACTION_SCREEN_PATCH",
    "UI_DEFAULT_WAV_SELECTION_PATCH",
    "UI_WAV_INFO_SCREEN_PATCH",
    "UI_FILES_TOUCH_FIX",
):
    if required not in (cat_h + cat_c + ui_h + ui_c + main_c):
        fail("Missing prerequisite: " + required)

for path in paths:
    backup(path)

# Catalog delete API.
cat_h = once(
    cat_h,
    "bool RecordingCatalog_EnsureSelectedValid(void);\n",
    """bool RecordingCatalog_EnsureSelectedValid(void);

/* UI_DELETE_CONFIRMATION_PATCH */
FRESULT RecordingCatalog_DeleteFile(const char *filename);
""",
    "catalog selected-valid API",
)

catalog_delete = r'''FRESULT RecordingCatalog_DeleteFile(const char *filename)
{
    FILINFO information;
    FRESULT result;
    size_t length;

    if (filename == NULL)
    {
        return FR_INVALID_NAME;
    }

    length = strlen(filename);
    if ((length == 0U) ||
        (length >= RECORDING_FILENAME_CAPACITY) ||
        !filename_is_wav(filename))
    {
        return FR_INVALID_NAME;
    }

    result = FileSystem_Mount();
    if (result != FR_OK)
    {
        return result;
    }

    result = f_stat(filename, &information);
    if (result != FR_OK)
    {
        return result;
    }

    if ((information.fattrib & AM_DIR) != 0U)
    {
        return FR_DENIED;
    }

    result = f_unlink(filename);
    if ((result == FR_OK) &&
        (strcmp(selected_filename, filename) == 0))
    {
        selected_filename[0] = '\0';
    }

    return result;
}

'''
cat_c = once(
    cat_c,
    "FRESULT RecordingCatalog_StartNext(void)\n",
    catalog_delete + "FRESULT RecordingCatalog_StartNext(void)\n",
    "catalog start-next definition",
)

# Public UI actions and APIs.
ui_h = once(
    ui_h,
    """    UI_ACTION_FILE_INFO,
    UI_ACTION_FILE_DELETE
} UI_Action;
""",
    """    UI_ACTION_FILE_INFO,
    UI_ACTION_FILE_DELETE,
    UI_ACTION_FILE_DELETE_CANCEL,
    UI_ACTION_FILE_DELETE_CONFIRM
} UI_Action;
""",
    "file-delete action enum tail",
)

api_anchor = "bool UI_IsWavInfoScreen(void);\n"
ui_h = once(
    ui_h,
    api_anchor,
    api_anchor + """

/* UI_DELETE_CONFIRMATION_PATCH */
void UI_ShowDeleteConfirmation(const char *filename);
void UI_ReturnToFileActions(void);
bool UI_IsDeleteConfirmationScreen(void);
const char *UI_GetDeleteFilename(void);
""",
    "WAV info public API",
)

# UI enums and state.
ui_c = once(
    ui_c,
    """    UI_BUTTON_FILE_ACTIONS_DELETE,
    UI_BUTTON_WAV_INFO_BACK
} UI_Button;
""",
    """    UI_BUTTON_FILE_ACTIONS_DELETE,
    UI_BUTTON_WAV_INFO_BACK,
    UI_BUTTON_DELETE_BACK,
    UI_BUTTON_DELETE_CANCEL,
    UI_BUTTON_DELETE_CONFIRM
} UI_Button;
""",
    "WAV info button enum tail",
)

ui_c = once(
    ui_c,
    """    UI_MODE_FILE_ACTIONS,
    UI_MODE_WAV_INFO
} UI_Mode;
""",
    """    UI_MODE_FILE_ACTIONS,
    UI_MODE_WAV_INFO,
    UI_MODE_FILE_DELETE_CONFIRM
} UI_Mode;
""",
    "WAV info mode enum tail",
)

ui_c = once(
    ui_c,
    "    UI_WavInfo wav_info;\n",
    """    UI_WavInfo wav_info;
    char delete_filename[RECORDING_FILENAME_CAPACITY];
""",
    "WAV info state field",
)

ui_c = once(
    ui_c,
    "static void ui_draw_wav_info_body(void);\n",
    """static void ui_draw_wav_info_body(void);
static void ui_draw_delete_confirmation_body(void);
""",
    "WAV info body prototype",
)

# Initialize frozen target.
start, end = function_range(ui_c, "void UI_Init(void)\n")
fn = ui_c[start:end]
fn = once(
    fn,
    "    memset(&ui.wav_info, 0, sizeof(ui.wav_info));\n",
    """    memset(&ui.wav_info, 0, sizeof(ui.wav_info));
    ui.delete_filename[0] = '\0';
""",
    "WAV info initialization",
)
ui_c = ui_c[:start] + fn + ui_c[end:]

# Transition APIs after WAV INFO query.
anchor = r'''bool UI_IsWavInfoScreen(void)
{
    return ui.mode == UI_MODE_WAV_INFO;
}

'''
replacement = anchor + r'''void UI_ShowDeleteConfirmation(const char *filename)
{
    if (filename == NULL)
    {
        return;
    }

    strncpy(ui.delete_filename,
            filename,
            sizeof(ui.delete_filename) - 1U);
    ui.delete_filename[sizeof(ui.delete_filename) - 1U] = '\0';
    ui.mode = UI_MODE_FILE_DELETE_CONFIRM;
    ui.running = false;
    ui.graph_dirty = true;
    ui.controls_dirty = true;
    Display_Clear(COLOR_BLACK);
}

void UI_ReturnToFileActions(void)
{
    UI_ShowFileActions();
}

bool UI_IsDeleteConfirmationScreen(void)
{
    return ui.mode == UI_MODE_FILE_DELETE_CONFIRM;
}

const char *UI_GetDeleteFilename(void)
{
    return ui.delete_filename;
}

'''
ui_c = once(ui_c, anchor, replacement, "WAV info screen query")

# Draw branch.
start, end = function_range(ui_c, "void UI_DrawSpectrum(void)\n")
fn = ui_c[start:end]
fn = once(
    fn,
    "    if (ui.mode == UI_MODE_WAV_INFO)\n",
    """    if (ui.mode == UI_MODE_FILE_DELETE_CONFIRM)
    {
        ui_draw_delete_confirmation_body();
        ui.graph_dirty = false;
        return;
    }

    if (ui.mode == UI_MODE_WAV_INFO)
""",
    "WAV info draw branch",
)
ui_c = ui_c[:start] + fn + ui_c[end:]

# Confirmation controls.
start, end = function_range(ui_c, "static void ui_draw_controls(void)\n")
fn = ui_c[start:end]
fn = once(
    fn,
    "    if (ui.mode == UI_MODE_WAV_INFO)\n",
    r'''    if (ui.mode == UI_MODE_FILE_DELETE_CONFIRM)
    {
        Display_FillRect(0, UI_CONTROL_TOP, DISPLAY_WIDTH,
                         UI_CONTROL_HEIGHT, COLOR_BLACK);
        ui_draw_button(0, COLOR_BLUE, COLOR_WHITE);
        ui_draw_button(64, COLOR_CYAN, COLOR_WHITE);
        ui_draw_button(128, COLOR_DARK_GRAY, COLOR_GRAY);
        ui_draw_button(192, COLOR_DARK_GRAY, COLOR_GRAY);
        ui_draw_button(256, COLOR_RED, COLOR_WHITE);
        Display_DrawString(22, 12, "BACK", COLOR_BLACK, COLOR_BLUE);
        Display_DrawString(72, 12, "CANCEL", COLOR_BLACK, COLOR_CYAN);
        Display_DrawString(260, 12, "CONFIRM", COLOR_WHITE, COLOR_RED);
        return;
    }

    if (ui.mode == UI_MODE_WAV_INFO)
''',
    "WAV info control branch",
)
ui_c = ui_c[:start] + fn + ui_c[end:]

# Confirmation body.
delete_body = r'''static void ui_draw_delete_confirmation_body(void)
{
    FILINFO information;
    FRESULT result;

    Display_FillRect(UI_GRAPH_LEFT, UI_GRAPH_TOP,
                     UI_GRAPH_WIDTH, UI_GRAPH_HEIGHT, COLOR_BLACK);
    Display_DrawRect(UI_GRAPH_LEFT, UI_GRAPH_TOP,
                     UI_GRAPH_WIDTH, UI_GRAPH_HEIGHT, COLOR_RED);

    Display_DrawString(66, 48, "DELETE WAV FILE?",
                       COLOR_RED, COLOR_BLACK);
    Display_DrawString(94, 82, ui.delete_filename,
                       COLOR_GREEN, COLOR_BLACK);

    result = f_stat(ui.delete_filename, &information);
    if (result == FR_OK)
    {
        ui_draw_uint(92, 110, (uint32_t)information.fsize, COLOR_YELLOW);
        Display_DrawString(176, 110, "BYTES", COLOR_GRAY, COLOR_BLACK);
    }
    else
    {
        Display_DrawString(82, 110, "FILE NOT FOUND",
                           COLOR_RED, COLOR_BLACK);
    }

    Display_DrawString(54, 148, "THIS CANNOT BE UNDONE",
                       COLOR_YELLOW, COLOR_BLACK);
    Display_DrawString(44, 184, "PRESS CONFIRM TO DELETE",
                       COLOR_WHITE, COLOR_BLACK);
}

'''
ui_c = once(
    ui_c,
    "static void ui_draw_wav_info_body(void)\n",
    delete_body + "static void ui_draw_wav_info_body(void)\n",
    "WAV info body definition",
)

# Status branch.
start, end = function_range(ui_c, "static void ui_draw_status(void)\n")
fn = ui_c[start:end]
fn = once(
    fn,
    "    if (ui.mode == UI_MODE_WAV_INFO)\n",
    r'''    if (ui.mode == UI_MODE_FILE_DELETE_CONFIRM)
    {
        Display_FillRect(0, UI_STATUS_TOP, DISPLAY_WIDTH,
                         UI_STATUS_HEIGHT, COLOR_BLACK);
        Display_DrawRect(0, UI_STATUS_TOP, DISPLAY_WIDTH,
                         UI_STATUS_HEIGHT, COLOR_RED);
        Display_DrawString(4, UI_STATUS_TOP + 8,
                           "DELETE TARGET", COLOR_WHITE, COLOR_BLACK);
        Display_DrawString(112, UI_STATUS_TOP + 8,
                           ui.delete_filename, COLOR_RED, COLOR_BLACK);
        return;
    }

    if (ui.mode == UI_MODE_WAV_INFO)
''',
    "WAV info status branch",
)
ui_c = ui_c[:start] + fn + ui_c[end:]

# Hit test confirmation controls.
start, end = function_range(
    ui_c,
    "static UI_Button ui_hit_test_button(uint16_t x, uint16_t y)\n",
)
fn = ui_c[start:end]
fn = once(
    fn,
    "    if (ui.mode == UI_MODE_WAV_INFO)\n",
    r'''    if (ui.mode == UI_MODE_FILE_DELETE_CONFIRM)
    {
        if (x < 64U) return UI_BUTTON_DELETE_BACK;
        if (x < 128U) return UI_BUTTON_DELETE_CANCEL;
        if (x >= 256U) return UI_BUTTON_DELETE_CONFIRM;
        return UI_BUTTON_NONE;
    }

    if (ui.mode == UI_MODE_WAV_INFO)
''',
    "WAV info hit-test branch",
)
ui_c = ui_c[:start] + fn + ui_c[end:]

# Button actions. DELETE opens confirmation; confirmation buttons queue actions.
ui_c = once(
    ui_c,
    """        case UI_BUTTON_FILE_ACTIONS_DELETE:
            ui.pending_action = UI_ACTION_FILE_DELETE;
            UART_SendString("UI FILE DELETE REQUEST\\r\\n");
            break;
""",
    """        case UI_BUTTON_FILE_ACTIONS_DELETE:
            ui.pending_action = UI_ACTION_FILE_DELETE;
            UART_SendString("UI FILE DELETE REQUEST\\r\\n");
            break;

        case UI_BUTTON_DELETE_BACK:
        case UI_BUTTON_DELETE_CANCEL:
            ui.pending_action = UI_ACTION_FILE_DELETE_CANCEL;
            UART_SendString("UI DELETE CANCEL REQUEST\\r\\n");
            break;

        case UI_BUTTON_DELETE_CONFIRM:
            ui.pending_action = UI_ACTION_FILE_DELETE_CONFIRM;
            UART_SendString("UI DELETE CONFIRM REQUEST\\r\\n");
            break;
""",
    "file-actions DELETE button case",
)

# Main feature marker.
main_c = once(
    main_c,
    "/* UI_WAV_INFO_SCREEN_PATCH */\n",
    """/* UI_WAV_INFO_SCREEN_PATCH */
/* UI_DELETE_CONFIRMATION_PATCH */
""",
    "WAV info feature marker",
)

# Replace disabled delete placeholder with open/cancel/confirm handlers.
old_delete = r'''        case UI_ACTION_FILE_DELETE:
            UART_SendString("UI file DELETE is disabled\r\n");
            break;
'''
new_delete = r'''        case UI_ACTION_FILE_DELETE:
            if (Capture_IsRecorderActive())
            {
                UART_SendString("Stop recording before delete\r\n");
                break;
            }

            Recorder_GetStatus(&status);
            if (status.state != RECORDER_STATE_IDLE)
            {
                UART_SendString("Wait for recorder IDLE before delete\r\n");
                break;
            }

            if (!RecordingCatalog_EnsureSelectedValid())
            {
                UART_SendString("Select a valid WAV before delete\r\n");
                break;
            }

            UI_ShowDeleteConfirmation(RecordingCatalog_GetSelected());
            UART_SendString("UI delete confirmation loaded: ");
            UART_SendString(RecordingCatalog_GetSelected());
            UART_SendString("\r\n");
            break;

        case UI_ACTION_FILE_DELETE_CANCEL:
            UI_ReturnToFileActions();
            UART_SendString("UI delete cancelled\r\n");
            break;

        case UI_ACTION_FILE_DELETE_CONFIRM:
        {
            char deleted_name[RECORDING_FILENAME_CAPACITY];
            uint32_t page;

            if (Capture_IsRecorderActive())
            {
                UART_SendString("Stop recording before delete\r\n");
                UI_ReturnToFileActions();
                break;
            }

            Recorder_GetStatus(&status);
            if (status.state != RECORDER_STATE_IDLE)
            {
                UART_SendString("Wait for recorder IDLE before delete\r\n");
                UI_ReturnToFileActions();
                break;
            }

            strncpy(deleted_name,
                    UI_GetDeleteFilename(),
                    sizeof(deleted_name) - 1U);
            deleted_name[sizeof(deleted_name) - 1U] = '\0';

            WavFftViewer_End();
            result = RecordingCatalog_DeleteFile(deleted_name);
            if (result != FR_OK)
            {
                UART_SendString("UI delete failed, FatFs = ");
                UART_SendDec((uint32_t)result);
                UART_SendString("\r\n");
                UI_ReturnToFileActions();
                break;
            }

            UART_SendString("UI delete OK: ");
            UART_SendString(deleted_name);
            UART_SendString("\r\n");

            page = RecordingCatalog_GetPageIndex();
            result = RecordingCatalog_LoadPage(page);
            if (result == FR_OK)
            {
                if (RecordingCatalog_EnsureSelectedValid())
                {
                    UART_SendString("UI selected WAV: ");
                    UART_SendString(RecordingCatalog_GetSelected());
                    UART_SendString("\r\n");
                }
                else
                {
                    UART_SendString("UI file browser has no WAV files\r\n");
                }
                UI_ReturnToFileBrowser();
                UI_RefreshFileBrowser();
            }
            else
            {
                UART_SendString("UI browser refresh failed, FatFs = ");
                UART_SendDec((uint32_t)result);
                UART_SendString("\r\n");
                UI_ShowRecorder();
            }
            break;
        }
'''
main_c = once(main_c, old_delete, new_delete, "disabled delete action")

# Normalize C null-character literals generated by Python string forms.
ui_c = ui_c.replace("\x00", r"\0")
for name, value in (("cat_c", cat_c), ("ui_c", ui_c), ("main_c", main_c)):
    value = value.replace(r"'\\0'", r"'\0'")
    if name == "cat_c":
        cat_c = value
    elif name == "ui_c":
        ui_c = value
    else:
        main_c = value

# Validate and write.
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
    "RecordingCatalog_DeleteFile",
    "UI_MODE_FILE_DELETE_CONFIRM",
    "UI_ACTION_FILE_DELETE_CONFIRM",
    "UI DELETE CONFIRM REQUEST",
):
    if symbol not in combined:
        fail("Missing generated symbol: " + symbol)

for path, text in outputs:
    path.write_text(text, encoding="utf-8")

print("Delete confirmation patch applied successfully.")
print("Modified: recording_catalog.h/.c, ui.h/.c, and main.c")
print("DELETE now opens a confirmation screen.")
print("Only CONFIRM calls f_unlink() in foreground processing.")
print("Next: make clean && make -j4")
