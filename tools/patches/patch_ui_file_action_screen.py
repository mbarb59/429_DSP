#!/usr/bin/env python3
'''Add a selected-WAV action screen to the stable read-only browser.'''
from pathlib import Path
import re
import shutil

UI_H = Path('Core/Inc/ui.h')
UI_C = Path('Core/Src/ui.c')
MAIN_C = Path('Core/Src/main.c')
MARKER = 'UI_FILE_ACTION_SCREEN_PATCH'

def fail(message):
    raise SystemExit(message)

def once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        fail(f'Expected exactly one {label}, found {count}')
    return text.replace(old, new, 1)

def function_range(text, signature):
    start = text.find(signature)
    if start < 0:
        fail('Missing function: ' + signature.strip())
    brace = text.find('{', start)
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == '{':
            depth += 1
        elif text[index] == '}':
            depth -= 1
            if depth == 0:
                end = index + 1
                while end < len(text) and text[end] in ' \t\r\n':
                    end += 1
                return start, end
    fail('Unbalanced function: ' + signature.strip())

def backup(path):
    target = path.with_name(path.name + '.before_file_action_screen')
    if not target.exists():
        shutil.copy2(path, target)

for path in (UI_H, UI_C, MAIN_C):
    if not path.exists():
        fail('Missing required file: ' + str(path))
    if bytes([0]) in path.read_bytes():
        fail('NUL byte found in ' + str(path))

ui_h = UI_H.read_text(encoding='utf-8')
ui_c = UI_C.read_text(encoding='utf-8')
main_c = MAIN_C.read_text(encoding='utf-8')

for path, text in ((UI_H, ui_h), (UI_C, ui_c), (MAIN_C, main_c)):
    if MARKER in text:
        fail('Action screen already installed in ' + str(path))
    if re.search(r'&(lt|gt|amp);|<br', text):
        fail('HTML artifact found in ' + str(path))

for required in ('UI_WAV_BROWSER_V2_PATCH', 'UI_MODE_FILE_BROWSER',
                 'UI_ACTION_BROWSER_SELECT_ROW', 'UI_ShowFileBrowser'):
    if required not in (ui_h + ui_c + main_c):
        fail('Missing browser prerequisite: ' + required)
for required in ('UI_TOUCH_STABILITY_PATCH', 'UI_FILES_TOUCH_FIX'):
    if required not in ui_c:
        fail('Missing touch prerequisite: ' + required)

for path in (UI_H, UI_C, MAIN_C):
    backup(path)

ui_h = once(ui_h,
'''    UI_ACTION_BROWSER_REFRESH,
    UI_ACTION_BROWSER_SELECT_ROW
} UI_Action;
''',
'''    UI_ACTION_BROWSER_REFRESH,
    UI_ACTION_BROWSER_SELECT_ROW,
    UI_ACTION_FILE_ACTIONS_OPEN,
    UI_ACTION_FILE_ACTIONS_BACK,
    UI_ACTION_FILE_INFO,
    UI_ACTION_FILE_DELETE
} UI_Action;
''', 'browser action enum tail')

ui_h = once(ui_h,
'''void UI_ShowFileBrowser(void);
bool UI_IsFileBrowserScreen(void);
void UI_RefreshFileBrowser(void);
uint32_t UI_GetBrowserPendingRow(void);
''',
'''void UI_ShowFileBrowser(void);
bool UI_IsFileBrowserScreen(void);
void UI_RefreshFileBrowser(void);
uint32_t UI_GetBrowserPendingRow(void);

/* UI_FILE_ACTION_SCREEN_PATCH */
void UI_ShowFileActions(void);
void UI_ReturnToFileBrowser(void);
bool UI_IsFileActionsScreen(void);
''', 'browser public API')

ui_c = once(ui_c,
'''    UI_BUTTON_BROWSER_SELECT,
    UI_BUTTON_BROWSER_REFRESH
} UI_Button;
''',
'''    UI_BUTTON_BROWSER_SELECT,
    UI_BUTTON_BROWSER_REFRESH,
    UI_BUTTON_FILE_ACTIONS_BACK,
    UI_BUTTON_FILE_ACTIONS_WAVE,
    UI_BUTTON_FILE_ACTIONS_FFT,
    UI_BUTTON_FILE_ACTIONS_INFO,
    UI_BUTTON_FILE_ACTIONS_DELETE
} UI_Button;
''', 'browser button enum tail')

ui_c = once(ui_c,
'''    UI_MODE_RECORDED_FFT,
    UI_MODE_FILE_BROWSER
} UI_Mode;
''',
'''    UI_MODE_RECORDED_FFT,
    UI_MODE_FILE_BROWSER,
    UI_MODE_FILE_ACTIONS
} UI_Mode;
''', 'browser mode enum tail')

