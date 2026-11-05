// A save written by the port before its layout matched the DS's is rewritten in the DS layout when it is
// loaded, before the save code reads it.
//
// Until then the port aligned 64-bit save members to 8 bytes (the DS's compiler aligns them to 4). That
// grew four structures: SystemData (the game clock's timestamps), the Day Care and the PC boxes (a Box
// Pokémon's Super Contest ribbons), and the battle videos (a header's 64-bit field). Every chunk after
// SystemData moved, so the general block was 0xCF40 bytes instead of the DS's 0xCF2C, and the box
// block 0x121EC instead of 0x121E4: an emulator's .sav could not be loaded, nor the port's exported.
//
// Such a save is recognised by its block footers at the old offsets with the old sizes. The structures
// that changed are copied with the runs in save_migrate_v1_runs.h (generated from the two builds' DWARF),
// the other chunks byte for byte, with the 4 bytes after each (some chunks keep a checksum there). The
// block footers keep their counters and get the new size and checksum. A battle video's header checksum
// covered 4 bytes more in the old layout, so a valid one is recomputed. A copy that already has a valid
// DS-layout block is left alone, so this runs once per save file.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants/savedata/savedata.h"
#include "struct_defs/battle_recording.h"

#include "savedata/save_table.h"

#include "math_util.h"
#include "savedata.h"

#include "port/save_migrate_v1.h"
#include "save_migrate_v1_runs.h"

#define IMAGE_COPY_SIZE   (SAVE_SECTOR_SIZE * SAVE_PAGE_MAX)
#define EXTRA_FOOTER_SIZE sizeof(SaveCheckFooter)

// Where the battle video's header (BattleRecording.unk_84) was before: 8-aligned after 132 bytes
#define OLD_RECORDING_HEADER_OFFSET 136
#define OLD_RECORDING_HEADER_CRC_LENGTH (sizeof(UnkStruct_0202F41C) - sizeof(UnkStruct_0202F298_sub1) - sizeof(u64) + 4)
#define NEW_RECORDING_HEADER_CRC_LENGTH (sizeof(UnkStruct_0202F41C) - sizeof(UnkStruct_0202F298_sub1) - sizeof(u64))

typedef struct BlockLayout {
    u32 chunkOffset[SAVE_TABLE_ENTRY_MAX];
    u32 blockOffset[SAVE_BLOCK_ID_MAX];
    u32 blockSize[SAVE_BLOCK_ID_MAX];
} BlockLayout;

static const PortSaveChangedStruct *FindChanged(int chunk)
{
    for (int i = 0; i < (int)NELEMS(sChangedStructs); i++) {
        if (sChangedStructs[i].chunk == chunk) {
            return &sChangedStructs[i];
        }
    }
    return NULL;
}

// As SaveTableEntry_BodySize: the chunk, padded to 4 (always at least one byte more), and 4 bytes after it
static u32 BodySize(u32 size)
{
    return size + (4 - size % 4) + 4;
}

static u32 ChunkSize(int entry, BOOL old)
{
    u32 size = gSaveTable[entry].sizeFunc();
    const PortSaveChangedStruct *changed = FindChanged(entry);

    if (old && changed != NULL) {
        GF_ASSERT(size == changed->newSize);
        size = changed->oldSize;
    }
    return size;
}

// As SavePageInfo_Init and SaveBlockInfo_Init lay the chunks and blocks out
static void ComputeLayout(BlockLayout *layout, BOOL old)
{
    u32 offset = 0;

    for (int block = 0; block < SAVE_BLOCK_ID_MAX; block++) {
        layout->blockOffset[block] = offset;
        for (int i = 0; i < gSaveTableSize; i++) {
            if (gSaveTable[i].blockID == block) {
                layout->chunkOffset[i] = offset;
                offset += BodySize(ChunkSize(i, old));
            }
        }
        offset += sizeof(SaveBlockFooter);
        layout->blockSize[block] = offset - layout->blockOffset[block];
    }
}

static SaveBlockFooter *Footer(u8 *copy, const BlockLayout *layout, int block)
{
    return (SaveBlockFooter *)(copy + layout->blockOffset[block] + layout->blockSize[block] - sizeof(SaveBlockFooter));
}

