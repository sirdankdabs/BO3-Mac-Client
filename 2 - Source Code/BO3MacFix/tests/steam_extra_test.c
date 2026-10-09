#include "steam.h"
#include "t7_runtime.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
static bool friends_only;
static uint8_t packet_type;
static uint32_t packet_size=6;
static uint64_t sender_id=42;
static int lobby_type;
static bool include_sender=true;
static bool is_friend;
bool get_friends_only(void){return friends_only;}
CSteamID steam_id(void){return (CSteamID){123};}
bool steam_friends_with_user(uint64_t id){return is_friend && id==42;}
void t7_log(const char *format,...){(void)format;}
int DobbyCodePatch(void *address,uint8_t *buffer,uint32_t size){memcpy(address,buffer,size);return 0;}
static bool read_packet(void *self,void *data,uint32_t cap,uint32_t *size,CSteamID *sender,int channel){
    (void)self;(void)channel;assert(cap>=6);memset(data,0,6);((uint8_t *)data)[5]=packet_type;
    *size=packet_size;sender->id=sender_id;return true;
}
static SteamAPICall_t create(void *self,int type,int maximum){(void)self;assert(maximum==18);lobby_type=type;return 999;}
static int chat(void *self,CSteamID lobby,int id,CSteamID *sender,void *data,int capacity,int *type){
    (void)self;(void)lobby;(void)id;assert(capacity>=8);memcpy(data,"hi ^%$(",8);*type=1;sender->id=42;return 8;
}
static int count(void *self,CSteamID lobby){(void)self;(void)lobby;return 1;}
static CSteamID member(void *self,CSteamID lobby,int i){(void)self;(void)lobby;(void)i;return(CSteamID){include_sender?42:99};}
int main(void){
    void *net_table[3]={NULL,NULL,read_packet};void **net=net_table;
    void *mm_table[28]={0};void **mm=mm_table;
    mm_table[13]=create;mm_table[17]=count;mm_table[18]=member;mm_table[27]=chat;
    CSteamAPIContext ctx={.m_pSteamNetworking=&net,.m_pSteamMatchmaking=&mm};
    steam_install_extra_hooks(&ctx);steam_install_extra_hooks(&ctx);
    bool(*read_hook)(void*,void*,uint32_t,uint32_t*,CSteamID*,int)=(void*)net[2];
    uint8_t data[16];uint32_t size;CSteamID sender;
    packet_type=0x65;assert(!read_hook(&net,data,sizeof(data),&size,&sender,0)&&size==0);
    packet_type=0x66;assert(!read_hook(&net,data,sizeof(data),&size,&sender,0));
    packet_type=0x68;assert(read_hook(&net,data,sizeof(data),&size,&sender,0));
    friends_only=true;assert(!read_hook(&net,data,sizeof(data),&size,&sender,0));
    is_friend=true;assert(read_hook(&net,data,sizeof(data),&size,&sender,0));
    packet_size=99;assert(!read_hook(&net,data,sizeof(data),&size,&sender,0));packet_size=6;
    SteamAPICall_t(*create_hook)(void*,int,int)=(void*)mm[13];
    assert(create_hook(&mm,2,18)==999&&lobby_type==1);
    int(*chat_hook)(void*,CSteamID,int,CSteamID*,void*,int,int*)=(void*)mm[27];
    assert(chat_hook(&mm,(CSteamID){1},0,NULL,data,sizeof(data),NULL)==8);
    assert(!strcmp((char*)data,"hi ...("));
    include_sender=false;assert(chat_hook(&mm,(CSteamID){1},0,NULL,data,sizeof(data),NULL)==0&&data[0]==0);
    puts("Steam P2P/chat/lobby integration tests passed.");
}
