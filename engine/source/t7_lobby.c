// Deep lobby inspectors adapted from T7Patch Protection.cpp, 46b268a.
// Opt-in because the 0x44-byte lobby-message layout needs in-game Mac testing.
#include "t7_runtime.h"
#include "dobby.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define __int16 short
#define __int32 int
#define __int64 long long
#define BOOL bool
#define STR(name) #name
static bool (*LobbyMsgRW_PackageInt)(void *, const char *, int *);
static bool (*LobbyMsgRW_PackageUChar)(void *, const char *, char *);
static bool (*LobbyMsgRW_PackageString)(void *, const char *, char *, int);
static bool (*LobbyMsgRW_PackageXuid)(void *, const char *, unsigned long long *);
static bool (*LobbyMsgRW_PackageBool)(void *, const char *, char *);
static bool (*LobbyMsgRW_PackageUInt)(void *, const char *, unsigned int *);
static bool (*LobbyMsgRW_PackageShort)(void *, const char *, short *);
static bool (*LobbyMsgRW_PackageUInt64)(void *, const char *, unsigned long long *);
static bool (*LobbyMsgRW_PackageGlob)(void *, const char *, char *, int);
static bool (*LobbyMsgRW_PackageFloat)(void *, const char *, float *);
static bool (*LobbyMsgRW_PackageArrayStart)(void *, const char *);
static bool (*LobbyMsgRW_PackageElement)(void *, bool);
static bool (*MsgMutableClientInfo_Package)(void *, void *);
int MSG_JoinParty_Package_Inspect(char* _this, char* lobbyMsg)
{
    BOOL packageOK =
        LobbyMsgRW_PackageInt(lobbyMsg, "targetlobby", (__int32*)_this)
        && LobbyMsgRW_PackageInt(lobbyMsg, STR(sourcelobby), (__int32*)(_this + 4))
        && LobbyMsgRW_PackageInt(lobbyMsg, STR(jointype), (__int32*)(_this + 8))
        && LobbyMsgRW_PackageXuid(lobbyMsg, STR(probedxuid), (unsigned __int64*)(_this + 16))
        && LobbyMsgRW_PackageInt(lobbyMsg, STR(playlistid), (__int32*)(_this + 612))
        && LobbyMsgRW_PackageInt(lobbyMsg, STR(playlistver), (__int32*)(_this + 616))
        && LobbyMsgRW_PackageInt(lobbyMsg, STR(ffotdver), (__int32*)(_this + 620))
        && LobbyMsgRW_PackageShort(lobbyMsg, STR(networkmode), (__int16*)(_this + 624))
        && LobbyMsgRW_PackageUInt(lobbyMsg, STR(netchecksum), (unsigned __int32*)(_this + 628))
        && LobbyMsgRW_PackageInt(lobbyMsg, STR(protocol), (__int32*)(_this + 632))
        && LobbyMsgRW_PackageInt(lobbyMsg, STR(changelist), (__int32*)(_this + 636))
        && LobbyMsgRW_PackageInt(lobbyMsg, STR(pingband), (__int32*)(_this + 640))
        && LobbyMsgRW_PackageUInt(lobbyMsg, STR(dlcbits), (unsigned __int32*)(_this + 644))
        && LobbyMsgRW_PackageUInt64(lobbyMsg, STR(joinnonce), (unsigned __int64*)(_this + 648));

    for (int i = 0; packageOK && (i < 3); i++)
    {
        packageOK = packageOK && LobbyMsgRW_PackageUChar(lobbyMsg, STR(chunk), (char*)(_this + i + 689));
    }

    packageOK = packageOK &&
        LobbyMsgRW_PackageBool(lobbyMsg, STR(isStarterPack), (char*)(_this + 656))
        && LobbyMsgRW_PackageString(lobbyMsg, STR(password), (char*)(_this + 657), 0x20)
        && LobbyMsgRW_PackageInt(lobbyMsg, STR(membercount), (__int32*)(_this + 24))
        && LobbyMsgRW_PackageArrayStart(lobbyMsg, STR(members));

    if (!packageOK)
    {
        // bad packet
        return 1;
    }

    if (*(__int32*)(_this + 24) < 0 || *(__int32*)(_this + 24) > 18)
    {
        // crash attempt via BoF
        return 2;
    }

    if (LobbyMsgRW_PackageElement(lobbyMsg, *(__int32*)(_this + 24) > 0))
    {
        if (*(__int32*)(_this + 24) <= 0) // this is an element even though the packet claims to have 0 members... BoF attempt!
        {
            // crash attempt via BoF
            return 3;
        }

        for (int i = 0; i < *(__int32*)(_this + 24); i++)
        {
            packageOK = packageOK
                && LobbyMsgRW_PackageXuid(lobbyMsg, STR(xuid), (unsigned __int64*)(_this + 616))
                && LobbyMsgRW_PackageUInt64(lobbyMsg, STR(lobbyid), (unsigned __int64*)(_this + 616))
                && LobbyMsgRW_PackageFloat(lobbyMsg, STR(skillrating), (float*)(_this + 616))
                && LobbyMsgRW_PackageFloat(lobbyMsg, STR(skillvariance), (float*)(_this + 616))
                && LobbyMsgRW_PackageUInt(lobbyMsg, STR(pprobation), (unsigned __int32*)(_this + 616))
                && LobbyMsgRW_PackageUInt(lobbyMsg, STR(aprobation), (unsigned __int32*)(_this + 616));

            if (!packageOK)
            {
                return 4; // bad packet
            }

            bool expected = (i + 1) < *(__int32*)(_this + 24);
            bool result = LobbyMsgRW_PackageElement(lobbyMsg, expected);
            if (result && !expected)
            {
                return 5; // BoF attempt via package element overflow
            }
        }
    }

    return 0; // packet is fine
}

