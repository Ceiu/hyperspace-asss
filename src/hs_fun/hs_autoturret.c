/*
   Replacement Autoturret by D1st0rt
   Includes:
   + No more team targeting
   + Permanent Turrets
   + Surrounding turrets, totally customizable
*/

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

local struct TurretData * new_sturret(Player *p, int timeout, int interval,
		Player *p_for_position, int weap)
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
	td->weapon = weap;
	pos->weapon.type = 0;
	pos->weapon.level = 3;
	pos->weapon.shraplevel = 3;
	pos->weapon.shrap = 31;
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

local void Cdropturret(const char *command, const char *params, Player *p, const Target *target)
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

local helptext_t permaturret_help =
"Module: autoturret\n"
"Targets: none\n"
"Args: <optional weapon code from ?surround>\n"
"Same as dropturret, but doesn't go away";

local void Cpermaturret(const char *command, const char *params, Player *p, const Target *target)
{
	Player *turret;
	int count, weap;
	char *next;

	pthread_mutex_lock(&turret_mtx);
	count = LLCount(&turrets);
	pthread_mutex_unlock(&turret_mtx);

	weap = strtol(params, &next, 0);
	if (weap == 0)
		weap = 4;

	if (count < MAXTURRETS)
	{
		turret = fake->CreateFakePlayer(
				"<permaturret>",
				p->arena,
				SHIP_TERRIER,
				p->p_freq);
		new_sturret(turret, 1500, 150, p, weap);
	}
}

local void Cresetturrets(const char *command, const char *params, Player *p, const Target *target)
{
	ticks_t now = current_ticks();
	Link *l;

	pthread_mutex_lock(&turret_mtx);
	for (l = LLGetHead(&turrets); l; l = l->next)
	{
		struct TurretData *td = l->data;
		td->endtime = now - 10;
		td->p->pkt.ship = SHIP_WARBIRD;
	}
	pthread_mutex_unlock(&turret_mtx);
}

/* fire control algorithm */

/* quick integer square root */
local long lhypot (register long dx, register long dy)
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
		if (i->type != T_FAKE && i->pkt.freq != td->p->pkt.freq)
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
		if ((TICK_GT(now, td->endtime) || !td->p->arena) &&  td->p->pkt.ship != SHIP_TERRIER)
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
			if(td->p->pkt.ship != SHIP_TERRIER)
				td->pos.bounty = TICK_DIFF(td->endtime, now) / 100;
			else
				td->pos.bounty = 1337;
			td->pos.time = now;
			td->pos.weapon.type = td->weapon;
			if(td->p->pkt.ship != SHIP_TERRIER)
				td->pos.energy = td->pos.extra.energy =	TICK_DIFF(td->endtime, now) / 100;
			else
				td->pos.energy = 1337;

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
						20000);
			pd->Unlock();

			game->FakePosition(td->p, &td->pos, sizeof(td->pos));
		}
		else if (TICK_GT(now, td->tosend))
		{
			Player *bp;

			td->tosend = now + 15;
			if(td->p->pkt.ship != SHIP_TERRIER)
				td->pos.bounty = TICK_DIFF(td->endtime, now) / 100;
			else
				td->pos.bounty = 1337;
			td->pos.time = now;
			td->pos.weapon.type = 0;
			if(td->p->pkt.ship != SHIP_TERRIER)
				td->pos.energy = td->pos.extra.energy =	TICK_DIFF(td->endtime, now) / 100;
			else
				td->pos.energy = 1337;

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

local helptext_t surround_help =
"Module: autoturret\n"
"Targets: none\n"
"Args: <size> <weapon> <shape>\n"
"Dropturret, but better\n"
"Size is in pixels, 400 is recommended\n"
"Weapons: 1=bullet, 2=bouncing, 3=bomb, 4=proxed, 5=rep, 6=decoy, 7=burst, 8=thor\n"
"Shapes: 1=Horiz line, 2=Vert line, 3=Circle";

#define defaultit size = 400;\
				  weap = W_PROXBOMB

#include <string.h>
local void Csurround(const char *command, const char *params, Player *p, const Target *target)
{
	int count, angle, size, weap, shape;
	char *next, *next2;
	Player *turret;
	Player *posit = amalloc(sizeof(*p));

	pthread_mutex_lock(&turret_mtx);
	count = LLCount(&turrets);
	pthread_mutex_unlock(&turret_mtx);

	size = strtol(params, &next, 0);
	if (next == params) return;

	while (*next == ',' || *next == ' ') next++;
	weap = strtol(next, &next2, 0);
	if (next == params) return;

	while (*next == ',' || *next == ' ') next++;
	shape = strtol(next2, NULL, 0);
	if (size == 0 || weap == 0 || shape == 0) return;

	switch(shape)
	{
		case 1: //Horizontal
			for(count = -5; count < 5; count++)
			{
				posit->position.x = p->position.x + (size * count);
				posit->position.y = p->position.y;
				turret = fake->CreateFakePlayer(
						"<autoturret>",
						p->arena,
						SHIP_JAVELIN,
						p->p_freq);
				new_sturret(turret, 1500, 50, posit, weap);
			}
		break;

		case 2: //Vertical
			for(count = -5; count < 5; count++)
			{
				posit->position.x = p->position.x;
				posit->position.y = p->position.y + (size * count);
				turret = fake->CreateFakePlayer(
						"<autoturret>",
						p->arena,
						SHIP_JAVELIN,
						p->p_freq);
				new_sturret(turret, 1500, 50, posit, weap);
			}
		break;

		default: //Circle
			for(angle = 1; angle < 360; angle += 30)
			{
				posit->position.y = p->position.y + size*sin(angle);
				posit->position.x = p->position.x + size*cos(angle);
				turret = fake->CreateFakePlayer(
						"<autoturret>",
						p->arena,
						SHIP_JAVELIN,
						p->p_freq);
				new_sturret(turret, 1500, 50, posit, weap);
			}
		break;

	}

}

EXPORT const char info_hs_autoturret[] = "v1.0 D1st0rt";

EXPORT int MM_hs_autoturret(int action, Imodman *mm_, Arena *arena)
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
		cmd->AddCommand("surround", Csurround, ALLARENAS, surround_help);
		cmd->AddCommand("resetturrets", Cresetturrets, ALLARENAS, NULL);
		cmd->AddCommand("permaturret", Cpermaturret, ALLARENAS, permaturret_help);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		cmd->RemoveCommand("dropturret", Cdropturret, ALLARENAS);
		cmd->RemoveCommand("resetturrets", Cresetturrets, ALLARENAS);
		cmd->RemoveCommand("surround", Csurround, ALLARENAS);
		cmd->RemoveCommand("permaturret", Cpermaturret, ALLARENAS);
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
