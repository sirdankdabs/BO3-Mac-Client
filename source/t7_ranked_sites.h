// Call sites of Mods_IsModsLoaded (0xe4962f) in the Mac v98 executable that
// gate ranked/stat tracking, matched to the T7Patch 3.09 (Scropts-QOL) list.
// Each must still be "call 0xe4962f" before it is patched.
static const struct { uint64_t rva; const char *what; } t7_ranked_sites[] = {
    {0x127afde, "session mode: online -> custom when modded"},
    {0x127aff8, "session mode: mode mapping when modded"},
    {0x127b043, "session mode: public online game"},
    {0x127b08c, "session mode: ranked game"},
    {0x127b0c8, "session mode: ranked/public eligibility"},
    {0x13e7228, "AddWeaponStat"},
    {0x13e784b, "AddSpecialistStat"},
    {0x13ea3d1, "DoubleXPTimerFired"},
    {0x13ea577, "ReportLootReward"},
    {0x13ea645, "ReportLootConsume"},
    {0x13ea868, "ReportLootConsume (client path)"},
    {0x118c99c, "giveachievement"},
    {0x1082535, "LiveStats first-time flow"},
    {0x1113a16, "playlist default event rules (xpGroups)"},
    {0x1113abe, "playlist custom event rules"},
    {0x11150e7, "gametype rules setup"},
    {0x1115264, "gametype rules setup (second path)"},
};

// Call sites of Mods_GetModName (0xe4963f) in the stats storage manager. With
// the ranked spoof they are redirected to 0xf16aae, a game function returning
// "", so stats storage stays on the normal profile while a mod is loaded.
#define T7_EMPTY_STRING_FN 0xf16aae
#define T7_GET_MOD_NAME_FN 0xe4963f
static const struct { uint64_t rva; const char *what; } t7_storage_modname_sites[] = {
    {0x1102e2b, "storage assignment"},
    {0x1103576, "mod change check"},
    {0x110358e, "mod change log"},
    {0x1103750, "mod change (second path)"},
};
