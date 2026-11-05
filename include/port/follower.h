#ifndef POKEPLATINUM_PORT_FOLLOWER_H
#define POKEPLATINUM_PORT_FOLLOWER_H

// Following Pokemon (Settings > Gameplay), as in HeartGold and SoulSilver: the first
// Pokemon in the party that can battle walks behind the player on the overworld.
//
// It's a map object of its own (local ID FOLLOWER_LOCAL_ID) that moves with the game's own
// "follow the player" movement, the one Cheryl, Riley and the other partners use, under a
// movement type of its own (MOVEMENT_TYPE_PORT_FOLLOWER) so the partners' scripts never
// pick it up. It's never written to the save: it comes back where it was after a battle
// or a menu (the field is rebuilt then) and appears behind the player after a warp.
//
// Pictures: the overworld sprites Platinum has (the starters, Pikachu, Pachirisu,
// Buneary, the legendaries...); every other species gets one built from its party icon
// (port/follower.c makes the texture file from Pikachu's, with the icon's pixels).
//
// It goes back into its ball on a bike, surfing, with a partner trainer, in the
// Distortion World, the Battle Frontier buildings and while linked up. Talking to it
// plays its cry.

#include <nitro/types.h>

#include "struct_decls/map_object.h"

#include "field/field_system_decl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FOLLOWER_LOCAL_ID 0xF3
#define MOVEMENT_TYPE_PORT_FOLLOWER 0x44 // one past the game's last movement type

// Once per main loop iteration (port/frame_rate.c)
void Follower_Update(void);

// The map object is the follower (map_object.c: not saved; field input: talking)
BOOL Follower_IsFollower(const MapObject *mapObj);

// The player talked to it (unk_0203C954.c)
void Follower_Talk(FieldSystem *fieldSystem, MapObject *mapObj);

// The overworld sprite loader reads a member of mmodel.narc: the follower's icon sprite
// when it's one of the icon slots, else NULL (ov5_021ECC20.c)
void *Follower_TextureFile(void *narc, u32 member, int fromFront);

// Port sprites (port/gear_sprites.c): a party icon as 4-bit indices, 32 wide and 64 high
// (two frames, rows of pixels), and its 16 colors (BGR555)
BOOL GearSprites_IconIndexed(u16 species, u8 form, BOOL isEgg, u8 *pixels, u16 *palette);

#ifdef __cplusplus
}
#endif

#endif // POKEPLATINUM_PORT_FOLLOWER_H
