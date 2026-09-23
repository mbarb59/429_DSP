#!/usr/bin/env python3
"""Add selected-file actions and a stylus-friendly FAT 8.3 rename keyboard."""
from pathlib import Path
import re, shutil

CH=Path('Core/Inc/recording_catalog.h'); CC=Path('Core/Src/recording_catalog.c')
UH=Path('Core/Inc/ui.h'); UC=Path('Core/Src/ui.c'); MC=Path('Core/Src/main.c')
MARK='UI_RENAME_KEYBOARD_PATCH'

def fail(m): raise SystemExit(m)
def once(s,a,b,n):
    c=s.count(a)
    if c!=1: fail(f'Expected exactly one {n}, found {c}')
    return s.replace(a,b,1)
def frange(s,sig):
    p=s.find(sig)
    if p<0: fail('Missing function: '+sig.strip())
    q=s.find('{',p); d=0
    for i in range(q,len(s)):
        d += (s[i]=='{')-(s[i]=='}')
        if d==0:
            e=i+1
            while e<len(s) and s[e] in ' \t\r\n': e+=1
            return p,e
    fail('Unbalanced function: '+sig.strip())
def backup(p):
    b=p.with_name(p.name+'.before_rename_keyboard')
    if not b.exists(): shutil.copy2(p,b)

for p in (CH,CC,UH,UC,MC):
    if not p.exists(): fail('Missing '+str(p))
    if b'\x00' in p.read_bytes(): fail('NUL byte in '+str(p))

ch=CH.read_text(); cc=CC.read_text(); uh=UH.read_text(); uc=UC.read_text(); mc=MC.read_text()
for p,s in ((CH,ch),(CC,cc),(UH,uh),(UC,uc),(MC,mc)):
    if MARK in s: fail('Patch already installed in '+str(p))
    if re.search(r'&(lt|gt|amp);|<br',s): fail('HTML artifact in '+str(p))
for p in (CH,CC,UH,UC,MC): backup(p)

# Catalog rename API.
ch=once(ch,'bool RecordingCatalog_SelectPageEntry(uint32_t index);\n',
'''bool RecordingCatalog_SelectPageEntry(uint32_t index);

/* UI_RENAME_KEYBOARD_PATCH */
FRESULT RecordingCatalog_RenameSelected(const char *base_name);
''','catalog page selection API')

rename_code=r'''static bool rename_character_valid(char character)
{
    return ((character >= 'A') && (character <= 'Z')) ||
           ((character >= '0') && (character <= '9')) ||
           (character == '_') || (character == '-');
}

FRESULT RecordingCatalog_RenameSelected(const char *base_name)
{
    FILINFO information;
    FRESULT result;
    char destination[RECORDING_FILENAME_CAPACITY];
    size_t length;

    if (base_name == NULL)
    {
        return FR_INVALID_NAME;
    }

    length = strlen(base_name);
    if ((length == 0U) || (length > 8U))
    {
        return FR_INVALID_NAME;
    }

    for (size_t index = 0U; index < length; index++)
    {
        if (!rename_character_valid(base_name[index]))
        {
            return FR_INVALID_NAME;
        }
        destination[index] = base_name[index];
    }

    destination[length++] = '.';
    destination[length++] = 'W';
    destination[length++] = 'A';
    destination[length++] = 'V';
    destination[length] = '\0';

    if (strcmp(destination, selected_filename) == 0)
    {
        return FR_OK;
    }

    result = f_stat(destination, &information);
    if (result == FR_OK)
    {
        return FR_EXIST;
    }
    if (result != FR_NO_FILE)
    {
        return result;
    }

    result = f_rename(selected_filename, destination);
    if (result == FR_OK)
    {
        strcpy(selected_filename, destination);
    }
    return result;
}

'''
cc=once(cc,'FRESULT RecordingCatalog_StartNext(void)\n',rename_code+'FRESULT RecordingCatalog_StartNext(void)\n','catalog start function')

# UI actions and public API.
uh=once(uh,'    UI_ACTION_BROWSER_SELECT\n} UI_Action;\n',
'''    UI_ACTION_BROWSER_SELECT,
    UI_ACTION_RENAME_SAVE
} UI_Action;
''','UI action tail')
uh=once(uh,'void UI_RefreshFileBrowser(void);\n',
'''void UI_RefreshFileBrowser(void);
const char *UI_GetRenameBase(void);
''','browser public API')