static BOOL BlockValid(u8 *copy, const BlockLayout *layout, int block)
{
    const SaveBlockFooter *footer = Footer(copy, layout, block);

    if (layout->blockOffset[block] + layout->blockSize[block] > IMAGE_COPY_SIZE) {
        return FALSE;
    }
    return footer->size == layout->blockSize[block]
        && footer->signature == SECTOR_SIGNATURE
        && footer->saveBlockID == block
        && footer->checksum == CalcCRC16Checksum(copy + layout->blockOffset[block], layout->blockSize[block] - sizeof(SaveBlockFooter));
}

static void CopyRuns(const PortSaveChangedStruct *changed, u8 *dest, const u8 *src)
{
    for (int i = 0; i < changed->numRuns; i++) {
        const PortSaveRun *run = &changed->runs[i];
        memcpy(dest + run->newOffset, src + run->oldOffset, run->length);
    }
}

static void ConvertBlock(u8 *dest, const u8 *src, const BlockLayout *oldLayout, const BlockLayout *newLayout, int block)
{
    memset(dest + newLayout->blockOffset[block], 0, newLayout->blockSize[block]);

    for (int i = 0; i < gSaveTableSize; i++) {
        if (gSaveTable[i].blockID != block) {
            continue;
        }

        const u8 *from = src + oldLayout->chunkOffset[i];
        u8 *to = dest + newLayout->chunkOffset[i];
        u32 oldBody = BodySize(ChunkSize(i, TRUE));
        u32 newBody = BodySize(ChunkSize(i, FALSE));
        const PortSaveChangedStruct *changed = FindChanged(i);

        if (changed != NULL) {
            CopyRuns(changed, to, from);
            // The 4 bytes after the chunk (SaveData_SetChecksum's place; none of these chunks use it)
            memcpy(to + newBody - 4, from + oldBody - 4, 4);
        } else {
            GF_ASSERT(oldBody == newBody);
            memcpy(to, from, newBody);
        }
    }

    const SaveBlockFooter *oldFooter = Footer((u8 *)src, oldLayout, block);
    SaveBlockFooter *footer = Footer(dest, newLayout, block);

    footer->saveCounter = oldFooter->saveCounter;
    footer->blockCounter = oldFooter->blockCounter;
    footer->size = newLayout->blockSize[block];
    footer->signature = SECTOR_SIGNATURE;
    footer->saveBlockID = block;
    footer->checksum = CalcCRC16Checksum(dest + newLayout->blockOffset[block], newLayout->blockSize[block] - sizeof(SaveBlockFooter));
}

// The battle videos are extra entries in sectors of their own, each with its footer after the data
static BOOL ConvertBattleVideos(u8 *image)
{
    const PortSaveChangedStruct *changed = FindChanged(-1);
    BOOL converted = FALSE;
    u8 *dest = malloc(changed->newSize + EXTRA_FOOTER_SIZE);

    for (int id = 0; id < gExtraSaveTableSize; id++) {
        const SaveTableEntry *entry = &gExtraSaveTable[id];

        if (entry->sizeFunc() != (int)changed->newSize) {
            continue;
        }

        for (int copy = 0; copy < SECTOR_ID_MAX; copy++) {
            u32 sector = (copy == SECTOR_ID_PRIMARY ? PRIMARY_SECTOR_START : BACKUP_SECTOR_START) + entry->blockID;
            u8 *src = image + sector * SAVE_SECTOR_SIZE;
            SaveCheckFooter *oldFooter = (SaveCheckFooter *)(src + changed->oldSize);

            if (oldFooter->signature != SECTOR_SIGNATURE || oldFooter->size != changed->oldSize || oldFooter->id != id
                || oldFooter->checksum != CalcCRC16Checksum(src, changed->oldSize + EXTRA_FOOTER_SIZE - 2)) {
                continue;
            }

            memset(dest, 0, changed->newSize + EXTRA_FOOTER_SIZE);
            CopyRuns(changed, dest, src);

            // The header's own checksum (sub_0202F510's caller): it covered the first 4 bytes of the 64-bit field
            BattleRecording *recording = (BattleRecording *)dest;
            const u8 *oldHeader = src + OLD_RECORDING_HEADER_OFFSET;
            u16 oldCRC;
            memcpy(&oldCRC, oldHeader + offsetof(UnkStruct_0202F41C, unk_60), sizeof(oldCRC));

            if (oldCRC == CalcCRC16Checksum(oldHeader, OLD_RECORDING_HEADER_CRC_LENGTH)) {
                recording->unk_84.unk_60.unk_00 = CalcCRC16Checksum(&recording->unk_84, NEW_RECORDING_HEADER_CRC_LENGTH);
            }

            SaveCheckFooter *footer = (SaveCheckFooter *)(dest + changed->newSize);
            footer->signature = SECTOR_SIGNATURE;
            footer->saveCounter = oldFooter->saveCounter;
            footer->size = changed->newSize;
            footer->id = id;
            footer->checksum = CalcCRC16Checksum(dest, changed->newSize + EXTRA_FOOTER_SIZE - 2);

            // The new one is shorter: clear the old one's tail so its footer can't be found again
            memset(src, 0xFF, changed->oldSize + EXTRA_FOOTER_SIZE);
            memcpy(src, dest, changed->newSize + EXTRA_FOOTER_SIZE);
            printf("Save: battle video %d (copy %d) converted to the DS layout\n", id, copy);
            converted = TRUE;
        }
    }

    free(dest);
    return converted;
}

