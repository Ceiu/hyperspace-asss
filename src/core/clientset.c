
/* dist: public */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "asss.h"
#include "clientset.h"


#include "packets/clientset.h"


#define COUNT(x) (sizeof(x)/sizeof(x[0]))

#define SIZE (sizeof(struct ClientSettings))

typedef struct overridedata
{
	byte bits[SIZE];
	byte mask[SIZE];
} overridedata;

typedef struct adata
{
	struct ClientSettings cs;
	overridedata od;
	/* prizeweight partial sums. 1-28 are used for now, representing
	 * prizes 1 to 28. 0 = null prize. */
	unsigned short pwps[32];
} adata;

typedef struct pdata
{
	overridedata *od;
	struct ClientSettings *cs;
} pdata;

/* global data */

local int adkey, pdkey;
local pthread_mutex_t setmtx = PTHREAD_MUTEX_INITIALIZER;
#define LOCK() pthread_mutex_lock(&setmtx)
#define UNLOCK() pthread_mutex_unlock(&setmtx)

/* cached interfaces */
local Iplayerdata *pd;
local Iconfig *cfg;
local Inet *net;
local Iprng *prng;
local Ilogman *lm;
local Imodman *mm;
local Iarenaman *aman;


/* the client settings definition */
#include "clientset.def"


