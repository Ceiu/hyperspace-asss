#ifndef AKD_ASSS
#define AKD_ASSS

#ifndef MODULENAME
#error #define MODULENAME modulename - before including akd_asss.h
#endif

#ifndef SZMODULENAME
#error #define SZMODULENAME "modulename" - before including akd_asss.h
#endif

#include "asss.h"
#include "game.h"
#include "util.h"

#include "persist.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>

local Imodman *mm;
local Ilogman *lm;
local Ichat *chat;
local Igame *game;
local Iconfig *cfg;
local Icmdman *cmd;
local Iarenaman *aman;
local Iplayerdata *pd;
local Imainloop *ml;
local Iprng *prng;
local Ipersist *persist;
local Inet *net;

#define IS_PLAYING(p,a) ((p->p_ship != SHIP_SPEC) && (p->arena == a))
#define IS_IN(p,a) (p->arena == a)
#define IS_ON_FREQ(p,a,f) ((p->arena == a) && (p->p_freq == f))

#define IS_VALID_SHIP(s) ((s >= SHIP_WARBIRD) && (s <= SHIP_SHARK))


#define DEF_T_P(x) Target t; t.type = T_PLAYER; t.u.p = x;
#define T_P(t, p) { t.type = T_PLAYER; t.u.p = p }
#define DEF_T_A(a) Target t; t.type = T_ARENA; t.u.arena = a;
#define T_A(a) { t.type = T_ARENA; t.u.arena = a }
#define DEF_T_F(a, f) Target t; t.type = T_FREQ; t.u.freq.arena = a; t.u.freq.freq = f;

#define T_F(a,f) { t.type = T_FREQ; t.u.freq.arena = a; t.u.freq.freq = f; }
#define MM_FUNC_HEADER() int fail = 0; DEF_AD(arena);
#define BMM_FUNC_HEADER() int fail = 0; BDEF_AD(arena);
#define DO_RETURN() if (fail) return MM_FAIL; return MM_OK;

#define FAIL_LOAD { fail = 1; goto Lfailload; }
#define FAIL_ATTACH { fail = 1; goto Lfailattach; }
#define GETINT(var,n) { var = mm->GetInterface(n, 0); if (!var) { if (lm) lm->Log('E', "<%s> error getting interface %s", SZMODULENAME , n); FAIL_LOAD; } }
#define OGETINT(var,n) { var = mm->GetInterface(n, 0); if (!var) { if (lm) lm->Log('W', "<%s> non-fatal error getting interface %s", SZMODULENAME, n); } }
#define GETARENAINT(var,n) { var = mm->GetInterface(n, arena); if (!var) { if (lm) lm->Log('E', "<%s> error getting interface %s for arena %s", SZMODULENAME, n, arena->name); FAIL_ATTACH; } }
#define OGETARENAINT(var,n) { var = mm->GetInterface(n, arena); if (!var) { if (lm) lm->Log('W', "<%s> non-fatal error getting interface %s for arena %s", SZMODULENAME, n, arena->name); } }
#define RELEASEINT(var) { if (var) { mm->ReleaseInterface(var); var = 0; } }

#define GET_USUAL_INTERFACES() GETINT(lm, I_LOGMAN); GETINT(game, I_GAME); GETINT(prng, I_PRNG); GETINT(cfg, I_CONFIG); GETINT(chat, I_CHAT); GETINT(cmd, I_CMDMAN); GETINT(ml, I_MAINLOOP); GETINT(aman, I_ARENAMAN); GETINT(pd, I_PLAYERDATA); GETINT(persist, I_PERSIST); GETINT(net, I_NET);
#define RELEASE_USUAL_INTERFACES() RELEASEINT(lm); RELEASEINT(game); RELEASEINT(prng); RELEASEINT(cfg); RELEASEINT(chat); RELEASEINT(cmd); RELEASEINT(ml); RELEASEINT(aman); RELEASEINT(pd); RELEASEINT(persist); RELEASEINT(net);

#ifdef INTERFACENAME
#define MYINTERFACE local INTERFACENAME templateinterface
#define DEF_GLOBALINTERFACE local INTERFACENAME globalinterface;
#define DEF_ARENAINTERFACE INTERFACENAME arenainterface
#define INIT_GLOBALINTERFACE() globalinterface = templateinterface;
#define INIT_ARENAINTERFACE() ad->arenainterface = templateinterface;
#define REG_GLOBALINTERFACE() mm->RegInterface(&globalinterface, 0)
#define UNREG_GLOBALINTERFACE() if (mm->UnregInterface(&globalinterface, 0)) return MM_FAIL;
#define REG_ARENAINTERFACE() mm->RegInterface(&ad->arenainterface, arena);
#define UNREG_ARENAINTERFACE() if (mm->UnregInterface(&ad->arenainterface, arena)) return MM_FAIL;
#endif


#define DEF_PARENA_TYPE typedef struct myADType {
#define ENDDEF_PARENA_TYPE } myADType; typedef struct myadtype { myADType *data; } myadtype;

#define DEF_PPLAYER_TYPE typedef struct myPDType {
#define ENDDEF_PPLAYER_TYPE } myPDType; typedef struct mypdtype { myPDType *data; } mypdtype;

