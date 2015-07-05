
/* dist: public */

#include <stdlib.h>

#include "asss.h"
#include "fake.h"

#define MAXTURRETS 15


struct TurretData
{
	Player *p;
	int interval, weapon;
	ticks_t endtime, tofire, tosend;
	struct C2SPosition pos;
};


local LinkedList turrets;
local pthread_mutex_t turret_mtx = PTHREAD_MUTEX_INITIALIZER;

local Imodman *mm;
local Iconfig *cfg;
local Iplayerdata *pd;
local Icmdman *cmd;
local Igame *game;
local Ifake *fake;

local struct TurretData * new_turret(Player *p, int timeout, int interval,
		Player *p_for_position)
{
	struct C2SPosition *pos;
	struct PlayerPosition *src = &p_for_position->position;
	struct TurretData *td = amalloc(sizeof(*td));
	ticks_t gtc = current_ticks();

	td->p = p;
	td->endtime = gtc + timeout;
	td->interval = interval;
	td->tosend = gtc;
	td->tofire = gtc;

	pos = &td->pos;
	pos->type = C2S_POSITION;
	pos->rotation = src->rotation;
	pos->x = src->x;
	pos->y = src->y;
	pos->xspeed = pos->yspeed = 0;
	pos->status = 0;
	pos->bounty = 0;
	pos->energy = pos->extra.energy = 1000;

	/* a default weapon. caller can fix this up. */
	td->weapon = W_PROXBOMB;
	pos->weapon.type = 0;
	pos->weapon.level = 0;
	pos->weapon.shraplevel = 0;
	pos->weapon.shrap = 0;
	pos->weapon.alternate = 0;

	pthread_mutex_lock(&turret_mtx);
	LLAdd(&turrets, td);
	pthread_mutex_unlock(&turret_mtx);
	return td;
}


local helptext_t dropturret_help =
"Module: autoturret\n"
"Targets: none\n"
"Args: none\n"
"Drops a turret right where your ship is. The turret will fire 10 level 1\n"
"bombs, 1.5 seconds apart, and then disappear.\n";

local void Cdropturret(const char *tc, const char *params, Player *p, const Target *target)
{
	Player *turret;
	int count;

	pthread_mutex_lock(&turret_mtx);
	count = LLCount(&turrets);
	pthread_mutex_unlock(&turret_mtx);

	if (count < MAXTURRETS)
	{
		turret = fake->CreateFakePlayer(
				"<autoturret>",
				p->arena,
				SHIP_WARBIRD,
				p->p_freq);
		new_turret(turret, 1500, 150, p);
	}
}


local void Cresetturrets(const char *tc, const char *params, Player *p, const Target *target)
{
	ticks_t now = current_ticks();
	Link *l;

	pthread_mutex_lock(&turret_mtx);
	for (l = LLGetHead(&turrets); l; l = l->next)
	{
		struct TurretData *td = l->data;
		td->endtime = now - 10;
	}
	pthread_mutex_unlock(&turret_mtx);
}

/* fire control algorithm */

/* quick integer square root */
static long lhypot (register long dx, register long dy)
{
	register unsigned long r, dd;

	dd = dx*dx+dy*dy;

	if (dx < 0) dx = -dx;
	if (dy < 0) dy = -dy;

	/* initial hypotenuse guess (from Gems) */
	r = (dx > dy) ? (dx+(dy>>1)) : (dy+(dx>>1));

	if (r == 0) return (long)r;

	/* converge 3 times */
	r = (dd/r+r)>>1;
	r = (dd/r+r)>>1;
	r = (dd/r+r)>>1;

	return (long)r;
}

#include <math.h>

#define PI 3.14159265f

local unsigned char fileAngle(int x, int y)
{
	double angle;

	/* [-Pi, Pi] + Pi -> [0, 2Pi] */
	angle = atan2(y, x) + PI;

	/* 0 degrees is +y-axis for us, 0 degrees is +x-axis for atan2 */
	return (unsigned char)(angle * 40.0 / (2.0 * PI) + 30) % 40;
}

local inline unsigned char fireControl(int x, int y, int x1, int y1, int vx, int vy, int vp)
{
	int t, guess, err, a, b;

	if (x < 0) x = -x;
	if (y < 0) y = -y;
	if (x1 < 0) x1 = -x1;
	if (y1 < 0) y1 = -y1;

	t = 0;

	do {
		a = x1 + vx * t;
		b = y1 + vy * t;

		guess = lhypot(a - x, b - y) / vp;

		err = abs(guess - t);
		t = guess;
	} while (err > 100);

	return fileAngle(a - x, b - y);
}

