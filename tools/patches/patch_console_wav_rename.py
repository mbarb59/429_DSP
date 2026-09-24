#!/usr/bin/env python3
"""Add a safe console WAV rename backend."""
from pathlib import Path
import re
import shutil

CAT_H=Path('Core/Inc/recording_catalog.h')
CAT_C=Path('Core/Src/recording_catalog.c')
MAIN=Path('Core/Src/main.c')
MARK='CONSOLE_WAV_RENAME_PATCH'

def fail(m): raise SystemExit(m)
def once(t,o,n,label):
    c=t.count(o)
    if c!=1: fail(f'Expected exactly one {label}, found {c}')
    return t.replace(o,n,1)
def backup(p):
    q=p.with_name(p.name+'.before_console_wav_rename')
    if not q.exists(): shutil.copy2(p,q)

for p in (CAT_H,CAT_C,MAIN):
    if not p.exists(): fail('Missing '+str(p))
    if b'\x00' in p.read_bytes(): fail('NUL byte in '+str(p))

h=CAT_H.read_text(); c=CAT_C.read_text(); m=MAIN.read_text()
for p,t in ((CAT_H,h),(CAT_C,c),(MAIN,m)):
    if MARK in t: fail('Rename patch already installed in '+str(p))
    if re.search(r'&(lt|gt|amp);|<br',t): fail('HTML artifact in '+str(p))
for req in ('UI_DELETE_CONFIRMATION_PATCH','RecordingCatalog_DeleteFile'):
    if req not in h+c+m: fail('Missing prerequisite: '+req)
for p in (CAT_H,CAT_C,MAIN): backup(p)

h=once(h,
'FRESULT RecordingCatalog_DeleteFile(const char *filename);\n',
'''FRESULT RecordingCatalog_DeleteFile(const char *filename);

/* CONSOLE_WAV_RENAME_PATCH */
bool RecordingCatalog_NormalizeWavFilename(
    const char *input,
    char *output);
FRESULT RecordingCatalog_RenameFile(
    const char *source,
    const char *target);
''','delete API')

code=r'''static bool filename_character_is_valid(char character)
{
    return ((character >= 'A') && (character <= 'Z')) ||
           ((character >= '0') && (character <= '9')) ||
           (character == '_');
}

bool RecordingCatalog_NormalizeWavFilename(
    const char *input,
    char *output)
{
    size_t length;
    size_t base_length;

    if ((input == NULL) || (output == NULL))
    {
        return false;
    }

    length = strlen(input);
    if ((length < 5U) || (length >= RECORDING_FILENAME_CAPACITY))
    {
        return false;
    }

    base_length = length - 4U;
    if ((base_length == 0U) || (base_length > 8U) ||
        (input[base_length] != '.'))
    {
        return false;
    }

    for (size_t index = 0U; index < base_length; index++)
    {
        char character = input[index];
        if ((character >= 'a') && (character <= 'z'))
        {
            character = (char)(character - 'a' + 'A');
        }
        if (!filename_character_is_valid(character))
        {
            return false;
        }
        output[index] = character;
    }

    if (((input[base_length + 1U] != 'W') &&
         (input[base_length + 1U] != 'w')) ||
        ((input[base_length + 2U] != 'A') &&
         (input[base_length + 2U] != 'a')) ||
        ((input[base_length + 3U] != 'V') &&
         (input[base_length + 3U] != 'v')))
    {
        return false;
    }

    output[base_length] = '.';
    output[base_length + 1U] = 'W';
    output[base_length + 2U] = 'A';
    output[base_length + 3U] = 'V';
    output[base_length + 4U] = '\0';
    return true;
}

FRESULT RecordingCatalog_RenameFile(
    const char *source,
    const char *target)
{
    FILINFO information;
    FRESULT result;
    char normalized_source[RECORDING_FILENAME_CAPACITY];
    char normalized_target[RECORDING_FILENAME_CAPACITY];

    if (!RecordingCatalog_NormalizeWavFilename(
            source, normalized_source) ||
        !RecordingCatalog_NormalizeWavFilename(
            target, normalized_target))
    {
        return FR_INVALID_NAME;
    }

    if (strcmp(normalized_source, normalized_target) == 0)
    {
        return FR_INVALID_NAME;
    }

    result = FileSystem_Mount();
    if (result != FR_OK) return result;

    result = f_stat(normalized_source, &information);
    if (result != FR_OK) return result;
    if ((information.fattrib & AM_DIR) != 0U) return FR_DENIED;

    result = f_stat(normalized_target, &information);
    if (result == FR_OK) return FR_EXIST;
    if (result != FR_NO_FILE) return result;

    result = f_rename(normalized_source, normalized_target);
    if ((result == FR_OK) &&
        (strcmp(selected_filename, normalized_source) == 0))
    {
        strcpy(selected_filename, normalized_target);
    }
    return result;
}

'''
c=once(c,'FRESULT RecordingCatalog_DeleteFile(const char *filename)\n',code+'FRESULT RecordingCatalog_DeleteFile(const char *filename)\n','delete definition')

m=once(m,
'''static void Console_RunSdDelete(
    const char *filename);
''',
'''static void Console_RunSdDelete(
    const char *filename);
static void Console_RunWavRename(const char *arguments);
''','delete prototype')

m=once(m,'/* UI_DELETE_CONFIRMATION_PATCH */\n',
'''/* UI_DELETE_CONFIRMATION_PATCH */
/* CONSOLE_WAV_RENAME_PATCH */
''','feature marker')

