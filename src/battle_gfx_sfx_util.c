#include "global.h"
#include "gflib.h"
#include "battle.h"
#include "battle_main.h"
#include "battle_anim.h"
#include "battle_interface.h"
#include "data.h"
#include "decompress.h"
#include "graphics.h"
#include "m4a.h"
#include "menu.h"
#include "palette.h"
#include "party_menu.h"
#include "sprite.h"
#include "task.h"
#include "util.h"
#include "constants/battle_anim.h"
#include "constants/event_objects.h"
#include "constants/moves.h"
#include "constants/songs.h"

static bool8 ShouldAnimBeDoneRegardlessOfSubstitute(u8 animId);
static void Task_ClearBitWhenBattleTableAnimDone(u8 taskId);
static void Task_ClearBitWhenSpecialAnimDone(u8 taskId);
static void ClearSpritesBattlerHealthboxAnimData(void);
static void SpriteCB_TrainerSlideVertical(struct Sprite *sprite);

static const struct CompressedSpriteSheet sSpriteSheet_SinglesPlayerHealthbox =
{
    .data = gHealthboxSinglesPlayerGfx,
    .size = 0x1000,
    .tag = TAG_HEALTHBOX_PLAYER1_TILE,
};

static const struct CompressedSpriteSheet sSpriteSheet_SinglesOpponentHealthbox =
{
    .data = gHealthboxSinglesOpponentGfx,
    .size = 0x1000,
    .tag = TAG_HEALTHBOX_OPPONENT1_TILE,
};

static const struct CompressedSpriteSheet sSpriteSheets_DoublesPlayerHealthbox[2] =
{
    {
        .data = gHealthboxDoublesPlayerGfx,
        .size = 0x800,
        .tag = TAG_HEALTHBOX_PLAYER1_TILE,
    },
    {
        .data = gHealthboxDoublesPlayerGfx,
        .size = 0x800,
        .tag = TAG_HEALTHBOX_PLAYER2_TILE,
    },
};

static const struct CompressedSpriteSheet sSpriteSheets_DoublesOpponentHealthbox[2] =
{
    {
        .data = gHealthboxDoublesOpponentGfx,
        .size = 0x800,
        .tag = TAG_HEALTHBOX_OPPONENT1_TILE,
    },
    {
        .data = gHealthboxDoublesOpponentGfx,
        .size = 0x800,
        .tag = TAG_HEALTHBOX_OPPONENT2_TILE,
    },
};

static const struct CompressedSpriteSheet sSpriteSheet_SafariHealthbox =
{
    .data = gHealthboxSafariGfx,
    .size = 0x1000,
    .tag = TAG_HEALTHBOX_SAFARI_TILE,
};

static const struct CompressedSpriteSheet sSpriteSheets_HealthBar[MAX_BATTLERS_COUNT] =
{
    {
        .data = gBlankGfxCompressed,
        .size = 0x100,
        .tag = TAG_HEALTHBAR_PLAYER1_TILE,
    },
    {
        .data = gBlankGfxCompressed,
        .size = 0x120,
        .tag = TAG_HEALTHBAR_OPPONENT1_TILE,
    },
    {
        .data = gBlankGfxCompressed,
        .size = 0x100,
        .tag = TAG_HEALTHBAR_PLAYER2_TILE,
    },
    {
        .data = gBlankGfxCompressed,
        .size = 0x120,
        .tag = TAG_HEALTHBAR_OPPONENT2_TILE,
    },
};

const struct SpritePalette sSpritePalettes_HealthBoxHealthBar[2] =
{
    {
        .data = gBattleInterface_Healthbox_Pal,
        .tag = TAG_HEALTHBOX_PAL,
    },
    {
        .data = gBattleInterface_Healthbar_Pal,
        .tag = TAG_HEALTHBAR_PAL,
    },
};

const struct CompressedSpriteSheet gSpriteSheet_EnemyShadow =
{
    .data = gEnemyMonShadow_Gfx, .size = 0x80, .tag = TAG_SHADOW_TILE
};

const struct CompressedSpriteSheet gSpriteSheet_EnemyShadowsSized =
{
    .data = gEnemyMonShadowsSized_Gfx,
    .size = TILE_SIZE_4BPP * 8 * 4, // 8 tiles per sprite, 4 sprites total
    .tag = TAG_SHADOW_TILE,
};

static const struct OamData sOamData_EnemyShadow =
{
    .y = 0,
    .affineMode = ST_OAM_AFFINE_OFF,
    .objMode = ST_OAM_OBJ_NORMAL,
    .mosaic = FALSE,
    .bpp = ST_OAM_4BPP,
    .shape = SPRITE_SHAPE(32x8),
    .x = 0,
    .matrixNum = 0,
    .size = SPRITE_SIZE(32x8),
    .tileNum = 0,
    .priority = 3,
    .paletteNum = 0,
    .affineParam = 0
};

const struct SpriteTemplate gSpriteTemplate_EnemyShadow =
{
    .tileTag = TAG_SHADOW_TILE,
    .paletteTag = TAG_SHADOW_PAL,
    .oam = &sOamData_EnemyShadow,
    .anims = gDummySpriteAnimTable,
    .images = NULL,
    .affineAnims = gDummySpriteAffineAnimTable,
    .callback = SpriteCallbackDummy,
};

void AllocateBattleSpritesData(void)
{
    gBattleSpritesDataPtr = AllocZeroed(sizeof(struct BattleSpriteData));
    gBattleSpritesDataPtr->battlerData = AllocZeroed(sizeof(struct BattleSpriteInfo) * MAX_BATTLERS_COUNT);
    gBattleSpritesDataPtr->healthBoxesData = AllocZeroed(sizeof(struct BattleHealthboxInfo) * MAX_BATTLERS_COUNT);
    gBattleSpritesDataPtr->animationData = AllocZeroed(sizeof(struct BattleAnimationInfo));
    gBattleSpritesDataPtr->battleBars = AllocZeroed(sizeof(struct BattleBarInfo) * MAX_BATTLERS_COUNT);
}

void FreeBattleSpritesData(void)
{
    if (gBattleSpritesDataPtr)
    {
        FREE_AND_SET_NULL(gBattleSpritesDataPtr->battleBars);
        FREE_AND_SET_NULL(gBattleSpritesDataPtr->animationData);
        FREE_AND_SET_NULL(gBattleSpritesDataPtr->healthBoxesData);
        FREE_AND_SET_NULL(gBattleSpritesDataPtr->battlerData);
        FREE_AND_SET_NULL(gBattleSpritesDataPtr);
    }
}

void SpriteCB_WaitForBattlerBallReleaseAnim(struct Sprite *sprite)
{
    u8 spriteId = sprite->data[1];

    if (!gSprites[spriteId].affineAnimEnded)
        return;
    if (gSprites[spriteId].invisible)
        return;
    if (gSprites[spriteId].animPaused)
        gSprites[spriteId].animPaused = 0;
    else if (gSprites[spriteId].animEnded)
        sprite->callback = SpriteCallbackDummy;
}

