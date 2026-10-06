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
};