ui_c = once(ui_c, 'static void ui_draw_file_browser_body(void);\n',
'''static void ui_draw_file_browser_body(void);
static void ui_draw_file_actions_body(void);
''', 'browser body prototype')

anchor = '''bool UI_IsFileBrowserScreen(void)
{
    return ui.mode == UI_MODE_FILE_BROWSER;
}

'''
ui_c = once(ui_c, anchor, anchor + '''void UI_ShowFileActions(void)
{
    ui.mode = UI_MODE_FILE_ACTIONS;
    ui.running = false;
    ui.graph_dirty = true;
    ui.controls_dirty = true;
    Display_Clear(COLOR_BLACK);
}

void UI_ReturnToFileBrowser(void)
{
    ui.mode = UI_MODE_FILE_BROWSER;
    ui.running = false;
    ui.graph_dirty = true;
    ui.controls_dirty = true;
    Display_Clear(COLOR_BLACK);
}

bool UI_IsFileActionsScreen(void)
{
    return ui.mode == UI_MODE_FILE_ACTIONS;
}

''', 'browser screen query')

start, end = function_range(ui_c, 'void UI_DrawSpectrum(void)\n')
fn = ui_c[start:end]
fn = once(fn, '    if (ui.mode == UI_MODE_FILE_BROWSER)\n',
'''    if (ui.mode == UI_MODE_FILE_ACTIONS)
    {
        ui_draw_file_actions_body();
        ui.graph_dirty = false;
        return;
    }

    if (ui.mode == UI_MODE_FILE_BROWSER)
''', 'browser draw branch')
ui_c = ui_c[:start] + fn + ui_c[end:]

start, end = function_range(ui_c, 'static void ui_draw_controls(void)\n')
fn = ui_c[start:end]
fn = once(fn, '    if (ui.mode == UI_MODE_FILE_BROWSER)\n',
'''    if (ui.mode == UI_MODE_FILE_ACTIONS)
    {
        Display_FillRect(0, UI_CONTROL_TOP, DISPLAY_WIDTH,
                         UI_CONTROL_HEIGHT, COLOR_BLACK);
        ui_draw_button(0, COLOR_BLUE, COLOR_WHITE);
        ui_draw_button(64, COLOR_CYAN, COLOR_WHITE);
        ui_draw_button(128, COLOR_GREEN, COLOR_WHITE);
        ui_draw_button(192, COLOR_YELLOW, COLOR_WHITE);
        ui_draw_button(256, COLOR_RED, COLOR_WHITE);
        Display_DrawString(22, 12, "BACK", COLOR_BLACK, COLOR_BLUE);
        Display_DrawString(80, 12, "WAVE", COLOR_BLACK, COLOR_CYAN);
        Display_DrawString(150, 12, "FFT", COLOR_BLACK, COLOR_GREEN);
        Display_DrawString(208, 12, "INFO", COLOR_BLACK, COLOR_YELLOW);
        Display_DrawString(270, 12, "DELETE", COLOR_BLACK, COLOR_RED);
        return;
    }

    if (ui.mode == UI_MODE_FILE_BROWSER)
''', 'browser control branch')
fn = once(fn,
'        Display_DrawString(196, 12, "SELECT", COLOR_BLACK, COLOR_YELLOW);\n',
'        Display_DrawString(208, 12, "OPEN", COLOR_BLACK, COLOR_YELLOW);\n',
'browser SELECT label')
ui_c = ui_c[:start] + fn + ui_c[end:]