// Unused
static void UNUSED DoBattleSpriteAffineAnim(struct Sprite *sprite, bool8 arg1)
{
    sprite->animPaused = 1;
    sprite->callback = SpriteCallbackDummy;
    if (!arg1)
        StartSpriteAffineAnim(sprite, 1);
    else
        StartSpriteAffineAnim(sprite, 1);
    AnimateSprite(sprite);
}

#define sSpeedX data[0]

void SpriteCB_TrainerSlideIn(struct Sprite *sprite)
{
    if (!(gIntroSlideFlags & 1))
    {
        sprite->x2 += sprite->sSpeedX;
        if (sprite->x2 == 0)
        {
            if (sprite->y2 != 0)
                sprite->callback = SpriteCB_TrainerSlideVertical;
            else
                sprite->callback = SpriteCallbackDummy;
        }
    }
}

void SpriteCB_TrainerSpawn(struct Sprite *sprite)
{
    if (!(gIntroSlideFlags & 1))
    {
        sprite->x2 = 0;
        if (sprite->y2 != 0)
            sprite->callback = SpriteCB_TrainerSlideVertical;
        else
            sprite->callback = SpriteCallbackDummy;
    }
}

// Slide up to 0 if necessary (used by multi battle intro)
static void SpriteCB_TrainerSlideVertical(struct Sprite *sprite)
{
    sprite->y2 -= 2;
    if (sprite->y2 == 0)
        sprite->callback = SpriteCallbackDummy;
}

#undef sSpeedX

void InitAndLaunchChosenStatusAnimation(u32 battler, bool8 isStatus2, u32 status)
{
    gBattleSpritesDataPtr->healthBoxesData[battler].statusAnimActive = 1;
    if (!isStatus2)
    {
        if (status == STATUS1_FREEZE || status == STATUS1_FROSTBITE)
            LaunchStatusAnimation(battler, B_ANIM_STATUS_FRZ);
        else if (status == STATUS1_POISON || status & STATUS1_TOXIC_POISON)
            LaunchStatusAnimation(battler, B_ANIM_STATUS_PSN);
        else if (status == STATUS1_BURN)
            LaunchStatusAnimation(battler, B_ANIM_STATUS_BRN);
        else if (status & STATUS1_SLEEP)
            LaunchStatusAnimation(battler, B_ANIM_STATUS_SLP);
        else if (status == STATUS1_PARALYSIS)
            LaunchStatusAnimation(battler, B_ANIM_STATUS_PRZ);
        else // no animation
            gBattleSpritesDataPtr->healthBoxesData[battler].statusAnimActive = 0;
    }
    else
    {
        if (status & STATUS2_INFATUATION)
            LaunchStatusAnimation(battler, B_ANIM_STATUS_INFATUATION);
        else if (status & STATUS2_CONFUSION)
            LaunchStatusAnimation(battler, B_ANIM_STATUS_CONFUSION);
        else if (status & STATUS2_CURSED)
            LaunchStatusAnimation(battler, B_ANIM_STATUS_CURSED);
        else if (status & STATUS2_NIGHTMARE)
            LaunchStatusAnimation(battler, B_ANIM_STATUS_NIGHTMARE);
        else // no animation
            gBattleSpritesDataPtr->healthBoxesData[battler].statusAnimActive = 0;
    }
}

#define tBattlerId data[0]

bool8 TryHandleLaunchBattleTableAnimation(u8 activeBattler, u8 atkBattler, u8 defBattler, u8 tableId, u16 argument)
{
    u8 taskId;

    if (gBattleSpritesDataPtr->battlerData[activeBattler].behindSubstitute
        && !ShouldAnimBeDoneRegardlessOfSubstitute(tableId))
    {
        return TRUE;
    }
    if (gBattleSpritesDataPtr->battlerData[activeBattler].behindSubstitute
        && tableId == B_ANIM_SUBSTITUTE_FADE
        && gSprites[gBattlerSpriteIds[activeBattler]].invisible)
    {
        LoadBattleMonGfxAndAnimate(activeBattler, TRUE, gBattlerSpriteIds[activeBattler]);
        ClearBehindSubstituteBit(activeBattler);
        return TRUE;
    }

    if (tableId == B_ANIM_ILLUSION_OFF)
    {
        gBattleStruct->illusion[activeBattler].broken = 1;
        gBattleStruct->illusion[activeBattler].on = 0;
    }

    gBattleAnimAttacker = atkBattler;
    gBattleAnimTarget = defBattler;
    gBattleSpritesDataPtr->animationData->animArg = argument;
    LaunchBattleAnimation(ANIM_TYPE_GENERAL, tableId);
    taskId = CreateTask(Task_ClearBitWhenBattleTableAnimDone, 10);
    gTasks[taskId].tBattlerId = activeBattler;
    gBattleSpritesDataPtr->healthBoxesData[gTasks[taskId].tBattlerId].animFromTableActive = 1;

    return FALSE;
}

static void Task_ClearBitWhenBattleTableAnimDone(u8 taskId)
{
    gAnimScriptCallback();
    if (!gAnimScriptActive)
    {
        gBattleSpritesDataPtr->healthBoxesData[gTasks[taskId].tBattlerId].animFromTableActive = 0;
        DestroyTask(taskId);
    }
}

static bool8 ShouldAnimBeDoneRegardlessOfSubstitute(u8 animId)
{
    switch (animId)
    {
    case B_ANIM_SUBSTITUTE_FADE:
    case B_ANIM_RAIN_CONTINUES:
    case B_ANIM_SUN_CONTINUES:
    case B_ANIM_SANDSTORM_CONTINUES:
    case B_ANIM_HAIL_CONTINUES:
    case B_ANIM_SNOW_CONTINUES:
    case B_ANIM_FOG_CONTINUES:
    case B_ANIM_SNATCH_MOVE:
        return TRUE;
    default:
        return FALSE;
    }
}

void InitAndLaunchSpecialAnimation(u8 activeBattler, u8 atkBattler, u8 defBattler, u8 tableId)
{
    u8 taskId;

    gBattleAnimAttacker = atkBattler;
    gBattleAnimTarget = defBattler;
    LaunchBattleAnimation(ANIM_TYPE_SPECIAL, tableId);
    taskId = CreateTask(Task_ClearBitWhenSpecialAnimDone, 10);
    gTasks[taskId].tBattlerId = activeBattler;
    gBattleSpritesDataPtr->healthBoxesData[gTasks[taskId].tBattlerId].specialAnimActive = 1;
}

static void Task_ClearBitWhenSpecialAnimDone(u8 taskId)
{
    gAnimScriptCallback();
    if (!gAnimScriptActive)
    {
        gBattleSpritesDataPtr->healthBoxesData[gTasks[taskId].tBattlerId].specialAnimActive = 0;
        DestroyTask(taskId);
    }
}

