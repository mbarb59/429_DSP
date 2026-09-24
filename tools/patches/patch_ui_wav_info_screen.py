#!/usr/bin/env python3
'Add a read-only WAV INFO screen to the current selected-file action screen.'

from pathlib import Path
import re
import shutil

UI_H = Path('Core/Inc/ui.h')
UI_C = Path('Core/Src/ui.c')
MAIN_C = Path('Core/Src/main.c')
MARKER = 'UI_WAV_INFO_SCREEN_PATCH'


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
    if brace < 0:
        fail('Missing opening brace: ' + signature.strip())
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
    target = path.with_name(path.name + '.before_wav_info_screen')
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
        fail('WAV INFO screen already installed in ' + str(path))
    if re.search(r'&(lt|gt|amp);|<br', text):
        fail('HTML artifact found in ' + str(path))

for required in ('UI_FILE_ACTION_SCREEN_PATCH', 'UI_ACTION_FILE_INFO',
                 'UI_MODE_FILE_ACTIONS', 'UI_ShowFileActions'):
    if required not in (ui_h + ui_c + main_c):
        fail('Missing action-screen prerequisite: ' + required)

for path in (UI_H, UI_C, MAIN_C):
    backup(path)

ui_h = once(ui_h,
'''typedef struct
{
    uint32_t recorder_state;
''',
'''/* UI_WAV_INFO_SCREEN_PATCH */
typedef struct
{
    char filename[13];
    uint32_t file_size;
    uint32_t sample_rate;
    uint32_t data_bytes;
    uint32_t duration_ms;
    uint16_t channels;
    uint16_t bits_per_sample;
    bool valid;
} UI_WavInfo;

typedef struct
{
    uint32_t recorder_state;
''', 'recorder status structure')

ui_h = once(ui_h,
'''void UI_ShowFileActions(void);
void UI_ReturnToFileBrowser(void);
bool UI_IsFileActionsScreen(void);
''',
'''void UI_ShowFileActions(void);
void UI_ReturnToFileBrowser(void);
bool UI_IsFileActionsScreen(void);
void UI_ShowWavInfo(const UI_WavInfo *info);
bool UI_IsWavInfoScreen(void);
''', 'file-action public API')

ui_c = once(ui_c,
'''    UI_BUTTON_FILE_ACTIONS_INFO,
    UI_BUTTON_FILE_ACTIONS_DELETE
} UI_Button;
''',
'''    UI_BUTTON_FILE_ACTIONS_INFO,
    UI_BUTTON_FILE_ACTIONS_DELETE,
    UI_BUTTON_WAV_INFO_BACK
} UI_Button;
''', 'file-action button enum tail')

ui_c = once(ui_c,
'''    UI_MODE_FILE_BROWSER,
    UI_MODE_FILE_ACTIONS
} UI_Mode;
''',
'''    UI_MODE_FILE_BROWSER,
    UI_MODE_FILE_ACTIONS,
    UI_MODE_WAV_INFO
} UI_Mode;
''', 'file-action mode enum tail')

ui_c = once(ui_c,
'''    UI_Action pending_action;
    UI_RecorderStatus recorder_status;
''',
'''    UI_Action pending_action;
    UI_RecorderStatus recorder_status;
    UI_WavInfo wav_info;
''', 'UI state recorder fields')

ui_c = once(ui_c, 'static void ui_draw_file_actions_body(void);\n',
'''static void ui_draw_file_actions_body(void);
static void ui_draw_wav_info_body(void);
''', 'file-actions body prototype')

start, end = function_range(ui_c, 'void UI_Init(void)\n')
fn = ui_c[start:end]
fn = once(fn, '    ui.pending_action = UI_ACTION_NONE;\n',
'''    ui.pending_action = UI_ACTION_NONE;
    memset(&ui.wav_info, 0, sizeof(ui.wav_info));
''', 'pending action initialization')
ui_c = ui_c[:start] + fn + ui_c[end:]

