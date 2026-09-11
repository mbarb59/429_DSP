#ifndef FILE_MANAGER_FRAMEWORK_H
#define FILE_MANAGER_FRAMEWORK_H

#include <stdbool.h>
#include <stdint.h>

#define FILE_MANAGER_MAX_ENTRIES        128U
#define FILE_MANAGER_SHORT_NAME_SIZE     13U
#define FILE_MANAGER_VISIBLE_ROWS         5U

typedef struct
{
    char name[FILE_MANAGER_SHORT_NAME_SIZE];
    uint32_t size_bytes;
    uint32_t duration_ms;
    bool is_directory;
} FileManagerEntry;

typedef struct
{
    uint32_t entry_count;
    uint32_t selected_index;
    uint32_t first_visible_index;
    uint32_t free_kib;
    bool mounted;
    bool busy;
} FileManagerState;

void FileManager_FrameworkInit(void);
const FileManagerState *FileManager_GetState(void);

#endif