bool8 IsBattleSEPlaying(u8 battlerId)
{
    u8 zero = 0;

    if (IsSEPlaying())
    {
        ++gBattleSpritesDataPtr->healthBoxesData[battlerId].soundTimer;
        if (gBattleSpritesDataPtr->healthBoxesData[battlerId].soundTimer < 30)
            return TRUE;
        m4aMPlayStop(&gMPlayInfo_SE1);
        m4aMPlayStop(&gMPlayInfo_SE2);
    }
    if (zero == 0)
    {
        gBattleSpritesDataPtr->healthBoxesData[battlerId].soundTimer = 0;
        return FALSE;
    }
    else
    {
        return TRUE;
    }
}

void BattleLoadMonSpriteGfx(struct Pokemon *mon, u32 battler)
{
    u32 monsPersonality, currentPersonality, isShiny, species, paletteOffset, position;
    const void *lzPaletteData;
    struct Pokemon *illusionMon = GetIllusionMonPtr(battler);
    if (illusionMon != NULL)
        mon = illusionMon;

    if (GetMonData(mon, MON_DATA_IS_EGG) || GetMonData(mon, MON_DATA_SPECIES) == SPECIES_NONE) // Don't load GFX of egg pokemon.
        return;

    monsPersonality = GetMonData(mon, MON_DATA_PERSONALITY);
    isShiny = GetMonData(mon, MON_DATA_IS_SHINY);

    if (gBattleSpritesDataPtr->battlerData[battler].transformSpecies == SPECIES_NONE)
    {
        species = GetMonData(mon, MON_DATA_SPECIES);
        currentPersonality = monsPersonality;
    }
    else
    {
        species = gBattleSpritesDataPtr->battlerData[battler].transformSpecies;
        if (B_TRANSFORM_SHINY >= GEN_4)
        {
            currentPersonality = gTransformedPersonalities[battler];
            isShiny = gTransformedShininess[battler];
        }
        else
        {
            currentPersonality = monsPersonality;
        }
    }

    position = GetBattlerPosition(battler);
    if (GetBattlerSide(battler) == B_SIDE_OPPONENT)
    {
        HandleLoadSpecialPokePic(TRUE,
                                 gMonSpritesGfxPtr->spritesGfx[position],
                                 species, currentPersonality);
    }
    else
    {
        HandleLoadSpecialPokePic(FALSE,
                                 gMonSpritesGfxPtr->spritesGfx[position],
                                 species, currentPersonality);
    }

    paletteOffset = OBJ_PLTT_ID(battler);

    if (gBattleSpritesDataPtr->battlerData[battler].transformSpecies == SPECIES_NONE)
        lzPaletteData = GetMonFrontSpritePal(mon);
    else
        lzPaletteData = GetMonSpritePalFromSpeciesAndPersonality(species, isShiny, currentPersonality);

    LZDecompressWram(lzPaletteData, gDecompressionBuffer);
    LoadPalette(gDecompressionBuffer, paletteOffset, PLTT_SIZE_4BPP);
    LoadPalette(gDecompressionBuffer, BG_PLTT_ID(8) + BG_PLTT_ID(battler), PLTT_SIZE_4BPP);

    // transform's pink color
    if (gBattleSpritesDataPtr->battlerData[battler].transformSpecies != SPECIES_NONE)
    {
        BlendPalette(paletteOffset, 16, 6, RGB_WHITE);
        CpuCopy32(&gPlttBufferFaded[paletteOffset], &gPlttBufferUnfaded[paletteOffset], PLTT_SIZEOF(16));
    }

    // dynamax tint
    if (GetActiveGimmick(battler) == GIMMICK_DYNAMAX)
    {
        // Calyrex and its forms have a blue dynamax aura instead of red.
        if (GET_BASE_SPECIES_ID(species) == SPECIES_CALYREX)
            BlendPalette(paletteOffset, 16, 4, RGB(12, 0, 31));
        else
            BlendPalette(paletteOffset, 16, 4, RGB(31, 0, 12));
        CpuCopy32(gPlttBufferFaded + paletteOffset, gPlttBufferUnfaded + paletteOffset, PLTT_SIZEOF(16));
    }

    // Terastallization's tint
    if (GetActiveGimmick(battler) == GIMMICK_TERA)
    {
        BlendPalette(paletteOffset, 16, 8, GetTeraTypeRGB(GetBattlerTeraType(battler)));
        CpuCopy32(gPlttBufferFaded + paletteOffset, gPlttBufferUnfaded + paletteOffset, PLTT_SIZEOF(16));
    }
}

void DecompressGhostFrontPic(struct Pokemon *unused, u8 battlerId)
{
    u16 palOffset;
    void *buffer;
    u8 position = GetBattlerPosition(battlerId);

    LZ77UnCompWram(gGhostFrontPic, gMonSpritesGfxPtr->spritesGfx[position]);
    palOffset = OBJ_PLTT_ID(battlerId);
    buffer = AllocZeroed(0x400);
    LZDecompressWram(gGhostPalette, buffer);
    LoadPalette(buffer, palOffset, PLTT_SIZE_4BPP);
    LoadPalette(buffer, BG_PLTT_ID(8) + BG_PLTT_ID(battlerId), PLTT_SIZE_4BPP);
    Free(buffer);
}

void DecompressTrainerFrontPic(u16 frontPicId, u8 battler)
{
    u8 position = GetBattlerPosition(battler);

    DecompressPicFromTable(&gTrainerSprites[frontPicId].frontPic, gMonSpritesGfxPtr->spritesGfx[position]);
    LoadCompressedSpritePalette(&gTrainerSprites[frontPicId].palette);
}

void DecompressTrainerBackPic(u16 backPicId, u8 battler)
{
    u8 position = GetBattlerPosition(battler);
    DecompressPicFromTable(&gTrainerBacksprites[backPicId].backPic,
                           gMonSpritesGfxPtr->spritesGfx[position]);
    LoadCompressedPalette(gTrainerBacksprites[backPicId].palette.data,
                          OBJ_PLTT_ID(battler), PLTT_SIZE_4BPP);
}

void DecompressTrainerBackPalette(u16 index, u8 palette)
{
    LoadCompressedPalette(gTrainerBacksprites[index].palette.data, OBJ_PLTT_ID2(palette), PLTT_SIZE_4BPP);
}

void BattleGfxSfxDummy2(u16 species)
{
}

void FreeTrainerFrontPicPalette(u16 frontPicId)
{
    FreeSpritePaletteByTag(gTrainerSprites[frontPicId].palette.tag);
}

