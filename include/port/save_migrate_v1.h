#ifndef POKEPLATINUM_PORT_SAVE_MIGRATE_V1_H
#define POKEPLATINUM_PORT_SAVE_MIGRATE_V1_H

// Saves from port builds before the DS layout (64-bit members 8-aligned) converted to it.

// Converts the save in the backup memory, if it is in the old layout. Before the save code reads it.
void PortSave_MigrateV1(void);

// The same on a whole backup image (512 KB) in memory; TRUE if anything in it was converted.
BOOL PortSave_MigrateImageV1(u8 *image, u32 size);

#endif // POKEPLATINUM_PORT_SAVE_MIGRATE_V1_H