# Parser insertion directly after existing sddel branch.
anchor='''    }
    else if (strcmp(command, "sdbench") == 0)
'''
insert='''    }
    else if ((strncmp(command, "wavrename", 9U) == 0) &&
             ((command[9] == '\0') ||
              (command[9] == ' ') ||
              (command[9] == '\t')))
    {
        Console_RunWavRename(&command[9]);
    }
    else if (strcmp(command, "sdbench") == 0)
'''
m=once(m,anchor,insert,'sdbench parser anchor')

# Help line after sddel.
m=once(m,
'\t"sddel       - Delete a specified file: sddel <filename>\\r\\n"\n',
'''\t"sddel       - Delete a specified file: sddel <filename>\\r\\n"
        "wavrename   - Rename WAV: wavrename <old> <new>\\r\\n"
''','sddel help line')

func=r'''static void Console_RunWavRename(const char *arguments)
{
    RecorderStatus status;
    FRESULT result;
    char source[RECORDING_FILENAME_CAPACITY];
    char target[RECORDING_FILENAME_CAPACITY];
    char normalized_source[RECORDING_FILENAME_CAPACITY];
    char normalized_target[RECORDING_FILENAME_CAPACITY];
    size_t source_length = 0U;
    size_t target_length = 0U;

    UART_SendString("WAV Rename\r\n");
    UART_SendString("----------\r\n");

    while ((*arguments == ' ') || (*arguments == '\t')) arguments++;
    while ((*arguments != '\0') && (*arguments != ' ') &&
           (*arguments != '\t') &&
           (source_length < sizeof(source) - 1U))
    {
        source[source_length++] = *arguments++;
    }
    source[source_length] = '\0';

    while ((*arguments == ' ') || (*arguments == '\t')) arguments++;
    while ((*arguments != '\0') && (*arguments != ' ') &&
           (*arguments != '\t') &&
           (target_length < sizeof(target) - 1U))
    {
        target[target_length++] = *arguments++;
    }
    target[target_length] = '\0';
    while ((*arguments == ' ') || (*arguments == '\t')) arguments++;

    if ((source_length == 0U) || (target_length == 0U) ||
        (*arguments != '\0'))
    {
        UART_SendString("Usage: wavrename <old.wav> <new.wav>\r\n");
        return;
    }

    if (!RecordingCatalog_NormalizeWavFilename(
            source, normalized_source))
    {
        UART_SendString("Invalid source WAV filename\r\n");
        return;
    }
    if (!RecordingCatalog_NormalizeWavFilename(
            target, normalized_target))
    {
        UART_SendString("Invalid target WAV filename\r\n");
        return;
    }

    if (Capture_IsRecorderActive())
    {
        UART_SendString("Stop recording before rename\r\n");
        return;
    }
    Recorder_GetStatus(&status);
    if (status.state != RECORDER_STATE_IDLE)
    {
        UART_SendString("Wait for recorder IDLE before rename\r\n");
        return;
    }

    UART_SendString("Source          = ");
    UART_SendString(normalized_source);
    UART_SendString("\r\nTarget          = ");
    UART_SendString(normalized_target);
    UART_SendString("\r\n");

    result = RecordingCatalog_RenameFile(
        normalized_source, normalized_target);

    if (result == FR_EXIST)
    {
        UART_SendString("Target already exists\r\n");
    }
    else if (result == FR_NO_FILE)
    {
        UART_SendString("Source file not found\r\n");
    }

    UART_SendString("Rename Result   = ");
    UART_SendDec((uint32_t)result);
    UART_SendString("\r\nResult          = ");
    UART_SendString((result == FR_OK) ? "PASS\r\n" : "FAIL\r\n");

    if (result == FR_OK)
    {
        UART_SendString("Selected WAV    = ");
        UART_SendString(RecordingCatalog_GetSelected());
        UART_SendString("\r\n");
    }
}

'''
delete_definition = '''static void Console_RunSdDelete(
    const char *filename)
{
'''
m = once(
    m,
    delete_definition,
    func + delete_definition,
    'delete function definition',
)

# Normalize any accidental literal NULs in generated text.
for name,val in [('h',h),('c',c),('m',m)]:
    val=val.replace('\x00',r'\0')
    if name=='h': h=val
    elif name=='c': c=val
    else: m=val

outs=[]
for p,t in ((CAT_H,h),(CAT_C,c),(MAIN,m)):
    t='\n'.join(line.rstrip() for line in t.splitlines())+'\n'
    if t.count('{')!=t.count('}'): fail('Brace balance failed in '+str(p))
    if t.count('/*')!=t.count('*/'): fail('Comment balance failed in '+str(p))
    if re.search(r'&(lt|gt|amp);|<br',t): fail('HTML artifact generated in '+str(p))
    if b'\x00' in t.encode(): fail('NUL generated in '+str(p))
    outs.append((p,t))
combined='\n'.join(t for _,t in outs)
for sym in (MARK,'RecordingCatalog_RenameFile','Console_RunWavRename','wavrename'):
    if sym not in combined: fail('Missing generated symbol: '+sym)
for p,t in outs: p.write_text(t)
print('Console WAV rename patch applied successfully.')
print('Modified: recording_catalog.h/.c and main.c')
print('Added strict FAT 8.3 WAV validation and collision-safe rename.')
print('Next: make clean && make -j4')
