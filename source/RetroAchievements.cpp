#include "StdAfx.h"

#if USE_RETROACHIEVEMENTS

#include <assert.h>

#include <RetroAchievements.h>
#include <RA_Emulators.h>
#include "RA_BuildVer.h"

#include "CardManager.h"
#include "Core.h"
#include "Disk.h"
#include "Interface.h"
#include "Harddisk.h"
#include "Keyboard.h"
#include "Memory.h"
#include "Utilities.h"

FileInfo loaded_floppy_disk = FINFO_DEFAULT;
FileInfo loaded_hard_disk = FINFO_DEFAULT;
FileInfo loading_file = FINFO_DEFAULT;
FileInfo *loaded_title = 0;
bool should_activate = true;

bool confirmed_quitting = false;
bool is_initialized = false;

void reset_file_info(FileInfo *file)
{
    file->data = 0;
    file->data_len = 0;
    file->name[0] = 0;
    file->title_id = 0;
    file->file_type = FileType::FLOPPY_DISK;
}

void free_file_info(FileInfo *file)
{
    if (file->data)
        free(file->data);

    reset_file_info(file);
}

/*****************************************************************************
 * Memory readers/writers for achievement processing                         *
 *****************************************************************************/

#ifndef RA_ENABLE_AUXRAM
#define RA_ENABLE_AUXRAM 1 // Enable auxiliary RAM by default
#endif

// http://www.applelogic.org/files/AIIETECHREF2.pdf
// Apple II memory: $0000-$BFFF = main RAM
//                  $C000-$CFFF = I/O chapter 2 and 6
//                  $D000-$DFFF = ROM or banked extended memory
//                  $E000-$FFFF = ROM or extended memory
// For RetroAchievements, we want $D000-$FFFF to only be the extended memory

static unsigned char MainRAMReader(size_t nOffs)
{
    assert(nOffs <= 0xFFFF);
    return *MemGetMainPtr((WORD)nOffs);
}

static void MainRAMWriter(size_t nOffs, unsigned char nVal)
{
    assert(nOffs <= 0xFFFF);
    memdirty[nOffs >> 8] |= 1;
    *MemGetMainPtr((WORD)nOffs) = nVal;
}

#if RA_ENABLE_AUXRAM
static unsigned char AuxRAMReader(size_t nOffs)
{
    assert(nOffs <= 0xFFFF);
    return *MemGetAuxPtr((WORD)nOffs);
}

static void AuxRAMWriter(size_t nOffs, unsigned char nVal)
{
    assert(nOffs <= 0xFFFF);
    *MemGetAuxPtr((WORD)nOffs) = nVal;
}
#endif



static int GetMenuItemIndex(HMENU hMenu, const char* ItemName)
{
    int index = 0;
    char buf[256];

    while (index < GetMenuItemCount(hMenu))
    {
        if (GetMenuString(hMenu, index, buf, sizeof(buf) - 1, MF_BYPOSITION))
        {
            if (!strcmp(ItemName, buf))
                return index;
        }
        index++;
    }

    // not found
    return -1;
}

static int GameIsActive()
{
    return (loaded_title != NULL) ? 1 : 0;
}

static void CauseUnpause()
{
    g_nAppMode = MODE_RUNNING;
}

static void CausePause()
{
    g_nAppMode = MODE_PAUSED;
}

static void RebuildMenu()
{
    // get main menu handle
    HMENU hMainMenu = GetMenu(GetFrame().g_hFrameWindow);
    if (!hMainMenu) return;

    // get file menu index
    int index = GetMenuItemIndex(hMainMenu, "&RetroAchievements");
    if (index >= 0)
        DeleteMenu(hMainMenu, index, MF_BYPOSITION);

    // embed RA
    AppendMenu(hMainMenu, MF_POPUP | MF_STRING, (UINT_PTR)RA_CreatePopupMenu(), TEXT("&RetroAchievements"));

    DrawMenuBar(GetFrame().g_hFrameWindow);
}