int MSG_LobbyState_Package_Inspect(char* __this, char* lobbyMsg)
{
    __int32* _this = (__int32*)__this;

    BOOL packageOK =
        LobbyMsgRW_PackageInt(lobbyMsg, STR(statenum), (__int32*)_this)
        && LobbyMsgRW_PackageInt(lobbyMsg, STR(networkmode), (__int32*)(_this + 1))
        && LobbyMsgRW_PackageInt(lobbyMsg, STR(mainmode), (__int32*)(_this + 2))
        && LobbyMsgRW_PackageInt(lobbyMsg, STR(partyprivacy), (__int32*)(_this + 3))
        && LobbyMsgRW_PackageInt(lobbyMsg, STR(lobbytype), (__int32*)(_this + 4))
        && LobbyMsgRW_PackageInt(lobbyMsg, STR(lobbymode), (__int32*)(_this + 5))
        && LobbyMsgRW_PackageInt(lobbyMsg, STR(sessionstatus), (__int32*)(_this + 6))
        && LobbyMsgRW_PackageInt(lobbyMsg, STR(uiscreen), (__int32*)(_this + 7))
        && LobbyMsgRW_PackageUChar(lobbyMsg, STR(leaderactivity), (char*)_this + 32)
        && LobbyMsgRW_PackageString(lobbyMsg, STR(key), (char*)(_this + 9), 0x20)
        && LobbyMsgRW_PackageXuid(lobbyMsg, STR(leader), (unsigned __int64*)(_this + 9))
        && LobbyMsgRW_PackageXuid(lobbyMsg, STR(platformsession), (unsigned __int64*)(_this + 10))
        && LobbyMsgRW_PackageInt(lobbyMsg, STR(maxclients), (__int32*)(_this + 22))
        && LobbyMsgRW_PackageBool(lobbyMsg, STR(isadvertised), (char*)_this + 92)
        && LobbyMsgRW_PackageInt(lobbyMsg, STR(clientcount), (__int32*)(_this + 24))
        && LobbyMsgRW_PackageString(lobbyMsg, STR(sessionid), (char*)(_this + 5320), 0x40)
        && LobbyMsgRW_PackageString(lobbyMsg, STR(sessioninfo), (char*)(_this + 5336), 0x40)
        && LobbyMsgRW_PackageString(lobbyMsg, STR(ugcName), (char*)(_this + 5352), 0x20)
        && LobbyMsgRW_PackageUInt(lobbyMsg, STR(ugcVersion), (unsigned __int32*)(_this + 5360));

    if (!packageOK)
    {
        // bad packet
        return 1;
    }

    if (!LobbyMsgRW_PackageArrayStart(lobbyMsg, STR(clientlist))) return 13;

    if (_this[24] < 0 || _this[24] > 18)
    {
        // crash attempt via BoF
        return 2;
    }

    int index = 0;
    bool hasNextElement = LobbyMsgRW_PackageElement(lobbyMsg, index < _this[24]);


    while (hasNextElement && index < _this[24])
    {
        __int32 offset = 292 * index;
        __int64 offset2 = (__int64)&_this[offset + 26];

        packageOK = packageOK
            && LobbyMsgRW_PackageXuid(lobbyMsg, STR(xuid), (unsigned __int64*)(offset2))
            && LobbyMsgRW_PackageUChar(lobbyMsg, STR(clientNum), (char*)(offset2 + 8))
            && LobbyMsgRW_PackageString(lobbyMsg, STR(gamertag), (char*)(offset2 + 9), 0x20)
            && LobbyMsgRW_PackageBool(lobbyMsg, STR(isGuest), (char*)(offset2 + 41))
            && LobbyMsgRW_PackageUInt64(lobbyMsg, STR(lobbyid), (unsigned __int64*)(offset2 + 48))
            && LobbyMsgRW_PackageInt(lobbyMsg, STR(connectbit), (__int32*)(offset2 + 1108))
            && LobbyMsgRW_PackageInt(lobbyMsg, STR(score), (__int32*)(offset2 + 1104))
            && LobbyMsgRW_PackageGlob(lobbyMsg, STR(address), (char*)(offset2 + 1113), 37)
            && LobbyMsgRW_PackageInt(lobbyMsg, STR(qport), (__int32*)(offset2 + 56))
            && LobbyMsgRW_PackageUChar(lobbyMsg, STR(band), (char*)(offset2 + 60))
            && LobbyMsgRW_PackageUInt(lobbyMsg, STR(netsrc), (unsigned __int32*)(offset2 + 1152))
            && LobbyMsgRW_PackageUInt(lobbyMsg, STR(joinorder), (unsigned __int32*)(offset2 + 1156))
            && LobbyMsgRW_PackageUInt(lobbyMsg, STR(dlcBits), (unsigned __int32*)(offset2 + 1160))
            && MsgMutableClientInfo_Package((char*)(offset2 + 64), lobbyMsg);

        if (!packageOK)
        {
            return 100 + 1; // bad packet
        }

        index++;
        hasNextElement = LobbyMsgRW_PackageElement(lobbyMsg, index < _this[24]);
    }

    if (hasNextElement)
    {
        return 100 + 2; // Crash attempt via BoF and PackElem RO exploit
    }

    packageOK = packageOK
        && LobbyMsgRW_PackageUChar(lobbyMsg, STR(migratebits), (char*)_this + 21128)
        && LobbyMsgRW_PackageInt(lobbyMsg, STR(lasthosttimems), (__int32*)(_this + 5283));

    if (!packageOK)
    {
        // bad packet
        return 3;
    }

    if (!LobbyMsgRW_PackageArrayStart(lobbyMsg, STR(nomineelist))) return 13;

    __int32 count = 0;
    hasNextElement = LobbyMsgRW_PackageElement(lobbyMsg, (count < 18));

    while (hasNextElement && (count < 18))
    {
        packageOK = packageOK && LobbyMsgRW_PackageXuid(lobbyMsg, STR(xuid), (unsigned __int64*)(&_this[0x14A4 + (2 * count)]));
        count++;
        hasNextElement = LobbyMsgRW_PackageElement(lobbyMsg, (count < 18));
    }

    if (hasNextElement)
    {
        return 100 + 3; // crash attempt via PERO nominee
    }

    if (!packageOK)
    {
        // bad packet
        return 4;
    }

    return 0;
}