BOOL PortSave_MigrateImageV1(u8 *image, u32 size)
{
    if (size < (BACKUP_SECTOR_START + SAVE_PAGE_MAX * 2) * SAVE_SECTOR_SIZE) {
        return FALSE;
    }

    GF_ASSERT(offsetof(BattleRecording, unk_84) == sRuns_BattleRecording[1].newOffset);
    GF_ASSERT(OLD_RECORDING_HEADER_OFFSET == sRuns_BattleRecording[1].oldOffset);

    BlockLayout *oldLayout = malloc(sizeof(BlockLayout));
    BlockLayout *newLayout = malloc(sizeof(BlockLayout));
    u8 *converted = malloc(IMAGE_COPY_SIZE);
    BOOL any = FALSE;

    ComputeLayout(oldLayout, TRUE);
    ComputeLayout(newLayout, FALSE);

    for (int copy = 0; copy < SECTOR_ID_MAX; copy++) {
        u8 *src = image + (copy == SECTOR_ID_PRIMARY ? PRIMARY_SECTOR_START : BACKUP_SECTOR_START) * SAVE_SECTOR_SIZE;
        BOOL hasNew = FALSE, hasOld = FALSE;

        for (int block = 0; block < SAVE_BLOCK_ID_MAX; block++) {
            hasNew |= BlockValid(src, newLayout, block);
            hasOld |= BlockValid(src, oldLayout, block);
        }
        if (hasNew || !hasOld) {
            continue;
        }

        memcpy(converted, src, IMAGE_COPY_SIZE);
        for (int block = 0; block < SAVE_BLOCK_ID_MAX; block++) {
            if (BlockValid(src, oldLayout, block)) {
                ConvertBlock(converted, src, oldLayout, newLayout, block);
                printf("Save: copy %d block %d converted to the DS layout\n", copy, block);
                any = TRUE;
            }
        }
        // Past the new blocks' end: what's left of the old box block's tail and footer
        u32 newEnd = newLayout->blockOffset[SAVE_BLOCK_ID_MAX - 1] + newLayout->blockSize[SAVE_BLOCK_ID_MAX - 1];
        u32 oldEnd = oldLayout->blockOffset[SAVE_BLOCK_ID_MAX - 1] + oldLayout->blockSize[SAVE_BLOCK_ID_MAX - 1];
        memset(converted + newEnd, 0xFF, oldEnd - newEnd);
        memcpy(src, converted, IMAGE_COPY_SIZE);
    }

    if (any) {
        ConvertBattleVideos(image);
    }

    free(converted);
    free(newLayout);
    free(oldLayout);
    return any;
}

void PortSave_MigrateV1(void)
{
    u32 size = (BACKUP_SECTOR_START + SAVE_PAGE_MAX * 2) * SAVE_SECTOR_SIZE;
    u8 *image = malloc(size);
    s32 lockID = OS_GetLockID();

    // Not SaveData_CardLoad: on a failed read it frees the save data and shows the backup error
    CARD_LockBackup(lockID);
    CARD_ReadFlashAsync(0, image, size, NULL, NULL);
    BOOL read = CARD_WaitBackupAsync();
    CARD_UnlockBackup(lockID);
    OS_ReleaseLockID(lockID);

    if (read && PortSave_MigrateImageV1(image, size)) {
        SaveData_CardSave(0, image, size);
    }
    free(image);
}