// not used
static void UNUSED BattleLoadAllHealthBoxesGfxAtOnce(void)
{
    u8 numberOfBattlers = 0;
    u8 i;

    LoadSpritePalette(&sSpritePalettes_HealthBoxHealthBar[0]);
    LoadSpritePalette(&sSpritePalettes_HealthBoxHealthBar[1]);
    if (!IsDoubleBattle())
    {
        LoadCompressedSpriteSheetUsingHeap(&sSpriteSheet_SinglesPlayerHealthbox);
        LoadCompressedSpriteSheetUsingHeap(&sSpriteSheet_SinglesOpponentHealthbox);
        numberOfBattlers = 2;
    }
    else
    {
        LoadCompressedSpriteSheetUsingHeap(&sSpriteSheets_DoublesPlayerHealthbox[0]);
        LoadCompressedSpriteSheetUsingHeap(&sSpriteSheets_DoublesPlayerHealthbox[1]);
        LoadCompressedSpriteSheetUsingHeap(&sSpriteSheets_DoublesOpponentHealthbox[0]);
        LoadCompressedSpriteSheetUsingHeap(&sSpriteSheets_DoublesOpponentHealthbox[1]);
        numberOfBattlers = MAX_BATTLERS_COUNT;
    }
    for (i = 0; i < numberOfBattlers; ++i)
        LoadCompressedSpriteSheetUsingHeap(&sSpriteSheets_HealthBar[gBattlerPositions[i]]);
}

bool8 BattleLoadAllHealthBoxesGfx(u8 state)
{
    bool8 retVal = FALSE;

    if (state)
    {
        if (state == 1)
        {
            LoadSpritePalette(&sSpritePalettes_HealthBoxHealthBar[0]);
            LoadSpritePalette(&sSpritePalettes_HealthBoxHealthBar[1]);
            LoadIndicatorSpritesGfx();
            CategoryIcons_LoadSpritesGfx();
        }
        else if (!IsDoubleBattle())
        {
            if (state == 2)
            {
                if (gBattleTypeFlags & BATTLE_TYPE_SAFARI)
                    LoadCompressedSpriteSheetUsingHeap(&sSpriteSheet_SafariHealthbox);
                else
                    LoadCompressedSpriteSheetUsingHeap(&sSpriteSheet_SinglesPlayerHealthbox);
            }
            else if (state == 3)
                LoadCompressedSpriteSheetUsingHeap(&sSpriteSheet_SinglesOpponentHealthbox);
            else if (state == 4)
                LoadCompressedSpriteSheetUsingHeap(&sSpriteSheets_HealthBar[gBattlerPositions[0]]);
            else if (state == 5)
                LoadCompressedSpriteSheetUsingHeap(&sSpriteSheets_HealthBar[gBattlerPositions[1]]);
            else
                retVal = TRUE;
        }
        else
        {
            if (state == 2)
            {
                if (WhichBattleCoords(0))
                    LoadCompressedSpriteSheetUsingHeap(&sSpriteSheets_DoublesPlayerHealthbox[0]);
                else
                    LoadCompressedSpriteSheetUsingHeap(&sSpriteSheet_SinglesPlayerHealthbox);
            }
            else if (state == 3)
                LoadCompressedSpriteSheetUsingHeap(&sSpriteSheets_DoublesPlayerHealthbox[1]);
            else if (state == 4)
                LoadCompressedSpriteSheetUsingHeap(&sSpriteSheets_DoublesOpponentHealthbox[0]);
            else if (state == 5)
                LoadCompressedSpriteSheetUsingHeap(&sSpriteSheets_DoublesOpponentHealthbox[1]);
            else if (state == 6)
                LoadCompressedSpriteSheetUsingHeap(&sSpriteSheets_HealthBar[gBattlerPositions[0]]);
            else if (state == 7)
                LoadCompressedSpriteSheetUsingHeap(&sSpriteSheets_HealthBar[gBattlerPositions[1]]);
            else if (state == 8)
                LoadCompressedSpriteSheetUsingHeap(&sSpriteSheets_HealthBar[gBattlerPositions[2]]);
            else if (state == 9)
                LoadCompressedSpriteSheetUsingHeap(&sSpriteSheets_HealthBar[gBattlerPositions[3]]);
            else
                retVal = TRUE;
        }
    }
    return retVal;
}

void LoadBattleBarGfx(u8 arg0)
{
    LZDecompressWram(gInterfaceGfx_HPNumbers, gMonSpritesGfxPtr->barFontGfx);
}

bool8 BattleInitAllSprites(u8 *state, u8 *battlerId)
{
    bool8 retVal = FALSE;

    switch (*state)
    {
    case 0:
        ClearSpritesBattlerHealthboxAnimData();
        ++*state;
        break;
    case 1:
        if (!BattleLoadAllHealthBoxesGfx(*battlerId))
        {
            ++*battlerId;
        }
        else
        {
            *battlerId = 0;
            ++*state;
        }
        break;
    case 2:
        ++*state;
        break;
    case 3:
        if ((gBattleTypeFlags & BATTLE_TYPE_SAFARI) && *battlerId == 0)
            gHealthboxSpriteIds[*battlerId] = CreateSafariPlayerHealthboxSprites();
        else
            gHealthboxSpriteIds[*battlerId] = CreateBattlerHealthboxSprites(*battlerId);

        ++*battlerId;
        if (*battlerId == gBattlersCount)
        {
            *battlerId = 0;
            ++*state;
        }
        break;
    case 4:
        InitBattlerHealthboxCoords(*battlerId);
        if (gBattlerPositions[*battlerId] <= 1)
            DummyBattleInterfaceFunc(gHealthboxSpriteIds[*battlerId], FALSE);
        else
            DummyBattleInterfaceFunc(gHealthboxSpriteIds[*battlerId], TRUE);

        ++*battlerId;
        if (*battlerId == gBattlersCount)
        {
            *battlerId = 0;
            ++*state;
        }
        break;
    case 5:
        if (GetBattlerSide(*battlerId) == B_SIDE_PLAYER)
        {
            if (!(gBattleTypeFlags & BATTLE_TYPE_SAFARI))
                UpdateHealthboxAttribute(gHealthboxSpriteIds[*battlerId], &gPlayerParty[gBattlerPartyIndexes[*battlerId]], HEALTHBOX_ALL);
        }
        else
        {
            UpdateHealthboxAttribute(gHealthboxSpriteIds[*battlerId], &gEnemyParty[gBattlerPartyIndexes[*battlerId]], HEALTHBOX_ALL);
        }
        SetHealthboxSpriteInvisible(gHealthboxSpriteIds[*battlerId]);
        ++*battlerId;
        if (*battlerId == gBattlersCount)
        {
            *battlerId = 0;
            ++*state;
        }
        break;
    case 6:
        LoadAndCreateEnemyShadowSprites();
        BufferBattlePartyCurrentOrder();
        retVal = TRUE;
        break;
    }
    return retVal;
}

void ClearSpritesHealthboxAnimData(void)
{
    memset(gBattleSpritesDataPtr->healthBoxesData, 0, sizeof(struct BattleHealthboxInfo) * MAX_BATTLERS_COUNT);
    memset(gBattleSpritesDataPtr->animationData, 0, sizeof(struct BattleAnimationInfo));
}

static void ClearSpritesBattlerHealthboxAnimData(void)
{
    ClearSpritesHealthboxAnimData();
    memset(gBattleSpritesDataPtr->battlerData, 0, sizeof(struct BattleSpriteInfo) * MAX_BATTLERS_COUNT);
}

