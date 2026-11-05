// Following Pokemon; see include/port/follower.h
#include "port/follower.h"

#include <nitro.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants/field/map_load.h"
#include "constants/heap.h"
#include "constants/player_avatar.h"
#include "constants/species.h"
#include "generated/map_headers.h"
#include "generated/object_events_gfx.h"

#include "field/field_system.h"
#include "overlay005/object_event_gfx_data.h"
#include "port/debug_field.h"
#include "port/sim_config_prj.h"

#include "comm_manager.h"
#include "field_task.h"
#include "heap.h"
#include "location.h"
#include "map_header_data.h"
#include "map_object.h"
#include "map_object_move.h"
#include "narc.h"
#include "party.h"
#include "player_avatar.h"
#include "pokemon.h"
#include "savedata.h"
#include "sound_playback.h"
#include "system_flags.h"
#include "unk_0206B9D8.h"
#include "unk_020655F4.h"
#include "vars_flags.h"

// --- pictures ----------------------------------------------------------------------------

// The species Platinum has walking sprites for (32x32, the usual walk frames or a float)
typedef struct SpeciesSprite {
    u16 species;
    u8 form; // 0xFF: any
    u16 graphicsID;
} SpeciesSprite;

static const SpeciesSprite sSprites[] = {
    { SPECIES_PIKACHU, 0xFF, OBJ_EVENT_GFX_PIKACHU },
    { SPECIES_CLEFAIRY, 0xFF, OBJ_EVENT_GFX_CLEFAIRY },
    { SPECIES_JIGGLYPUFF, 0xFF, OBJ_EVENT_GFX_JIGGLYPUFF },
    { SPECIES_PSYDUCK, 0xFF, OBJ_EVENT_GFX_PSYDUCK },
    { SPECIES_MACHOP, 0xFF, OBJ_EVENT_GFX_MACHOP },
    { SPECIES_TORCHIC, 0xFF, OBJ_EVENT_GFX_TORCHIC },
    { SPECIES_SHROOMISH, 0xFF, OBJ_EVENT_GFX_SHROOMISH },
    { SPECIES_SKITTY, 0xFF, OBJ_EVENT_GFX_SKITTY },
    { SPECIES_TURTWIG, 0xFF, OBJ_EVENT_GFX_TURTWIG },
    { SPECIES_GROTLE, 0xFF, OBJ_EVENT_GFX_GROTLE },
    { SPECIES_TORTERRA, 0xFF, OBJ_EVENT_GFX_TORTERRA },
    { SPECIES_CHIMCHAR, 0xFF, OBJ_EVENT_GFX_CHIMCHAR },
    { SPECIES_MONFERNO, 0xFF, OBJ_EVENT_GFX_MONFERNO },
    { SPECIES_INFERNAPE, 0xFF, OBJ_EVENT_GFX_INFERNAPE },
    { SPECIES_PIPLUP, 0xFF, OBJ_EVENT_GFX_PIPLUP },
    { SPECIES_PRINPLUP, 0xFF, OBJ_EVENT_GFX_PRINPLUP },
    { SPECIES_EMPOLEON, 0xFF, OBJ_EVENT_GFX_EMPOLEON },
    { SPECIES_PACHIRISU, 0xFF, OBJ_EVENT_GFX_PACHIRISU },
    { SPECIES_DRIFLOON, 0xFF, OBJ_EVENT_GFX_DRIFLOON },
    { SPECIES_BUNEARY, 0xFF, OBJ_EVENT_GFX_BUNEARY },
    { SPECIES_HAPPINY, 0xFF, OBJ_EVENT_GFX_HAPPINY },
    { SPECIES_CROAGUNK, 0xFF, OBJ_EVENT_GFX_CROAGUNK },
    { SPECIES_UXIE, 0xFF, OBJ_EVENT_GFX_UXIE },
    { SPECIES_MESPRIT, 0xFF, OBJ_EVENT_GFX_MESPRIT },
    { SPECIES_AZELF, 0xFF, OBJ_EVENT_GFX_AZELF },
    { SPECIES_CRESSELIA, 0xFF, OBJ_EVENT_GFX_CRESSELIA },
    { SPECIES_DARKRAI, 0xFF, OBJ_EVENT_GFX_DARKRAI },
    { SPECIES_SHAYMIN, 0, OBJ_EVENT_GFX_SHAYMIN },
    { SPECIES_ROTOM, 1, OBJ_EVENT_GFX_ROTOM_HEAT },
    { SPECIES_ROTOM, 2, OBJ_EVENT_GFX_ROTOM_WASH },
    { SPECIES_ROTOM, 3, OBJ_EVENT_GFX_ROTOM_FROST },
    { SPECIES_ROTOM, 4, OBJ_EVENT_GFX_ROTOM_FAN },
    { SPECIES_ROTOM, 5, OBJ_EVENT_GFX_ROTOM_MOW },
};