static void GetEstimatedGameTitle(char* sNameOut)
{
    const int ra_buffer_size = 64;

    if (loading_file.data_len > 0)
    {
        // Return the file name being loaded
        memcpy(sNameOut, loading_file.name, ra_buffer_size);
    }
    else if (loaded_title != NULL && loaded_title->name[0] != NULL)
    {
        memcpy(sNameOut, loaded_title->name, ra_buffer_size);
    }
    else
    {
        memset(sNameOut, 0, ra_buffer_size);
    }

    // Always null-terminate strings
    sNameOut[ra_buffer_size - 1] = '\0';
}

static void ResetEmulation()
{
    ResetMachineState();
}

static void LoadROM(const char* sFullPath)
{
    // Assume that the image is a floppy disk
    DoDiskInsert(5, DRIVE_1, sFullPath);
}

static void RA_InitShared()
{
    RA_InstallSharedFunctions(&GameIsActive, &CauseUnpause, &CausePause, &RebuildMenu, &GetEstimatedGameTitle, &ResetEmulation, &LoadROM);
}

void RA_InitSystem()
{
    if (is_initialized)
    {
        RA_UpdateHWnd(GetFrame().g_hFrameWindow);
    }
    else
    {
        RA_Init(GetFrame().g_hFrameWindow, RA_AppleWin, RAPPLEWIN_VERSION);
        RA_InitShared();
        RA_AttemptLogin(true);
        is_initialized = true;
    }

    confirmed_quitting = false;
}

void RA_InitUI()
{
    RebuildMenu();
    RA_InitMemory();
}

void RA_InitMemory()
{
    int bank_id = 0;

    RA_ClearMemoryBanks();

    RA_InstallMemoryBank(bank_id++, MainRAMReader, MainRAMWriter, 0x10000);

#if RA_ENABLE_AUXRAM
    RA_InstallMemoryBank(bank_id++, AuxRAMReader, AuxRAMWriter, 0x10000);
#endif
}

int RA_PrepareLoadNewRom(const char *file_name, FileType file_type)
{
    if (!file_name)
        return FALSE;

    char file_extension[_MAX_EXT];
    _splitpath(file_name, NULL, NULL, NULL, file_extension);

    if (!strcmp(_strupr(file_extension), ".ZIP") ||
        !strcmp(_strupr(file_extension), ".GZ"))
    {
        return false;
    }

    FILE *f = fopen(file_name, "rb");

    if (!f)
        return FALSE;

    char basename[_MAX_FNAME];
    _splitpath(file_name, NULL, NULL, basename, NULL);
    strcpy(loading_file.name, basename);

    fseek(f, 0, SEEK_END);
    const unsigned long file_size = (unsigned long)ftell(f);
    loading_file.data_len = file_size;

    BYTE * const file_data = (BYTE *)malloc(file_size * sizeof(BYTE));
    if (!file_data)
        return FALSE;

    loading_file.data = file_data;
    fseek(f, 0, SEEK_SET);
    fread(file_data, sizeof(BYTE), file_size, f);

    fflush(f);
    fclose(f);

    loading_file.title_id = RA_IdentifyRom(file_data, file_size);
    loading_file.file_type = file_type;

    if (loaded_title != NULL && loaded_title->data_len > 0)
    {
        if (loaded_title->title_id != loading_file.title_id || loaded_title->file_type != loading_file.file_type)
        {
            if (!RA_WarnDisableHardcore("load a new title without ejecting all images and resetting the emulator"))
            {
                free_file_info(&loading_file);
                return FALSE; // Interrupt loading
            }
        }
    }

    should_activate = should_activate ? true :
        loaded_title != NULL &&
        loaded_title->title_id > 0 &&
        loaded_title->title_id == loading_file.title_id ?
        false :
        true;

    return TRUE;
}

