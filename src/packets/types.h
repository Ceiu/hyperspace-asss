
/* dist: public */

#ifndef __PACKETS_TYPES_H
#define __PACKETS_TYPES_H


/* S2C PACKET TYPES */
#define S2C_ZERO 0x00
#define S2C_WHOAMI 0x01
#define S2C_ENTERINGARENA 0x02
#define S2C_PLAYERENTERING 0x03
#define S2C_PLAYERLEAVING 0x04
#define S2C_WEAPON 0x05
#define S2C_KILL 0x06
#define S2C_CHAT 0x07
#define S2C_GREEN 0x08
#define S2C_SCOREUPDATE 0x09
#define S2C_LOGINRESPONSE 0x0A
#define S2C_SOCCERGOAL 0x0B
#define S2C_VOICE 0x0C
#define S2C_FREQCHANGE 0x0D
#define S2C_TURRET 0x0E
#define S2C_SETTINGS 0x0F
#define S2C_INCOMINGFILE 0x10
/* subspace client does no operation with 0x11 */
#define S2C_FLAGLOC 0x12
#define S2C_FLAGPICKUP 0x13
#define S2C_FLAGRESET 0x14
#define S2C_TURRETKICKOFF 0x15
#define S2C_FLAGDROP 0x16
/* subspace client does no operation with 0x17 */
#define S2C_SECURITY 0x18
#define S2C_REQUESTFORFILE 0x19
#define S2C_TIMEDGAME 0x1A
/* just 1 byte, tells client they need to reset their ship */
#define S2C_SHIPRESET 0x1B
/* two bytes, if byte two is true, client needs to send their item info in
 * position packets, OR
 * three bytes, parameter is the player id of a player going into spectator
 * mode */
#define S2C_SPECDATA 0x1C
#define S2C_SHIPCHANGE 0x1D
#define S2C_BANNERTOGGLE 0x1E
#define S2C_BANNER 0x1F
#define S2C_PRIZERECV 0x20
#define S2C_BRICK 0x21
#define S2C_TURFFLAGS 0x22
#define S2C_PERIODICREWARD 0x23
/* complex speed stats */
#define S2C_SPEED 0x24
/* two bytes, if byte two is true, you can use UFO if you want to */
#define S2C_UFO 0x25
/* subspace client does no operation with 0x26 */
#define S2C_KEEPALIVE 0x27
#define S2C_POSITION 0x28
#define S2C_MAPFILENAME 0x29
#define S2C_MAPDATA 0x2A
#define S2C_SETKOTHTIMER 0x2B
#define S2C_KOTH 0x2C
/* missing 2D : some timer change? */
#define S2C_BALL 0x2E
#define S2C_ARENA 0x2F
/* vie's old method of showing ads */
#define S2C_ADBANNER 0x30
/* vie sent it after a good login, only with billing. */
#define S2C_LOGINOK 0x31
/* u8 type - ui16 x tile coords - ui16 y tile coords */
#define S2C_WARPTO 0x32
#define S2C_LOGINTEXT 0x33
#define S2C_CONTVERSION 0x34
/* u8 type - unlimited number of ui16 with obj id (if & 0xF000, means
 * turning off) */
#define S2C_TOGGLEOBJ 0x35
#define S2C_MOVEOBJECT 0x36
/* two bytes, if byte two is true, client should send damage info */
#define S2C_TOGGLEDAMAGE 0x37
/* complex, the info used from a *watchdamage */
#define S2C_DAMAGE 0x38
/* missing 39 3A */
#define S2C_REDIRECT 0x3B


/* C2S PACKET TYPES */
#define C2S_GOTOARENA 0x01
#define C2S_LEAVING 0x02
#define C2S_POSITION 0x03
/* missing 04 : appears to be disabled in subgame */
#define C2S_DIE 0x05
#define C2S_CHAT 0x06
#define C2S_GREEN 0x07
#define C2S_SPECREQUEST 0x08
#define C2S_LOGIN 0x09
#define C2S_REBROADCAST 0x0A
#define C2S_UPDATEREQUEST 0x0B
#define C2S_MAPREQUEST 0x0C
#define C2S_NEWSREQUEST 0x0D
#define C2S_RELAYVOICE 0x0E
#define C2S_SETFREQ 0x0F
#define C2S_ATTACHTO 0x10
/* missing 12 : appears to be disabled in subgame */
#define C2S_PICKUPFLAG 0x13
#define C2S_TURRETKICKOFF 0x14
#define C2S_DROPFLAGS 0x15
/* uploading a file to server */
#define C2S_UPLOADFILE 0x16
#define C2S_REGDATA 0x17
#define C2S_SETSHIP 0x18
/* sending new banner */
#define C2S_BANNER 0x19
#define C2S_SECURITYRESPONSE 0x1A
#define C2S_CHECKSUMMISMATCH 0x1B
#define C2S_BRICK 0x1C
#define C2S_SETTINGCHANGE 0x1D
#define C2S_KOTHEXPIRED 0x1E
#define C2S_SHOOTBALL 0x1F
#define C2S_PICKUPBALL 0x20
#define C2S_GOAL 0x21
/* missing 22 : subspace client sends extra checksums and other security stuff */
/* missing 23 */
#define C2S_CONTLOGIN 0x24
#define C2S_DAMAGE 0x32


#endif