void CopyAllBattleSpritesInvisibilities(void)
{
    s32 i;

    for (i = 0; i < gBattlersCount; ++i)
        gBattleSpritesDataPtr->battlerData[i].invisible = gSprites[gBattlerSpriteIds[i]].invisible;
}

void CopyBattleSpriteInvisibility(u8 battlerId)
{
    gBattleSpritesDataPtr->battlerData[battlerId].invisible = gSprites[gBattlerSpriteIds[battlerId]].invisible;
}

void HandleSpeciesGfxDataChange(u8 battlerAtk, u8 battlerDef, bool32 megaEvo, bool32 trackEnemyPersonality, bool32 ghostUnveil)
{
    u32 personalityValue, position, paletteOffset, targetSpecies;
    bool32 isShiny;
    const void *lzPaletteData, *src;
    struct Pokemon *monAtk = GetPartyBattlerData(battlerAtk);
    struct Pokemon *monDef = GetPartyBattlerData(battlerDef);
    void *dst;

    if (IsContest())
    {
        position = 0; // position = B_POSITION_PLAYER_LEFT;
        targetSpecies = 0; // targetSpecies = gContestResources->moveAnim->targetSpecies;
        personalityValue = 0; // personalityValue = gContestResources->moveAnim->personality;
        isShiny = 0; // isShiny = gContestResources->moveAnim->isShiny;

        // HandleLoadSpecialPokePic(FALSE,
        //                          gMonSpritesGfxPtr->spritesGfx[position],
        //                          targetSpecies,
        //                          gContestResources->moveAnim->targetPersonality);
    }
    else
    {
        position = GetBattlerPosition(battlerAtk);
        if (gBattleSpritesDataPtr->battlerData[battlerAtk].transformSpecies == SPECIES_NONE)
        {
            // Get base form if its currently Gigantamax
            if (IsGigantamaxed(battlerDef))
                targetSpecies = gBattleStruct->changedSpecies[GetBattlerSide(battlerDef)][gBattlerPartyIndexes[battlerDef]];
            else
                targetSpecies = GetMonData(monDef, MON_DATA_SPECIES);
            personalityValue = GetMonData(monAtk, MON_DATA_PERSONALITY);
            isShiny = GetMonData(monAtk, MON_DATA_IS_SHINY);
        }
        else
        {
            targetSpecies = gBattleSpritesDataPtr->battlerData[battlerAtk].transformSpecies;
            if (B_TRANSFORM_SHINY >= GEN_4 && trackEnemyPersonality && !megaEvo)
            {
                personalityValue = GetMonData(monDef, MON_DATA_PERSONALITY);
                isShiny = GetMonData(monDef, MON_DATA_IS_SHINY);
            }
            else
            {
                personalityValue = GetMonData(monAtk, MON_DATA_PERSONALITY);
                isShiny = GetMonData(monAtk, MON_DATA_IS_SHINY);
            }
        }

        HandleLoadSpecialPokePic((GetBattlerSide(battlerAtk) != B_SIDE_PLAYER),
                                 gMonSpritesGfxPtr->spritesGfx[position],
                                 targetSpecies,
                                 personalityValue);
    }
    src = gMonSpritesGfxPtr->spritesGfx[position];
    dst = (void *)(OBJ_VRAM0 + gSprites[gBattlerSpriteIds[battlerAtk]].oam.tileNum * 32);
    DmaCopy32(3, src, dst, MON_PIC_SIZE);
    paletteOffset = OBJ_PLTT_ID(battlerAtk);
    lzPaletteData = GetMonSpritePalFromSpeciesAndPersonality(targetSpecies, isShiny, personalityValue);
    void *buffer = MallocAndDecompress(lzPaletteData, NULL);
    LoadPalette(buffer, paletteOffset, PLTT_SIZE_4BPP);
    Free(buffer);

    
    if (ghostUnveil)
    {
        SetMonData(&gEnemyParty[gBattlerPartyIndexes[battlerAtk]], MON_DATA_NICKNAME, gSpeciesInfo[targetSpecies].speciesName);
        UpdateNickInHealthbox(gHealthboxSpriteIds[battlerAtk], &gEnemyParty[gBattlerPartyIndexes[battlerAtk]]);
        TryAddPokeballIconToHealthbox(gHealthboxSpriteIds[battlerAtk], 1);
    }
    else if (!megaEvo)
    {
        BlendPalette(paletteOffset, 16, 6, RGB_WHITE);
        CpuCopy32(&gPlttBufferFaded[paletteOffset], &gPlttBufferUnfaded[paletteOffset], PLTT_SIZEOF(16));
        if (!IsContest())
        {
            gBattleSpritesDataPtr->battlerData[battlerAtk].transformSpecies = targetSpecies;
        }
    }

    // dynamax tint
    if (GetActiveGimmick(battlerAtk) == GIMMICK_DYNAMAX)
    {
        // Calyrex and its forms have a blue dynamax aura instead of red.
        if (GET_BASE_SPECIES_ID(targetSpecies) == SPECIES_CALYREX)
            BlendPalette(paletteOffset, 16, 4, RGB(12, 0, 31));
        else
            BlendPalette(paletteOffset, 16, 4, RGB(31, 0, 12));
        CpuCopy32(gPlttBufferFaded + paletteOffset, gPlttBufferUnfaded + paletteOffset, PLTT_SIZEOF(16));
    }

    // Terastallization's tint
    if (GetActiveGimmick(battlerAtk) == GIMMICK_TERA)
    {
        BlendPalette(paletteOffset, 16, 8, GetTeraTypeRGB(GetBattlerTeraType(battlerAtk)));
        CpuCopy32(gPlttBufferFaded + paletteOffset, gPlttBufferUnfaded + paletteOffset, PLTT_SIZEOF(16));
    }

    gSprites[gBattlerSpriteIds[battlerAtk]].y = GetBattlerSpriteDefault_Y(battlerAtk);
    StartSpriteAnim(&gSprites[gBattlerSpriteIds[battlerAtk]], 0);
}

void BattleLoadSubstituteOrMonSpriteGfx(u8 battlerId, bool8 loadMonSprite)
{
    u8 position;
    s32 i;
    u32 palOffset;

    if (!loadMonSprite)
    {
        position = GetBattlerPosition(battlerId);
        if (GetBattlerSide(battlerId) != B_SIDE_PLAYER)
            LZDecompressVram(gSubstituteDollGfx, gMonSpritesGfxPtr->spritesGfx[position]);
        else
            LZDecompressVram(gSubstituteDollTilemap, gMonSpritesGfxPtr->spritesGfx[position]);
        for (i = 1; i < 4; ++i)
        {
            Dma3CopyLarge32_(gMonSpritesGfxPtr->spritesGfx[position], &gMonSpritesGfxPtr->spritesGfx[position][MON_PIC_SIZE * i], MON_PIC_SIZE);
        }
        palOffset = OBJ_PLTT_ID(battlerId);
        LoadCompressedPalette(gSubstituteDollPal, palOffset, PLTT_SIZE_4BPP);
    }
    else
    {
        if (GetBattlerSide(battlerId) != B_SIDE_PLAYER)
            BattleLoadMonSpriteGfx(&gEnemyParty[gBattlerPartyIndexes[battlerId]], battlerId);
        else
            BattleLoadMonSpriteGfx(&gPlayerParty[gBattlerPartyIndexes[battlerId]], battlerId);
    }
}