void RA_CommitLoadNewRom()
{
    switch (loading_file.file_type)
    {
    case FileType::FLOPPY_DISK:
        free_file_info(&loaded_floppy_disk);
        loaded_floppy_disk = loading_file;
        loaded_title = &loaded_floppy_disk;
        break;
    case FileType::HARD_DISK:
        free_file_info(&loaded_hard_disk);
        loaded_hard_disk = loading_file;
        loaded_title = &loaded_hard_disk;
        break;
    default:
        break;
    }

    RA_UpdateAppTitle(loading_file.name);

    if (should_activate)
    {
        // Initialize title data in the achievement system
        RA_ActivateGame(loading_file.title_id);
        should_activate = false;
    }

    // Clear loading data
    reset_file_info(&loading_file);
}

void RA_OnGameClose(int file_type)
{
    if (loaded_title != NULL && loaded_title->file_type == file_type)
        loaded_title = NULL;

    switch (file_type)
    {
    case FileType::FLOPPY_DISK:
        free_file_info(&loaded_floppy_disk);
        if (loaded_hard_disk.data_len > 0 && !RA_HardcoreModeIsActive())
        {
            loaded_title = &loaded_hard_disk;
            RA_UpdateAppTitle(loaded_title->name);
            RA_ActivateGame(loaded_title->title_id);
        }
        break;
    case FileType::HARD_DISK:
        free_file_info(&loaded_hard_disk);
        if (loaded_floppy_disk.data_len > 0 && !RA_HardcoreModeIsActive())
        {
            loaded_title = &loaded_floppy_disk;
            RA_UpdateAppTitle(loaded_title->name);
            RA_ActivateGame(loaded_title->title_id);
        }
        break;
    default:
        break;
    }

    if (loaded_title == NULL && loading_file.data_len == 0)
    {
        RA_ClearTitle();
    }
}

void RA_ClearTitle()
{
    RA_UpdateAppTitle("");
    RA_OnLoadNewRom(NULL, 0);
    should_activate = true;
}

void RA_ProcessReset()
{
    if (GetCardMgr().QuerySlot(SLOT6) == CT_Disk2)
    {
        Disk2InterfaceCard& disk2Card = dynamic_cast<Disk2InterfaceCard&>(GetCardMgr().GetRef(SLOT6));

        if (disk2Card.IsDriveEmpty(DRIVE_1))
            RA_OnGameClose(FileType::FLOPPY_DISK);
        if (HD_IsDriveUnplugged(HARDDISK_1))
            RA_OnGameClose(FileType::HARD_DISK);
    }

    if (RA_HardcoreModeIsActive())
    {
        if (loaded_floppy_disk.data_len > 0 && loaded_hard_disk.data_len > 0)
        {
            if (loaded_title != NULL)
            {
                switch (loaded_title->file_type)
                {
                case FileType::FLOPPY_DISK:
                    HD_Unplug(HARDDISK_1);
                    break;
                case FileType::HARD_DISK:
                    GetCardMgr().Remove(SLOT6);
                    break;
                default:
                    // Prioritize floppy disks
                    HD_Unplug(HARDDISK_1);
                    break;
                }
            }
        }
    }

    if (g_dwSpeed < SPEED_NORMAL)
    {
        g_dwSpeed = SPEED_NORMAL;
    }

    if (loaded_floppy_disk.data_len > 0)
        loaded_title = &loaded_floppy_disk;
    else if (loaded_hard_disk.data_len > 0)
        loaded_title = &loaded_hard_disk;

    if (loaded_title != NULL)
    {
        RA_UpdateAppTitle(loaded_title->name);
        RA_ActivateGame(loaded_title->title_id);
    }

    RA_OnReset();
}

int RA_HandleMenuEvent(int id)
{
    if (LOWORD(id) >= IDM_RA_MENUSTART &&
        LOWORD(id) < IDM_RA_MENUEND)
    {
        RA_InvokeDialog(LOWORD(id));
        return TRUE;
    }

    return FALSE;
}

int RA_ConfirmQuit()
{
    if (!confirmed_quitting)
        confirmed_quitting = RA_ConfirmLoadNewRom(true);

    return confirmed_quitting;
}

#endif
