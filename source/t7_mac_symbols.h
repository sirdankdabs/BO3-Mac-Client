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
};