anchor = '''bool UI_IsFileActionsScreen(void)
{
    return ui.mode == UI_MODE_FILE_ACTIONS;
}

'''
ui_c = once(ui_c, anchor, anchor + '''void UI_ShowWavInfo(const UI_WavInfo *info)
{
    if (info == NULL)
    {
        return;
    }

    ui.wav_info = *info;
    ui.mode = UI_MODE_WAV_INFO;
    ui.running = false;
    ui.graph_dirty = true;
    ui.controls_dirty = true;
    Display_Clear(COLOR_BLACK);
}

bool UI_IsWavInfoScreen(void)
{
    return ui.mode == UI_MODE_WAV_INFO;
}

''', 'file-actions screen query')

start, end = function_range(ui_c, 'void UI_DrawSpectrum(void)\n')
fn = ui_c[start:end]
fn = once(fn, '    if (ui.mode == UI_MODE_FILE_ACTIONS)\n',
'''    if (ui.mode == UI_MODE_WAV_INFO)
    {
        ui_draw_wav_info_body();
        ui.graph_dirty = false;
        return;
    }

    if (ui.mode == UI_MODE_FILE_ACTIONS)
''', 'file-actions draw branch')
ui_c = ui_c[:start] + fn + ui_c[end:]

start, end = function_range(ui_c, 'static void ui_draw_controls(void)\n')
fn = ui_c[start:end]
fn = once(fn, '    if (ui.mode == UI_MODE_FILE_ACTIONS)\n',
'''    if (ui.mode == UI_MODE_WAV_INFO)
    {
        Display_FillRect(0, UI_CONTROL_TOP, DISPLAY_WIDTH,
                         UI_CONTROL_HEIGHT, COLOR_BLACK);
        ui_draw_button(0, COLOR_BLUE, COLOR_WHITE);
        ui_draw_button(64, COLOR_DARK_GRAY, COLOR_GRAY);
        ui_draw_button(128, COLOR_DARK_GRAY, COLOR_GRAY);
        ui_draw_button(192, COLOR_DARK_GRAY, COLOR_GRAY);
        ui_draw_button(256, COLOR_DARK_GRAY, COLOR_GRAY);
        Display_DrawString(22, 12, "BACK", COLOR_BLACK, COLOR_BLUE);
        Display_DrawString(92, 12, "WAV FILE INFO", COLOR_WHITE, COLOR_BLACK);
        return;
    }

    if (ui.mode == UI_MODE_FILE_ACTIONS)
''', 'file-actions control branch')
ui_c = ui_c[:start] + fn + ui_c[end:]

info_body = '''static void ui_draw_wav_info_body(void)
{
    Display_FillRect(UI_GRAPH_LEFT, UI_GRAPH_TOP,
                     UI_GRAPH_WIDTH, UI_GRAPH_HEIGHT, COLOR_BLACK);
    Display_DrawRect(UI_GRAPH_LEFT, UI_GRAPH_TOP,
                     UI_GRAPH_WIDTH, UI_GRAPH_HEIGHT, COLOR_GRAY);

    Display_DrawString(8, 40, "FILE", COLOR_WHITE, COLOR_BLACK);
    Display_DrawString(86, 40, ui.wav_info.filename,
                       COLOR_GREEN, COLOR_BLACK);
    Display_DrawString(8, 62, "SIZE", COLOR_WHITE, COLOR_BLACK);
    ui_draw_uint(86, 62, ui.wav_info.file_size, COLOR_YELLOW);
    Display_DrawString(174, 62, "BYTES", COLOR_GRAY, COLOR_BLACK);
    Display_DrawString(8, 84, "RATE", COLOR_WHITE, COLOR_BLACK);
    ui_draw_uint(86, 84, ui.wav_info.sample_rate, COLOR_YELLOW);
    Display_DrawString(174, 84, "HZ", COLOR_GRAY, COLOR_BLACK);
    Display_DrawString(8, 106, "CHANNELS", COLOR_WHITE, COLOR_BLACK);
    ui_draw_uint(86, 106, ui.wav_info.channels, COLOR_YELLOW);
    Display_DrawString(8, 128, "BITS", COLOR_WHITE, COLOR_BLACK);
    ui_draw_uint(86, 128, ui.wav_info.bits_per_sample, COLOR_YELLOW);
    Display_DrawString(8, 150, "DATA", COLOR_WHITE, COLOR_BLACK);
    ui_draw_uint(86, 150, ui.wav_info.data_bytes, COLOR_YELLOW);
    Display_DrawString(174, 150, "BYTES", COLOR_GRAY, COLOR_BLACK);
    Display_DrawString(8, 172, "DURATION", COLOR_WHITE, COLOR_BLACK);
    ui_draw_uint(86, 172, ui.wav_info.duration_ms, COLOR_YELLOW);
    Display_DrawString(174, 172, "MS", COLOR_GRAY, COLOR_BLACK);
    Display_DrawString(8, 194, "FORMAT", COLOR_WHITE, COLOR_BLACK);
    Display_DrawString(86, 194,
                       ui.wav_info.valid ? "VALID PCM WAV" : "INVALID WAV",
                       ui.wav_info.valid ? COLOR_GREEN : COLOR_RED,
                       COLOR_BLACK);
}

'''
ui_c = once(ui_c, 'static void ui_draw_file_actions_body(void)\n',
            info_body + 'static void ui_draw_file_actions_body(void)\n',
            'file-actions body definition')

