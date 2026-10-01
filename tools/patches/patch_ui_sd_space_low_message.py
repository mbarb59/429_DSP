#!/usr/bin/env python3
from pathlib import Path
import re, shutil

UH=Path('Core/Inc/ui.h'); UC=Path('Core/Src/ui.c'); MC=Path('Core/Src/main.c')
MARK='UI_SD_SPACE_LOW_MESSAGE_PATCH'

def die(s): raise SystemExit(s)
def one(t,a,b,n):
    c=t.count(a)
    if c!=1: die(f'Expected one {n}, found {c}')
    return t.replace(a,b,1)
def fr(t,s):
    a=t.find(s)
    if a<0: die('Missing function: '+s.strip())
    b=t.find('{',a); d=0
    for i in range(b,len(t)):
        if t[i]=='{': d+=1
        elif t[i]=='}':
            d-=1
            if d==0:
                e=i+1
                while e<len(t) and t[e] in ' \t\r\n': e+=1
                return a,e
    die('Unbalanced function: '+s.strip())

for p in (UH,UC,MC):
    if not p.exists(): die('Missing '+str(p))
    if b'\x00' in p.read_bytes(): die('NUL byte in '+str(p))
h=UH.read_text(); u=UC.read_text(); m=MC.read_text()
for p,t in ((UH,h),(UC,u),(MC,m)):
    if MARK in t: die('Patch already installed in '+str(p))
    if re.search(r'&(lt|gt|amp);|<br',t): die('HTML artifact in '+str(p))
for x in ('RECORDING_SPACE_CHECK_PATCH','UI_MODE_RECORD_DURATION','space_check.sufficient'):
    if x not in h+u+m: die('Missing prerequisite: '+x)
if 'UI_RECORDING_PROGRESS_PATCH' in u:
    die('Restore stable ui.c before applying this patch')
for p in (UH,UC,MC):
    q=p.with_name(p.name+'.before_sd_space_low_message')
    if not q.exists(): shutil.copy2(p,q)

h=one(h,'uint32_t UI_GetRequestedRecordingSeconds(void);\n',
'''uint32_t UI_GetRequestedRecordingSeconds(void);

/* UI_SD_SPACE_LOW_MESSAGE_PATCH */
void UI_ShowSdSpaceLow(
    uint32_t required_kib,
    uint32_t available_kib);
void UI_ClearRecorderMessage(void);
''','duration getter')

u=one(u,'    uint32_t requested_recording_seconds;\n',
'''    uint32_t requested_recording_seconds;
    bool recorder_space_low;
    uint32_t recorder_required_kib;
    uint32_t recorder_available_kib;
''','duration state')

a,b=fr(u,'void UI_Init(void)\n'); f=u[a:b]
f=one(f,'    ui.requested_recording_seconds = 5U;\n',
'''    ui.requested_recording_seconds = 5U;
    ui.recorder_space_low = false;
    ui.recorder_required_kib = 0U;
    ui.recorder_available_kib = 0U;
''','duration init')
u=u[:a]+f+u[b:]

api=r'''void UI_ShowSdSpaceLow(
    uint32_t required_kib,
    uint32_t available_kib)
{
    ui.recorder_space_low = true;
    ui.recorder_required_kib = required_kib;
    ui.recorder_available_kib = available_kib;
    ui.mode = UI_MODE_RECORDER;
    ui.graph_dirty = true;
    ui.controls_dirty = true;
}

void UI_ClearRecorderMessage(void)
{
    ui.recorder_space_low = false;
    ui.recorder_required_kib = 0U;
    ui.recorder_available_kib = 0U;
}

'''
u=one(u,'void UI_ShowRecordDurationSelector(void)\n',api+'void UI_ShowRecordDurationSelector(void)\n','selector API')

a,b=fr(u,'void UI_ShowRecordDurationSelector(void)\n'); f=u[a:b]
f=one(f,'{\n    ui.mode = UI_MODE_RECORD_DURATION;\n','{\n    UI_ClearRecorderMessage();\n    ui.mode = UI_MODE_RECORD_DURATION;\n','selector opening')
u=u[:a]+f+u[b:]

