
/* dist: public */

#ifndef __PACKETS_BILLING_H
#define __PACKETS_BILLING_H

#pragma pack(push,1)

#ifdef __GNUC__
#define PACKED __attribute__((packed))
#else
#define PACKED
#endif

/* ====================================================================
 * Zone to billing
 *
 */

#define S2B_PING                 1
#define S2B_SERVER_CONNECT       2
#define S2B_SERVER_DISCONNECT    3
#define S2B_USER_LOGIN           4
#define S2B_USER_LOGOFF          5
#define S2B_USER_PRIVATE_CHAT    7
#define S2B_USER_DEMOGRAPHICS   13
#define S2B_USER_BANNER         16
#define S2B_USER_SCORE          17
#define S2B_USER_COMMAND        19
#define S2B_USER_CHANNEL_CHAT   20
#define S2B_SERVER_CAPABILITIES 21

#define BANNER_SIZE 96

struct PlayerScore {
	u16 Kills;
	u16 Deaths;
	u16 Flags;
	u32 Score;
	u32 FlagScore;
} PACKED;

struct S2B_ServerConnect {
	u8  Type;
	u32 ServerID;
	u32 GroupID;
	u32 ScoreID;
	u8  ServerName[126];
	u16 Port;
	u8  Password[32];
} PACKED;

struct S2B_UserLogin {
	u8  Type;
	u8  MakeNew;
	u32 IPAddress;
	u8  Name[32];
	u8  Password[32];
	u32 ConnectionID;
	u32 MachineID;
	i32 Timezone;
	u8  Unused0;
	u8  Sysop;
	u16 ClientVersion;
	u8  ClientExtraData[256];
} PACKED;

struct S2B_UserLogoff {
	u8  Type;
	u32 ConnectionID;
	u16 DisconnectReason;
	u16 Latency;           //not sure of particular order of these 4
	u16 Ping;
	u16 PacketLossS2C;
	u16 PacketLossC2S;
	struct PlayerScore Score;
} PACKED;

struct S2B_UserScore {
	u8  Type;
	u32 ConnectionID;
	struct PlayerScore Score;
} PACKED;

struct S2B_UserBanner {
	u8  Type;
	u32 ConnectionID;
	u8  Data[BANNER_SIZE];
} PACKED;

struct S2B_UserPrivateChat {
	u8  Type;
	u32 ConnectionID;
	u32 GroupID;
	u8  SubType;
	u8  Sound;
	u8  Text[250];
} PACKED;

struct S2B_UserChannelChat {
	u8  Type;
	u32 ConnectionID;
	u8  ChannelNr[32];
	u8  Text[250];
} PACKED;

struct S2B_UserCommand {
	u8  Type;
	u32 ConnectionID;
	u8  Text[250];
} PACKED;

struct S2B_UserDemographics {
	u8  Type;
	u32 ConnectionID;
	u8  Data[765];
} PACKED;

struct S2B_ServerCapabilities {
	u8  Type;
	u32 MultiCastChat:1;
	u32 SupportDemographics:1;
	u32 Unused:30;
} PACKED;

/* ====================================================================
 * Billing to zone
 *
 */

#define B2S_USER_LOGIN          0x01
#define B2S_USER_PRIVATE_CHAT   0x03
#define B2S_USER_KICKOUT        0x08
#define B2S_USER_COMMAND_CHAT   0x09
#define B2S_USER_CHANNEL_CHAT   0x0a
#define B2S_SCORERESET          0x31
#define B2S_USER_PACKET         0x32
#define B2S_BILLING_IDENTITY    0x33
#define B2S_USER_MCHANNEL_CHAT  0x34

enum {
	B2S_LOGIN_OK=0,B2S_LOGIN_NEWUSER=1,B2S_LOGIN_INVALIDPW=2,B2S_LOGIN_BANNED=3,
	B2S_LOGIN_NONEWCONNS=4,B2S_LOGIN_BADUSERNAME=5,B2S_LOGIN_DEMOVERSION=6,
	B2S_LOGIN_SERVERBUSY=7,B2S_LOGIN_ASKDEMOGRAPHICS=8
};

struct B2S_UserLogin {
	u8  Type;
	u8  Result;
	u32 ConnectionID;
	u8  Name[24];
	u8  Squad[24];
	u8  Banner[BANNER_SIZE];
	u32 SecondsPlayed;
	struct {
		u16 Year,Month,Day,Hour,Min,Sec;
	} PACKED FirstLogin;
	u32 Unused0;
	u32 UserID;
	u32 Unused1;
	struct PlayerScore Score; //present only if Result==OK
} PACKED;

struct B2S_UserPrivateChat {
	u8  Type;
	u32 SourceServerID;
	u8  SubType;       //2
	u8  Sound;
	u8  Text[250];
} PACKED;

struct B2S_UserKickout {
	u8  Type;
	u32 ConnectionID;
	u16 Reason;
} PACKED;

struct B2S_UserCommandChat {
	u8  Type;
	u32 ConnectionID;
	u8  Text[250];
} PACKED;

struct B2S_UserChannelChat {
	u8  Type;
	u32 ConnectionID;
	u8  ChannelNr;
	u8  Text[250];
} PACKED;

struct B2S_UserMChannelChat {
	u8  Type;
	u8  Count;
	struct Recipients {
		u32 ConnectionID;
		u8  ChanNr;
	} PACKED Recipient[45]; // repeated Count times actually, Text follows
	u8  Text[250];
} PACKED;

struct B2S_Scorereset {
	u8  Type;
	u32 ScoreID;
	u32 ScoreIDNeg;
} PACKED;

struct B2S_UserPacket {
	u8  Type;
	u32 ConnectionID;
	u8  Data[1024];
} PACKED;

struct B2S_BillingIdentity {
	u8  Type;
	u8  IDData[256];
} PACKED;

#pragma pack(pop)

#endif