void LoadBattleMonGfxAndAnimate(u8 battlerId, bool8 loadMonSprite, u8 spriteId)
{
    BattleLoadSubstituteOrMonSpriteGfx(battlerId, loadMonSprite);
    StartSpriteAnim(&gSprites[spriteId], 0);
    if (!loadMonSprite)
        gSprites[spriteId].y = GetSubstituteSpriteDefault_Y(battlerId);
    else
        gSprites[spriteId].y = GetBattlerSpriteDefault_Y(battlerId);
}

void TrySetBehindSubstituteSpriteBit(u8 battlerId, u16 move)
{
    if (move == MOVE_SUBSTITUTE)
        gBattleSpritesDataPtr->battlerData[battlerId].behindSubstitute = 1;
}

void ClearBehindSubstituteBit(u8 battlerId)
{
    gBattleSpritesDataPtr->battlerData[battlerId].behindSubstitute = 0;
}

void HandleLowHpMusicChange(struct Pokemon *mon, u8 battlerId)
{
    u16 hp = GetMonData(mon, MON_DATA_HP);
    u16 maxHP = GetMonData(mon, MON_DATA_MAX_HP);

    if (GetHPBarLevel(hp, maxHP) == HP_BAR_RED)
    {
        if (!gBattleSpritesDataPtr->battlerData[battlerId].lowHpSong)
        {
            if (!gBattleSpritesDataPtr->battlerData[battlerId ^ BIT_FLANK].lowHpSong)
                PlaySE(SE_LOW_HEALTH);
            gBattleSpritesDataPtr->battlerData[battlerId].lowHpSong = 1;
        }
    }
    else
    {
        gBattleSpritesDataPtr->battlerData[battlerId].lowHpSong = 0;
        if (!IsDoubleBattle())
            m4aSongNumStop(SE_LOW_HEALTH);
        else if (IsDoubleBattle() && !gBattleSpritesDataPtr->battlerData[battlerId ^ BIT_FLANK].lowHpSong)
            m4aSongNumStop(SE_LOW_HEALTH);
    }
}

void BattleStopLowHpSound(void)
{
    u8 playerBattler = GetBattlerAtPosition(B_POSITION_PLAYER_LEFT);

    gBattleSpritesDataPtr->battlerData[playerBattler].lowHpSong = 0;
    if (IsDoubleBattle())
        gBattleSpritesDataPtr->battlerData[playerBattler ^ BIT_FLANK].lowHpSong = 0;
    m4aSongNumStop(SE_LOW_HEALTH);
}

// not used
static u8 UNUSED GetMonHPBarLevel(struct Pokemon *mon)
{
    u16 hp = GetMonData(mon, MON_DATA_HP);
    u16 maxHP = GetMonData(mon, MON_DATA_MAX_HP);

    return GetHPBarLevel(hp, maxHP);
}

void HandleBattleLowHpMusicChange(void)
{
    if (gMain.inBattle)
    {
        u8 playerBattler1 = GetBattlerAtPosition(B_POSITION_PLAYER_LEFT);
        u8 playerBattler2 = GetBattlerAtPosition(B_POSITION_PLAYER_RIGHT);
        u8 battler1PartyId = GetPartyIdFromBattlePartyId(gBattlerPartyIndexes[playerBattler1]);
        u8 battler2PartyId = GetPartyIdFromBattlePartyId(gBattlerPartyIndexes[playerBattler2]);

        if (GetMonData(&gPlayerParty[battler1PartyId], MON_DATA_HP) != 0)
            HandleLowHpMusicChange(&gPlayerParty[battler1PartyId], playerBattler1);
        if (IsDoubleBattle() && GetMonData(&gPlayerParty[battler2PartyId], MON_DATA_HP) != 0)
            HandleLowHpMusicChange(&gPlayerParty[battler2PartyId], playerBattler2);
    }
}

void SetBattlerSpriteAffineMode(u8 affineMode)
{
    s32 i;

    for (i = 0; i < gBattlersCount; ++i)
    {
        if (IsBattlerSpritePresent(i))
        {
            gSprites[gBattlerSpriteIds[i]].oam.affineMode = affineMode;
            if (affineMode == ST_OAM_AFFINE_OFF)
            {
                gBattleSpritesDataPtr->healthBoxesData[i].matrixNum = gSprites[gBattlerSpriteIds[i]].oam.matrixNum;
                gSprites[gBattlerSpriteIds[i]].oam.matrixNum = 0;
            }
            else
            {
                gSprites[gBattlerSpriteIds[i]].oam.matrixNum = gBattleSpritesDataPtr->healthBoxesData[i].matrixNum;
            }
        }
    }
}

#define tSpriteSide  data[1]
#define tBaseTileNum data[2]

#define SPRITE_SIDE_LEFT    0
#define SPRITE_SIDE_RIGHT   1

void CreateEnemyShadowSprite(u32 battler)
{
    if (B_ENEMY_MON_SHADOW_STYLE >= GEN_4 && P_GBA_STYLE_SPECIES_GFX == FALSE)
    {
        u16 species = SanitizeSpeciesId(gBattleMons[battler].species);
        u8 size = gSpeciesInfo[species].enemyShadowSize;

        gBattleSpritesDataPtr->healthBoxesData[battler].shadowSpriteIdPrimary = CreateSprite(&gSpriteTemplate_EnemyShadow,
                                                                                             GetBattlerSpriteCoord(battler, BATTLER_COORD_X),
                                                                                             GetBattlerSpriteCoord(battler, BATTLER_COORD_Y),
                                                                                             0xC8);
        if (gBattleSpritesDataPtr->healthBoxesData[battler].shadowSpriteIdPrimary < MAX_SPRITES)
        {
            struct Sprite *sprite = &gSprites[gBattleSpritesDataPtr->healthBoxesData[battler].shadowSpriteIdPrimary];
            sprite->tBattlerId = battler;
            sprite->tSpriteSide = SPRITE_SIDE_LEFT;
            sprite->tBaseTileNum = sprite->oam.tileNum;
            sprite->oam.tileNum = sprite->tBaseTileNum + (8 * size);
            sprite->invisible = TRUE;
        }

        gBattleSpritesDataPtr->healthBoxesData[battler].shadowSpriteIdSecondary = CreateSprite(&gSpriteTemplate_EnemyShadow,
                                                                                               GetBattlerSpriteCoord(battler, BATTLER_COORD_X),
                                                                                               GetBattlerSpriteCoord(battler, BATTLER_COORD_Y),
                                                                                               0xC8);
        if (gBattleSpritesDataPtr->healthBoxesData[battler].shadowSpriteIdSecondary < MAX_SPRITES)
        {
            struct Sprite *sprite = &gSprites[gBattleSpritesDataPtr->healthBoxesData[battler].shadowSpriteIdSecondary];
            sprite->tBattlerId = battler;
            sprite->tSpriteSide = SPRITE_SIDE_RIGHT;
            sprite->tBaseTileNum = sprite->oam.tileNum + 4;
            sprite->oam.tileNum = sprite->tBaseTileNum + (8 * size);
            sprite->invisible = TRUE;
        }
    }
    else
    {
        gBattleSpritesDataPtr->healthBoxesData[battler].shadowSpriteIdPrimary = CreateSprite(&gSpriteTemplate_EnemyShadow,
                                                                                             GetBattlerSpriteCoord(battler, BATTLER_COORD_X),
                                                                                             GetBattlerSpriteCoord(battler, BATTLER_COORD_Y),
                                                                                             0xC8);
        if (gBattleSpritesDataPtr->healthBoxesData[battler].shadowSpriteIdPrimary < MAX_SPRITES)
        {
            struct Sprite *sprite = &gSprites[gBattleSpritesDataPtr->healthBoxesData[battler].shadowSpriteIdPrimary];
            sprite->tBattlerId = battler;
            sprite->tBaseTileNum = sprite->oam.tileNum;
            sprite->invisible = TRUE;
        }
    }
}

