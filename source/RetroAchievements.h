/******************************************************************************
*   RetroAchievements interface adapter                                       *
*   This file implements the RetroAchievements interface,                     *
*   as well as any helper function used to handle achievement functionality.  *
*******************************************************************************/

#if USE_RETROACHIEVEMENTS

#ifndef RETROACHIEVEMENTS_H
#define RETROACHIEVEMENTS_H

#include <RA_Interface.h>

typedef enum FileType
{
    FLOPPY_DISK,
    HARD_DISK
} FileType;

typedef struct FileInfo
{
    BYTE *data;
    unsigned long data_len;
    char name[1024];
    unsigned int title_id;
    FileType file_type;
} FileInfo;

#define FINFO_DEFAULT FileInfo { 0, 0, { 0 }, 0, FileType::FLOPPY_DISK };

// Save loaded media data
extern FileInfo loaded_floppy_disk;
extern FileInfo loaded_hard_disk;
extern FileInfo loading_file;
extern FileInfo *loaded_title;

extern bool confirmed_quitting;
extern bool is_initialized;

void reset_file_info(FileInfo *file);
void free_file_info(FileInfo *file);

void RA_InitSystem();
void RA_InitUI();
void RA_InitMemory();
int RA_PrepareLoadNewRom(const char *file_name, FileType file_type);
void RA_CommitLoadNewRom();
void RA_OnGameClose(int file_type);
void RA_ClearTitle();
void RA_ProcessReset();
int RA_HandleMenuEvent(int id);
int RA_ConfirmQuit();

#endif /* RETROACHIEVEMENTS_H */

#endif