# Add modes/buttons/state.
uc=once(uc,'    UI_BUTTON_BROWSER_OPEN\n} UI_Button;\n',
'''    UI_BUTTON_BROWSER_OPEN,
    UI_BUTTON_ACTION_BACK,
    UI_BUTTON_ACTION_WAVE,
    UI_BUTTON_ACTION_FFT,
    UI_BUTTON_ACTION_RENAME,
    UI_BUTTON_ACTION_DELETE,
    UI_BUTTON_RENAME_BACK,
    UI_BUTTON_RENAME_ABC,
    UI_BUTTON_RENAME_123,
    UI_BUTTON_RENAME_LEFT,
    UI_BUTTON_RENAME_BKSP,
    UI_BUTTON_RENAME_CLEAR,
    UI_BUTTON_RENAME_CANCEL,
    UI_BUTTON_RENAME_SAVE
} UI_Button;
''','UI button tail')
uc=once(uc,'    UI_MODE_FILE_BROWSER\n} UI_Mode;\n',
'''    UI_MODE_FILE_BROWSER,
    UI_MODE_FILE_ACTIONS,
    UI_MODE_RENAME
} UI_Mode;
''','UI mode tail')
uc=once(uc,'    uint32_t browser_row;\n',
'''    uint32_t browser_row;
    char rename_base[9];
    uint32_t rename_cursor;
    bool rename_numeric;
''','browser state')
uc=once(uc,'static void ui_draw_file_browser_body(void);\n',
'''static void ui_draw_file_browser_body(void);
static void ui_draw_file_actions_body(void);
static void ui_draw_rename_body(void);
static void ui_begin_rename(void);
static void ui_rename_insert(char character);
''','browser body prototypes')
# Initialize rename state only inside UI_Init(). The browser row is also
# reset by UI_ShowFileBrowser() and UI_RefreshFileBrowser().
init_start,init_end=frange(uc,'void UI_Init(void)\n')
init_text=uc[init_start:init_end]
init_text=once(init_text,'    ui.browser_row = 0U;\n',
'''    ui.browser_row = 0U;
    ui.rename_base[0] = '\\0';
    ui.rename_cursor = 0U;
    ui.rename_numeric = false;
''','browser state initialization inside UI_Init')
uc=uc[:init_start]+init_text+uc[init_end:]

# Getter and helper functions before zoom APIs.
api_anchor='void UI_SetRunning(bool running)\n'
api_code=r'''const char *UI_GetRenameBase(void)
{
    return ui.rename_base;
}

static void ui_begin_rename(void)
{
    const char *selected = RecordingCatalog_GetSelected();
    uint32_t index = 0U;

    while ((selected[index] != '\0') &&
           (selected[index] != '.') &&
           (index < 8U))
    {
        ui.rename_base[index] = selected[index];
        index++;
    }
    ui.rename_base[index] = '\0';
    ui.rename_cursor = index;
    ui.rename_numeric = false;
    ui.mode = UI_MODE_RENAME;
    ui.graph_dirty = true;
    ui.controls_dirty = true;
    Display_Clear(COLOR_BLACK);
}

static void ui_rename_insert(char character)
{
    uint32_t length = 0U;

    while ((length < 8U) && (ui.rename_base[length] != '\0'))
    {
        length++;
    }
    if ((ui.rename_cursor >= 8U) || (length >= 8U))
    {
        return;
    }

    for (uint32_t index = length + 1U;
         index > ui.rename_cursor;
         index--)
    {
        ui.rename_base[index] = ui.rename_base[index - 1U];
    }
    ui.rename_base[ui.rename_cursor] = character;
    ui.rename_cursor++;
    ui.graph_dirty = true;
}

'''
uc=once(uc,api_anchor,api_code+api_anchor,'UI_SetRunning function')

# Draw modes in UI_DrawSpectrum.
s,e=frange(uc,'void UI_DrawSpectrum(void)\n'); f=uc[s:e]
f=once(f,'    if (ui.mode == UI_MODE_FILE_BROWSER)\n',
'''    if (ui.mode == UI_MODE_RENAME)
    {
        ui_draw_rename_body();
        ui.graph_dirty = false;
        return;
    }

    if (ui.mode == UI_MODE_FILE_ACTIONS)
    {
        ui_draw_file_actions_body();
        ui.graph_dirty = false;
        return;
    }

    if (ui.mode == UI_MODE_FILE_BROWSER)
''','browser draw branch')
uc=uc[:s]+f+uc[e:]