start, end = function_range(ui_c, 'static void ui_draw_status(void)\n')
fn = ui_c[start:end]
fn = once(fn, '    if (ui.mode == UI_MODE_FILE_ACTIONS)\n',
'''    if (ui.mode == UI_MODE_WAV_INFO)
    {
        Display_FillRect(0, UI_STATUS_TOP, DISPLAY_WIDTH,
                         UI_STATUS_HEIGHT, COLOR_BLACK);
        Display_DrawRect(0, UI_STATUS_TOP, DISPLAY_WIDTH,
                         UI_STATUS_HEIGHT, COLOR_GRAY);
        Display_DrawString(4, UI_STATUS_TOP + 8,
                           "WAV INFO", COLOR_WHITE, COLOR_BLACK);
        Display_DrawString(88, UI_STATUS_TOP + 8,
                           ui.wav_info.valid ? "VALID" : "INVALID",
                           ui.wav_info.valid ? COLOR_GREEN : COLOR_RED,
                           COLOR_BLACK);
        return;
    }

    if (ui.mode == UI_MODE_FILE_ACTIONS)
''', 'file-actions status branch')
ui_c = ui_c[:start] + fn + ui_c[end:]

start, end = function_range(ui_c,
    'static UI_Button ui_hit_test_button(uint16_t x, uint16_t y)\n')
fn = ui_c[start:end]
fn = once(fn, '    if (ui.mode == UI_MODE_FILE_ACTIONS)\n',
'''    if (ui.mode == UI_MODE_WAV_INFO)
    {
        if (x < 64U)
        {
            return UI_BUTTON_WAV_INFO_BACK;
        }
        return UI_BUTTON_NONE;
    }

    if (ui.mode == UI_MODE_FILE_ACTIONS)
''', 'file-actions hit-test branch')
ui_c = ui_c[:start] + fn + ui_c[end:]

ui_c = once(ui_c, '        case UI_BUTTON_FILE_ACTIONS_BACK:\n',
'''        case UI_BUTTON_WAV_INFO_BACK:
            UI_ShowFileActions();
            UART_SendString("UI WAV INFO BACK\\r\\n");
            break;

        case UI_BUTTON_FILE_ACTIONS_BACK:
''', 'file-actions BACK button case')

main_c = once(main_c, 'static void UI_ProcessRecorderActions(void);\n',
'''static void UI_ProcessRecorderActions(void);
static FRESULT UI_LoadSelectedWavInfo(UI_WavInfo *info);
''', 'recorder action prototype')

main_c = once(main_c, '/* UI_DEFAULT_WAV_SELECTION_PATCH */\n',
'''/* UI_DEFAULT_WAV_SELECTION_PATCH */
/* UI_WAV_INFO_SCREEN_PATCH */
''', 'default-selection marker')