// Every other species walks as its party icon, in one of these unused sprite slots (each
// has its own texture file, so a new icon never shows an old one still loaded)
static const u16 sIconSlots[] = {
    OBJ_EVENT_GFX_DUMMY_075,
    OBJ_EVENT_GFX_DUMMY_076,
    OBJ_EVENT_GFX_DUMMY_077,
    OBJ_EVENT_GFX_DUMMY_080,
};
#define ICON_SLOTS NELEMS(sIconSlots)

typedef struct IconSlot {
    u16 species;
    u8 form;
    u32 usedAt; // sUseCounter when last given out
} IconSlot;

static IconSlot sIconSlotState[ICON_SLOTS];
static u32 sUseCounter = 0;

static int TextureMember(u16 graphicsID)
{
    for (const ObjectEventGfxFileEntry *entry = gObjectEventGfxTexturesTable; entry->id != 0xFFFF; entry++) {
        if (entry->id == graphicsID) {
            return entry->narcIdx;
        }
    }
    return -1;
}

static u16 GraphicsFor(u16 species, u8 form, int currentGraphicsID)
{
    for (int i = 0; i < (int)NELEMS(sSprites); i++) {
        if (sSprites[i].species == species && (sSprites[i].form == 0xFF || sSprites[i].form == form)) {
            return sSprites[i].graphicsID;
        }
    }

    // The slot already showing it (the follower being rebuilt after a menu)
    for (int i = 0; i < (int)ICON_SLOTS; i++) {
        if (sIconSlotState[i].species == species && sIconSlotState[i].form == form
            && (currentGraphicsID < 0 || currentGraphicsID == sIconSlots[i])) {
            sIconSlotState[i].usedAt = ++sUseCounter;
            return sIconSlots[i];
        }
    }
    // Else the one used longest ago
    int oldest = 0;
    for (int i = 1; i < (int)ICON_SLOTS; i++) {
        if (sIconSlotState[i].usedAt < sIconSlotState[oldest].usedAt) {
            oldest = i;
        }
    }
    sIconSlotState[oldest].species = species;
    sIconSlotState[oldest].form = form;
    sIconSlotState[oldest].usedAt = ++sUseCounter;
    return sIconSlots[oldest];
}

static u16 Read16(const u8 *p)
{
    return (u16)(p[0] | (p[1] << 8));
}

static u32 Read32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

// Pikachu's texture file with the icon in it: 16 frames of 32x32 at 16 colors, named
// "pikachu.1" to "pikachu.16" (four per direction: standing, step, standing, step), and
// one palette. The icon's two frames go to the standing and step frames.
static BOOL PutIconInTexture(u8 *file, u32 size, const u8 *icon, const u16 *palette)
{
    if (size < 0x40 || memcmp(file, "BTX0", 4) != 0) {
        return FALSE;
    }
    const u32 tex0 = Read32(file + 0x10);
    if (tex0 + 0x3C > size || memcmp(file + tex0, "TEX0", 4) != 0) {
        return FALSE;
    }
    u8 *block = file + tex0;
    const u32 texInfo = Read16(block + 0x0E);
    const u32 texData = Read32(block + 0x14);
    const u32 palDict = Read32(block + 0x34);
    const u32 palData = Read32(block + 0x38);

    const u8 *dict = block + texInfo;
    const int count = dict[1];
    const u8 *entries = dict + Read16(dict + 6);
    const u32 unit = Read16(entries);
    const u8 *names = entries + Read16(entries + 2);
    for (int i = 0; i < count; i++) {
        const u32 params = Read32(entries + 4 + unit * i);
        const u32 offset = (params & 0xFFFF) << 3;
        const int width = 8 << ((params >> 20) & 7);
        const int height = 8 << ((params >> 23) & 7);
        const int format = (params >> 26) & 7;
        if (width != 32 || height != 32 || format != GX_TEXFMT_PLTT16 || tex0 + texData + offset + 512 > size) {
            return FALSE;
        }
        const char *dot = memchr(names + 16 * i, '.', 16);
        const int frame = dot != NULL ? atoi(dot + 1) : 1;
        const u8 *src = icon + ((frame % 2 == 1) ? 0 : 32 * 32);
        u8 *dst = block + texData + offset;
        for (int p = 0; p < 32 * 32; p += 2) {
            dst[p / 2] = (u8)((src[p] & 0x0F) | ((src[p + 1] & 0x0F) << 4));
        }
    }

    const u8 *palDictPtr = block + palDict;
    const u8 *palEntries = palDictPtr + Read16(palDictPtr + 6);
    const u32 palOffset = (u32)Read16(palEntries + 4) << 3;
    if (tex0 + palData + palOffset + 32 > size) {
        return FALSE;
    }
    u8 *colors = block + palData + palOffset;
    for (int c = 0; c < 16; c++) {
        colors[c * 2] = (u8)(palette[c] & 0xFF);
        colors[c * 2 + 1] = (u8)(palette[c] >> 8);
    }
    return TRUE;
}