int MSG_LobbyStateGame_Package_Inspect(char* __this, char* lobbyMsg)
{
    int lspiResult = MSG_LobbyState_Package_Inspect(__this, lobbyMsg);

    if (lspiResult)
    {
        return lspiResult;
    }

    __int32* _this = (__int32*)__this;
    bool packageOK = LobbyMsgRW_PackageInt(lobbyMsg, STR(serverstatus), (__int32*)(_this + 5362));

    if (!packageOK)
    {
        return 11;
    }

    packageOK = packageOK
        && LobbyMsgRW_PackageInt(lobbyMsg, STR(launchnonce), (__int32*)(_this + 5363))
        && LobbyMsgRW_PackageUInt64(lobbyMsg, STR(matchhashlow), (unsigned __int64*)(_this + 2682))
        && LobbyMsgRW_PackageUInt64(lobbyMsg, STR(matchhashhigh), (unsigned __int64*)(_this + 2683))
        && LobbyMsgRW_PackageInt(lobbyMsg, STR(status), (__int32*)(_this + 5394))
        && LobbyMsgRW_PackageInt(lobbyMsg, STR(statusvalue), (__int32*)(_this + 5395))
        && LobbyMsgRW_PackageInt(lobbyMsg, STR(gamemode), (__int32*)(_this + 5368))
        && LobbyMsgRW_PackageString(lobbyMsg, STR(gametype), (char*)(_this + 5369), 0x20)
        && LobbyMsgRW_PackageString(lobbyMsg, STR(map), (char*)(_this + 5377), 0x20)
        && LobbyMsgRW_PackageString(lobbyMsg, STR(ugcName), (char*)(_this + 5385), 0x20)
        && LobbyMsgRW_PackageUInt(lobbyMsg, STR(ugcVersion), (unsigned __int32*)(_this + 5393));

    if (!_this[2])
    {
        packageOK = packageOK
            && LobbyMsgRW_PackageString(lobbyMsg, STR(cpqueuedlevel), (char*)(_this + 7293), 0x20)
            && LobbyMsgRW_PackageBool(lobbyMsg, STR(movieskipped), (char*)_this + 29204);
    }

    __int32 lobbyMode = _this[5];
    if (lobbyMode < 0 || lobbyMode > 4) return 14;

    if (!lobbyMode)
    {
        if (!LobbyMsgRW_PackageArrayStart(lobbyMsg, STR(clientlist))) return 13;
        int index = 0;
        bool hasNextElement = LobbyMsgRW_PackageElement(lobbyMsg, (index < 18));
        while (hasNextElement && (index < 18))
        {
            __int64 currentClient = (__int64)&_this[9 * index];
            packageOK = packageOK
                && LobbyMsgRW_PackageInt(lobbyMsg, STR(team), (__int32*)(currentClient + 28524))
                && LobbyMsgRW_PackageInt(lobbyMsg, STR(mapvote), (__int32*)(currentClient + 28528))
                && LobbyMsgRW_PackageBool(lobbyMsg, STR(readyup), (char*)(currentClient + 28532));

            if (!packageOK)
            {
                // bad pack
                return 20 + 1;
            }

            index++;
            hasNextElement = LobbyMsgRW_PackageElement(lobbyMsg, (index < 18));
        }

        if (hasNextElement)
        {
            // PERO
            return 20 + 2;
        }

        packageOK = packageOK
            && LobbyMsgRW_PackageInt(lobbyMsg, STR(plistid), (__int32*)(_this + 7126))
            && LobbyMsgRW_PackageInt(lobbyMsg, STR(plistcurr), (__int32*)(_this + 7127))
            && LobbyMsgRW_PackageGlob(lobbyMsg, STR(plistentries), (char*)_this + 28512, 8)
            && LobbyMsgRW_PackageUChar(lobbyMsg, STR(plistnext), (char*)_this + 28520)
            && LobbyMsgRW_PackageUChar(lobbyMsg, STR(plistprev), (char*)_this + 28521)
            && LobbyMsgRW_PackageUChar(lobbyMsg, STR(plistprevcount), (char*)_this + 28522);

        if (!packageOK)
        {
            // bad packet
            return 5;
        }

        goto PackageOK;
    }

    lobbyMode--;
    if (!lobbyMode)
    {
        packageOK = packageOK && LobbyMsgRW_PackageInt(lobbyMsg, STR(votecount), (__int32*)(_this + 5397));
        if (!LobbyMsgRW_PackageArrayStart(lobbyMsg, STR(votes))) return 13;

        int numVotes = *(__int32*)(_this + 5397);
        if (packageOK && (numVotes < 0 || numVotes > 216))
        {
            // crash attempt via votecount BoF
            return 20 + 6;
        }

        int index = 0;
        bool hasNextElement = LobbyMsgRW_PackageElement(lobbyMsg, (index < numVotes));

        while (hasNextElement && (index < numVotes))
        {
            __int64 currentVote = (__int64)(&_this[8 * index + 5398]);
            packageOK = packageOK
                && LobbyMsgRW_PackageShort(lobbyMsg, STR(itemtype), (__int16*)(currentVote + 8))
                && LobbyMsgRW_PackageUInt(lobbyMsg, STR(item), (unsigned int*)(currentVote + 12))
                && LobbyMsgRW_PackageShort(lobbyMsg, STR(itemgroup), (__int16*)(currentVote + 16))
                && LobbyMsgRW_PackageShort(lobbyMsg, STR(attachment), (__int16*)(currentVote + 20))
                && LobbyMsgRW_PackageShort(lobbyMsg, STR(votetype), (__int16*)(currentVote + 24))
                && LobbyMsgRW_PackageXuid(lobbyMsg, STR(votexuid), (unsigned __int64*)(currentVote));

            if (!packageOK)
            {
                return 6; // bad packet
            }

            index++;
            hasNextElement = LobbyMsgRW_PackageElement(lobbyMsg, (index < numVotes));
        }

        if (hasNextElement)
        {
            // PERO
            return 20 + 3;
        }

        if (!LobbyMsgRW_PackageArrayStart(lobbyMsg, STR(clientlist))) return 13;
        index = 0;
        hasNextElement = LobbyMsgRW_PackageElement(lobbyMsg, (index < 18));
        while (hasNextElement && (index < 18))
        {
            __int64 currentClient = (__int64)&_this[index + 8 * index + 7131];
            packageOK = packageOK
                && LobbyMsgRW_PackageInt(lobbyMsg, STR(team), (__int32*)(currentClient))
                && LobbyMsgRW_PackageInt(lobbyMsg, STR(pregamepos), (__int32*)(currentClient + 4))
                && LobbyMsgRW_PackageInt(lobbyMsg, STR(pregamestate), (__int32*)(currentClient + 3))
                && LobbyMsgRW_PackageUInt(lobbyMsg, STR(clvotecount), (unsigned __int32*)(currentClient + 6))
                && LobbyMsgRW_PackageUInt(lobbyMsg, STR(character), (unsigned __int32*)(currentClient + 7))
                && LobbyMsgRW_PackageUInt(lobbyMsg, STR(loadout), (unsigned __int32*)(currentClient + 8));

            if (!packageOK)
            {
                // bad packet
                return 7;
            }

            index++;
            hasNextElement = LobbyMsgRW_PackageElement(lobbyMsg, (index < 18));
        }

        if (hasNextElement)
        {
            // PERO
            return 20 + 4;
        }

        packageOK = packageOK && LobbyMsgRW_PackageInt(lobbyMsg, STR(settingssize), (__int32*)(_this + 7350));

        if ((_this[7350] <= 0) || (_this[7350] > 0xC000))
        {
            // BoF attempt
            return 20 + 5;
        }

        goto PackageOK;
    }

    lobbyMode--;
    if (!lobbyMode)
    {
        goto PackageOK;
    }

    if (lobbyMode == 1)
    {
        packageOK = packageOK
            && LobbyMsgRW_PackageInt(lobbyMsg, STR(compstate), _this + 5396)
            && LobbyMsgRW_PackageInt(lobbyMsg, STR(votecount), _this + 5397);

        if (!packageOK)
        {
            // bad packet
            return 10;
        }


        __int32 numVotes = _this[5397];
        if (_this[5397] < 0 || _this[5397] > 216)
        {
            return 20 + 7;
        }

        int index = 0;
        bool hasNextElement = LobbyMsgRW_PackageElement(lobbyMsg, (index < numVotes));

        while (hasNextElement && (index < numVotes))
        {
            __int64 currentVote = (__int64)(&_this[8 * index + 5398]);
            packageOK = packageOK
                && LobbyMsgRW_PackageShort(lobbyMsg, STR(itemtype), (__int16*)(currentVote + 8))
                && LobbyMsgRW_PackageUInt(lobbyMsg, STR(item), (unsigned int*)(currentVote + 12))
                && LobbyMsgRW_PackageShort(lobbyMsg, STR(itemgroup), (__int16*)(currentVote + 16))
                && LobbyMsgRW_PackageShort(lobbyMsg, STR(attachment), (__int16*)(currentVote + 20))
                && LobbyMsgRW_PackageShort(lobbyMsg, STR(votetype), (__int16*)(currentVote + 24))
                && LobbyMsgRW_PackageXuid(lobbyMsg, STR(votexuid), (unsigned __int64*)(currentVote));

            if (!packageOK)
            {
                return 8; // bad packet
            }

            index++;
            hasNextElement = LobbyMsgRW_PackageElement(lobbyMsg, (index < numVotes));
        }

        if (hasNextElement)
        {
            // PERO
            return 20 + 8;
        }

        if (!LobbyMsgRW_PackageArrayStart(lobbyMsg, STR(clientlist))) return 13;
        index = 0;
        hasNextElement = LobbyMsgRW_PackageElement(lobbyMsg, (index < 18));

        while (hasNextElement && (index < 18))
        {
            __int64 currentClient = (__int64)&_this[index + 8 * index + 7131];
            packageOK = packageOK
                && LobbyMsgRW_PackageInt(lobbyMsg, STR(team), (__int32*)(currentClient))
                && LobbyMsgRW_PackageInt(lobbyMsg, STR(pregamepos), (__int32*)(currentClient + 4))
                && LobbyMsgRW_PackageInt(lobbyMsg, STR(pregamestate), (__int32*)(currentClient + 3))
                && LobbyMsgRW_PackageUInt(lobbyMsg, STR(clvotecount), (unsigned __int32*)(currentClient + 6))
                && LobbyMsgRW_PackageUInt(lobbyMsg, STR(character), (unsigned __int32*)(currentClient + 7))
                && LobbyMsgRW_PackageUInt(lobbyMsg, STR(loadout), (unsigned __int32*)(currentClient + 8));

            if (!packageOK)
            {
                // bad packet
                return 9;
            }

            index++;
            hasNextElement = LobbyMsgRW_PackageElement(lobbyMsg, (index < 18));
        }

        if (hasNextElement)
        {
            // PERO
            return 20 + 9;
        }

        goto PackageOK;
    }

PackageOK:

    return packageOK ? 0 : 12;
}

