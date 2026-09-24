#!/usr/bin/env python3
'''Fix the browser default WAV and make OPEN commit the highlighted row.'''
from pathlib import Path
import re
import shutil

CAT_H = Path('Core/Inc/recording_catalog.h')
CAT_C = Path('Core/Src/recording_catalog.c')
MAIN_C = Path('Core/Src/main.c')
MARKER = 'UI_DEFAULT_WAV_SELECTION_PATCH'

def fail(message):
    raise SystemExit(message)

def once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        fail(f'Expected exactly one {label}, found {count}')
    return text.replace(old, new, 1)

def backup(path):
    target = path.with_name(path.name + '.before_default_wav_selection')
    if not target.exists():
        shutil.copy2(path, target)

for path in (CAT_H, CAT_C, MAIN_C):
    if not path.exists():
        fail('Missing required file: ' + str(path))
    if bytes([0]) in path.read_bytes():
        fail('NUL byte found in ' + str(path))

cat_h = CAT_H.read_text(encoding='utf-8')
cat_c = CAT_C.read_text(encoding='utf-8')
main_c = MAIN_C.read_text(encoding='utf-8')

for path, text in ((CAT_H, cat_h), (CAT_C, cat_c), (MAIN_C, main_c)):
    if MARKER in text:
        fail('Default WAV selection patch already installed in ' + str(path))
    if re.search(r'&(lt|gt|amp);|<br', text):
        fail('HTML artifact found in ' + str(path))

for required in ('UI_WAV_BROWSER_V2_PATCH', 'RecordingCatalog_LoadPage',
                 'RecordingCatalog_SelectPageEntry'):
    if required not in (cat_h + cat_c + main_c):
        fail('Missing browser prerequisite: ' + required)
if 'UI_FILE_ACTION_SCREEN_PATCH' not in main_c:
    fail('Missing selected-file action-screen prerequisite')

for path in (CAT_H, CAT_C, MAIN_C):
    backup(path)

cat_h = once(cat_h,
    'bool RecordingCatalog_SelectPageEntry(uint32_t index);\n',
    '''bool RecordingCatalog_SelectPageEntry(uint32_t index);

/* UI_DEFAULT_WAV_SELECTION_PATCH */
bool RecordingCatalog_EnsureSelectedValid(void);
''', 'catalog page-selection API')

anchor = '''bool RecordingCatalog_SelectPageEntry(uint32_t index)
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
implementation = anchor + '''bool RecordingCatalog_EnsureSelectedValid(void)
{
    FILINFO information;
    FRESULT result;

    if (selected_filename[0] != '\\0')
    {
        result = f_stat(selected_filename, &information);

        if ((result == FR_OK) &&
            ((information.fattrib & AM_DIR) == 0U) &&
            filename_is_wav(selected_filename))
        {
            return true;
        }
    }

    if (browser_entry_count != 0U)
    {
        return RecordingCatalog_SelectPageEntry(0U);
    }

    selected_filename[0] = '\\0';
    return false;
}

'''
cat_c = once(cat_c, anchor, implementation,
             'RecordingCatalog_SelectPageEntry function')

old_open = '''            result = RecordingCatalog_LoadPage(0U);
            if (result == FR_OK)
            {
                UI_ShowFileBrowser();
                UART_SendString("UI file browser loaded\\r\\n");
            }
            else
'''
new_open = '''            result = RecordingCatalog_LoadPage(0U);
            if (result == FR_OK)
            {
                /* UI_DEFAULT_WAV_SELECTION_PATCH */
                if (RecordingCatalog_EnsureSelectedValid())
                {
                    UART_SendString("UI selected WAV: ");
                    UART_SendString(RecordingCatalog_GetSelected());
                    UART_SendString("\\r\\n");
                }
                else
                {
                    UART_SendString("UI file browser has no WAV files\\r\\n");
                }

                UI_ShowFileBrowser();
                UART_SendString("UI file browser loaded\\r\\n");
            }
            else
'''
main_c = once(main_c, old_open, new_open,
              'browser-open foreground action')

old_actions = '''        case UI_ACTION_FILE_ACTIONS_OPEN:
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
'''
new_actions = '''        case UI_ACTION_FILE_ACTIONS_OPEN:
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

            if (!RecordingCatalog_SelectPageEntry(
                    UI_GetBrowserPendingRow()))
            {
                UART_SendString("Select a valid WAV before OPEN\\r\\n");
                break;
            }

            UART_SendString("UI selected WAV: ");
            UART_SendString(RecordingCatalog_GetSelected());
            UART_SendString("\\r\\n");
            UI_ShowFileActions();
            UART_SendString("UI file actions loaded\\r\\n");
            break;
'''
main_c = once(main_c, old_actions, new_actions,
              'file-actions OPEN foreground action')

main_c = once(main_c, '/* UI_FILE_ACTION_SCREEN_PATCH */\n',
'''/* UI_FILE_ACTION_SCREEN_PATCH */
/* UI_DEFAULT_WAV_SELECTION_PATCH */
''', 'file-action marker')

outputs = []
for path, text in ((CAT_H, cat_h), (CAT_C, cat_c), (MAIN_C, main_c)):
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
for symbol in (MARKER, 'RecordingCatalog_EnsureSelectedValid',
               'Select a valid WAV before OPEN'):
    if symbol not in combined:
        fail('Missing generated symbol: ' + symbol)

for path, text in outputs:
    path.write_text(text, encoding='utf-8')

print('Default WAV selection patch applied successfully.')
print('Modified: recording_catalog.h/.c and main.c')
print('FILES auto-selects the first WAV if the old selection is missing.')
print('OPEN commits the highlighted row before loading file actions.')
print('Next: make clean && make -j4')
