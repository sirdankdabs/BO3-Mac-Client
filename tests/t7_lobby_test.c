// Exercise the actual copied inspectors with controlled game-serializer callbacks.
#include "../source/t7_lobby.c"
#include <assert.h>
#include <stdio.h>
static int members, clients, votes, mode, settings=1, elements;
static bool parser_ok=true;
void *t7_resolve_function(const char *name){(void)name;return NULL;}
void t7_log(const char *fmt,...){(void)fmt;}
bool t7_memory_readable(const void *p,size_t n){(void)n;return p!=NULL;}
int DobbyHook(void *a,void *b,void **c){(void)a;(void)b;(void)c;return -1;}
static bool integer(void *m,const char *key,int *p){
    (void)m;int value=0;
    if(!strcmp(key,"membercount"))value=members;
    if(!strcmp(key,"clientcount"))value=clients;
    if(!strcmp(key,"votecount"))value=votes;
    if(!strcmp(key,"lobbymode"))value=mode;
    if(!strcmp(key,"settingssize"))value=settings;
    memcpy(p,&value,4);return parser_ok;
}
#define SCALAR(name,type) static bool name(void *m,const char *key,type *p){(void)m;(void)key;memset(p,0,sizeof(*p));return parser_ok;}
SCALAR(uinteger,unsigned int)
SCALAR(byte,char)
SCALAR(short_value,short)
SCALAR(long_value,unsigned long long)
SCALAR(float_value,float)
static bool string(void*m,const char*key,char*p,int n){(void)m;(void)key;if(n>0)memset(p,0,n);return parser_ok;}
static bool array(void*m,const char*key){(void)m;(void)key;return parser_ok;}
static bool element(void*m,bool add){(void)m;(void)add;if(elements>0){--elements;return true;}return false;}
static bool mutable(void*out,void*m){(void)out;(void)m;return parser_ok;}
int main(void){
    LobbyMsgRW_PackageInt=integer;LobbyMsgRW_PackageUInt=uinteger;
    LobbyMsgRW_PackageUChar=byte;LobbyMsgRW_PackageBool=byte;
    LobbyMsgRW_PackageString=string;LobbyMsgRW_PackageGlob=string;
    LobbyMsgRW_PackageShort=short_value;LobbyMsgRW_PackageXuid=long_value;
    LobbyMsgRW_PackageUInt64=long_value;LobbyMsgRW_PackageFloat=float_value;
    LobbyMsgRW_PackageArrayStart=array;LobbyMsgRW_PackageElement=element;
    MsgMutableClientInfo_Package=mutable;
    union{uint64_t align;char data[0x20000];} scratch={0};
    char message[0x48]={0};
    members=19;assert(MSG_JoinParty_Package_Inspect(scratch.data,message)!=0);
    members=-1;assert(MSG_JoinParty_Package_Inspect(scratch.data,message)!=0);
    members=0;elements=1;assert(MSG_JoinParty_Package_Inspect(scratch.data,message)!=0);
    elements=0;assert(MSG_JoinParty_Package_Inspect(scratch.data,message)==0);
    clients=19;assert(MSG_LobbyState_Package_Inspect(scratch.data,message)!=0);
    clients=-1;assert(MSG_LobbyState_Package_Inspect(scratch.data,message)!=0);
    clients=0;mode=1;votes=217;assert(MSG_LobbyStateGame_Package_Inspect(scratch.data,message)!=0);
    votes=-1;assert(MSG_LobbyStateGame_Package_Inspect(scratch.data,message)!=0);
    votes=0;settings=0xc001;assert(MSG_LobbyStateGame_Package_Inspect(scratch.data,message)!=0);
    settings=1;assert(MSG_LobbyStateGame_Package_Inspect(scratch.data,message)==0);
    elements=19;assert(MSG_HostHeartbeat_Inspect(scratch.data,message)!=0);
    elements=18;assert(MSG_HostHeartbeat_Inspect(scratch.data,message)==0);
    parser_ok=false;assert(MSG_HostHeartbeat_Inspect(scratch.data,message)!=0);
    Msg_InfoResponse response={0};LobbyMsg lm={0};
    assert(MSG_InfoResponseSafe(&response,&lm)!=0);
    parser_ok=true;elements=0;assert(MSG_InfoResponseSafe(&response,&lm)==0);
    assert(t7_inspect_lobby(NULL)); // Optional layout-dependent mode is off by default.
    inspector_active=true;assert(!t7_inspect_lobby(NULL));
    unsigned char payload[16]={0};lm.msg.data=(char*)payload;
    lm.msg.maxsize=16;lm.msg.cursize=16;lm.msg.readcount=17;
    assert(!t7_inspect_lobby(&lm));
    lm.msg.readcount=0;lm.msgType=0xf;assert(!t7_inspect_lobby(&lm));
    lm.msgType=0x10;members=0;assert(t7_inspect_lobby(&lm));
    assert(lm.msg.data==(char*)payload&&lm.msg.readcount==0);
    puts("Deep lobby inspector tests passed.");
}