# Controls.
s,e=frange(uc,'static void ui_draw_controls(void)\n'); f=uc[s:e]
f=once(f,'    if (ui.mode == UI_MODE_FILE_BROWSER)\n',r'''    if (ui.mode == UI_MODE_RENAME)
    {
        Display_FillRect(0, UI_CONTROL_TOP, DISPLAY_WIDTH,
                         UI_CONTROL_HEIGHT, COLOR_BLACK);
        ui_draw_button(0, COLOR_BLUE, COLOR_WHITE);
        ui_draw_button(64, COLOR_CYAN, COLOR_WHITE);
        ui_draw_button(128, COLOR_GREEN, COLOR_WHITE);
        ui_draw_button(192, COLOR_YELLOW, COLOR_WHITE);
        ui_draw_button(256, COLOR_RED, COLOR_WHITE);
        Display_DrawString(22, 12, "BACK", COLOR_BLACK, COLOR_BLUE);
        Display_DrawString(84, 12, "ABC", COLOR_BLACK, COLOR_CYAN);
        Display_DrawString(148, 12, "123", COLOR_BLACK, COLOR_GREEN);
        Display_DrawString(216, 12, "LEFT", COLOR_BLACK, COLOR_YELLOW);
        Display_DrawString(274, 12, "BKSP", COLOR_BLACK, COLOR_RED);
        return;
    }

    if (ui.mode == UI_MODE_FILE_ACTIONS)
    {
        Display_FillRect(0, UI_CONTROL_TOP, DISPLAY_WIDTH,
                         UI_CONTROL_HEIGHT, COLOR_BLACK);
        ui_draw_button(0, COLOR_BLUE, COLOR_WHITE);
        ui_draw_button(64, COLOR_CYAN, COLOR_WHITE);
        ui_draw_button(128, COLOR_GREEN, COLOR_WHITE);
        ui_draw_button(192, COLOR_YELLOW, COLOR_WHITE);
        ui_draw_button(256, COLOR_RED, COLOR_WHITE);
        Display_DrawString(22, 12, "BACK", COLOR_BLACK, COLOR_BLUE);
        Display_DrawString(82, 12, "WAVE", COLOR_BLACK, COLOR_CYAN);
        Display_DrawString(150, 12, "FFT", COLOR_BLACK, COLOR_GREEN);
        Display_DrawString(196, 12, "RENAME", COLOR_BLACK, COLOR_YELLOW);
        Display_DrawString(270, 12, "DELETE", COLOR_BLACK, COLOR_RED);
        return;
    }

    if (ui.mode == UI_MODE_FILE_BROWSER)
''','browser control branch')
uc=uc[:s]+f+uc[e:]