a,b=fr(u,'static void ui_draw_recorder_body(void)\n'); f=u[a:b]
anchor='''    Display_FillRect(UI_FILES_X, UI_FILES_Y,
                     UI_FILES_WIDTH, UI_FILES_HEIGHT, COLOR_BLUE);
'''
warn=r'''    if (ui.recorder_space_low &&
        !ui.recorder_status.capture_active)
    {
        Display_FillRect(176, 58, 136, 100, COLOR_BLACK);
        Display_DrawRect(176, 58, 136, 100, COLOR_RED);
        Display_DrawString(188, 68, "SD SPACE LOW", COLOR_RED, COLOR_BLACK);
        Display_DrawString(184, 94, "REQUIRED", COLOR_WHITE, COLOR_BLACK);
        ui_draw_uint(250, 94, ui.recorder_required_kib, COLOR_YELLOW);
        Display_DrawString(292, 94, "K", COLOR_GRAY, COLOR_BLACK);
        Display_DrawString(184, 118, "AVAILABLE", COLOR_WHITE, COLOR_BLACK);
        ui_draw_uint(250, 118, ui.recorder_available_kib, COLOR_YELLOW);
        Display_DrawString(292, 118, "K", COLOR_GRAY, COLOR_BLACK);
        Display_DrawString(188, 142, "DELETE FILES", COLOR_WHITE, COLOR_BLACK);
    }

'''
f=one(f,anchor,warn+anchor,'FILES drawing')
u=u[:a]+f+u[b:]
u=one(u,'/* UI_WAV_RENAME_PATCH */\n','/* UI_WAV_RENAME_PATCH */\n/* UI_SD_SPACE_LOW_MESSAGE_PATCH */\n','UI marker')

m=one(m,'/* RECORDING_SPACE_CHECK_PATCH */\n','/* RECORDING_SPACE_CHECK_PATCH */\n/* UI_SD_SPACE_LOW_MESSAGE_PATCH */\n','main marker')
old=r'''                if (!space_check.sufficient)
                {
                    UART_SendString(
                        "UI recording blocked: SD space low\r\n");
                    break;
                }
'''
new=r'''                if (!space_check.sufficient)
                {
                    UI_ShowSdSpaceLow(
                        space_check.required_kib,
                        space_check.available_kib);
                    UART_SendString(
                        "UI recording blocked: SD space low\r\n");
                    break;
                }
'''
s=m.find('        case UI_ACTION_RECORD_DURATION:\n'); e=m.find('        case UI_ACTION_RECORD_5:\n',s)
if s<0 or e<0: die('Cannot isolate duration action')
c=m[s:e]; c=one(c,old,new,'low-space branch'); m=m[:s]+c+m[e:]

outs=[]
for p,t in ((UH,h),(UC,u),(MC,m)):
    t='\n'.join(x.rstrip() for x in t.splitlines())+'\n'
    if t.count('{')!=t.count('}'): die('Brace mismatch in '+str(p))
    if t.count('/*')!=t.count('*/'): die('Comment mismatch in '+str(p))
    if b'\x00' in t.encode(): die('NUL generated in '+str(p))
    outs.append((p,t))
alltext='\n'.join(t for _,t in outs)
for x in (MARK,'UI_ShowSdSpaceLow','recorder_space_low','"SD SPACE LOW"','"DELETE FILES"'):
    if x not in alltext: die('Missing generated symbol: '+x)
if m.count('UI_ShowSdSpaceLow(')!=1: die('Low-space call missing or duplicated')
for p,t in outs: p.write_text(t)
print('Safe SD SPACE LOW message patch applied successfully.')
print('Modified: Core/Inc/ui.h, Core/Src/ui.c, Core/Src/main.c')
print('Warning appears only after a rejected request while recorder is idle.')
print('RECORD clears the warning; FILES remains available.')
print('DMA, queue, WAV writing, and active-recording redraws were unchanged.')
print('Next: make clean && make -j4')