void *Follower_TextureFile(void *narcPtr, u32 member, int fromFront)
{
    NARC *narc = narcPtr;
    int slot = -1;
    for (int i = 0; i < (int)ICON_SLOTS; i++) {
        if (sIconSlotState[i].species != SPECIES_NONE && (int)member == TextureMember(sIconSlots[i])) {
            slot = i;
            break;
        }
    }
    const int templateMember = TextureMember(OBJ_EVENT_GFX_PIKACHU);
    if (slot < 0 || templateMember < 0) {
        return NULL;
    }

    static u8 sIcon[32 * 64];
    u16 palette[16];
    if (!GearSprites_IconIndexed(sIconSlotState[slot].species, sIconSlotState[slot].form, FALSE, sIcon, palette)) {
        return NULL;
    }
    const u32 size = NARC_GetMemberSize(narc, templateMember);
    void *file = fromFront == 1 ? Heap_Alloc(HEAP_ID_FIELD1, size) : Heap_AllocAtEnd(HEAP_ID_FIELD1, size);
    NARC_ReadWholeMember(narc, templateMember, file);
    if (!PutIconInTexture(file, size, sIcon, palette)) {
        printf("Follower: the icon texture couldn't be made\n");
    }
    return file;
}

// --- the follower ------------------------------------------------------------------------

static MapObject *sFollower = NULL;
static const MapObjectManager *sFollowerManager = NULL;
static u16 sFollowerSpecies = SPECIES_NONE;
static u8 sFollowerForm = 0;
static BOOL sFollowerShowing = FALSE; // FALSE: hidden on the player's tile until the player moves

// Where it last was, to put it back after the field is rebuilt (a battle, a menu)
static BOOL sHaveLast = FALSE;
static int sLastMap = -1;
static int sLastX, sLastZ, sLastDir;
static VecFx32 sLastPos;

BOOL Follower_IsFollower(const MapObject *mapObj)
{
    return mapObj != NULL && MapObject_GetMovementType(mapObj) == MOVEMENT_TYPE_PORT_FOLLOWER
        && MapObject_GetLocalID(mapObj) == FOLLOWER_LOCAL_ID;
}

static BOOL FollowerValid(FieldSystem *fieldSystem)
{
    return sFollower != NULL && sFollowerManager == fieldSystem->mapObjMan
        && MapObject_CheckStatus(sFollower, MAP_OBJ_STATUS_0) && Follower_IsFollower(sFollower);
}

// The party's first Pokemon that can battle
static Pokemon *Lead(FieldSystem *fieldSystem)
{
    Party *party = SaveData_GetParty(fieldSystem->saveData);
    for (int i = 0; i < Party_GetCurrentCount(party); i++) {
        Pokemon *mon = Party_GetPokemonBySlotIndex(party, i);
        if (!Pokemon_GetValue(mon, MON_DATA_IS_EGG, NULL) && Pokemon_GetValue(mon, MON_DATA_HP, NULL) > 0) {
            return mon;
        }
    }
    return NULL;
}

// The field is up and nothing else is going on: map objects can be added and removed
static BOOL FieldIsFree(FieldSystem *fieldSystem)
{
    return fieldSystem->processManager != NULL && !fieldSystem->processManager->pause
        && fieldSystem->processManager->child == NULL && fieldSystem->runningFieldMap
        && fieldSystem->mapObjMan != NULL && fieldSystem->playerAvatar != NULL
        && MapObjectMan_IsDrawInitialized(fieldSystem->mapObjMan);
}