local Player *findTurretTarget(struct TurretData *td)
{
	Player *i, *bp = 0;
	int bd = 1024 * 16, dist, x, y, x1, y1;
	Link *link;

	x = td->pos.x;
	y = td->pos.y;
	if (x < 0) x = -x;
	if (y < 0) y = -y;

	FOR_EACH_PLAYER(i)
		if (i->type != T_FAKE)
		{
			x1 = i->position.x;
			y1 = i->position.y;
			if (x1 < 0) x1 = -x1;
			if (y1 < 0) y1 = -y1;

			dist = lhypot(x - x1, y - y1);
			if (dist < bd &&
			    i->p_ship >= 0 &&
			    i->p_ship < 8)
			{
				bp = i;
				bd = dist;
			}
		}

	return bp;
}

local void mlfunc()
{
	ticks_t now;
	Link *l, *next;

	pthread_mutex_lock(&turret_mtx);
	for (l = LLGetHead(&turrets); l; l = next)
	{
		struct TurretData *td = l->data;
		next = l->next; /* so we can remove during the loop */
		now = current_ticks();
		if (TICK_GT(now, td->endtime) || !td->p->arena)
		{
			/* remove it from the list, kill the turret, and free the
			 * memory */
			LLRemove(&turrets, td);
			fake->EndFaked(td->p);
			afree(td);
		}
		else if (TICK_GT(now, td->tofire))
		{
			Player *bp;

			td->tofire = now + td->interval;
			td->tosend = now + 15;
			td->pos.bounty = TICK_DIFF(td->endtime, now) / 100;
			td->pos.time = now;
			td->pos.weapon.type = td->weapon;
			td->pos.energy = td->pos.extra.energy =
				TICK_DIFF(td->endtime, now) / 100;

			pd->Lock();
			bp = findTurretTarget(td);
			td->pos.rotation = !bp ? 0 :
				fireControl(
						td->p->position.x,
						td->p->position.y,
						bp->position.x,
						bp->position.y,
						bp->position.xspeed - td->p->position.xspeed,
						bp->position.yspeed - td->p->position.yspeed,
						cfg->GetInt(td->p->arena->cfg,
							cfg->SHIP_NAMES[(int)td->p->p_ship], "BombSpeed", 10));
			pd->Unlock();

			game->FakePosition(td->p, &td->pos, sizeof(td->pos));
		}
		else if (TICK_GT(now, td->tosend))
		{
			Player *bp;

			td->tosend = now + 15;
			td->pos.bounty = TICK_DIFF(td->endtime, now) / 100;
			td->pos.time = now;
			td->pos.weapon.type = 0;
			td->pos.energy = td->pos.extra.energy =
				TICK_DIFF(td->endtime, now) / 100;

			pd->Lock();
			bp = findTurretTarget(td);
			td->pos.rotation = !bp ? 0 :
				fireControl(
						td->p->position.x,
						td->p->position.y,
						bp->position.x,
						bp->position.y,
						bp->position.xspeed - td->p->position.xspeed,
						bp->position.yspeed - td->p->position.yspeed,
						cfg->GetInt(td->p->arena->cfg,
							cfg->SHIP_NAMES[(int)td->p->p_ship], "BombSpeed", 10));
			pd->Unlock();

			game->FakePosition(td->p, &td->pos, sizeof(td->pos));
		}
	}
	pthread_mutex_unlock(&turret_mtx);
}

EXPORT const char info_autoturret[] = CORE_MOD_INFO("autoturret");

EXPORT int MM_autoturret(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		game = mm->GetInterface(I_GAME, ALLARENAS);
		fake = mm->GetInterface(I_FAKE, ALLARENAS);
		if (!pd || !cmd || !game || !fake) return MM_FAIL;
		LLInit(&turrets);
		mm->RegCallback(CB_MAINLOOP, mlfunc, ALLARENAS);
		cmd->AddCommand("dropturret", Cdropturret, ALLARENAS, dropturret_help);
		cmd->AddCommand("resetturrets", Cresetturrets, ALLARENAS, NULL);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		cmd->RemoveCommand("dropturret", Cdropturret, ALLARENAS);
		cmd->RemoveCommand("resetturrets", Cresetturrets, ALLARENAS);
		mm->UnregCallback(CB_MAINLOOP, mlfunc, ALLARENAS);
		LLEmpty(&turrets);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(game);
		mm->ReleaseInterface(fake);
		return MM_OK;
	}
	return MM_FAIL;
}