action_body = '''static void ui_draw_file_actions_body(void)
{
    const char *selected = RecordingCatalog_GetSelected();
    FILINFO information;
    FRESULT result;

    Display_FillRect(UI_GRAPH_LEFT, UI_GRAPH_TOP,
                     UI_GRAPH_WIDTH, UI_GRAPH_HEIGHT, COLOR_BLACK);
    Display_DrawRect(UI_GRAPH_LEFT, UI_GRAPH_TOP,
                     UI_GRAPH_WIDTH, UI_GRAPH_HEIGHT, COLOR_GRAY);
    Display_DrawString(10, 44, "SELECTED WAV", COLOR_CYAN, COLOR_BLACK);
    Display_DrawString(10, 68, selected, COLOR_GREEN, COLOR_BLACK);

    result = f_stat(selected, &information);
    if (result == FR_OK)
    {
        Display_DrawString(10, 96, "SIZE", COLOR_WHITE, COLOR_BLACK);
        ui_draw_uint(58, 96, (uint32_t)information.fsize, COLOR_YELLOW);
        Display_DrawString(140, 96, "BYTES", COLOR_GRAY, COLOR_BLACK);
    }
    else
    {
        Display_DrawString(10, 96, "FILE NOT FOUND", COLOR_RED, COLOR_BLACK);
    }

    Display_DrawString(10, 128, "WAVE", COLOR_CYAN, COLOR_BLACK);
    Display_DrawString(76, 128, "VIEW WAVEFORM", COLOR_WHITE, COLOR_BLACK);
    Display_DrawString(10, 150, "FFT", COLOR_GREEN, COLOR_BLACK);
    Display_DrawString(76, 150, "VIEW SPECTRUM", COLOR_WHITE, COLOR_BLACK);
    Display_DrawString(10, 172, "INFO", COLOR_YELLOW, COLOR_BLACK);
    Display_DrawString(76, 172, "NEXT MILESTONE", COLOR_GRAY, COLOR_BLACK);
    Display_DrawString(10, 194, "DELETE", COLOR_RED, COLOR_BLACK);
    Display_DrawString(76, 194, "DISABLED", COLOR_GRAY, COLOR_BLACK);
}

'''
ui_c = once(ui_c, 'static void ui_draw_file_browser_body(void)\n',
            action_body + 'static void ui_draw_file_browser_body(void)\n',
            'browser body definition')

start, end = function_range(ui_c, 'static void ui_draw_status(void)\n')
fn = ui_c[start:end]
fn = once(fn, '    if (ui.mode == UI_MODE_FILE_BROWSER)\n',
'''    if (ui.mode == UI_MODE_FILE_ACTIONS)
    {
        Display_FillRect(0, UI_STATUS_TOP, DISPLAY_WIDTH,
                         UI_STATUS_HEIGHT, COLOR_BLACK);
        Display_DrawRect(0, UI_STATUS_TOP, DISPLAY_WIDTH,
                         UI_STATUS_HEIGHT, COLOR_GRAY);
        Display_DrawString(4, UI_STATUS_TOP + 8,
                           "SELECTED", COLOR_WHITE, COLOR_BLACK);
        Display_DrawString(70, UI_STATUS_TOP + 8,
                           RecordingCatalog_GetSelected(),
                           COLOR_GREEN, COLOR_BLACK);
        return;
    }

    if (ui.mode == UI_MODE_FILE_BROWSER)
''', 'browser status branch')
ui_c = ui_c[:start] + fn + ui_c[end:]

start, end = function_range(ui_c,
    'static UI_Button ui_hit_test_button(uint16_t x, uint16_t y)\n')
fn = ui_c[start:end]
fn = once(fn, '    if (ui.mode == UI_MODE_FILE_BROWSER)\n',
'''    if (ui.mode == UI_MODE_FILE_ACTIONS)
    {
        if (x < 64U) return UI_BUTTON_FILE_ACTIONS_BACK;
        if (x < 128U) return UI_BUTTON_FILE_ACTIONS_WAVE;
        if (x < 192U) return UI_BUTTON_FILE_ACTIONS_FFT;
        if (x < 256U) return UI_BUTTON_FILE_ACTIONS_INFO;
        if (x < DISPLAY_WIDTH) return UI_BUTTON_FILE_ACTIONS_DELETE;
        return UI_BUTTON_NONE;
    }

    if (ui.mode == UI_MODE_FILE_BROWSER)
''', 'browser hit-test branch')
ui_c = ui_c[:start] + fn + ui_c[end:]

ui_c = once(ui_c,
'''        case UI_BUTTON_BROWSER_SELECT:
            ui.browser_pending_row = ui.browser_row;
            ui.pending_action = UI_ACTION_BROWSER_SELECT_ROW;
            UART_SendString("UI FILE SELECT REQUEST\\r\\n");
            break;
''',
'''        case UI_BUTTON_BROWSER_SELECT:
            ui.pending_action = UI_ACTION_FILE_ACTIONS_OPEN;
            UART_SendString("UI FILE ACTIONS REQUEST\\r\\n");
            break;
''', 'browser SELECT action')