# Bodies before browser body.
body_anchor='static void ui_draw_file_browser_body(void)\n'
body_code=r'''static void ui_draw_file_actions_body(void)
{
    Display_FillRect(UI_GRAPH_LEFT, UI_GRAPH_TOP,
                     UI_GRAPH_WIDTH, UI_GRAPH_HEIGHT, COLOR_BLACK);
    Display_DrawRect(UI_GRAPH_LEFT, UI_GRAPH_TOP,
                     UI_GRAPH_WIDTH, UI_GRAPH_HEIGHT, COLOR_GRAY);
    Display_DrawString(12, 54, "SELECTED FILE", COLOR_CYAN, COLOR_BLACK);
    Display_DrawString(12, 80, RecordingCatalog_GetSelected(),
                       COLOR_GREEN, COLOR_BLACK);
    Display_DrawString(12, 120, "WAVE FFT RENAME DELETE",
                       COLOR_WHITE, COLOR_BLACK);
    Display_DrawString(12, 148, "DELETE NOT ENABLED YET",
                       COLOR_GRAY, COLOR_BLACK);
}

static void ui_draw_rename_body(void)
{
    static const char alphabet[28] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ_-";
    static const char numeric[14] = "0123456789_-<>";

    Display_FillRect(UI_GRAPH_LEFT, UI_GRAPH_TOP,
                     UI_GRAPH_WIDTH, UI_GRAPH_HEIGHT, COLOR_BLACK);
    Display_DrawRect(UI_GRAPH_LEFT, UI_GRAPH_TOP,
                     UI_GRAPH_WIDTH, UI_GRAPH_HEIGHT, COLOR_GRAY);
    Display_DrawString(8, 40, "RENAME FILE", COLOR_CYAN, COLOR_BLACK);

    for (uint32_t slot = 0U; slot < 8U; slot++)
    {
        int32_t x = 8 + ((int32_t)slot * 32);
        uint16_t border = (slot == ui.rename_cursor) ? COLOR_YELLOW : COLOR_GRAY;
        char character[2] = {'_', '\0'};
        if (ui.rename_base[slot] != '\0') character[0] = ui.rename_base[slot];
        Display_DrawRect(x, 58, 28, 24, border);
        Display_DrawString(x + 10, 67, character, COLOR_WHITE, COLOR_BLACK);
    }
    Display_DrawString(270, 67, ".WAV", COLOR_GREEN, COLOR_BLACK);

    if (!ui.rename_numeric)
    {
        for (uint32_t key = 0U; key < 28U; key++)
        {
            int32_t x = 4 + ((int32_t)(key % 7U) * 45);
            int32_t y = 92 + ((int32_t)(key / 7U) * 28);
            char label[2] = {alphabet[key], '\0'};
            Display_DrawRect(x, y, 42, 25, COLOR_GRAY);
            Display_DrawString(x + 17, y + 9, label, COLOR_WHITE, COLOR_BLACK);
        }
    }
    else
    {
        for (uint32_t key = 0U; key < 14U; key++)
        {
            int32_t x = 4 + ((int32_t)(key % 7U) * 45);
            int32_t y = 106 + ((int32_t)(key / 7U) * 34);
            char label[2] = {numeric[key], '\0'};
            Display_DrawRect(x, y, 42, 31, COLOR_GRAY);
            Display_DrawString(x + 17, y + 11, label, COLOR_WHITE, COLOR_BLACK);
        }
    }

    Display_FillRect(4, 194, 100, 20, COLOR_YELLOW);
    Display_FillRect(110, 194, 100, 20, COLOR_RED);
    Display_FillRect(216, 194, 100, 20, COLOR_GREEN);
    Display_DrawString(34, 201, "CLEAR", COLOR_BLACK, COLOR_YELLOW);
    Display_DrawString(136, 201, "CANCEL", COLOR_BLACK, COLOR_RED);
    Display_DrawString(250, 201, "SAVE", COLOR_BLACK, COLOR_GREEN);
}

'''
uc=once(uc,body_anchor,body_code+body_anchor,'browser body definition')

# Hit-test top controls.
s,e=frange(uc,'static UI_Button ui_hit_test_button(uint16_t x, uint16_t y)\n'); f=uc[s:e]
f=once(f,'    if (ui.mode == UI_MODE_FILE_BROWSER)\n',r'''    if (ui.mode == UI_MODE_RENAME)
    {
        if (x < 64U) return UI_BUTTON_RENAME_BACK;
        if (x < 128U) return UI_BUTTON_RENAME_ABC;
        if (x < 192U) return UI_BUTTON_RENAME_123;
        if (x < 256U) return UI_BUTTON_RENAME_LEFT;
        if (x < DISPLAY_WIDTH) return UI_BUTTON_RENAME_BKSP;
        return UI_BUTTON_NONE;
    }

    if (ui.mode == UI_MODE_FILE_ACTIONS)
    {
        if (x < 64U) return UI_BUTTON_ACTION_BACK;
        if (x < 128U) return UI_BUTTON_ACTION_WAVE;
        if (x < 192U) return UI_BUTTON_ACTION_FFT;
        if (x < 256U) return UI_BUTTON_ACTION_RENAME;
        if (x < DISPLAY_WIDTH) return UI_BUTTON_ACTION_DELETE;
        return UI_BUTTON_NONE;
    }

    if (ui.mode == UI_MODE_FILE_BROWSER)
''','browser hit branch')
uc=uc[:s]+f+uc[e:]

