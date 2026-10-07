#include "t7_protection.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static void put32(uint8_t *p, uint32_t n) { memcpy(p,&n,4); }
int main(void) {
    uint8_t packet[6] = {0};
    for (unsigned i = 0; i < 256; ++i) {
        packet[5] = i;
        assert(t7_p2p_allowed(packet,6,false,false) == (i != 0x65 && i != 0x66 && i != 0x6d));
    }
    packet[5] = 0x68;
    assert(!t7_p2p_allowed(packet,6,true,false));
    assert(t7_p2p_allowed(packet,6,true,true));
    assert(t7_p2p_allowed(packet,5,true,false));
    assert(!t7_p2p_allowed(NULL,6,false,true));
    char chat[] = "hi^1 % $(x) [{x}]\n\xff";
    t7_sanitize_chat(chat, sizeof(chat));
    assert(!strcmp(chat,"hi.1 . .(x) .{x}].."));
    char single = '$'; t7_sanitize_chat(&single,1); assert(single == '$');
    char path[128]; memset(path,'a',sizeof(path)); path[63]=0;
    assert(t7_model_path_valid(path)); path[63]='a';path[64]=0;
    assert(!t7_model_path_valid(path)); path[32]='.';assert(t7_model_path_valid(path));
    assert(!t7_model_path_valid(NULL));
    char input[4097], output[4096]; bool changed;
    memset(input,'a',sizeof(input)); input[4096]=0;
    assert(!t7_sanitize_directives(input,output));
    assert(t7_sanitize_directives("^Bbad",output) && !strcmp(output,".Bbad"));
    assert(t7_sanitize_directives("^Bgood^",output) && !strcmp(output,"^Bgood^"));
    assert(t7_sanitize_directives("[{bad",output) && !strcmp(output,"..bad"));
    assert(t7_sanitize_model_string("bad $(x",output,&changed) && changed && !strcmp(output,"bad ..x"));
    assert(t7_sanitize_model_string("$(x)",output,&changed) && !changed);
    assert(!t7_sanitize_model_string(NULL,output,&changed));
    assert(t7_lobby_value_valid("lobbytype",2));
    assert(!t7_lobby_value_valid("lobbytype",-1));
    assert(!t7_lobby_value_valid("srcLOBBYtype",3));
    assert(!t7_lobby_value_valid("membercount",19));
    assert(!t7_lobby_value_valid("clientcount",-1));
    assert(!t7_lobby_value_valid("votecount",217));
    assert(t7_lobby_value_valid("votecount",216));
    assert(!t7_lobby_value_valid("settingssize",0xc001));
    assert(!t7_connectionless_allowed("rcon",false));
    assert(!t7_connectionless_allowed("requeststats",false));
    assert(t7_connectionless_allowed("requeststats",true));
    assert(t7_connectionless_allowed("connectResponse",false));
    assert(!t7_menu_response_allowed("endgame",false));
    assert(t7_menu_response_allowed("endgame",true));
    assert(!t7_menu_response_allowed("badspawn",true));
    const uint8_t old1b[]={0xd9,0xca,0xb7,0x6e,0xe2,5,0x28};
    const uint8_t old1c[]={0xd9,0xca,0xb7,0x6e,0xa2,3,5,0x34,0x0d,0x12};
    uint8_t script[512]; size_t offset,n; const uint8_t *replacement;
    for(int version=0x1b;version<=0x1c;++version) {
        memset(script,0,sizeof(script));
        memcpy(script,"\x80GSC\r\n\0",7);script[7]=version;
        put32(script+0x28,sizeof(script));put32(script+0x20,0x40);script[0x3a]=1;
        put32(script+0x40, version==0x1b?0xc5ba252d:0x6ba4a2e4);
        put32(script+0x44,0x80);put32(script+0x48,0x56f9ab2e);put32(script+0x4c,0xb0a87e94);
        size_t condition=0x80+(version==0x1b?0xb4:0xc4),len=version==0x1b?7:10;
        memcpy(script+condition,version==0x1b?old1b:old1c,len);
        assert(t7_campaign_patch(script,sizeof(script),&offset,&replacement,&n));
        assert(offset==condition && n==len && replacement[0]==0x6b);
        assert(!t7_campaign_patch(script,0x40,&offset,&replacement,&n));
        put32(script+0x44,UINT32_MAX);
        assert(!t7_campaign_patch(script,sizeof(script),&offset,&replacement,&n));
        put32(script+0x44,0x80);
        memcpy(script+condition,replacement,len);
        assert(!t7_campaign_patch(script,sizeof(script),&offset,&replacement,&n));
    }
    // Random malformed headers must be ignored without touching out-of-bounds memory.
    srand(42);
    for (int run=0;run<10000;++run) {
        for(size_t i=0;i<sizeof(script);++i)script[i]=(uint8_t)rand();
        size_t len=(size_t)rand()%sizeof(script);
        (void)t7_campaign_patch(script,len,&offset,&replacement,&n);
    }
    puts("T7 protection/campaign tests passed.");
}
