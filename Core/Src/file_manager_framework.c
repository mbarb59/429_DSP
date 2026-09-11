#include "file_manager_framework.h"

#include <string.h>

static FileManagerState file_manager_state;

void FileManager_FrameworkInit(void)
{
    memset(&file_manager_state, 0, sizeof(file_manager_state));
}

const FileManagerState *FileManager_GetState(void)
{
    return &file_manager_state;
}
