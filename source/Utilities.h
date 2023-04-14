#pragma once

#include "Disk.h"
#include "Harddisk.h"


void LoadConfiguration(bool loadImages);
void InsertFloppyDisks(const UINT slot, LPCSTR szImageName_drive[NUM_DRIVES], bool driveConnected[NUM_DRIVES], bool& bBoot);
void InsertHardDisks(const UINT slot, LPCSTR szImageName_harddisk[NUM_HARDDISKS], bool& bBoot);
bool DoDiskInsert(const UINT slot, const int nDrive, LPCSTR szFileName);
void GetAppleWindowTitle();

void CtrlReset();
void ResetMachineState();