local int AD_key;
#define AD(a) (myadtype *)(a?P_ARENA_DATA(a, AD_key):0)
#define BAD(a) (myADType *)(a?P_ARENA_DATA(a, AD_key):0)
#define DEF_AD(a) myadtype *_ad = AD(a); myADType *ad = _ad?_ad->data:0
#define DEF_AD_ALT(a, n) myadtype *_##n = AD(a); myADType  *n = _##n?_##n->data:0
#define BDEF_AD(a) myADType *ad = BAD(a)
#define BDEF_AD_ALT(a, n) myADType *n = BAD(a)
#define ALLOC_ARENA_DATA(n) { _##n->data = amalloc(sizeof(myADType)); n = _##n->data; }
#define FREE_ARENA_DATA(n) { afree(_##n->data); _##n->data = 0; n = 0; }

#ifndef NOT_USING_PDATA
local int PD_key;
#endif

#define PD(p) (mypdtype *)(PPDATA(p, PD_key))
#define BPD(p) (myPDType *)(PPDATA(p, PD_key))
#define DEF_PD(p) mypdtype *_pdat = PD(p); myPDType *pdat = _pdat?_pdat->data:0
#define DEF_PD_ALT(p, n) mypdtype *_##n = PD(p); myPDType *n = _##n?_##n->data:0
#define BDEF_PD(p) myPDType *pdat = p?BPD(p):0
#define BDEF_PD_ALT(p, n) myPDType *n = p?BPD(p):0
#define ALLOC_PLAYER_DATA(n) { _##n->data = amalloc(sizeof(myPDType)); n = _##n->data; }
#define FREE_PLAYER_DATA(n) { afree(_##n->data); _##n->data = 0; n = 0; }

#define ALLOC_ARENA_PLAYER_DATA(a) { Player *p; Link *link; PDLOCK; FOR_EACH_PLAYER(p) { if (p->arena == a) { DEF_PD(p); if (!pdat) ALLOC_PLAYER_DATA(pdat); } } PDUNLOCK; }
#define FREE_ARENA_PLAYER_DATA(a) { Player *p; Link *link; PDLOCK; FOR_EACH_PLAYER(p) { if (p->arena == a) { DEF_PD(p); if (pdat) FREE_PLAYER_DATA(pdat); } } PDUNLOCK; }

#define ALLOC_GLOBAL_PLAYER_DATA() { Player *p; Link *link; PDLOCK; FOR_EACH_PLAYER(p) { if ((p->type == T_VIE) || (p->type == T_CONT)) { DEF_PD(p); if (!pdat) { ALLOC_PLAYER_DATA(pdat); } } } PDUNLOCK; }
#define FREE_GLOBAL_PLAYER_DATA() { Player *p; Link *link; PDLOCK; FOR_EACH_PLAYER(p) { if ((p->type == T_VIE) || (p->type == T_CONT)) { DEF_PD(p); if (pdat) { FREE_PLAYER_DATA(pdat); } } } PDUNLOCK; }

#define REG_PARENA_DATA() { AD_key = aman->AllocateArenaData(sizeof(myadtype)); if (AD_key == -1) FAIL_LOAD; }
#define REG_PPLAYER_DATA() { PD_key = pd->AllocatePlayerData(sizeof(mypdtype)); if (PD_key == -1) FAIL_LOAD; }
#define BREG_PARENA_DATA() { AD_key = aman->AllocateArenaData(sizeof(myADType)); if (AD_key == -1) FAIL_LOAD; }
#define BREG_PPLAYER_DATA() { PD_key = pd->AllocatePlayerData(sizeof(myPDType)); if (PD_key == -1) FAIL_LOAD; }

#define UNREG_PARENA_DATA() { aman->FreeArenaData(AD_key); }
#define UNREG_PPLAYER_DATA() { pd->FreePlayerData(PD_key); }

#define INIT_MUTEX(m) { pthread_mutexattr_t attr; pthread_mutexattr_init(&attr); pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE); pthread_mutex_init(&m, &attr); pthread_mutexattr_destroy(&attr); }
#define DESTROY_MUTEX(m) { pthread_mutex_destroy(&m); }

#define MYGLOCK pthread_mutex_lock(&globalmutex);
#define MYGUNLOCK pthread_mutex_unlock(&globalmutex);
#define MYALOCK pthread_mutex_lock(&ad->arenamutex);
#define MYAUNLOCK pthread_mutex_unlock(&ad->arenamutex);
#define PDLOCK pd->Lock();
#define PDUNLOCK pd->Unlock();

#define AKDASSERT(t, s) if (!(t)) { lm->Log(L_ERROR, "Assertion Failure! " s); }

#define BV(v,i) (int)((((int)(0xFF) << (i*8)) & v) >> (i * 8))
#define BDSZ " (%2.2x)"
#define SDSZ " (%2.2x %2.2x)"
#define WDSZ " (%2.2x %2.2x %2.2x %2.2x)"
#define LDSZ " (%2.2x %2.2x %2.2x %2.2x %2.2x %2.2x %2.2x %2.2x)"
#define BDISSECT(v) BV(v,0)
#define SDISSECT(v) BV(v,0),BV(v,1)
#define WDISSECT(v) BV(v,0),BV(v,1),BV(v,2),BV(v,3)
#define LDISSECT(v) BV(v,0),BV(v,1),BV(v,2),BV(v,3),BV(v,4),BV(v,5),BV(v,6),BV(v,7)

#define ADV if (!ad) return;
#define ADI(i) if (!ad) return i;
#define ADZ ADI(0)

#endif

#define I_LEAGUE "null-league"
typedef struct Ileague { INTERFACE_HEAD_DECL } Ileague;