int MSG_HostHeartbeat_Inspect(char* __this, char* lobbyMsg)
{
    bool packageOK = LobbyMsgRW_PackageInt(lobbyMsg, STR(heartbeatnum), (__int32*)__this)
        && LobbyMsgRW_PackageInt(lobbyMsg, STR(lobbytype), (__int32*)__this)
        && LobbyMsgRW_PackageInt(lobbyMsg, STR(lasthosttimems), (__int32*)__this);

    if (!packageOK) // invalid packet
    {
        return 1;
    }

    if (!LobbyMsgRW_PackageArrayStart(lobbyMsg, STR(nomineelist))) return 13;

    __int32 count = 0;
    bool hasNextElement = LobbyMsgRW_PackageElement(lobbyMsg, (count < 18));
    while (hasNextElement && (count < 18))
    {
        packageOK = packageOK && LobbyMsgRW_PackageXuid(lobbyMsg, STR(xuid), (unsigned __int64*)__this);
        count++;
        hasNextElement = LobbyMsgRW_PackageElement(lobbyMsg, (count < 18));
    }

    if (hasNextElement)
    {
        return 100 + 1; // crash attempt via PERO nominee
    }

    if (!packageOK)
    {
        // bad packet
        return 2;
    }

    return 0;
}


typedef struct {
    unsigned char overflowed, readOnly;
    short pad1; int pad2;
    char *data, *splitData;
    unsigned maxsize, cursize, splitSize, readcount;
    int bit, lastEntityRef, flush, targetLocalNetID;
} msg_t;
typedef struct { msg_t msg; int msgType; char encodeFlags; int packageType; } LobbyMsg;
typedef struct {
    char isValid;
    unsigned long long hostXuid;
    char hostName[32];
    struct { unsigned long long id; } secId;
    struct { char ab[16]; } secKey;
    struct { char addrBuf[37]; } serializedAdr;
    struct { int networkMode, mainMode; } lobbyParams;
    char ugcName[32]; unsigned ugcVersion;
} InfoResponseLobby;
typedef struct { unsigned nonce; int uiScreen; char natType; InfoResponseLobby lobby[2]; } Msg_InfoResponse;
_Static_assert(sizeof(msg_t) == 0x38, "Experimental message layout");