# Execute top actions.
action_anchor='        case UI_BUTTON_BROWSER_BACK:\n'
action_code=r'''        case UI_BUTTON_ACTION_BACK:
            ui.mode = UI_MODE_FILE_BROWSER;
            ui.graph_dirty = true;
            ui.controls_dirty = true;
            Display_Clear(COLOR_BLACK);
            break;
        case UI_BUTTON_ACTION_WAVE:
            ui.pending_action = UI_ACTION_VIEW_WAVEFORM;
            break;
        case UI_BUTTON_ACTION_FFT:
            ui.pending_action = UI_ACTION_VIEW_SPECTRUM;
            break;
        case UI_BUTTON_ACTION_RENAME:
            ui_begin_rename();
            break;
        case UI_BUTTON_ACTION_DELETE:
            UART_SendString("UI DELETE NOT ENABLED YET\r\n");
            break;
        case UI_BUTTON_RENAME_BACK:
        case UI_BUTTON_RENAME_CANCEL:
            ui.mode = UI_MODE_FILE_ACTIONS;
            ui.graph_dirty = true;
            ui.controls_dirty = true;
            Display_Clear(COLOR_BLACK);
            break;
        case UI_BUTTON_RENAME_ABC:
            ui.rename_numeric = false; ui.graph_dirty = true; break;
        case UI_BUTTON_RENAME_123:
            ui.rename_numeric = true; ui.graph_dirty = true; break;
        case UI_BUTTON_RENAME_LEFT:
            if (ui.rename_cursor > 0U) ui.rename_cursor--;
            ui.graph_dirty = true; break;
        case UI_BUTTON_RENAME_BKSP:
            if (ui.rename_cursor > 0U)
            {
                uint32_t index = --ui.rename_cursor;
                while (index < 8U)
                {
                    ui.rename_base[index] = ui.rename_base[index + 1U];
                    index++;
                }
            }
            ui.graph_dirty = true; break;
        case UI_BUTTON_RENAME_CLEAR:
            ui.rename_base[0] = '\0'; ui.rename_cursor = 0U;
            ui.graph_dirty = true; break;
        case UI_BUTTON_RENAME_SAVE:
            ui.pending_action = UI_ACTION_RENAME_SAVE; break;

        case UI_BUTTON_BROWSER_BACK:
'''
uc=once(uc,action_anchor,action_code,'browser back action')

# OPEN now enters actions screen.
uc=once(uc,
'''        case UI_BUTTON_BROWSER_OPEN:
            ui.pending_action = UI_ACTION_BROWSER_OPEN;
            UART_SendString("UI FILE OPEN REQUEST\\r\\n");
            break;
''',
'''        case UI_BUTTON_BROWSER_OPEN:
            ui.mode = UI_MODE_FILE_ACTIONS;
            ui.graph_dirty = true;
            ui.controls_dirty = true;
            Display_Clear(COLOR_BLACK);
            UART_SendString("UI FILE ACTIONS SCREEN\\r\\n");
            break;
''','browser open action')

# Touch rename body keys and action buttons before analyzer cursor branch.
s,e=frange(uc,'static void ui_handle_touch_up(void)\n'); f=uc[s:e]
needle='''    else if (((ui.mode == UI_MODE_ANALYZER) ||
              (ui.mode == UI_MODE_RECORDED_FFT)) &&
'''
insert=r'''    else if ((ui.mode == UI_MODE_RENAME) &&
             (ui.touch_start_y >= 58U) &&
             (ui.touch_start_y < 82U) &&
             (ui.touch_start_x < 264U))
    {
        ui.rename_cursor = ui.touch_start_x / 32U;
        if (ui.rename_cursor > 8U) ui.rename_cursor = 8U;
        ui.graph_dirty = true;
    }
    else if ((ui.mode == UI_MODE_RENAME) &&
             (ui.touch_start_y >= 194U) &&
             (ui.touch_start_y < 216U))
    {
        if (ui.touch_start_x < 106U) ui_execute_button(UI_BUTTON_RENAME_CLEAR);
        else if (ui.touch_start_x < 212U) ui_execute_button(UI_BUTTON_RENAME_CANCEL);
        else ui_execute_button(UI_BUTTON_RENAME_SAVE);
    }
    else if ((ui.mode == UI_MODE_RENAME) && !ui.rename_numeric &&
             (ui.touch_start_y >= 92U) && (ui.touch_start_y < 204U))
    {
        static const char alphabet[28] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ_-";
        uint32_t column = ui.touch_start_x / 45U;
        uint32_t row = (ui.touch_start_y - 92U) / 28U;
        uint32_t key = (row * 7U) + column;
        if ((column < 7U) && (key < 28U)) ui_rename_insert(alphabet[key]);
    }
    else if ((ui.mode == UI_MODE_RENAME) && ui.rename_numeric &&
             (ui.touch_start_y >= 106U) && (ui.touch_start_y < 174U))
    {
        static const char numeric[14] = "0123456789_-<>";
        uint32_t column = ui.touch_start_x / 45U;
        uint32_t row = (ui.touch_start_y - 106U) / 34U;
        uint32_t key = (row * 7U) + column;
        if ((column < 7U) && (key < 12U)) ui_rename_insert(numeric[key]);
        else if (key == 12U && ui.rename_cursor > 0U) ui.rename_cursor--;
        else if (key == 13U && ui.rename_cursor < 8U) ui.rename_cursor++;
        ui.graph_dirty = true;
    }
    else if (((ui.mode == UI_MODE_ANALYZER) ||
              (ui.mode == UI_MODE_RECORDED_FFT)) &&
'''
f=once(f,needle,insert,'FFT cursor touch branch')
uc=uc[:s]+f+uc[e:]