button_cases = '''        case UI_BUTTON_FILE_ACTIONS_BACK:
            ui.pending_action = UI_ACTION_FILE_ACTIONS_BACK;
            UART_SendString("UI FILE ACTIONS BACK REQUEST\\r\\n");
            break;

        case UI_BUTTON_FILE_ACTIONS_WAVE:
            ui.pending_action = UI_ACTION_VIEW_WAVEFORM;
            UART_SendString("UI FILE ACTIONS WAVE REQUEST\\r\\n");
            break;

        case UI_BUTTON_FILE_ACTIONS_FFT:
            ui.pending_action = UI_ACTION_VIEW_SPECTRUM;
            UART_SendString("UI FILE ACTIONS FFT REQUEST\\r\\n");
            break;

        case UI_BUTTON_FILE_ACTIONS_INFO:
            ui.pending_action = UI_ACTION_FILE_INFO;
            UART_SendString("UI FILE INFO REQUEST\\r\\n");
            break;

        case UI_BUTTON_FILE_ACTIONS_DELETE:
            ui.pending_action = UI_ACTION_FILE_DELETE;
            UART_SendString("UI FILE DELETE REQUEST\\r\\n");
            break;

        case UI_BUTTON_FFT_BACK:
'''
ui_c = once(ui_c, '        case UI_BUTTON_FFT_BACK:\n', button_cases,
            'FFT back action anchor')

main_c = once(main_c, '/* UI_WAV_BROWSER_V2_PATCH */\n',
'''/* UI_WAV_BROWSER_V2_PATCH */
/* UI_FILE_ACTION_SCREEN_PATCH */
''', 'browser marker')

main_anchor = '''        case UI_ACTION_BROWSER_SELECT_ROW:
            if (RecordingCatalog_SelectPageEntry(
                    UI_GetBrowserPendingRow()))
            {
                UART_SendString("UI selected WAV: ");
                UART_SendString(RecordingCatalog_GetSelected());
                UART_SendString("\\r\\n");
                UI_RefreshFileBrowser();
            }
            else
            {
                UART_SendString("UI file selection failed\\r\\n");
            }
            break;

        case UI_ACTION_NONE:
'''
main_replacement = '''        case UI_ACTION_BROWSER_SELECT_ROW:
            if (RecordingCatalog_SelectPageEntry(
                    UI_GetBrowserPendingRow()))
            {
                UART_SendString("UI selected WAV: ");
                UART_SendString(RecordingCatalog_GetSelected());
                UART_SendString("\\r\\n");
                UI_RefreshFileBrowser();
            }
            else
            {
                UART_SendString("UI file selection failed\\r\\n");
            }
            break;

        case UI_ACTION_FILE_ACTIONS_OPEN:
            if (Capture_IsRecorderActive())
            {
                UART_SendString("Stop recording before file actions\\r\\n");
                break;
            }
            Recorder_GetStatus(&status);
            if (status.state != RECORDER_STATE_IDLE)
            {
                UART_SendString("Wait for recorder IDLE before file actions\\r\\n");
                break;
            }
            UI_ShowFileActions();
            UART_SendString("UI file actions loaded\\r\\n");
            break;

        case UI_ACTION_FILE_ACTIONS_BACK:
            UI_ReturnToFileBrowser();
            UART_SendString("UI file browser restored\\r\\n");
            break;

        case UI_ACTION_FILE_INFO:
            UART_SendString("UI file INFO is the next milestone\\r\\n");
            break;

        case UI_ACTION_FILE_DELETE:
            UART_SendString("UI file DELETE is disabled\\r\\n");
            break;

        case UI_ACTION_NONE:
'''
main_c = once(main_c, main_anchor, main_replacement,
              'browser selection action tail')

outputs = []
for path, text in ((UI_H, ui_h), (UI_C, ui_c), (MAIN_C, main_c)):
    text = '\n'.join(line.rstrip() for line in text.splitlines()) + '\n'
    if text.count('{') != text.count('}'):
        fail('Brace balance failed in ' + str(path))
    if text.count('/*') != text.count('*/'):
        fail('Comment balance failed in ' + str(path))
    if re.search(r'&(lt|gt|amp);|<br', text):
        fail('HTML artifact generated in ' + str(path))
    if bytes([0]) in text.encode('utf-8'):
        fail('NUL byte generated in ' + str(path))
    outputs.append((path, text))

combined = '\n'.join(text for _, text in outputs)
for symbol in (MARKER, 'UI_MODE_FILE_ACTIONS',
               'UI_ACTION_FILE_ACTIONS_OPEN', 'UI_ShowFileActions',
               'ui_draw_file_actions_body'):
    if symbol not in combined:
        fail('Missing generated symbol: ' + symbol)

for path, text in outputs:
    path.write_text(text, encoding='utf-8')

print('Selected-file action screen patch applied successfully.')
print('Modified: Core/Inc/ui.h, Core/Src/ui.c, Core/Src/main.c')
print('Browser SELECT is now OPEN.')
print('WAVE and FFT are functional from the action screen.')
print('INFO and DELETE remain safe placeholders.')
print('Next: make clean && make -j4')