local void load_settings(adata *ad, ConfigHandle conf)
{
	struct ClientSettings *cs = &ad->cs;
	int i, j;
	unsigned short total = 0;

	/* clear and set type */
	memset(cs, 0, sizeof(*cs));

	cs->bit_set.type = S2C_SETTINGS;
	cs->bit_set.ExactDamage = cfg->GetInt(conf, "Bullet", "ExactDamage", 0);
	cs->bit_set.HideFlags = cfg->GetInt(conf, "Spectator", "HideFlags", 0);
	cs->bit_set.NoXRadar = cfg->GetInt(conf, "Spectator", "NoXRadar", 0);
	cs->bit_set.SlowFrameRate = cfg->GetInt(conf, "Misc", "SlowFrameCheck", 0);
	cs->bit_set.DisableScreenshot = cfg->GetInt(conf, "Misc", "DisableScreenshot", 0);
	cs->bit_set.MaxTimerDrift = cfg->GetInt(conf, "Misc", "MaxTimerDrift", 0);
	cs->bit_set.DisableBallThroughWalls = cfg->GetInt(conf, "Soccer", "DisableWallPass", 0);
	cs->bit_set.DisableBallKilling = cfg->GetInt(conf, "Soccer", "DisableBallKilling", 0);

	/* do ships */
	for (i = 0; i < 8; i++)
	{
		struct WeaponBits wb;
		struct MiscBitfield misc;
		struct ShipSettings *ss = cs->ships + i;
		const char *shipname = cfg->SHIP_NAMES[i];

		/* basic stuff */
		for (j = 0; j < COUNT(ss->long_set); j++)
			ss->long_set[j] = cfg->GetInt(conf,
					shipname, ship_long_names[j], 0);
		for (j = 0; j < COUNT(ss->short_set); j++)
			ss->short_set[j] = cfg->GetInt(conf,
					shipname, ship_short_names[j], 0);
		for (j = 0; j < COUNT(ss->byte_set); j++)
			ss->byte_set[j] = cfg->GetInt(conf,
					shipname, ship_byte_names[j], 0);

		/* weapons bits */
#define DO(x) \
		wb.x = cfg->GetInt(conf, shipname, #x, 0)
		DO(ShrapnelMax);  DO(ShrapnelRate);  DO(AntiWarpStatus);
		DO(CloakStatus);  DO(StealthStatus); DO(XRadarStatus);
		DO(InitialGuns);  DO(MaxGuns);
		DO(InitialBombs); DO(MaxBombs);
		DO(DoubleBarrel); DO(EmpBomb); DO(SeeMines);
		DO(Unused1);
#undef DO
		ss->Weapons = wb;

		/* now do the strange bitfield */
		memset(&misc, 0, sizeof(misc));
		misc.SeeBombLevel = cfg->GetInt(conf, shipname, "SeeBombLevel", 0);
		misc.DisableFastShooting = cfg->GetInt(conf, shipname,
				"DisableFastShooting", 0);
		misc.Radius = cfg->GetInt(conf, shipname, "Radius", 0);
		memcpy(&ss->short_set[10], &misc, 2);
	}

	/* spawn locations */
	for (i = 0; i < 4; i++)
	{
		char xname[] = "Team#-X";
		char yname[] = "Team#-Y";
		char rname[] = "Team#-Radius";
		xname[4] = yname[4] = rname[4] = '0' + i;
		cs->spawn_pos[i].x = cfg->GetInt(conf, "Spawn", xname, 0);
		cs->spawn_pos[i].y = cfg->GetInt(conf, "Spawn", yname, 0);
		cs->spawn_pos[i].r = cfg->GetInt(conf, "Spawn", rname, 0);
	}

	/* do rest of settings */
	for (i = 0; i < COUNT(cs->long_set); i++)
		cs->long_set[i] = cfg->GetInt(conf, long_names[i], NULL, 0);
	for (i = 0; i < COUNT(cs->short_set); i++)
		cs->short_set[i] = cfg->GetInt(conf, short_names[i], NULL, 0);
	for (i = 0; i < COUNT(cs->byte_set); i++)
		cs->byte_set[i] = cfg->GetInt(conf, byte_names[i], NULL, 0);

	ad->pwps[0] = 0;
	for (i = 0; i < COUNT(cs->prizeweight_set); i++)
	{
		cs->prizeweight_set[i] = cfg->GetInt(conf,
				prizeweight_names[i], NULL, 0);
		ad->pwps[i+1] = (total += cs->prizeweight_set[i]);
	}


	/* cfghelp: Prize:UseDeathPrizeWeights, arena, bool, def: 0
	 * Whether to use the DPrizeWeight section for death
	 * prizes instead of the PrizeWeight section. */
	if (cfg->GetInt(conf, "Prize", "UseDeathPrizeWeights", 0))
	{
		/* cfghelp: DPrizeWeight:QuickCharge, arena, int
		 * Likelihood of an empty prize appearing */
		total = ad->pwps[0] = cfg->GetInt(conf, "DPrizeWeight", "NullPrize", 0);
		for (i = 0; i < COUNT(cs->prizeweight_set); i++)
		{
			ad->pwps[i+1] = (total += cfg->GetInt(conf,
					deathprizeweight_names[i], NULL, 0));
		}
	}

	/* the funky ones */
	cs->long_set[0] *= 1000; /* BulletDamageLevel */
	cs->long_set[1] *= 1000; /* BombDamageLevel */
	cs->long_set[10] *= 1000; /* BurstDamageLevel */
	cs->long_set[11] *= 1000; /* BulletDamageUpgrade */
	cs->long_set[16] *= 1000; /* InactiveShrapDamage */

	/* Radar:MapZoomFactor of 0 will crash Continuum. Set it to 1 at least */
	assert(!strcasecmp("Radar:MapZoomFactor", short_names[11]));
	if (0 == cs->short_set[11])
		cs->short_set[11] = 1;
}


local override_key_t GetOverrideKey(const char *section, const char *key)
{
#define MAKE_UNSIGNED_KEY(field, len) ((offsetof(struct ClientSettings, field)) << 3 | ((len) << 16))
#define MAKE_SIGNED_KEY(field, len) (MAKE_UNSIGNED_KEY(field, len) | 0x80000000u)
#define MAKE_BKEY(field, off, len) ((((offsetof(struct ClientSettings, field)) << 3) + (off)) | ((len) << 16))
	char fullkey[MAXSECTIONLEN+MAXKEYLEN];
	int i, j;

	/* do prizeweights */
	if (strcasecmp(section, "PrizeWeight") == 0)
	{
		for (i = 0; i < COUNT(prizeweight_names); i++)
			/* HACK: that +12 there is kind of sneaky */
			if (strcasecmp(prizeweight_names[i]+12, key) == 0)
				return MAKE_UNSIGNED_KEY(prizeweight_set[i], 8);
		return 0;
	}

	/* do ships */
	for (i = 0; i < 8; i++)
		if (strcasecmp(cfg->SHIP_NAMES[i], section) == 0)
		{
			/* basic stuff */
			for (j = 0; j < COUNT(ship_long_names); j++)
				if (strcasecmp(ship_long_names[j], key) == 0)
					return MAKE_SIGNED_KEY(ships[i].long_set[j], 32);
			for (j = 0; j < COUNT(ship_short_names); j++)
				if (strcasecmp(ship_short_names[j], key) == 0)
					return MAKE_SIGNED_KEY(ships[i].short_set[j], 16);
			for (j = 0; j < COUNT(ship_byte_names); j++)
				if (strcasecmp(ship_byte_names[j], key) == 0)
					return MAKE_UNSIGNED_KEY(ships[i].byte_set[j], 8);

#define DO(field, x, off, len) \
			if (strcasecmp(#x, key) == 0) \
				return MAKE_BKEY(ships[i].field, off, len)
			DO(Weapons, ShrapnelMax, 0, 5);
			DO(Weapons, ShrapnelRate, 5, 5);
			DO(Weapons, CloakStatus, 10, 2);
			DO(Weapons, StealthStatus, 12, 2);
			DO(Weapons, XRadarStatus, 14, 2);
			DO(Weapons, AntiWarpStatus, 16, 2);
			DO(Weapons, InitialGuns, 18, 2);
			DO(Weapons, MaxGuns, 20, 2);
			DO(Weapons, InitialBombs, 22, 2);
			DO(Weapons, MaxBombs, 24, 2);
			DO(Weapons, DoubleBarrel, 26, 1);
			DO(Weapons, EmpBomb, 27, 1);
			DO(Weapons, SeeMines, 28, 1);
			DO(Weapons, Unused1, 29, 3);

			DO(short_set[10], SeeBombLevel, 0, 2);
			DO(short_set[10], DisableFastShooting, 2, 1);
			DO(short_set[10], Radius, 3, 8);
#undef DO
			return 0;
		}

	/* spawn locations */
	if (strcasecmp("Spawn", section) == 0)
	{
		for (i = 0; i < 4; i++)
		{
			char xname[] = "Team#-X";
			char yname[] = "Team#-Y";
			char rname[] = "Team#-Radius";
			xname[4] = yname[4] = rname[4] = '0' + i;
			if (strcasecmp(xname, key) == 0)
				return MAKE_BKEY(spawn_pos[i], 0, 10);
			if (strcasecmp(yname, key) == 0)
				return MAKE_BKEY(spawn_pos[i], 10, 10);
			if (strcasecmp(rname, key) == 0)
				return MAKE_BKEY(spawn_pos[i], 20, 9);
		}
		return 0;
	}

	/* need full key for remaining ones: */
	snprintf(fullkey, sizeof(fullkey), "%s:%s", section, key);

	/* do rest of settings */
	for (i = 0; i < COUNT(long_names); i++)
		if (strcasecmp(long_names[i], fullkey) == 0)
			return MAKE_SIGNED_KEY(long_set[i], 32);
	for (i = 0; i < COUNT(short_names); i++)
		if (strcasecmp(short_names[i], fullkey) == 0)
			return MAKE_SIGNED_KEY(short_set[i], 16);
	for (i = 0; i < COUNT(byte_names); i++)
		if (strcasecmp(byte_names[i], fullkey) == 0)
			return MAKE_UNSIGNED_KEY(byte_set[i], 8);

#define DO(k, s, off, len) \
	if (strcasecmp(#k ":" #s, fullkey) == 0) \
		return MAKE_BKEY(bit_set, off, len)
	DO(Bullet, ExactDamage, 8, 1);
	DO(Spectator, HideFlags, 9, 1);
	DO(Spectator, NoXRadar, 10, 1);
	DO(Misc, SlowFrameCheck, 11, 3);
	DO(Misc, DisableScreenshot, 14, 1);
	DO(Misc, MaxTimerDrift, 16, 3);
	DO(Misc, DisableBallThroughWalls, 19, 1);
	DO(Misc, DisableBallKilling, 20, 1);
#undef DO

	return 0;
#undef MAKE_UNSIGNED_KEY
#undef MAKE_SIGNED_KEY
#undef MAKE_BKEY
}


/* override keys are three small integers stuffed into an unsigned 32-bit
 * integer. the upper bit indicates signed/unsigned, the next 15 are the
 * length in bits, and the lower 16 are the offset in bits */
/* call with lock held */
local void override_work(overridedata *od, override_key_t key, i32 val, int set)
{
	int len = (key >> 16) & 0x7fffu;
	int offset = key & 0xffffu;

	/* don't override type byte */
	if (offset < 8)
		return;

	if (len <= 32 && ((offset & 31) + len) <= 32)
	{
		/* easier case: a bunch of bits that fit within a word boundary */
		int wordoff = offset >> 5;
		int bitoff = offset & 31;
		u32 mask = (0xffffffffu >> (32 - len)) << bitoff;

		if (set)
		{
			((u32*)od->bits)[wordoff] &= ~mask;
			((u32*)od->bits)[wordoff] |= (val << bitoff) & mask;
			((u32*)od->mask)[wordoff] |= mask;
		}
		else
			((u32*)od->mask)[wordoff] &= ~mask;
	}
#if 0
	else if (len <= 32 && ((offset & 31) + len) <= 64)
	{
		/* harder case: some bunch of bits that cross a word boundary.
		 * this code is untested and probably contains bugs. luckily, no
		 * actual settings need to use it. if, in the future, it becomes
		 * useful, the #if 0 can be removed. */
		int wordoff = offset >> 5;
		int bitoff = offset & 31;
		u32 maskv = (0xffffffffu >> (32 - bitoff));
		u32 mask0 = maskv << bitoff;
		u32 mask1 = 0xffffffffu >> (bitoff + len - 32);

		if (set)
		{
			((u32*)od->bits)[wordoff+0] &= ~mask0;
			((u32*)od->bits)[wordoff+0] |= (val & maskv) << bitoff;
			((u32*)od->mask)[wordoff+0] |= mask0;
			((u32*)od->bits)[wordoff+1] &= ~mask1;
			((u32*)od->bits)[wordoff+1] |= val >> (32 - bitoff);
			((u32*)od->mask)[wordoff+1] |= mask1;
		}
		else
		{
			((u32*)od->mask)[wordoff+0] &= ~mask0;
			((u32*)od->mask)[wordoff+1] &= ~mask1;
		}
	}
#endif
	else
	{
		lm->Log(L_WARN, "<clientset> illegal override key: %x", key);
	}
}


local void ArenaOverride(Arena *arena, override_key_t key, i32 val)
{
	adata *ad = P_ARENA_DATA(arena, adkey);
	LOCK();
	override_work(&ad->od, key, val, TRUE);
	UNLOCK();
}

local void ArenaUnoverride(Arena *arena, override_key_t key)
{
	adata *ad = P_ARENA_DATA(arena, adkey);
	LOCK();
	override_work(&ad->od, key, 0, FALSE);
	UNLOCK();
}

local void PlayerOverride(Player *p, override_key_t key, i32 val)
{
	pdata *data = PPDATA(p, pdkey);
	LOCK();
	if (!data->od)
		data->od = amalloc(sizeof(*data->od));
	override_work(data->od, key, val, TRUE);
	UNLOCK();
}

local void PlayerUnoverride(Player *p, override_key_t key)
{
	pdata *data = PPDATA(p, pdkey);
	LOCK();
	if (data->od)
		override_work(data->od, key, 0, FALSE);
	UNLOCK();
}

/* call with lock held */
local int get_override(overridedata *od, override_key_t key, int *val)
{
	int is_signed = (key & 0x80000000u);
	int len = (key >> 16) & 0x7fffu;
	int offset = key & 0xffffu;

	if (len <= 32 && ((offset & 31) + len) <= 32)
	{
		/* easier case: a bunch of bits that fit within a word boundary */
		int wordoff = offset >> 5;
		int bitoff = offset & 31;
		u32 mask = (0xffffffffu >> (32 - len)) << bitoff;

		if ((((u32*)od->mask)[wordoff] & mask) == mask)
		{
			u32 value = (((u32*)od->bits)[wordoff] & mask) >> bitoff;
			if (is_signed && (value & (1 << (len - 1))))
			{
				value |= (0xffffffffu << len); // do sign extension
			}
			*val = (int)value;
			return 1;
		}
		else
		{
			return 0;
		}
	}
	else
	{
		lm->Log(L_WARN, "<clientset> illegal override key: %x", key);
		return 0;
	}
}

local int GetArenaOverride(Arena *arena, override_key_t key, int *value)
{
	int return_value;
	adata *ad = P_ARENA_DATA(arena, adkey);
	LOCK();
	return_value = get_override(&ad->od, key, value);
	UNLOCK();
	return return_value;
}

local int GetPlayerOverride(Player *p, override_key_t key, int *value)
{
	int return_value;
	pdata *data = PPDATA(p, pdkey);
	LOCK();
	if (data->od)
	{
		return_value = get_override(data->od, key, value);
	}
	else
	{
		return_value = 0;
	}
	UNLOCK();
	return return_value;
}

/* call with lock held */
local int get_cs_value(struct ClientSettings *cs, override_key_t key)
{
	int is_signed = (key & 0x80000000u);
	int len = (key >> 16) & 0x7fffu;
	int offset = key & 0xffffu;

	if (len <= 32 && ((offset & 31) + len) <= 32)
	{
		/* easier case: a bunch of bits that fit within a word boundary */
		int wordoff = offset >> 5;
		int bitoff = offset & 31;
		u32 mask = (0xffffffffu >> (32 - len)) << bitoff;

		u32 value = (((u32*)cs)[wordoff] & mask) >> bitoff;
		if (is_signed && (value & (1 << (len - 1))))
		{
			value |= (0xffffffffu << len); // do sign extension
		}
		return (int)value;
	}
	else
	{
		lm->Log(L_WARN, "<clientset> illegal override key: %x", key);
		return 0;
	}
}

local int GetArenaValue(Arena *arena, override_key_t key)
{
	int value;
	adata *ad = P_ARENA_DATA(arena, adkey);

	LOCK();

	value = get_cs_value(&ad->cs, key);

	UNLOCK();

	return value;
}

local int GetPlayerValue(Player *p, override_key_t key)
{
	int value;
	pdata *data = PPDATA(p, pdkey);

	LOCK();

	if (data->cs)
	{
		value = get_cs_value(data->cs, key);
	}
	else
	{
		/* player hasn't been sent a settings packet */
		value = 0;
	}

	UNLOCK();

	return value;
}

/* call with lock held */
local void do_mask(
		struct ClientSettings *dest,
		struct ClientSettings *src,
		overridedata *od1,
		overridedata *od2)
{
	int i;
	unsigned int *s = (unsigned int*)src;
	unsigned int *d = (unsigned int*)dest;
	unsigned int *o1 = (unsigned int*)od1->bits;
	unsigned int *m1 = (unsigned int*)od1->mask;

	if (od2)
	{
		unsigned int *o2 = (unsigned int*)od2->bits;
		unsigned int *m2 = (unsigned int*)od2->mask;

		for (i = 0; i < sizeof(*dest)/sizeof(unsigned int); i++)
			d[i] = (((s[i] & ~m1[i]) | (o1[i] & m1[i])) & ~m2[i]) | (o2[i] & m2[i]);
	}
	else
	{
		for (i = 0; i < sizeof(*dest)/sizeof(unsigned int); i++)
			d[i] = (s[i] & ~m1[i]) | (o1[i] & m1[i]);
	}
}


/* call with lock held */
local void send_one_settings(Player *p, adata *ad, RelCallback callback, void *clos)
{
	pdata *data = PPDATA(p, pdkey);
	if (!data->cs)
	{
		data->cs = amalloc(sizeof(struct ClientSettings));
	}
	do_mask(data->cs, &ad->cs, &ad->od, data->od);
	if (data->cs->bit_set.type == S2C_SETTINGS)
		net->SendToOneWithCallback(p, (byte*)data->cs, sizeof(struct ClientSettings), NET_RELIABLE, callback, clos);
}



local void aaction(Arena *arena, int action)
{
	adata *ad = P_ARENA_DATA(arena, adkey);
	LOCK();
	if (action == AA_CREATE)
	{
		load_settings(ad, arena->cfg);
	}
	else if (action == AA_CONFCHANGED)
	{
		struct ClientSettings old;

		/** cfghelp: Misc:SendUpdatedSettings, arena, bool, def: 1
		 * Whether to send updates to players when the arena
		 * settings change.
		 */
		int send_updated = cfg->GetInt(arena->cfg, "Misc", "SendUpdatedSettings", 1);

		memcpy(&old, &ad->cs, SIZE);
		load_settings(ad, arena->cfg);
		if (send_updated && memcmp(&old, &ad->cs, SIZE) != 0)
		{
			Player *p;
			Link *link;
			lm->LogA(L_INFO, "clientset", arena, "sending modified settings");
			pd->Lock();
			FOR_EACH_PLAYER(p)
				if (p->arena == arena && p->status == S_PLAYING)
					send_one_settings(p, ad, NULL, NULL);
			pd->Unlock();
		}
	}
	else if (action == AA_DESTROY)
	{
		/* mark settings as destroyed (for asserting later) */
		ad->cs.bit_set.type = 0;
	}
	UNLOCK();
}


local void paction(Player *p, int action, Arena *arena)
{
	if (action == PA_LEAVEARENA || action == PA_DISCONNECT)
	{
		/* reset/free player overrides on any arena change */
		pdata *data = PPDATA(p, pdkey);
		afree(data->od);
		data->od = NULL;
		afree(data->cs);
		data->cs = NULL;
	}
}


local void SendClientSettings(Player *p)
{
	adata *ad = P_ARENA_DATA(p->arena, adkey);
	if (!p->arena)
		return;
	LOCK();
	send_one_settings(p, ad, NULL, NULL);
	UNLOCK();
}


local void SendClientSettingsWithCallback(Player *p, RelCallback callback, void *clos)
{
	adata *ad = P_ARENA_DATA(p->arena, adkey);
	if (!p->arena)
		return;
	LOCK();
	send_one_settings(p, ad, callback, clos);
	UNLOCK();
}


local u32 GetChecksum(Player *p, u32 key)
{
	adata *ad = P_ARENA_DATA(p->arena, adkey);
	pdata *data = PPDATA(p, pdkey);
	u32 *bits;
	u32 csum = 0;
	int i;

	if (p->status != S_PLAYING)
		return -1;

	LOCK();

	if (!data->cs)
	{
		data->cs = amalloc(sizeof(struct ClientSettings));
	}
	bits = (u32*)(data->cs);

	do_mask(data->cs, &ad->cs, &ad->od, data->od);
	for (i = 0; i < (SIZE/sizeof(u32)); i++, bits++)
		csum += (*bits ^ key);

	UNLOCK();

	return csum;
}


local int GetRandomPrize(Arena *arena)
{
	adata *ad = P_ARENA_DATA(arena, adkey);
	int max = ad->pwps[28], r, i = 0, j = 28;

	if (max == 0)
		return 0;

	r = prng->Number(0, max-1);

	/* binary search */
	while (r >= ad->pwps[i])
	{
		int m = (i + j)/2;
		if (r < ad->pwps[m])
			j = m;
		else
			i = m + 1;
	}

	/* our indexes are zero-based but prize numbers are one-based */
	return i;
}


local Iclientset csint =
{
	INTERFACE_HEAD_INIT(I_CLIENTSET, "clientset")
	SendClientSettings, SendClientSettingsWithCallback, GetChecksum, GetRandomPrize,
	GetOverrideKey,
	ArenaOverride, ArenaUnoverride, GetArenaOverride, GetArenaValue,
	PlayerOverride, PlayerUnoverride, GetPlayerOverride, GetPlayerValue,
};

EXPORT const char info_clientset[] = CORE_MOD_INFO("clientset");

EXPORT int MM_clientset(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		prng = mm->GetInterface(I_PRNG, ALLARENAS);

		if (!net || !cfg || !lm || !aman || !prng) return MM_FAIL;

		adkey = aman->AllocateArenaData(sizeof(adata));
		if (adkey == -1) return MM_FAIL;
		pdkey = pd->AllocatePlayerData(sizeof(pdata));
		if (pdkey == -1) return MM_FAIL;

		mm->RegCallback(CB_ARENAACTION, aaction, ALLARENAS);
		mm->RegCallback(CB_PLAYERACTION, paction, ALLARENAS);

		mm->RegInterface(&csint, ALLARENAS);

		/* do these at least once */
#define cs (*((struct ClientSettings*)0))
#define ss (*((struct ShipSettings*)0))
		assert((sizeof(cs) % sizeof(uint32_t)) == 0);
		assert(COUNT(cs.long_set) == COUNT(long_names));
		assert(COUNT(cs.short_set) == COUNT(short_names));
		assert(COUNT(cs.byte_set) == COUNT(byte_names));
		assert(COUNT(cs.prizeweight_set) == COUNT(prizeweight_names));
		assert(COUNT(ss.long_set) == COUNT(ship_long_names));
		assert(COUNT(ss.short_set) == COUNT(ship_short_names));
		assert(COUNT(ss.byte_set) == COUNT(ship_byte_names));
#undef cs
#undef ss

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&csint, ALLARENAS))
			return MM_FAIL;
		mm->UnregCallback(CB_ARENAACTION, aaction, ALLARENAS);
		mm->UnregCallback(CB_PLAYERACTION, paction, ALLARENAS);
		aman->FreeArenaData(adkey);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(prng);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(aman);
		return MM_OK;
	}
	return MM_FAIL;
}