void LoadAndCreateEnemyShadowSprites(void)
{
    u8 battler;
    u32 i;

    if (B_ENEMY_MON_SHADOW_STYLE >= GEN_4 && P_GBA_STYLE_SPECIES_GFX == FALSE)
    {
        LoadCompressedSpriteSheet(&gSpriteSheet_EnemyShadowsSized);

        // initialize shadow sprite ids
        for (i = 0; i < gBattlersCount; i++)
        {
            gBattleSpritesDataPtr->healthBoxesData[i].shadowSpriteIdPrimary = MAX_SPRITES;
            gBattleSpritesDataPtr->healthBoxesData[i].shadowSpriteIdSecondary = MAX_SPRITES;
        }
    }
    else
    {
        LoadCompressedSpriteSheet(&gSpriteSheet_EnemyShadow);

        // initialize shadow sprite ids
        for (i = 0; i < gBattlersCount; i++)
        {
            gBattleSpritesDataPtr->healthBoxesData[i].shadowSpriteIdPrimary = MAX_SPRITES;
        }
    }

    battler = GetBattlerAtPosition(B_POSITION_OPPONENT_LEFT);
    CreateEnemyShadowSprite(battler);

    if (IsDoubleBattle())
    {
        battler = GetBattlerAtPosition(B_POSITION_OPPONENT_RIGHT);
        CreateEnemyShadowSprite(battler);
    }
}

void SpriteCB_EnemyShadow(struct Sprite *shadowSprite)
{
    bool8 invisible = FALSE;
    u8 battler = shadowSprite->tBattlerId;
    struct Sprite *battlerSprite = &gSprites[gBattlerSpriteIds[battler]];
    u16 transformSpecies = SanitizeSpeciesId(gBattleSpritesDataPtr->battlerData[battler].transformSpecies);

    if (!battlerSprite->inUse || !IsBattlerSpritePresent(battler))
    {
        shadowSprite->callback = SpriteCB_SetInvisible;
        return;
    }

    s8 xOffset = 0, yOffset = 0, size = SHADOW_SIZE_S;
    if (gAnimScriptActive || battlerSprite->invisible)
        invisible = TRUE;
    else if (transformSpecies != SPECIES_NONE)
    {
        xOffset = gSpeciesInfo[transformSpecies].enemyShadowXOffset;
        yOffset = gSpeciesInfo[transformSpecies].enemyShadowYOffset;
        size = gSpeciesInfo[transformSpecies].enemyShadowSize;

        invisible = (B_ENEMY_MON_SHADOW_STYLE >= GEN_4 && P_GBA_STYLE_SPECIES_GFX == FALSE)
                  ? gSpeciesInfo[transformSpecies].suppressEnemyShadow
                  : gSpeciesInfo[transformSpecies].enemyMonElevation == 0;
    }
    else if (B_ENEMY_MON_SHADOW_STYLE >= GEN_4 && P_GBA_STYLE_SPECIES_GFX == FALSE)
    {
        u16 species = SanitizeSpeciesId(gBattleMons[battler].species);
        xOffset = gSpeciesInfo[species].enemyShadowXOffset + (shadowSprite->tSpriteSide == SPRITE_SIDE_LEFT ? -16 : 16);
        yOffset = gSpeciesInfo[species].enemyShadowYOffset + 16;
        size = gSpeciesInfo[species].enemyShadowSize;
    }
    else
    {
        yOffset = 29;
    }

    if (gBattleSpritesDataPtr->battlerData[battler].behindSubstitute)
        invisible = TRUE;

    shadowSprite->x = battlerSprite->x + xOffset;
    shadowSprite->x2 = battlerSprite->x2;
    shadowSprite->y = battlerSprite->y + yOffset;
    shadowSprite->invisible = invisible;

    if (B_ENEMY_MON_SHADOW_STYLE >= GEN_4 && P_GBA_STYLE_SPECIES_GFX == FALSE)
        shadowSprite->oam.tileNum = shadowSprite->tBaseTileNum + (8 * size);
}

#undef tBattlerId

void SpriteCB_SetInvisible(struct Sprite *sprite)
{
    sprite->invisible = TRUE;
}