helper = '''static FRESULT UI_LoadSelectedWavInfo(UI_WavInfo *info)
{
    FIL file;
    UINT bytes_read = 0U;
    FRESULT result;
    uint8_t header[44];
    uint32_t riff_size;
    uint32_t format_chunk_size;
    uint32_t byte_rate;
    uint32_t expected_file_size;
    uint16_t audio_format;
    uint16_t block_alignment;
    const char *filename = RecordingCatalog_GetSelected();

    if ((info == NULL) || (filename == NULL) || (filename[0] == '\\0'))
    {
        return FR_INVALID_NAME;
    }

    memset(info, 0, sizeof(*info));
    strncpy(info->filename, filename, sizeof(info->filename) - 1U);
    info->filename[sizeof(info->filename) - 1U] = '\\0';

    result = FileSystem_Mount();
    if (result != FR_OK)
    {
        return result;
    }

    result = f_open(&file, filename, FA_READ);
    if (result != FR_OK)
    {
        return result;
    }

    info->file_size = (uint32_t)f_size(&file);
    result = f_read(&file, header, sizeof(header), &bytes_read);
    (void)f_close(&file);

    if (result != FR_OK)
    {
        return result;
    }
    if (bytes_read != sizeof(header))
    {
        return FR_INVALID_OBJECT;
    }

    riff_size = Console_ReadLe32(&header[4]);
    format_chunk_size = Console_ReadLe32(&header[16]);
    audio_format = Console_ReadLe16(&header[20]);
    info->channels = Console_ReadLe16(&header[22]);
    info->sample_rate = Console_ReadLe32(&header[24]);
    byte_rate = Console_ReadLe32(&header[28]);
    block_alignment = Console_ReadLe16(&header[32]);
    info->bits_per_sample = Console_ReadLe16(&header[34]);
    info->data_bytes = Console_ReadLe32(&header[40]);
    expected_file_size = info->data_bytes + 44U;

    if (byte_rate != 0U)
    {
        info->duration_ms =
            (uint32_t)(((uint64_t)info->data_bytes * 1000U) / byte_rate);
    }

    info->valid =
        (memcmp(&header[0], "RIFF", 4U) == 0) &&
        (memcmp(&header[8], "WAVE", 4U) == 0) &&
        (memcmp(&header[12], "fmt ", 4U) == 0) &&
        (memcmp(&header[36], "data", 4U) == 0) &&
        (riff_size == (info->file_size - 8U)) &&
        (format_chunk_size == 16U) &&
        (audio_format == 1U) &&
        (info->channels == 1U) &&
        (info->bits_per_sample == 16U) &&
        (block_alignment == 2U) &&
        (byte_rate == (info->sample_rate * (uint32_t)block_alignment)) &&
        (info->file_size == expected_file_size);

    return FR_OK;
}

'''
main_c = once(main_c, 'static void UI_ProcessRecorderActions(void)\n',
              helper + 'static void UI_ProcessRecorderActions(void)\n',
              'recorder action definition')

main_c = once(main_c,
'''        case UI_ACTION_FILE_INFO:
            UART_SendString("UI file INFO is the next milestone\\r\\n");
            break;
''',
'''        case UI_ACTION_FILE_INFO:
        {
            UI_WavInfo info;

            result = UI_LoadSelectedWavInfo(&info);
            if (result == FR_OK)
            {
                UI_ShowWavInfo(&info);
                UART_SendString("UI WAV info loaded: ");
                UART_SendString(info.filename);
                UART_SendString("\\r\\n");
            }
            else
            {
                UART_SendString("UI WAV info load failed, FatFs = ");
                UART_SendDec((uint32_t)result);
                UART_SendString("\\r\\n");
            }
            break;
        }
''', 'INFO placeholder action')

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
for symbol in (MARKER, 'UI_MODE_WAV_INFO', 'UI_ShowWavInfo',
               'UI_LoadSelectedWavInfo', 'VALID PCM WAV'):
    if symbol not in combined:
        fail('Missing generated symbol: ' + symbol)

for path, text in outputs:
    path.write_text(text, encoding='utf-8')

print('Read-only WAV INFO screen patch applied successfully.')
print('Modified: Core/Inc/ui.h, Core/Src/ui.c, Core/Src/main.c')
print('INFO now shows WAV metadata and validity.')
print('DELETE remains disabled.')
print('Next: make clean && make -j4')