int MSG_InfoResponseSafe(Msg_InfoResponse* infoResponse, LobbyMsg* lm)
{
    bool packIsOK = true;
    packIsOK = LobbyMsgRW_PackageUInt(lm, "nonce", (unsigned __int32*)&infoResponse->nonce)
        && LobbyMsgRW_PackageInt(lm, "uiscreen", &infoResponse->uiScreen)
        && LobbyMsgRW_PackageUChar(lm, "nattype", &infoResponse->natType);

    if (!packIsOK)
    {
        return 1;
    }

    if (!LobbyMsgRW_PackageArrayStart(lm, "lobbies")) return 13;


    bool hasNextElement = LobbyMsgRW_PackageElement(lm, true);
    bool hasNextElement2 = false;
    for (__int32 cResponse = 0; hasNextElement && (cResponse < 2); cResponse++)
    {
        if (!LobbyMsgRW_PackageArrayStart(lm, "lobbies")) return 13;

        hasNextElement2 = LobbyMsgRW_PackageElement(lm, true);
        for (__int32 i = 0; hasNextElement2 && (i < 2); i++)
        {
            packIsOK = packIsOK && LobbyMsgRW_PackageBool(lm, "valid", &infoResponse->lobby[cResponse].isValid);

            if (!packIsOK)
            {
                return 2;
            }

            if (!infoResponse->lobby[cResponse].isValid)
            {
                hasNextElement2 = LobbyMsgRW_PackageElement(lm, (i < 2));
                continue;
            }

            packIsOK = packIsOK
                && LobbyMsgRW_PackageXuid(lm, "hostxuid", (unsigned __int64*)&infoResponse->lobby[cResponse].hostXuid)
                && LobbyMsgRW_PackageString(lm, "hostname", infoResponse->lobby[cResponse].hostName, 32)
                && LobbyMsgRW_PackageInt(lm, "networkmode", &infoResponse->lobby[cResponse].lobbyParams.networkMode)
                && LobbyMsgRW_PackageInt(lm, "mainmode", &infoResponse->lobby[cResponse].lobbyParams.mainMode)
                && LobbyMsgRW_PackageGlob(lm, "secid", (char*)&infoResponse->lobby[cResponse].secId.id, 8)
                && LobbyMsgRW_PackageGlob(lm, "seckey", infoResponse->lobby[cResponse].secKey.ab, 16)
                && LobbyMsgRW_PackageGlob(lm, "addrbuff", infoResponse->lobby[cResponse].serializedAdr.addrBuf, 37)
                && LobbyMsgRW_PackageString(lm, "ugcName", infoResponse->lobby[cResponse].ugcName, 32)
                && LobbyMsgRW_PackageUInt(lm, "ugcVersion", (unsigned __int32*)&infoResponse->lobby[cResponse].ugcVersion);

            if (!packIsOK)
            {
                return 3;
            }
            hasNextElement2 = LobbyMsgRW_PackageElement(lm, (i < 2));
        }

        if (hasNextElement2)
        {
            return 5;
        }

        hasNextElement = LobbyMsgRW_PackageElement(lm, (cResponse < 2));
    }

    if (hasNextElement || lm->msg.overflowed)
    {
        return 4;
    }

    return 0;
}