void SetBattlerShadowSpriteCallback(u8 battler, u16 species)
{
    if (B_ENEMY_MON_SHADOW_STYLE >= GEN_4 && P_GBA_STYLE_SPECIES_GFX == FALSE)
    {
        if (GetBattlerSide(battler) == B_SIDE_PLAYER || gBattleScripting.monCaught)
        {
            gSprites[gBattleSpritesDataPtr->healthBoxesData[battler].shadowSpriteIdPrimary].callback = SpriteCB_SetInvisible;
            gSprites[gBattleSpritesDataPtr->healthBoxesData[battler].shadowSpriteIdSecondary].callback = SpriteCB_SetInvisible;
            return;
        }

        if (gBattleSpritesDataPtr->healthBoxesData[battler].shadowSpriteIdPrimary >= MAX_SPRITES
            || gBattleSpritesDataPtr->healthBoxesData[battler].shadowSpriteIdSecondary >= MAX_SPRITES)
            return;

        if (gBattleSpritesDataPtr->battlerData[battler].transformSpecies != SPECIES_NONE)
            species = gBattleSpritesDataPtr->battlerData[battler].transformSpecies;

        if (gSpeciesInfo[SanitizeSpeciesId(species)].suppressEnemyShadow == FALSE)
        {
            gSprites[gBattleSpritesDataPtr->healthBoxesData[battler].shadowSpriteIdPrimary].callback = SpriteCB_EnemyShadow;
            gSprites[gBattleSpritesDataPtr->healthBoxesData[battler].shadowSpriteIdSecondary].callback = SpriteCB_EnemyShadow;
        }
        else
        {
            gSprites[gBattleSpritesDataPtr->healthBoxesData[battler].shadowSpriteIdPrimary].callback = SpriteCB_SetInvisible;
            gSprites[gBattleSpritesDataPtr->healthBoxesData[battler].shadowSpriteIdSecondary].callback = SpriteCB_SetInvisible;
        }
    }
    else
    {
        if (GetBattlerSide(battler) == B_SIDE_PLAYER || gBattleScripting.monCaught)
        {
            gSprites[gBattleSpritesDataPtr->healthBoxesData[battler].shadowSpriteIdPrimary].callback = SpriteCB_SetInvisible;
            return;
        }

        if (gBattleSpritesDataPtr->healthBoxesData[battler].shadowSpriteIdPrimary >= MAX_SPRITES)
            return;

        if (gBattleSpritesDataPtr->battlerData[battler].transformSpecies != SPECIES_NONE)
            species = gBattleSpritesDataPtr->battlerData[battler].transformSpecies;

        if (gSpeciesInfo[SanitizeSpeciesId(species)].enemyMonElevation != 0)
            gSprites[gBattleSpritesDataPtr->healthBoxesData[battler].shadowSpriteIdPrimary].callback = SpriteCB_EnemyShadow;
        else
            gSprites[gBattleSpritesDataPtr->healthBoxesData[battler].shadowSpriteIdPrimary].callback = SpriteCB_SetInvisible;
    }
}

void HideBattlerShadowSprite(u8 battler)
{
    gSprites[gBattleSpritesDataPtr->healthBoxesData[battler].shadowSpriteIdPrimary].callback = SpriteCB_SetInvisible;
    if (B_ENEMY_MON_SHADOW_STYLE >= GEN_4 && P_GBA_STYLE_SPECIES_GFX == FALSE)
        gSprites[gBattleSpritesDataPtr->healthBoxesData[battler].shadowSpriteIdSecondary].callback = SpriteCB_SetInvisible;
}

// Low-level function that sets specific interface tiles' palettes,
// overwriting any pixel with value 0.
void BattleInterfaceSetWindowPals(void)
{
    // 9 tiles at 0x06000240
    u16 *vramPtr = (u16 *)(BG_VRAM + 0x240);
    s32 i;
    s32 j;

    for (i = 0; i < 9; ++i)
    {
        for (j = 0; j < 16; ++vramPtr, ++j)
        {
            if (!(*vramPtr & 0xF000))
                *vramPtr |= 0xF000;
            if (!(*vramPtr & 0x0F00))
                *vramPtr |= 0x0F00;
            if (!(*vramPtr & 0x00F0))
                *vramPtr |= 0x00F0;
            if (!(*vramPtr & 0x000F))
                *vramPtr |= 0x000F;
        }
    }

    // 18 tiles at 0x06000600
    vramPtr = (u16 *)(BG_VRAM + 0x600);
    for (i = 0; i < 18; ++i)
    {
        for (j = 0; j < 16; ++vramPtr, ++j)
        {
            if (!(*vramPtr & 0xF000))
                *vramPtr |= 0x6000;
            if (!(*vramPtr & 0x0F00))
                *vramPtr |= 0x0600;
            if (!(*vramPtr & 0x00F0))
                *vramPtr |= 0x0060;
            if (!(*vramPtr & 0x000F))
                *vramPtr |= 0x0006;
        }
    }
}

void ClearTemporarySpeciesSpriteData(u32 battler, bool32 dontClearTransform, bool32 dontClearSubstitute)
{
    if (!dontClearTransform)
        gBattleSpritesDataPtr->battlerData[battler].transformSpecies = SPECIES_NONE;
    if (!dontClearSubstitute)
        ClearBehindSubstituteBit(battler);
}

void AllocateMonSpritesGfx(void)
{
    u8 i = 0, j;

    gMonSpritesGfxPtr = NULL;
    gMonSpritesGfxPtr = AllocZeroed(sizeof(*gMonSpritesGfxPtr));
    gMonSpritesGfxPtr->firstDecompressed = AllocZeroed(MON_PIC_SIZE * 4 * MAX_BATTLERS_COUNT);

    for (i = 0; i < MAX_BATTLERS_COUNT; i++)
    {
        gMonSpritesGfxPtr->spritesGfx[i] = gMonSpritesGfxPtr->firstDecompressed + (i * MON_PIC_SIZE * 4);
        gMonSpritesGfxPtr->templates[i] = gBattlerSpriteTemplates[i];

        for (j = 0; j < MAX_MON_PIC_FRAMES; j++)
        {
            if (gMonSpritesGfxPtr->spritesGfx[i])
            {
                gMonSpritesGfxPtr->frameImages[i][j].data = gMonSpritesGfxPtr->spritesGfx[i] + (j * MON_PIC_SIZE);
                gMonSpritesGfxPtr->frameImages[i][j].size = MON_PIC_SIZE;
            }
        }

        gMonSpritesGfxPtr->templates[i].images = gMonSpritesGfxPtr->frameImages[i];
    }

    gMonSpritesGfxPtr->barFontGfx = AllocZeroed(0x1000);
}

void FreeMonSpritesGfx(void)
{
    if (gMonSpritesGfxPtr == NULL)
        return;

    TRY_FREE_AND_SET_NULL(gMonSpritesGfxPtr->buffer);
    FREE_AND_SET_NULL(gMonSpritesGfxPtr->barFontGfx);
    FREE_AND_SET_NULL(gMonSpritesGfxPtr->firstDecompressed);
    gMonSpritesGfxPtr->spritesGfx[B_POSITION_PLAYER_LEFT] = NULL;
    gMonSpritesGfxPtr->spritesGfx[B_POSITION_OPPONENT_LEFT] = NULL;
    gMonSpritesGfxPtr->spritesGfx[B_POSITION_PLAYER_RIGHT] = NULL;
    gMonSpritesGfxPtr->spritesGfx[B_POSITION_OPPONENT_RIGHT] = NULL;
    FREE_AND_SET_NULL(gMonSpritesGfxPtr);
}

bool32 ShouldPlayNormalMonCry(struct Pokemon *mon)
{
    s16 hp, maxHP;
    s32 barLevel;

    if (GetMonData(mon, MON_DATA_STATUS) & (STATUS1_ANY | STATUS1_TOXIC_COUNTER))
        return FALSE;

    hp = GetMonData(mon, MON_DATA_HP);
    maxHP = GetMonData(mon, MON_DATA_MAX_HP);

    barLevel = GetHPBarLevel(hp, maxHP);
    if (barLevel <= HP_BAR_YELLOW)
        return FALSE;

    return TRUE;
}