# Status branches.
s,e=frange(uc,'static void ui_draw_status(void)\n'); f=uc[s:e]
f=once(f,'    if (ui.mode == UI_MODE_FILE_BROWSER)\n',r'''    if (ui.mode == UI_MODE_RENAME)
    {
        Display_FillRect(0, UI_STATUS_TOP, DISPLAY_WIDTH,
                         UI_STATUS_HEIGHT, COLOR_BLACK);
        Display_DrawRect(0, UI_STATUS_TOP, DISPLAY_WIDTH,
                         UI_STATUS_HEIGHT, COLOR_GRAY);
        Display_DrawString(4, UI_STATUS_TOP + 8, "OLD",
                           COLOR_WHITE, COLOR_BLACK);
        Display_DrawString(30, UI_STATUS_TOP + 8,
                           RecordingCatalog_GetSelected(),
                           COLOR_CYAN, COLOR_BLACK);
        Display_DrawString(190, UI_STATUS_TOP + 8,
                           "NAME READY", COLOR_GREEN, COLOR_BLACK);
        return;
    }

    if (ui.mode == UI_MODE_FILE_ACTIONS)
    {
        Display_FillRect(0, UI_STATUS_TOP, DISPLAY_WIDTH,
                         UI_STATUS_HEIGHT, COLOR_BLACK);
        Display_DrawRect(0, UI_STATUS_TOP, DISPLAY_WIDTH,
                         UI_STATUS_HEIGHT, COLOR_GRAY);
        Display_DrawString(4, UI_STATUS_TOP + 8, "SELECTED",
                           COLOR_WHITE, COLOR_BLACK);
        Display_DrawString(70, UI_STATUS_TOP + 8,
                           RecordingCatalog_GetSelected(),
                           COLOR_GREEN, COLOR_BLACK);
        return;
    }

    if (ui.mode == UI_MODE_FILE_BROWSER)
''','browser status branch')
uc=uc[:s]+f+uc[e:]

# Main rename action before NONE.
main_anchor='''        case UI_ACTION_NONE:
        default:
'''
main_code=r'''        case UI_ACTION_RENAME_SAVE:
            result = RecordingCatalog_RenameSelected(UI_GetRenameBase());
            if (result == FR_OK)
            {
                UART_SendString("UI rename OK: ");
                UART_SendString(RecordingCatalog_GetSelected());
                UART_SendString("\r\n");
                (void)RecordingCatalog_LoadPage(
                    RecordingCatalog_GetPageIndex());
            }
            else
            {
                UART_SendString("UI rename failed, FatFs = ");
                UART_SendDec((uint32_t)result);
                UART_SendString("\r\n");
            }
            break;

        case UI_ACTION_NONE:
        default:
'''
mc=once(mc,main_anchor,main_code,'main action default')
mc=once(mc,'/* UI_FILE_BROWSER_PATCH */\n','/* UI_FILE_BROWSER_PATCH */\n/* UI_RENAME_KEYBOARD_PATCH */\n','browser marker')

# Validate/write.
outs=[]
for p,s in ((CH,ch),(CC,cc),(UH,uh),(UC,uc),(MC,mc)):
    s='\n'.join(line.rstrip() for line in s.splitlines())+'\n'
    if s.count('{')!=s.count('}'): fail('Brace imbalance '+str(p))
    if s.count('/*')!=s.count('*/'): fail('Comment imbalance '+str(p))
    if re.search(r'&(lt|gt|amp);|<br',s): fail('HTML artifact '+str(p))
    if b'\x00' in s.encode(): fail('NUL byte '+str(p))
    outs.append((p,s))
combined='\n'.join(s for _,s in outs)
for sym in (MARK,'RecordingCatalog_RenameSelected','UI_MODE_RENAME','UI_ACTION_RENAME_SAVE','ui_draw_rename_body'):
    if sym not in combined: fail('Missing '+sym)
for p,s in outs: p.write_text(s)
print('Rename keyboard patch applied successfully.')
print('Added selected-file actions and FAT 8.3 rename screen.')
print('DELETE remains a safe placeholder.')
print('Next: make clean && make -j4')
