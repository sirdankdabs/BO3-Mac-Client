// Generated from CoDBlkOps3_Exe (Mac v1.0.0.0, in-game v98) by static analysis.
// Each entry is accepted only if the executable SHA-1 matches and the first
// 16 bytes at the address equal the recorded prologue.
#define T7_MAC_SYMBOLS_SHA1 "5e42371b6a16f2b11e637f48e38554625570aa9f"
static const struct { const char *name; uint64_t rva; const char *prologue; } t7_mac_symbols[] = {
    {"LobbyMsgRW_PackageInt", 0xf41d33, "4889f08b4f4085c9742683f901752455"}, // type-0 wrapper of LobbyMsgRW_WriteBasicElement
    {"LobbyMsgRW_PackageUInt", 0xf41db0, "4889f08b4f4085c9742983f901752755"}, // type-1 wrapper; callers pass "datamask"
    {"LobbyMsgRW_PackageUChar", 0xf41fc0, "4889f08b4f4085c9742983f901752755"}, // type-5 wrapper; callers pass "nattype"
    {"LobbyMsgRW_PrintMessage", 0xf417ae, "554889e5534883ec184889fb4084f675"}, // "Lobby Message Dump [%s]"
    {"LobbyMsgRW_ReadDebugSection", 0xf41836, "554889e5415741564155415453504989"}, // recursive "Lobby Message Debug Section" reader
    {"CL_ConnectionlessCMD", 0xe19402, "554889e54157415641554154534881ec"}, // tokenizes and dispatches "connectResponse", "requeststats", ...
    {"CMD_MenuResponse_f", 0xf451d1, "554889e54156534881ec200d00004989"}, // "badspawn", "killserverpc", "banClient %i"
    {"SV_Cmd_ArgvBuffer", 0xf75cfb, "554889e54863c2488d0db76666114863"}, // sv_cmd_args argv copy
    {"CL_GetConfigString", 0xe2e90a, "554889e54863c7488d0d985c70104863"}, // called with 0xe2b (mspreload config string)
    {"MSG_BeginReading", 0x1247036, "554889e531c089078947248947285dc3"}, // zeroes overflowed/readcount/bit
    {"MSG_ReadLong", 0x1247a49, "554889e54156534883ec104889fb4863"}, // 4-byte read, sets overflowed
    {"MSG_ReadStringLine", 0x1247c28, "554889e5415741564155415453504189"}, // MSG_ReadByte loop stopping at '\n'
    {"Cbuf_AddText", 0xf754aa, "554889e541574156534883ec184989f7"}, // same address as upstream offsets.h
    {"SEH_ParseNextDirective", 0xf47819, "554889e54157415641554154534881ec"}, // "[{"/"}]" + KEY_UNBOUND parser behind SEH_ReplaceDirectiveInStringWithBinding (0xf47799)
    {"SEH_ParseNextDirectiveShort", 0x12c8e15, "554889e54157415641554154534881ec"}, // second "[{"/"}]" parser (256-byte callers, FAKE_INTRO_SECONDS)
    {"UI_DoModelStringReplacement", 0xd50fdc, "554889e54157415641554154534881ec"}, // "$(" ... ")" model substitution, "<InvalidModel>"
    {"UI_Model_ResolvePath", 0x1036426, "554889e54157415641554154534883ec"}, // shared walker of GetModelFromPath (0x1036418) / CreateModelFromPath (0x10365c3, 0x10365d4)
    {"UI_Model_AllocateNode", 0x1036158, "554889e54157415641554154534883ec"}, // "EXE_LUI_OUT_OF_MEMORY", free list head at 0x431d728
    {"LiveInvites_AcceptInvite", 0x134b1b9, "554889e5415741564155415453504889"}, // "Accepting invite from a friend id %llu"
    {"LiveInvites_JoinMessageAction", 0x134bbf3, "554889e5415741564154534883ec7049"}, // "Got a Join request message from %016llx"
    {"LiveInvites_SendJoinInfo", 0x134bdb2, "554889e541574156534881eca8000000"}, // "Sending JOIN_REPLY info to %llu"
    {"Scr_GscObjLink", 0x149293c, "554889e54157415641554154534881ec"}, // "Invalid gscobj file '%s'", GSC version 0x1C
    {"CL_SwitchState_Idle_Update", 0x131489c, "554889e54157415641554154534881ec"}, // idle update slot of the preload state table at 0x1b78e98
    {"LiveSteam_InitServer", 0xf6956d, "554889e54157415641554154534883ec"}, // "STEAM_SERVER: Init IP: %s:%u"
    {"UI_BrowserOpen", 0xe0e2da, "554889e5488b7750483b7748730c31d2"}, // Lua "OpenURL" -> Sys_OpenURL (0xd62926)
    {"Mods_SubscribeUGC", 0xe4971e, "554889e5535031db31f6ba0a000000e8"}, // native behind Lua "Mods_SubscribeUGC" (0x13abdaa)
    {"LobbyTypes_GetMsgTypeName", 0x1311e7c, "554889e583ffff74104863c7488d0d61"}, // unbounded lookup in the 33-entry name table at 0x1b78cf0
    {"LivePresence_Serialize", 0x15312a1, "554889e54157415641554154534883ec"}, // "Presence %s received data(%qu) smaller than expected"
    {"LiveSteam_ServerFrame", 0xf6908e, "554889e553504889fbe896d96b00e876"}, // "STEAM_SERVER: Frame: Init!" state machine, called every frame via 0xf6a7ce
    {"GScr_AddRankXP", 0x13e97f4, "554889e54157415641554154534881ec"}, // "AddRankXP: score type %s not found" (diagnostics only)
    {"SDL_OpenAudioDeviceInternal", 0x12c570, "554889e54157415641554154534881ec"}, // statically linked SDL2 open_audio_device ("No capture support")
    {"CL_CheckForResend", 0xe17e98, "554889e54157415641554154534881ec"}, // "getchallenge", builds 'connect "..."'
    {"NET_OutOfBandData", 0x14022b6, "554889e54157415653504489c34989d0"}, // (sock, netadr, data, len)
    {"NET_OutOfBandPrint", 0x14021e9, "554889e5415741564155415453504889"}, // (sock, netadr, text)
    {"CL_LaunchClient", 0xd6f75d, "554889e54157415653504989d74989f6"}, // (map, gametype, &error) -> CL_Connect
    {"CL_GetNetSrc", 0xeae661, "554889e54863f7488d0cf6488d15fd2c"}, // client -> socket id
};