// Where a Pokemon may walk behind the player
static BOOL PlaceAllows(FieldSystem *fieldSystem)
{
    PlayerAvatar *playerAvatar = fieldSystem->playerAvatar;
    VarsFlags *varsFlags = SaveData_GetVarsFlags(fieldSystem->saveData);
    return fieldSystem->mapLoadType == MAP_LOAD_TYPE_OVERWORLD
        && fieldSystem->location->mapHeaderID != MAP_HEADER_BATTLE_TOWER
        && !FieldSystem_IsInBattleTowerSalon(fieldSystem)
        && !SystemFlag_CheckHasPartner(varsFlags)
        && PlayerAvatar_GetPlayerState(playerAvatar) == PLAYER_AVATAR_WALKING
        && PlayerAvatar_MapDistortionState(playerAvatar) == AVATAR_DISTORTION_STATE_NONE
        && !CommManager_IsInitializedNotPoketch();
}

static BOOL PlayerIsStill(FieldSystem *fieldSystem)
{
    MapObject *player = PlayerAvatar_GetMapObject(fieldSystem->playerAvatar);
    return player != NULL && LocalMapObj_IsAnimationSet(player)
        && PlayerAvatar_GetPlayerMoveState(fieldSystem->playerAvatar) == PLAYER_MOVE_STATE_NONE;
}

static void Remove(void)
{
    if (sFollower != NULL && MapObject_CheckStatus(sFollower, MAP_OBJ_STATUS_0) && Follower_IsFollower(sFollower)) {
        MapObject_Delete(sFollower);
    }
    sFollower = NULL;
    sFollowerManager = NULL;
}

static void Spawn(FieldSystem *fieldSystem, Pokemon *lead)
{
    PlayerAvatar *playerAvatar = fieldSystem->playerAvatar;
    const int playerX = PlayerAvatar_GetXPos(playerAvatar);
    const int playerZ = PlayerAvatar_GetZPos(playerAvatar);
    const int mapID = fieldSystem->location->mapHeaderID;

    // Back where it was (the field was rebuilt after a battle or a menu), when that's
    // next to the player; else on the player's tile, hidden until the player moves
    BOOL showing = FALSE;
    int x = playerX;
    int z = playerZ;
    int dir = PlayerAvatar_GetFacingDir(playerAvatar);
    if (sHaveLast && sLastMap == mapID && abs(sLastX - playerX) + abs(sLastZ - playerZ) == 1) {
        x = sLastX;
        z = sLastZ;
        dir = sLastDir;
        showing = TRUE;
    }

    const u16 species = Pokemon_GetValue(lead, MON_DATA_SPECIES, NULL);
    const u8 form = Pokemon_GetValue(lead, MON_DATA_FORM, NULL);

    ObjectEvent event;
    memset(&event, 0, sizeof(event));
    ObjectEvent_SetLocalID(&event, FOLLOWER_LOCAL_ID);
    ObjectEvent_SetGraphicsID(&event, GraphicsFor(species, form, -1));
    ObjectEvent_SetMovementType(&event, MOVEMENT_TYPE_PORT_FOLLOWER);
    ObjectEvent_SetTrainerType(&event, 0);
    ObjectEvent_SetHiddenFlag(&event, 0);
    ObjectEvent_SetScript(&event, 0);
    ObjectEvent_SetInitialDir(&event, dir);
    ObjectEvent_SetMovementRangeX(&event, 0);
    ObjectEvent_SetMovementRangeZ(&event, 0);
    ObjectEvent_SetX(&event, x);
    ObjectEvent_SetZ(&event, z);
    ObjectEvent_SetY(&event, 0);

    MapObject *mapObj = MapObjectMan_AddMapObjectFromHeader(fieldSystem->mapObjMan, &event, mapID);
    if (mapObj == NULL) {
        return; // no room for another object on this map
    }

    // Its height: the remembered one (bridges), else the player's
    VecFx32 pos;
    if (showing) {
        pos = sLastPos;
    } else {
        PlayerAvatar_GetPosPtr(playerAvatar, &pos);
    }
    MapObject_SetPosDirFromVec(mapObj, &pos, dir);
    MapObject_SetHidden(mapObj, showing ? FALSE : TRUE);

    sFollower = mapObj;
    sFollowerManager = fieldSystem->mapObjMan;
    sFollowerSpecies = species;
    sFollowerForm = form;
    sFollowerShowing = showing;
}