static bool inspector_active;
static bool inspect_as(void *message, int override_type);
static bool (*orig_info_response)(void *, void *);
static bool (*orig_heartbeat)(void *, void *);
static bool guarded_info_response(void *output, void *message) {
    return inspect_as(message, 0x10000) && orig_info_response(output, message);
}
static bool guarded_heartbeat(void *output, void *message) {
    return inspect_as(message, 0x10001) && orig_heartbeat(output, message);
}

void t7_init_lobby_inspector(void) {
    const char *enabled = getenv("BO3MACFIX_EXPERIMENTAL_LOBBY_LAYOUT");
    if (!enabled || strcmp(enabled, "1")) {
        t7_log("[BO3MacFix] Deep lobby inspectors: opt-in disabled (unverified Mac layout).\n");
        return;
    }
    bool complete = true;
#define RESOLVE(name) do { name = (void *)t7_resolve_function(#name); if (!name) { complete = false; t7_log("[BO3MacFix] Inspector missing %s\n", #name); } } while (0)
    RESOLVE(LobbyMsgRW_PackageInt);
    RESOLVE(LobbyMsgRW_PackageUChar);
    RESOLVE(LobbyMsgRW_PackageString);
    RESOLVE(LobbyMsgRW_PackageXuid);
    RESOLVE(LobbyMsgRW_PackageBool);
    RESOLVE(LobbyMsgRW_PackageUInt);
    RESOLVE(LobbyMsgRW_PackageShort);
    RESOLVE(LobbyMsgRW_PackageUInt64);
    RESOLVE(LobbyMsgRW_PackageGlob);
    RESOLVE(LobbyMsgRW_PackageFloat);
    RESOLVE(LobbyMsgRW_PackageArrayStart);
    RESOLVE(LobbyMsgRW_PackageElement);
    RESOLVE(MsgMutableClientInfo_Package);
    inspector_active = complete;
    if (complete) {
        void *info = t7_resolve_function("MSG_InfoResponse");
        if (info) t7_log("[BO3MacFix] Deep info-response hook: %s\n",
            !DobbyHook(info, guarded_info_response, (void **)&orig_info_response) ? "active" : "failed");
        else t7_log("[BO3MacFix] Deep info-response hook: missing symbol.\n");
        void *heartbeat = t7_resolve_function("MSG_HostHeartbeat_Package");
        if (heartbeat) t7_log("[BO3MacFix] Deep heartbeat hook: %s\n",
            !DobbyHook(heartbeat, guarded_heartbeat, (void **)&orig_heartbeat) ? "active" : "failed");
        else t7_log("[BO3MacFix] Deep heartbeat hook: missing symbol.\n");
    }
    t7_log("[BO3MacFix] Deep lobby inspectors: %s\n", complete ? "experimental active" : "missing dependencies");
}
static bool inspect_as(void *message, int override_type) {
    if (!inspector_active) return true;
    if (!t7_memory_readable(message, 0x44)) return false;
    union { uint64_t alignment; unsigned char bytes[0x48]; } copy = {0};
    memcpy(copy.bytes, message, 0x44);
    // Sanity-check the opted-in layout before passing its copy to serializers.
    unsigned char *payload; uint32_t capacity, length, read, split; int32_t type;
    memcpy(&payload, copy.bytes + 8, 8);
    memcpy(&capacity, copy.bytes + 0x18, 4);
    memcpy(&length, copy.bytes + 0x1c, 4);
    memcpy(&split, copy.bytes + 0x20, 4);
    memcpy(&read, copy.bytes + 0x24, 4);
    memcpy(&type, copy.bytes + 0x38, 4);
    if (copy.bytes[0] || copy.bytes[1] > 1 || read > length || length > capacity ||
        capacity > 1024 * 1024 || split || !t7_memory_readable(payload, length)) return false;
    // Isolate the inspected payload too: never let the dry run mutate the real
    // packet even if a game serializer unexpectedly uses write mode.
    unsigned char *payload_copy = calloc(1, capacity ? capacity : 1);
    if (!payload_copy) return false;
    memcpy(payload_copy, payload, length);
    memcpy(copy.bytes + 8, &payload_copy, 8);
    // Serialized output buffers are local to each call, avoiding upstream's
    // shared scratch races. Largest upstream output is below 0x20000 bytes.
    union { uint64_t alignment; char bytes[0x20000]; } *scratch = calloc(1, sizeof(*scratch));
    if (!scratch) { free(payload_copy); return false; }
    int result = 0;
    if (override_type >= 0) type = override_type;
    switch (type) {
        case 0x10000: result = MSG_InfoResponseSafe((Msg_InfoResponse *)scratch->bytes, (LobbyMsg *)copy.bytes); break;
        case 0x10001: result = MSG_HostHeartbeat_Inspect(scratch->bytes, (char *)copy.bytes); break;
        case 0x10: result = MSG_JoinParty_Package_Inspect(scratch->bytes, (char *)copy.bytes); break;
        case 2: result = MSG_LobbyState_Package_Inspect(scratch->bytes, (char *)copy.bytes); break;
        case 3: result = MSG_LobbyStateGame_Package_Inspect(scratch->bytes, (char *)copy.bytes); break;
        case 0xf: case 0x1e: case 0x16: case 0x17: case 0x18: result = 1; break;
        default: break;
    }
    if (copy.bytes[0]) result = 1;
    free(scratch); free(payload_copy);
    return result == 0;
}

bool t7_inspect_lobby(void *message) { return inspect_as(message, -1); }