void Follower_Update(void)
{
    const SIM_Config_prj_type *config = SIM_Config_prj_GetConfig();
    FieldSystem *fieldSystem = DEBUG_GetFieldSystem();
    if (fieldSystem == NULL || fieldSystem->saveData == NULL) {
        sFollower = NULL;
        sFollowerManager = NULL;
        sHaveLast = FALSE;
        return;
    }
    if (!FieldIsFree(fieldSystem)) {
        return; // the field is being torn down or rebuilt: leave its objects alone
    }

    if (!FollowerValid(fieldSystem)) {
        sFollower = NULL;
        sFollowerManager = NULL;
    }

    Pokemon *lead = Lead(fieldSystem);
    const BOOL wanted = config->followerPokemon && lead != NULL && PlaceAllows(fieldSystem);

    if (!wanted) {
        if (sFollower != NULL) {
            Remove();
        }
        sHaveLast = FALSE;
        return;
    }

    PlayerAvatar *playerAvatar = fieldSystem->playerAvatar;
    const int playerX = PlayerAvatar_GetXPos(playerAvatar);
    const int playerZ = PlayerAvatar_GetZPos(playerAvatar);

    if (sFollower == NULL) {
        // Only while nothing is going on (warps, events) and the player stands still
        if (!FieldSystem_IsRunningTask(fieldSystem) && PlayerIsStill(fieldSystem)) {
            Spawn(fieldSystem, lead);
        }
        return;
    }

    // Another lead (party order, a faint, an evolution): the new one comes out where the
    // old one was
    const u16 species = Pokemon_GetValue(lead, MON_DATA_SPECIES, NULL);
    const u8 form = Pokemon_GetValue(lead, MON_DATA_FORM, NULL);
    if (species != sFollowerSpecies || form != sFollowerForm) {
        if (FieldSystem_IsRunningTask(fieldSystem) || !PlayerIsStill(fieldSystem) || MapObject_IsMoving(sFollower)) {
            return;
        }
        sHaveLast = sFollowerShowing;
        Remove();
        Spawn(fieldSystem, lead);
        return;
    }

    const int x = MapObject_GetX(sFollower);
    const int z = MapObject_GetZ(sFollower);
    if (!sFollowerShowing && (x != playerX || z != playerZ)) {
        // The player stepped off its tile: it comes out
        MapObject_SetHidden(sFollower, FALSE);
        sFollowerShowing = TRUE;
    }
    if (sFollowerShowing) {
        // Gone with the player through a door (the player is hidden while entering)
        MapObject *player = PlayerAvatar_GetMapObject(playerAvatar);
        const BOOL playerHidden = player != NULL && MapObject_CheckStatus(player, MAP_OBJ_STATUS_HIDE);
        MapObject_SetHidden(sFollower, playerHidden ? TRUE : FALSE);
    }

    // Left behind (an event moved the player, a jump, a slide): it catches up by
    // appearing on the player's tile again once both stand still
    const int distance = abs(x - playerX) + abs(z - playerZ);
    if (sFollowerShowing && distance > 1 && !FieldSystem_IsRunningTask(fieldSystem) && PlayerIsStill(fieldSystem)
        && !MapObject_IsMoving(sFollower) && LocalMapObj_IsAnimationSet(sFollower)) {
        sHaveLast = FALSE;
        Remove();
        Spawn(fieldSystem, lead);
        return;
    }

    if (sFollowerShowing) {
        sHaveLast = TRUE;
        sLastMap = fieldSystem->location->mapHeaderID;
        sLastX = x;
        sLastZ = z;
        sLastDir = MapObject_GetFacingDir(sFollower);
        MapObject_GetPosPtr(sFollower, &sLastPos);
    }
}

void Follower_Talk(FieldSystem *fieldSystem, MapObject *mapObj)
{
    // It turns to the player and calls out
    const int playerDir = PlayerAvatar_GetFacingDir(fieldSystem->playerAvatar);
    MapObject_TryFace(mapObj, Direction_GetOpposite(playerDir));
    Pokemon *lead = Lead(fieldSystem);
    if (lead != NULL) {
        Sound_PlayPokemonCry(Pokemon_GetValue(lead, MON_DATA_SPECIES, NULL), Pokemon_GetValue(lead, MON_DATA_FORM, NULL));
    }
}
