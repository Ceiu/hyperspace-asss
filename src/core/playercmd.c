
/* dist: public */

#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#ifndef WIN32
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/utsname.h>
#include <dirent.h>
#include <unistd.h>
#endif


#include "asss.h"
#include "jackpot.h"
#include "persist.h"
#include "redirect.h"


/* global data */

local Iplayerdata *pd;
local Ichat *chat;
local Ilogman *lm;
local Icmdman *cmd;
local Inet *net;
local Iconfig *cfg;
local Icapman *capman;
local Igroupman *groupman;
local Imainloop *ml;
local Iarenaman *aman;
local Igame *game;
local Ijackpot *jackpot;
local Iflagcore *flagcore;
local Iballs *balls;
local Ilagquery *lagq;
local Ipersist *persist;
local Istats *stats;
local Imapdata *mapdata;
local Iredirect *redir;
local Imodman *mm;

static ticks_t startedat;


struct arena_list_piece
{
	const char *name;
	int count : 31;
	unsigned current : 1;
};

local int sort_arena_list(const void *_a, const void *_b)
{
	const struct arena_list_piece *a = (struct arena_list_piece *)_a;
	const struct arena_list_piece *b = (struct arena_list_piece *)_b;
	if (a->count)
	{
		if (!b->count)
			return TRUE;
	}
	else
	{
		if (b->count)
			return FALSE;
	}

	if (a->name[0] == '#')
	{
		if (b->name[0] != '#')
			return FALSE;
	}
	else
	{
		if (b->name[0] == '#')
			return TRUE;
	}

	if (a->count > b->count)
		return TRUE;
	else if (b->count > a->count)
		return FALSE;

	return strcmp(a->name, b->name) < 0;
}

local void translate_arena_packet(Player *p, char *pkt, int len)
{
	Link *link;
	LinkedList arenas = LL_INITIALIZER;
	struct arena_list_piece *piece;

	const char *pos = pkt + 1;

	chat->SendMessage(p, "Available arenas:");
	while (pos-pkt < len)
	{
		struct arena_list_piece *newPiece = alloca(sizeof(*piece));

		const char *next = pos + strlen(pos) + 3;
		int count = ((byte)next[-1] << 8) | (byte)next[-2];
		/* manually two's complement. yuck. */
		if (count & 0x8000)
		{
			newPiece->current = 1;
			newPiece->count = (count ^ 0xffff) + 1;
		}
		else
		{
			newPiece->current = 0;
			newPiece->count = count;
		}
		newPiece->name = pos;
		LLAdd(&arenas, newPiece);

		pos = next;
	}
	LLSort(&arenas, sort_arena_list);
	FOR_EACH(&arenas, piece, link)
	{
		if (piece->current)
			chat->SendMessage(p, "  %-16s %3d (current)", piece->name, piece->count);
		else
			chat->SendMessage(p, "  %-16s %3d", piece->name, piece->count);
	}
	LLEmpty(&arenas);
}

/* returns 0 if found, 1 if not */
local int check_arena(char *pkt, int len, char *check)
{
	char *pos = pkt + 1;
	while (pos-pkt < len)
	{
		if (strcasecmp(pos, check) == 0)
			return 0;
		/* skip over string, null terminator, and two bytes of count */
		pos += strlen(pos) + 3;
	}
	return 1;
}

local helptext_t arena_help =
"Targets: none\n"
"Args: [{-a}] [{-t}]\n"
"Lists the available arenas. Specifying {-a} will also include\n"
"empty arenas that the server knows about. The {-t} switch forces\n"
"the output to be in text even for regular clients (useful when using\n"
"the Continuum chat window).\n";

local void Carena(const char *tc, const char *params, Player *p, const Target *target)
{
	byte buf[1024];
	byte *pos = buf;
	int l, seehid;
	Arena *a;
	Link *link;
	int cutoff = 0;
	int chatBasedOutput = (IS_CHAT(p) || strstr(params, "-t"));

	*pos++ = S2C_ARENA;

	aman->Lock();

	aman->GetPopulationSummary(NULL, NULL);

	/* build arena info packet */
	seehid = capman && capman->HasCapability(p, CAP_SEEPRIVARENA);
	FOR_EACH_ARENA(a)
	{
		int nameLen = strlen(a->name) + 1;
		int newPacketLen = (pos - buf) + 2 + nameLen;

		/* stop throwing things into the S2C_ARENA packet, but continue if we're looking for a chat-based output */
		/* subtract 6 to account for reliable packet overhead */
		if (!cutoff && newPacketLen > (MAXPACKET-6))
		{
			if (chatBasedOutput)
				cutoff = pos-buf;
			else
				break;
		}
		/* regardless of the type of output, don't buffer overrun */
		if (newPacketLen > sizeof(buf))
			break;

		if (a->status == ARENA_RUNNING &&
		    ( a->name[0] != '#' || seehid || p->arena == a ))
		{
			int count = a->total;
			/* signify current arena */
			if (p->arena == a)
				count = -count;
			memcpy(pos, a->name, nameLen);
			pos += nameLen;
			*pos++ = (count >> 0) & 0xFF;
			*pos++ = (count >> 8) & 0xFF;
		}
	}

	aman->Unlock();

#ifdef CFG_DO_EXTRAARENAS
	/* add in more arenas if requested */
	if (strstr(params, "-a"))
	{
		char aconf[PATH_MAX];
		DIR *dir = opendir("arenas");
		if (dir)
		{
			struct dirent *de;
			while ((de = readdir(dir)))
			{
				int nameLen = strlen(de->d_name) + 1;
				int newPacketLen = (pos - buf) + 2 + nameLen;

				if (!cutoff && newPacketLen > (MAXPACKET-6))
				{
					if (chatBasedOutput)
						cutoff = pos-buf;
					else
						break;
				}

				if (newPacketLen > sizeof(buf))
					break;

				/* every arena must have an arena.conf. this filters out
				 * ., .., CVS, etc. */
				snprintf(aconf, sizeof(aconf), "arenas/%s/arena.conf", de->d_name);
				if (
						de->d_name[0] != '(' &&
						access(aconf, R_OK) == 0 &&
						(de->d_name[0] != '#' || seehid) &&
						check_arena((char *)buf, pos-buf, de->d_name)
				   )
				{
					l = strlen(de->d_name) + 1;
					strncpy((char *)pos, de->d_name, l);
					pos += l;
					*pos++ = 0;
					*pos++ = 0;
				}
			}
			closedir(dir);
		}
	}
#endif

	/* send it */
	if (chatBasedOutput)
		translate_arena_packet(p, (char *)buf, pos-buf);
	else if (IS_STANDARD(p))
		net->SendToOne(p, buf, (cutoff?cutoff:(pos-buf)), NET_RELIABLE);
}


local helptext_t shutdown_help =
"Targets: none\n"
"Args: [{-r}]\n"
"Immediately shuts down the server, exiting with {EXIT_NONE}. If\n"
"{-r} is specified, exit with {EXIT_RECYCLE} instead. The {run-asss}\n"
"script, if it is being used, will notice {EXIT_RECYCLE} and restart\n"
"the server.\n";

local void Cshutdown(const char *tc, const char *params, Player *p, const Target *target)
{
	int code = EXIT_NONE;

	if (!strcmp(params, "-r"))
		code = EXIT_RECYCLE;

	ml->Quit(code);
}

local helptext_t recyclezone_help =
"Targets: none\n"
"Args: none\n"
"Immediately shuts down the server, exiting with {EXIT_RECYCLE}. The "
"{run-asss} script, if it is being used, will notice {EXIT_RECYCLE} "
"and restart the server.\n";

local void Crecyclezone(const char *tc, const char *params, Player *p, const Target *target)
{
	ml->Quit(EXIT_RECYCLE);
}

local helptext_t ballcount_help =
"Targets: none\n"
"Args: [<new # of balls> | +<balls to add> | -<balls to remove>]\n"
"Displays or changes the number of balls in the arena. A number without\n"
"a plus or minus sign is taken as a new count. A plus signifies adding\n"
"that many, and a minus removes that many. Continuum currently supports\n"
"only eight balls.\n";

local void Cballcount(const char *tc, const char *params, Player *p, const Target *target)
{
	Arena *arena = p->arena;
	if (arena)
	{
		ArenaBallData *abd = balls->GetBallData(arena);
		if (*params == '\0')
			chat->SendMessage(p, "%d balls.", abd->ballcount);
		else if (*params == '+' || *params == '-')
			balls->SetBallCount(arena, abd->ballcount + strtol(params, NULL, 0));
		else
			balls->SetBallCount(arena, strtol(params, NULL, 0));
		balls->ReleaseBallData(arena);
	}
}


local helptext_t giveball_help =
"Targets: player or none\n"
"Args: [{-f}] [<ballid>]\n"
"Moves the specified ball to you, or to target player. "
"If no ball is specified, ball id 0 is assumed.\n"
"If -f is specified, the ball is forced onto the player and there will be no shot timer, "
"and if the player is already carrying a ball it will be dropped where they are standing.\n"
"If -f is not specified, then the ball is simply moved underneath a player for him to pick up, "
"but any balls already carried are not dropped.\n";
local void Cgiveball(const char *tc, const char *params, Player *p, const Target *target)
{
	if (p->arena)
	{
		int force = FALSE;
		ArenaBallData *abd = balls->GetBallData(p->arena);
		const char *t = params;

		if (0 == strncmp(t, "-f", 2))
		{
			force = TRUE;
			t += 2;
			while (isspace(*t)) t++;
		}

		int bid = atoi(t);
		if (bid < 0)
		{
			chat->SendMessage(p, "Invalid ball ID.");
		}
		else if (bid >= abd->ballcount)
		{
			chat->SendMessage(p, "Ball %d doesn't exist. Use ?ballcount to add more balls to the arena.", bid);
		}
		else
		{
			int i;
			Player *t = (target->type == T_PLAYER) ? (target->u.p) : p;

			if (t->p_ship == SHIP_SPEC)
			{
				if (t == p)
					chat->SendMessage(p, "You are in spec.");
				else
					chat->SendMessage(p, "%s is in spec.", t->name);
			}
			else if (t->arena != p->arena || t->status != S_PLAYING)
			{
				chat->SendMessage(p, "%s is not in this arena.", t->name);
			}
			else
			{
				struct BallData newbd;
				newbd.state = BALL_ONMAP;
				newbd.carrier = 0;
				newbd.freq = -1;
				newbd.xspeed = newbd.yspeed = 0;
				newbd.x = t->position.x;
				newbd.y = t->position.y;
				newbd.time = current_ticks();

				if (force)
				{
					for (i = 0; i < abd->ballcount; ++i)
					{
						struct BallData *bd = abd->balls + i;
						if (bd->carrier == t && bd->state == BALL_CARRIED)
						{
							balls->PlaceBall(p->arena, i, &newbd);
						}
					}

					newbd.state = BALL_CARRIED;
					newbd.carrier = t;
					newbd.freq = t->p_freq;
					newbd.time = 0;
				}
				balls->PlaceBall(p->arena, bid, &newbd);

				if (t != p)
					chat->SendMessage(p, "Gave ball %d to %s.", bid, t->name);
			}
		}
		balls->ReleaseBallData(p->arena);
	}
}


local helptext_t moveball_help =
"Targets: none\n"
"Args: <ballid> <xtile> <ytile>\n"
"Moves the specified ball to the specified coordinates.\n";
local void Cmoveball(const char *tc, const char *params, Player *p, const Target *target)
{
	if (p->arena)
	{
		char *next, *next2;
		ArenaBallData *abd = balls->GetBallData(p->arena);
		int bid = strtol(params, &next, 0);

		if (bid < 0 || next == params)
		{
			chat->SendMessage(p, "Invalid ball ID.");
		}
		else if (bid >= abd->ballcount)
		{
			chat->SendMessage(p, "Ball %d doesn't exist. Use ?ballcount to add more balls to the arena.", bid);
		}
		else
		{
			struct BallData newbd;
			int x, y;
			int proceed = TRUE;
			x = strtol(next, &next2, 0);
			if (next == next2 || x < 0 || x >= 1024)
			{
				chat->SendMessage(p, "Invalid X coordinate.");
				proceed = FALSE;
			}
			else
			{
				while (*next2 == ',' || *next2 == ' ') next2++;
				y = strtol(next2, &next, 0);
				if (next == next2 || y < 0 || y >= 1024)
				{
					chat->SendMessage(p, "Invalid Y coordinate.");
					proceed = FALSE;
				}
			}

			if (proceed)
			{
				newbd.state = BALL_ONMAP;
				newbd.carrier = NULL;
				newbd.freq = -1;
				newbd.xspeed = newbd.yspeed = 0;
				newbd.x = (x << 4) + 8;
				newbd.y = (y << 4) + 8;
				newbd.time = current_ticks();

				balls->PlaceBall(p->arena, bid, &newbd);
				chat->SendMessage(p, "Moved ball %d to (%d,%d).", bid, x, y);
			}
		}
		balls->ReleaseBallData(p->arena);
	}
}


local helptext_t spawnball_help =
"Targets: none\n"
"Args: [<ballid>]\n"
"Resets the specified existing ball back to its spawn location.\n"
"If no ball is specified, ball id 0 is assumed.\n";
local void Cspawnball(const char *tc, const char *params, Player *p, const Target *target)
{
	if (p->arena)
	{
		int bid = atoi(params);
		ArenaBallData *abd = balls->GetBallData(p->arena);

		if (bid < 0)
		{
			chat->SendMessage(p, "Invalid ball ID.");
		}
		else if (bid >= abd->ballcount)
		{
			chat->SendMessage(p, "Ball %d doesn't exist. Use ?ballcount to add more balls to the arena.", bid);
		}
		else
		{
			balls->SpawnBall(p->arena, bid);
			chat->SendMessage(p, "Respawned ball %d.", bid);
		}

		balls->ReleaseBallData(p->arena);
	}
}


local helptext_t ballinfo_help =
"Targets: none\n"
"Args: none\n"
"Displays the last known position of balls, as well as the player\n"
"who is carrying it or who fired it, if applicable.\n";
local void Cballinfo(const char *tc, const char *params, Player *p, const Target *target)
{
	int i;

	if (p->arena)
	{
		ArenaBallData *abd = balls->GetBallData(p->arena);
		for (i = 0; i < abd->ballcount; ++i)
		{
			struct BallData *bd = abd->balls + i;
			unsigned short x = (bd->x >> 4) * 20 / 1024;
			unsigned short y = (bd->y >> 4) * 20 / 1024;

			switch (bd->state)
			{
			case BALL_ONMAP:
				if (bd->carrier)
				{
					chat->SendMessage(p,
						"ball %d: shot by %s (freq %d) from %c%d (%d,%d)",
						i, bd->carrier->name, bd->freq, 'A'+x, y+1, bd->x>>4, bd->y>>4);
				}
				else
				{
					chat->SendMessage(p,
						"ball %d: on map (freq %d) %s at %c%d (%d,%d)",
						i, bd->freq, (bd->xspeed||bd->yspeed)?"last seen":"still", 'A'+x, y+1, bd->x>>4, bd->y>>4);
				}
				break;
			case BALL_CARRIED:
				chat->SendMessage(p,
					"ball %d: carried by %s (freq %d) at %c%d (%d,%d)",
					i, bd->carrier->name, bd->freq, 'A'+x, y+1, p->position.x>>4, p->position.y>>4);
				break;
			case BALL_WAITING:
				chat->SendMessage(p, "ball %d: waiting to be respawned", i);
			default:
				break;
			}
		}
		balls->ReleaseBallData(p->arena);
	}
}

local helptext_t setfreq_help =
"Targets: player, freq, or arena\n"
"Args: [{-f}] <freq number>\n"
"Moves the targets to the specified freq.\n"
"If -f is specified, this command ignores the arena freqman.\n";

local void Csetfreq(const char *tc, const char *params, Player *sender, const Target *target)
{
	int use_fm = 1;
	int freq;
	const char *t = params;
	char err_buf[200];

	if (!*params)
		return;

	if (0 == strncmp(t, "-f", 2))
	{
		use_fm = 0;
		t += 2;
		while (isspace(*t)) t++;
	}
	
	if (use_fm && capman->HasCapability(sender, CAP_FORCE_SHIPFREQCHANGE))
		use_fm = 0;

	freq = atoi(t);

	if (target->type == T_PLAYER)
	{
		Player *p = target->u.p;

		if (use_fm)
		{
			Ifreqman *fm = mm->GetInterface(I_FREQMAN, p->arena);
			if (fm)
			{
				err_buf[0] = '\0';
				fm->FreqChange(p, freq, err_buf, sizeof(err_buf));
				mm->ReleaseInterface(fm);
				if (err_buf[0] != '\0')
					chat->SendMessage(sender, "%s: %s", p->name, err_buf);

			}
			else
			{
				game->SetFreq(p, freq);
			}
		}
		else
		{
			game->SetFreq(p, freq);
		}
	}
	else
	{
		LinkedList set = LL_INITIALIZER;
		Link *l;

		pd->TargetToSet(target, &set);
		for (l = LLGetHead(&set); l; l = l->next)
		{
			Player *p = l->data;

			if (use_fm)
			{
				Ifreqman *fm = mm->GetInterface(I_FREQMAN, p->arena);
				if (fm)
				{
					err_buf[0] = '\0';
					fm->FreqChange(p, freq, err_buf, sizeof(err_buf));
					mm->ReleaseInterface(fm);
					if (err_buf[0] != '\0')
						chat->SendMessage(sender, "%s: %s", p->name, err_buf);
				}
				else
				{
					game->SetFreq(p, freq);
				}
			}
			else
			{
				game->SetFreq(p, freq);
			}
		}
		LLEmpty(&set);
	}
}


local helptext_t setship_help =
"Targets: player, freq, or arena\n"
"Args: [{-f}] <ship number>\n"
"Sets the targets to the specified ship. The argument must be a\n"
"number from 1 (Warbird) to 8 (Shark), or 9 (Spec).\n"
"If -f is specified, this command ignores the arena freqman.\n";

local void Csetship(const char *tc, const char *params, Player *sender, const Target *target)
{
	int use_fm = 1;
	int ship;
	const char *t = params;
	char err_buf[200];

	if (!*params)
		return;

	if (0 == strncmp(t, "-f", 2))
	{
		use_fm = 0;
		t += 2;
		while (isspace(*t)) t++;
	}
	
	if (use_fm && capman->HasCapability(sender, CAP_FORCE_SHIPFREQCHANGE))
		use_fm = 0;

	ship = (atoi(t) - 1) % (SHIP_SPEC + 1);
	ship = abs(ship);

	if (target->type == T_PLAYER)
	{
		Player *p = target->u.p;

		if (use_fm)
		{
			Ifreqman *fm = mm->GetInterface(I_FREQMAN, p->arena);
			if (fm)
			{
				err_buf[0] = '\0';
				fm->ShipChange(p, ship, err_buf, sizeof(err_buf));
				mm->ReleaseInterface(fm);
				if (err_buf[0] != '\0')
					chat->SendMessage(sender, "%s: %s", p->name, err_buf);
			}
			else
			{
				game->SetShip(p, ship);
			}
		}
		else
		{
			game->SetShip(p, ship);
		}
	}
	else
	{
		LinkedList set = LL_INITIALIZER;
		Link *l;

		pd->TargetToSet(target, &set);
		for (l = LLGetHead(&set); l; l = l->next)
		{
			Player *p = l->data;

			if (use_fm)
			{
				Ifreqman *fm = mm->GetInterface(I_FREQMAN, p->arena);
				if (fm)
				{
					err_buf[0] = '\0';
					fm->ShipChange(p, ship, err_buf, sizeof(err_buf));
					mm->ReleaseInterface(fm);
					if (err_buf[0] != '\0')
						chat->SendMessage(sender, "%s: %s", p->name, err_buf);
				}
				else
				{
					game->SetShip(p, ship);
				}
			}
			else
			{
				game->SetShip(p, ship);
			}
		}
		LLEmpty(&set);
	}
}


local helptext_t owner_help =
"Targets: none\n"
"Args: none\n"
"Displays the arena owner.\n";

local void Cowner(const char *tc, const char *params, Player *p, const Target *target)
{
	const char *owner = cfg->GetStr(p->arena->cfg, "Owner", "Name");

	if (owner)
	{
		chat->SendMessage(p, "This arena is owned by %s.", owner);
	}
	else
	{
		chat->SendMessage(p, "This arena has no listed owner.");
	}
}


local helptext_t zone_help =
"Targets: none\n"
"Args: none\n"
"Displays the name of this zone.\n";

local void Czone(const char *tc, const char *params, Player *p, const Target *target)
{
	const char *zone = cfg->GetStr(GLOBAL, "Billing", "ServerName");
	chat->SendMessage(p, "Zone: %s.", zone ? zone : "(none)");
}


local helptext_t version_help =
"Targets: none\n"
"Args: none\n"
"Prints out the version and compilation date of the server. It might also\n"
"print out some information about the machine that it's running on.\n";

local void Cversion(const char *tc, const char *params, Player *p, const Target *target)
{
	chat->SendMessage(p, "ASSS %s built on %s", ASSSVERSION, BUILDDATE);
#ifdef CFG_EXTRA_VERSION_INFO
#ifndef WIN32
	{
		struct utsname un;
		uname(&un);
		chat->SendMessage(p, "Running on %s %s, host: %s, machine: %s",
				un.sysname, un.release, un.nodename, un.machine);
	}
#else
	{
		OSVERSIONINFO vi;
		DWORD len;
		char name[MAX_COMPUTERNAME_LENGTH + 1];

		vi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
		GetVersionEx(&vi);

		len = MAX_COMPUTERNAME_LENGTH + 1;
		GetComputerName(name, &len);

		chat->SendMessage(p, "Running on %s %s (version %ld.%ld.%ld), host: %s",
			vi.dwPlatformId == VER_PLATFORM_WIN32s ? "Windows 3.11" :
				vi.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS ?
					(vi.dwMinorVersion == 0 ? "Windows 95" : "Windows 98") :
				vi.dwPlatformId == VER_PLATFORM_WIN32_NT ? "Windows NT" : "Windows",
			vi.szCSDVersion,
			vi.dwMajorVersion, vi.dwMinorVersion,
			vi.dwBuildNumber,
			name);
	}
#endif
#endif

#ifdef CFG_LOG_PRIVATE
	chat->SendMessage(p, "This server IS logging private and chat messages.");
#endif
}


struct lsmod_ctx
{
	LinkedList names;
	const char *substr;
};

local void add_mod(const char *name, const char *info, void *clos)
{
	struct lsmod_ctx *ctx = clos;
	if (ctx->substr == NULL || strcasestr(name, ctx->substr) != NULL)
		LLAdd(&ctx->names, name);
}

local helptext_t lsmod_help =
"Targets: none\n"
"Args: [{-a}] [{-s}] [<text>]\n"
"Lists all the modules currently loaded into the server. With {-a}, lists\n"
"only modules attached to this arena. With {-s}, sorts by name.\n"
"With optional text, limits modules displayed to those whose names\n"
"contain the given text.\n";

local void Clsmod(const char *tc, const char *params, Player *p, const Target *target)
{
	Arena *limit = NULL;
	int sort = FALSE;
	struct lsmod_ctx ctx;
	char word[64], substr[64];
	const char *tmp = NULL;

	LLInit(&ctx.names);
	ctx.substr = NULL;

	while (strsplit(params, " ", word, sizeof(word), &tmp))
	{
		if (strcmp(word, "-a") == 0)
			limit = p->arena;
		else if (strcmp(word, "-s") == 0)
			sort = TRUE;
		else
		{
			astrncpy(substr, word, sizeof(substr));
			ctx.substr = substr;
		}
	}

	mm->EnumModules(add_mod, (void*)&ctx, limit);
	if (sort)
		LLSort(&ctx.names, LLSort_StringCompare);

	if (limit)
		chat->SendMessage(p, "Modules attached to arena %s:", limit->name);
	else
		chat->SendMessage(p, "Loaded modules:");

	{
		Link *l;
		StringBuffer sb;
		SBInit(&sb);
		for (l = LLGetHead(&ctx.names); l; l = l->next)
			SBPrintf(&sb, ", %s", (const char*)l->data);
		chat->SendWrappedText(p, SBText(&sb, 2));
		SBDestroy(&sb);
	}

	LLEmpty(&ctx.names);
}


local helptext_t modinfo_help =
"Targets: none\n"
"Args: <module name>\n"
"Displays information about the specified module. This might include a\n"
"version number, contact information for the author, and a general\n"
"description of the module.\n";

local void Cmodinfo(const char *tc, const char *params, Player *p, const Target *target)
{
	const char *info = mm->GetModuleInfo(params);
	const char *loader = mm->GetModuleLoader(params);
	if (!info && !loader)
		chat->SendMessage(p, "No information available for module '%s'.", params);
	else
	{
		const char *tmp = NULL, *prefix = "Info: ";
		char buf[100];
		chat->SendMessage(p, "Module: %s", params);
		if (loader)
			chat->SendMessage(p, "Loader: %s", loader);
		if (info)
			while (strsplit(info, "\n", buf, sizeof(buf), &tmp))
			{
				chat->SendMessage(p, "%s%s", prefix, buf);
				prefix = "  ";
			}
	}
}


#ifndef CFG_NO_RUNTIME_LOAD
local helptext_t insmod_help =
"Targets: none\n"
"Args: <module specifier>\n"
"Immediately loads the specified module into the server.\n";

local void Cinsmod(const char *tc, const char *params, Player *p, const Target *target)
{
	int ret;
	ret = mm->LoadModule(params);
	if (ret == MM_OK)
		chat->SendMessage(p, "Module %s loaded successfully.", params);
	else
		chat->SendMessage(p, "Loading module %s failed.", params);
}
#endif


local helptext_t rmmod_help =
"Targets: none\n"
"Args: <module name>\n"
"Attempts to unload the specified module from the server.\n";

local void Crmmod(const char *tc, const char *params, Player *p, const Target *target)
{
	int ret;
	ret = mm->UnloadModule(params);
	if (ret == MM_OK)
		chat->SendMessage(p, "Module %s unloaded successfully.", params);
	else
		chat->SendMessage(p, "Unloading module %s failed.", params);
}


local helptext_t attmod_help =
"Targets: none\n"
"Args: [{-d}] <module name>\n"
"Attaches the specified module to this arena. Or with {-d},\n"
"detaches the module from the arena.\n";

local void Cattmod(const char *tc, const char *params, Player *p, const Target *target)
{
	if (strncmp(params, "-d", 2) == 0)
	{
		const char *t = params + 2;
		while (isspace(*t)) t++;
		if (mm->DetachModule(t, p->arena) == MM_OK)
			chat->SendMessage(p, "Module %s detached.", t);
		else
			chat->SendMessage(p, "Detaching module %s failed.", t);
	}
	else
	{
		if (mm->AttachModule(params, p->arena) == MM_OK)
			chat->SendMessage(p, "Module %s attached.", params);
		else
			chat->SendMessage(p, "Attaching module %s failed.", params);
	}
}

local helptext_t detmod_help = 
"Targets: none\n"
"Args: <module name>\n"
"Detaches the module from the arena.\n";

local void Cdetmod(const char *tc, const char *params, Player *p, const Target *target)
{
	if (mm->DetachModule(params, p->arena) == MM_OK)
		chat->SendMessage(p, "Module %s detached.", params);
	else
		chat->SendMessage(p, "Detaching module %s failed.", params);
}

local helptext_t find_help =
"Targets: none\n"
"Args: <all or part of a player name>\n"
"Tells you where the specified player is right now. If you specify\n"
"only part of a player name, it will try to find a matching name\n"
"using a case insensitive substring search.\n";

local void Cfind(const char *tc, const char *params, Player *p, const Target *target)
{
	Link *link;
	Player *i, *best = NULL;
	int score = INT_MAX; /* lower is better */

	if (target->type != T_ARENA || !*params) return;

	pd->Lock();
	FOR_EACH_PLAYER(i)
	{
		const char *pos;
		if (i->status != S_PLAYING)
			continue;
		if (strcasecmp(i->name, params) == 0)
		{
			/* exact matches always win */
			best = i;
			break;
		}
		pos = strcasestr(i->name, params);
		if (pos)
		{
			/* if only a substring match, score based on distance from
			 * start of name. */
			int newscore = pos - i->name;
			if (newscore < score)
			{
				best = i;
				score = newscore;
			}
		}
	}
	pd->Unlock();

	if (best)
	{
		if (best->arena->name[0] != '#' ||
		    capman->HasCapability(p, CAP_SEEPRIVARENA) ||
		    p->arena == best->arena)
			chat->SendMessage(p, "%s is in arena %s.", best->name, best->arena->name);
		else
			chat->SendMessage(p, "%s is in a private arena.", best->name);
	}
	else
	{
		/* if not found, fall back to the default */
		char newcmd[64];
		snprintf(newcmd, sizeof(newcmd), "\\find %s", params);
		cmd->Command(newcmd, p, target, 0);
	}
}


local helptext_t getgroup_help =
"Targets: player or none\n"
"Args: none\n"
"Displays the group of the player, or if none specified, you.\n";

local void Cgetgroup(const char *tc, const char *params, Player *p, const Target *target)
{
	if (target->type == T_PLAYER)
		chat->SendMessage(p, "%s is in group %s.",
				target->u.p->name,
				groupman->GetGroup(target->u.p));
	else if (target->type == T_ARENA)
		chat->SendMessage(p, "You are in group %s.",
				groupman->GetGroup(p));
	else
		chat->SendMessage(p, "Bad target!");
}


local helptext_t setgroup_help =
"Targets: player\n"
"Args: [{-a}] [{-p}] <group name>\n"
"Assigns the group given as an argument to the target player. The player\n"
"must be in group {default}, or the server will refuse to change his\n"
"group. Additionally, the player giving the command must have an\n"
"appropriate capability: {setgroup_foo}, where {foo} is the\n"
"group that he's trying to set the target to.\n"
"\n"
"The optional {-p} means to assign the group permanently. Otherwise, when\n"
"the target player logs out or changes arenas, the group will be lost.\n"
"\n"
"The optional {-a} means to make the assignment local to the current\n"
"arena, rather than being valid in the entire zone.\n";

local void Csetgroup(const char *tc, const char *params, Player *p, const Target *target)
{
	int perm = 0, global = 1;
	Player *t = target->u.p;
	char cap[MAXGROUPLEN+16];

	if (!*params) return;
	if (target->type != T_PLAYER) return;

	while (*params && strchr(params, ' '))
	{
		if (!strncmp(params, "perm", 4) || !strncmp(params, "-p", 2))
			perm = 1;
		if (!strncmp(params, "arena", 5) || !strncmp(params, "-a", 2))
			global = 0;
		params = strchr(params, ' ') + 1;
	}
	if (!*params) return;

	/* make sure the setter has permissions to set people to this group */
	snprintf(cap, sizeof(cap), "higher_than_%s", params);
	if (!capman->HasCapability(p, cap))
	{
		chat->SendMessage(p, "You don't have permission to give people group %s.", params);
		lm->LogP(L_WARN, "playercmd", p, "doesn't have permission to set to group '%s'",
				params);
		return;
	}

	/* make sure the target isn't in a group already */
	if (strcasecmp(groupman->GetGroup(t), "default"))
	{
		chat->SendMessage(p, "Player %s already has a group. You need to use ?rmgroup first.", t->name);
		lm->LogP(L_WARN, "playercmd", p, "tried to set the group of [%s],"
				"who is in '%s' already, to '%s'",
				t->name, groupman->GetGroup(t), params);
		return;
	}

	if (perm)
	{
		time_t _time;
		struct tm _tm;
		char info[128];

		time(&_time);
		alocaltime_r(&_time, &_tm);
		snprintf(info, sizeof(info), "set by %s on ", p->name);
		strftime(info + strlen(info), sizeof(info) - strlen(info),
				"%a %b %d %H:%M:%S %Y", &_tm);

		groupman->SetPermGroup(t, params, global, info);
		chat->SendMessage(p, "%s is now in group %s.",
				t->name, params);
		chat->SendMessage(t, "You have been assigned to group %s by %s.",
				params, p->name);
	}
	else
	{
		groupman->SetTempGroup(t, params);
		chat->SendMessage(p, "%s is now temporarily in group %s.",
				t->name, params);
		chat->SendMessage(t, "You have temporarily been assigned to group %s by %s.",
				params, p->name);
	}
}


local helptext_t rmgroup_help =
"Targets: player\n"
"Args: none\n"
"Removes the group from a player, returning him to group 'default'. If\n"
"the group was assigned for this session only, then it will be removed\n"
"for this session; if it is a global group, it will be removed globally;\n"
"and if it is an arena group, it will be removed for this arena.\n";

local void Crmgroup(const char *tc, const char *params, Player *p, const Target *target)
{
	Player *t = target->u.p;
	char cap[MAXGROUPLEN+16], info[128];
	const char *grp;
	struct tm _tm;
	time_t _time;

	if (target->type != T_PLAYER) return;

	grp = groupman->GetGroup(t);
	snprintf(cap, sizeof(cap), "higher_than_%s", grp);

	if (!capman->HasCapability(p, cap))
	{
		chat->SendMessage(p, "You don't have permission to take away group %s.", grp);
		lm->LogP(L_WARN, "playercmd", p, "doesn't have permission to take away group '%s'",
				grp);
		return;
	}

	chat->SendMessage(p, "%s has been removed from group %s.", t->name, grp);
	chat->SendMessage(t, "You have been removed group %s.", grp);

	time(&_time);
	alocaltime_r(&_time, &_tm);

	snprintf(info, sizeof(info), "set by %s on ", p->name);
	strftime(info + strlen(info), sizeof(info) - strlen(info),
			"%a %b %d %H:%M:%S %Y", &_tm);

	/* groupman keeps track of the source of the group, so we just have
	 * to call this. */
	groupman->RemoveGroup(t, info);
}


local helptext_t grplogin_help =
"Targets: none\n"
"Args: <group name> <password>\n"
"Logs you in to the specified group, if the password is correct.\n";

local void Cgrplogin(const char *tc, const char *params, Player *p, const Target *target)
{
	char grp[MAXGROUPLEN+1];
	const char *pw;

	pw = delimcpy(grp, params, MAXGROUPLEN, ' ');
	if (grp[0] == '\0' || pw == NULL)
		chat->SendMessage(p, "You must specify a group name and password.");
	else if (groupman->CheckGroupPassword(grp, pw))
	{
		groupman->SetTempGroup(p, grp);
		chat->SendMessage(p, "You are now in group %s.", grp);
	}
	else
		chat->SendMessage(p, "Bad password for group %s.", grp);
}


local helptext_t listmod_help =
"Targets: none\n"
"Args: none\n"
"Lists all staff members logged on, which arena they are in, and\n"
"which group they belong to.\n";

local void Clistmod(const char *tc, const char *params, Player *p, const Target *target)
{
	int seehid = capman->HasCapability(p, CAP_SEEPRIVARENA);
	int seeallstaff = capman->HasCapability(p, CAP_SEE_ALL_STAFF);
	Player *i;
	Link *link;
	const char *grp, *fmt;

	pd->Lock();
	FOR_EACH_PLAYER(i)
	{
		if (i->status != S_PLAYING)
			continue;

		grp = groupman->GetGroup(i);

		if (capman->HasCapability(i, CAP_IS_STAFF))
			fmt = ": %20s %10s %10s";
		else if (seeallstaff && strcmp(grp, "default") != 0 && strcmp(grp, "none") != 0)
			fmt = ": %20s %10s (%s)";
		else
			fmt = NULL;

		if (fmt)
			chat->SendMessage( p, fmt, i->name,
					(i->arena->name[0] != '#' || seehid || p->arena == i->arena) ?
						i->arena->name : "(private)", grp);
	}
	pd->Unlock();
}


local helptext_t netstats_help =
"Targets: none\n"
"Args: none\n"
"Prints out some statistics from the network layer.\n";

local void Cnetstats(const char *tc, const char *params, Player *p, const Target *target)
{
	ticks_t secs = TICK_DIFF(current_ticks(), startedat) / 100;
	unsigned int bwin, bwout;
	struct net_stats stats;

	net->GetStats(&stats);

	chat->SendMessage(p, "netstats: pings=%u  pkts sent=%u  pkts recvd=%u",
			stats.pcountpings, stats.pktsent, stats.pktrecvd);
	bwout = (stats.bytesent + stats.pktsent * 28) / secs;
	bwin = (stats.byterecvd + stats.pktrecvd * 28) / secs;
	chat->SendMessage(p, "netstats: bw out=%u  bw in=%u", bwout, bwin);
	chat->SendMessage(p, "netstats: buffers used=%u/%u (%.1f%%)",
			stats.buffersused, stats.buffercount,
			(double)stats.buffersused/(double)stats.buffercount*100.0);
	chat->SendMessage(p, "netstats: grouped=%d/%d/%d/%d/%d/%d/%d/%d",
			stats.grouped_stats[0],
			stats.grouped_stats[1],
			stats.grouped_stats[2],
			stats.grouped_stats[3],
			stats.grouped_stats[4],
			stats.grouped_stats[5],
			stats.grouped_stats[6],
			stats.grouped_stats[7]);
	chat->SendMessage(p, "netstats: pri=%d/%d/%d/%d/%d",
			stats.pri_stats[0],
			stats.pri_stats[1],
			stats.pri_stats[2],
			stats.pri_stats[3],
			stats.pri_stats[4]);
}


local void do_common_bw_stuff(Player *p, Player *t, ticks_t tm,
		const char *prefix, int include_sensitive)
{
	struct net_client_stats s;
	int ignoring;

	if (!net) return;

	net->GetClientStats(t, &s);
	if (include_sensitive)
	{
		chat->SendMessage(p,
				"%s: ip=%s  port=%d  encname=%s  macid=%u  permid=%u",
				prefix, s.ipaddr, s.port, s.encname, t->macid, t->permid);
	}
	ignoring = game ? (int)(100.0 * game->GetIgnoreWeapons(t)) : 0;
	chat->SendMessage(p,
			"%s: avg bw in/out=%d/%d  ignoringwpns=%d%%  dropped=%d",
			prefix, s.byterecvd*100/tm, s.bytesent*100/tm,
			ignoring, s.pktdropped);
	chat->SendMessage(p,
			"%s: bwlimit=%s",
			prefix, s.bwlimitinfo);
	if (t->flags.no_ship)
		chat->SendMessage(p, "%s: lag too high to play", prefix);
	if (t->flags.no_flags_balls)
		chat->SendMessage(p, "%s: lag too high to carry flags or balls", prefix);
}


local helptext_t info_help =
"Targets: player\n"
"Args: none\n"
"Displays various information on the target player, including which\n"
"client they are using, their resolution, IP address, how long they have\n"
"been connected, and bandwidth usage information.\n";

local void Cinfo(const char *tc, const char *params, Player *p, const Target *target)
{
	if (target->type != T_PLAYER)
		chat->SendMessage(p, "info: must use on a player");
	else
	{
		Player *t = target->u.p;
		const char *prefix = t->name;
		ticks_t tm = TICK_DIFF(current_ticks(), t->connecttime);

		chat->SendMessage(p,
				"%s: pid=%d  name='%s'  squad='%s'  auth=%c  ship=%d  freq=%d",
				prefix, t->pid, t->name, t->squad, t->flags.authenticated ? 'y' : 'n',
				t->p_ship, t->p_freq);
		chat->SendMessage(p,
				"%s: arena=%s  client=%s  res=%dx%d  onfor=%d  connectas=%s",
				prefix, t->arena ? t->arena->name : "(none)", t->clientname, t->xres,
				t->yres, tm / 100, p->connectas ? p->connectas : "<default>");
		if (IS_STANDARD(t))
		{
			do_common_bw_stuff(p, t, tm, prefix, TRUE);
		}
		else if (IS_CHAT(t))
		{
			Ichatnet *chatnet = mm->GetInterface(I_CHATNET, ALLARENAS);
			if (chatnet)
			{
				struct chat_client_stats s;
				chatnet->GetClientStats(t, &s);
				chat->SendMessage(p,
						"%s: ip=%s  port=%d",
						prefix, s.ipaddr, s.port);
				mm->ReleaseInterface(chatnet);
			}
		}
		if (t->flags.see_all_posn)
			chat->SendMessage(p, "%s: requested all position packets", prefix);
		if (t->status != S_PLAYING)
			chat->SendMessage(p, "%s: status=%d", prefix, t->status);
	}
}


local helptext_t where_help =
"Targets: player\n"
"Args: none\n"
"Displays the current location (on the map) of the target player.\n";

local void Cwhere(const char *tc, const char *params, Player *p, const Target *target)
{
	Player *t = target->type == T_PLAYER ? target->u.p : p;
	const char *name = t == p ? "You" : t->name;
	const char *verb = t == p ? "are" : "is";
	if (IS_STANDARD(t))
	{
		int x = t->position.x >> 4;
		int y = t->position.y >> 4;
		chat->SendMessage(p, "%s %s at %c%d (%d,%d)",
				name, verb, 'A' + (x * 20 / 1024), (y * 20 / 1024) + 1, x, y);
	}
	else
		chat->SendMessage(p, "%s %s not using a playable client.", name, verb);
}


local helptext_t setcm_help =
"Targets: player or arena\n"
"Args: see description\n"
"Modifies the chat mask for the target player, or if no target, for the\n"
"current arena. The arguments must all be of the form\n"
"{(-|+)(pub|pubmacro|freq|nmefreq|priv|chat|modchat|all)} or {-t <seconds>}.\n"
"A minus sign and then a word disables that type of chat, and a plus sign\n"
"enables it. The special type {all} means to apply the plus or minus to\n"
"all of the above types. {-t} lets you specify a timeout in seconds.\n"
"The mask will be effective for that time, even across logouts.\n"
"\n"
"Examples:\n"
" * If someone is spamming public macros: {:player:?setcm -pubmacro -t 600}\n"
" * To disable all blue messages for this arena: {?setcm -pub -pubmacro}\n"
" * An equivalent to *shutup: {:player:?setcm -all}\n"
" * To restore chat to normal: {?setcm +all}\n"
"\n"
"Current limitations: You can't currently restrict a particular\n"
"frequency. Leaving and entering an arena will remove a player's chat\n"
"mask, unless it has a timeout.\n";

local void Csetcm(const char *tc, const char *params, Player *p, const Target *target)
{
	chat_mask_t mask;
	int timeout = 0;
	const char *c = params;

	/* grab the original mask */
	if (target->type == T_ARENA)
		mask = chat->GetArenaChatMask(target->u.arena);
	else if (target->type == T_PLAYER)
		mask = chat->GetPlayerChatMask(target->u.p);
	else
	{
		chat->SendMessage(p, "Bad target!");
		return;
	}

	/* change it */
	for (;;)
	{
		chat_mask_t newmask = 0;
		int all = 0;

		/* move to next + or - */
		while (*c != '\0' && *c != '-' && *c != '+')
			c++;
		if (*c == '\0')
			break;

		/* figure out which thing to change */
		c++;
		if (!strncasecmp(c, "all", 3))
			all = 1;
		if (all || !strncasecmp(c, "pubmacro", 8))
			newmask |= 1 << MSG_PUBMACRO;
		if (all || !strncasecmp(c, "pub", 3))
			newmask |= 1 << MSG_PUB;
		if (all || !strncasecmp(c, "freq", 4))
			newmask |= 1 << MSG_FREQ;
		if (all || !strncasecmp(c, "nmefreq", 7))
			newmask |= 1 << MSG_NMEFREQ;
		if (all || !strncasecmp(c, "priv", 4))
			newmask |= (1 << MSG_PRIV) | (1 << MSG_REMOTEPRIV);
		if (all || !strncasecmp(c, "chat", 4))
			newmask |= 1 << MSG_CHAT;
		if (all || !strncasecmp(c, "modchat", 7))
			newmask |= 1 << MSG_MODCHAT;

		if (!strncasecmp(c, "time", 4))
			timeout = strtol(c+4, NULL, 0);
		else if (!strncasecmp(c, "t", 1))
			timeout = strtol(c+1, NULL, 0);

		/* change it */
		if (c[-1] == '+')
			mask &= ~newmask;
		else
			mask |= newmask;
	}

	/* and install it back where it came from */
	if (target->type == T_ARENA)
	{
		chat->SetArenaChatMask(target->u.arena, mask);
		chat->SendMessage(p,
				"Arena %s: %cpub %cpubmacro %cfreq %cnmefreq %cpriv %cchat %cmodchat",
				target->u.arena->name,
				IS_RESTRICTED(mask, MSG_PUB) ? '-' : '+',
				IS_RESTRICTED(mask, MSG_PUBMACRO) ? '-' : '+',
				IS_RESTRICTED(mask, MSG_FREQ) ? '-' : '+',
				IS_RESTRICTED(mask, MSG_NMEFREQ) ? '-' : '+',
				IS_RESTRICTED(mask, MSG_PRIV) ? '-' : '+',
				IS_RESTRICTED(mask, MSG_CHAT) ? '-' : '+',
				IS_RESTRICTED(mask, MSG_MODCHAT) ? '-' : '+'
				);
	}
	else
	{
		chat->SetPlayerChatMask(target->u.p, mask, timeout);
		chat->SendMessage(p,
				"%s: %cpub %cpubmacro %cfreq %cnmefreq %cpriv %cchat %cmodchat -t %d",
				target->u.p->name,
				IS_RESTRICTED(mask, MSG_PUB) ? '-' : '+',
				IS_RESTRICTED(mask, MSG_PUBMACRO) ? '-' : '+',
				IS_RESTRICTED(mask, MSG_FREQ) ? '-' : '+',
				IS_RESTRICTED(mask, MSG_NMEFREQ) ? '-' : '+',
				IS_RESTRICTED(mask, MSG_PRIV) ? '-' : '+',
				IS_RESTRICTED(mask, MSG_CHAT) ? '-' : '+',
				IS_RESTRICTED(mask, MSG_MODCHAT) ? '-' : '+',
				timeout
				);
	}
}

local helptext_t getcm_help =
"Targets: player or arena\n"
"Args: none\n"
"Prints out the chat mask for the target player, or if no target, for the\n"
"current arena. The chat mask specifies which types of chat messages are\n"
"allowed.\n";

local void Cgetcm(const char *tc, const char *params, Player *p, const Target *target)
{
	chat_mask_t mask;

	if (target->type == T_ARENA)
	{
		mask = chat->GetArenaChatMask(target->u.arena);
		chat->SendMessage(p,
				"Arena %s: %cpub %cpubmacro %cfreq %cnmefreq %cpriv %cchat %cmodchat",
				target->u.arena->name,
				IS_RESTRICTED(mask, MSG_PUB) ? '-' : '+',
				IS_RESTRICTED(mask, MSG_PUBMACRO) ? '-' : '+',
				IS_RESTRICTED(mask, MSG_FREQ) ? '-' : '+',
				IS_RESTRICTED(mask, MSG_NMEFREQ) ? '-' : '+',
				IS_RESTRICTED(mask, MSG_PRIV) ? '-' : '+',
				IS_RESTRICTED(mask, MSG_CHAT) ? '-' : '+',
				IS_RESTRICTED(mask, MSG_MODCHAT) ? '-' : '+'
				);
	}
	else if (target->type == T_PLAYER)
	{
		mask = chat->GetPlayerChatMask(target->u.p);
		chat->SendMessage(p,
				"%s: %cpub %cpubmacro %cfreq %cnmefreq %cpriv %cchat %cmodchat -t %d",
				target->u.p->name,
				IS_RESTRICTED(mask, MSG_PUB) ? '-' : '+',
				IS_RESTRICTED(mask, MSG_PUBMACRO) ? '-' : '+',
				IS_RESTRICTED(mask, MSG_FREQ) ? '-' : '+',
				IS_RESTRICTED(mask, MSG_NMEFREQ) ? '-' : '+',
				IS_RESTRICTED(mask, MSG_PRIV) ? '-' : '+',
				IS_RESTRICTED(mask, MSG_CHAT) ? '-' : '+',
				IS_RESTRICTED(mask, MSG_MODCHAT) ? '-' : '+',
				chat->GetPlayerChatMaskTime(target->u.p)
				);
	}
	else
	{
		chat->SendMessage(p, "Bad target!");
		return;
	}
}


local helptext_t a_help =
"Targets: player, freq, or arena\n"
"Args: <text>\n"
"Displays the text as an arena (green) message to the targets.\n";

local void Ca(const char *tc, const char *params, Player *p, const Target *target)
{
	int sound = tc[strlen(tc)+1];
	LinkedList set = LL_INITIALIZER;
	pd->TargetToSet(target, &set);
	chat->SendSetSoundMessage(&set, sound, "%s  -%s", params, p->name);
	LLEmpty(&set);
}


local helptext_t aa_help =
"Targets: player, freq, or arena\n"
"Args: <text>\n"
"Displays the text as an anonymous arena (green) message to the targets.\n";

local void Caa(const char *tc, const char *params, Player *p, const Target *target)
{
	int sound = tc[strlen(tc)+1];
	LinkedList set = LL_INITIALIZER;
	pd->TargetToSet(target, &set);
	chat->SendSetSoundMessage(&set, sound, "%s", params);
	LLEmpty(&set);
}


local helptext_t z_help =
"Targets: none\n"
"Args: <text>\n"
"Displays the text as an arena (green) message to the whole zone.\n";

local void Cz(const char *tc, const char *params, Player *p, const Target *target)
{
	int sound = tc[strlen(tc)+1];
	chat->SendArenaSoundMessage(NULL, sound, "%s  -%s", params, p->name);
}


local helptext_t az_help =
"Targets: none\n"
"Args: <text>\n"
"Displays the text as an anonymous arena (green) message to the whole zone.\n";

local void Caz(const char *tc, const char *params, Player *p, const Target *target)
{
	int sound = tc[strlen(tc)+1];
	chat->SendArenaSoundMessage(NULL, sound, "%s", params);
}


local helptext_t warn_help =
"Targets: player\n"
"Args: <message>\n"
"Send a red warning message to a player.\n";

local void Cwarn(const char *tc, const char *params, Player *p, const Target *target)
{
	if (target->type != T_PLAYER)
		chat->SendMessage(p, "You must target a player.");
	else
	{
		Link link = { NULL, target->u.p };
		LinkedList lst = { &link, &link };
		if (capman->HasCapability(p, CAP_IS_STAFF))
		{
			chat->SendAnyMessage(&lst, MSG_SYSOPWARNING,
					SOUND_BEEP1, NULL, "WARNING: %s  -%s",
					params, p->name);
		}
		else
		{
			chat->SendAnyMessage(&lst, MSG_SYSOPWARNING,
					SOUND_BEEP1, NULL, "WARNING: %s",
					params);
		}
		chat->SendMessage(p, "Player warned.");
	}
}


local helptext_t reply_help =
"Targets: player\n"
"Args: <message>\n"
"Sends a private message to a player.\n"
"Useful for logging replies to moderator help requests.\n";

local void Creply(const char *tc, const char *params, Player *p, const Target *target)
{
	if (target->type != T_PLAYER)
		chat->SendMessage(p, "You must target a player.");
	else
	{
		Link link = { NULL, target->u.p };
		LinkedList lst = { &link, &link };
		chat->SendAnyMessage(&lst, MSG_PRIV, 0, p, "%s", params);
		chat->SendMessage(p, "Private message sent to player.");
	}
}


local helptext_t warpto_help =
"Targets: player, freq, or arena\n"
"Args: <x coord> <y coord>\n"
"Warps target player to coordinate x,y.\n";

local void Cwarpto(const char *tc, const char *params, Player *p, const Target *target)
{
	char *next;
	int x, y;

	x = strtol(params, &next, 0);
	if (next == params) return;
	while (*next == ',' || *next == ' ') next++;
	y = strtol(next, NULL, 0);
	if (x == 0 || y == 0) return;
	game->WarpTo(target, x, y);
}


local helptext_t send_help =
"Targets: player\n"
"Args: <arena name>\n"
"Sends target player to the named arena. (Works on Continuum users only.)\n";

local void Csend(const char *tc, const char *params, Player *p, const Target *target)
{
	Player *t = target->u.p;
	if (target->type != T_PLAYER || *params == '\0')
		return;
	if (t->type == T_CONT || t->type == T_CHAT)
		aman->SendToArena(t, params, 0, 0);
	else
		chat->SendMessage(p,
				"You can only use ?send on players using Continuum or chat clients");
}


local helptext_t recyclearena_help =
"Targets: none\n"
"Args: none\n"
"Recycles the current arena without kicking players off.\n";

local void Crecyclearena(const char *tc, const char *params, Player *p, const Target *target)
{
	if (aman->RecycleArena(p->arena) == MM_FAIL)
		chat->SendMessage(p, "Arena recycle failed; check the log for details.");
}


local helptext_t shipreset_help =
"Targets: player, freq, or arena\n"
"Args: none\n"
"Resets the target players' ship(s).\n";

local void Cshipreset(const char *tc, const char *params, Player *p, const Target *target)
{
	game->ShipReset(target);
}


local helptext_t sheep_help = NULL;

local void Csheep(const char *tc, const char *params, Player *p, const Target *target)
{
	Arena *arena = p->arena;
	const char *sheepmsg = NULL;

	if (target->type != T_ARENA)
		return;

	/* cfghelp: Misc:SheepMessage, arena, string
	 * The message that appears when someone says ?sheep */
	if (arena)
		sheepmsg = cfg->GetStr(arena->cfg, "Misc", "SheepMessage");

	if (sheepmsg)
		chat->SendSoundMessage(p, 24, "%s", sheepmsg);
	else
		chat->SendSoundMessage(p, 24, "Sheep successfully cloned -- hello Dolly");
}


local helptext_t specall_help =
"Targets: player, freq, or arena\n"
"Args: none\n"
"Sends all of the targets to spectator mode.\n";

local void Cspecall(const char *tc, const char *params, Player *p, const Target *target)
{
	Arena *arena = p->arena;
	LinkedList set = LL_INITIALIZER;
	Link *l;

	pd->TargetToSet(target, &set);
	for (l = LLGetHead(&set); l; l = l->next)
		game->SetShipAndFreq(l->data, SHIP_SPEC, arena->specfreq);
	LLEmpty(&set);
}


local helptext_t geta_help =
"Targets: none\n"
"Args: section:key\n"
"Displays the value of an arena setting. Make sure there are no\n"
"spaces around the colon.\n";

local helptext_t getg_help =
"Targets: none\n"
"Args: section:key\n"
"Displays the value of a global setting. Make sure there are no\n"
"spaces around the colon.\n";

local void Cget_generic(const char *tc, const char *params, Player *p, const Target *target)
{
	ConfigHandle ch = strcasecmp(tc, "geta") == 0 ? p->arena->cfg : GLOBAL;
	const char *res = cfg->GetStr(ch, params, NULL);
	if (res)
		chat->SendMessage(p, "%s=%s", params, res);
	else
		chat->SendMessage(p, "%s not found.", params);
}

local helptext_t seta_help =
"Targets: none\n"
"Args: [{-t}] section:key=value\n"
"Sets the value of an arena setting. Make sure there are no\n"
"spaces around either the colon or the equals sign. A {-t} makes\n"
"the setting temporary.\n";

local helptext_t setg_help =
"Targets: none\n"
"Args: [{-t}] section:key=value\n"
"Sets the value of a global setting. Make sure there are no\n"
"spaces around either the colon or the equals sign. A {-t} makes\n"
"the setting temporary.\n";

local void Cset_generic(const char *tc, const char *params, Player *p, const Target *target)
{
	ConfigHandle ch = strcasecmp(tc, "seta") == 0 ? p->arena->cfg : GLOBAL;
	time_t _time;
	struct tm _tm;
	char info[128], key[MAXSECTIONLEN+MAXKEYLEN+2], *k = key;
	const char *t = params;
	int perm = TRUE, colons = 0;

	time(&_time);
	alocaltime_r(&_time, &_tm);
	snprintf(info, sizeof(info), "set by %s on ", p->name);
	strftime(info + strlen(info), sizeof(info) - strlen(info),
			"%a %b %d %H:%M:%S %Y", &_tm);

	if (strncmp(t, "-t", 2) == 0)
	{
		perm = FALSE;
		t += 2;
		while (*t && *t == ' ') t++;
	}

	while (*t && *t != '=' && (*t != ':' || colons != 1) && (k-key) < (MAXSECTIONLEN+MAXKEYLEN))
		if ((*k++ = *t++) == ':')
			colons++;
	if (*t != '=' && (*t != ':' || colons != 1)) return;
	*k = '\0'; /* terminate key */
	t++; /* skip over = or : */

	cfg->SetStr(ch, key, NULL, t, info, perm);
}


local helptext_t prize_help =
"Targets: player, freq, or arena\n"
"Args: see description\n"
"Gives the specified prizes to the target player(s).\n"
"\n"
"Prizes are specified with an optional count, and then a prize name (e.g.\n"
"{3 reps}, {anti}). Negative prizes can be specified with a '-' before\n"
"the prize name or the count (e.g. {-prox}, {-3 bricks}, {5 -guns}). More\n"
"than one prize can be specified in one command. A count without a prize\n"
"name means {random}. For compatability, numerical prize ids with {#} are\n"
"supported.\n";

local void Cprize(const char *tc, const char *params, Player *p, const Target *target)
{
#define BAD_TYPE 10000
	const char *tmp = NULL;
	char word[32];
	int i, type, count = 1, t;
	enum { last_none, last_count, last_word } last = last_none;
	struct
	{
		const char *string;
		int type;
	}
	lookup[] =
	{
		{ "random",    0 },
		{ "charge",   13 }, /* must come before "recharge" */
		{ "x",         6 }, /* must come before "prox" */
		{ "recharge",  1 },
		{ "energy",    2 },
		{ "rot",       3 },
		{ "stealth",   4 },
		{ "cloak",     5 },
		{ "warp",      7 },
		{ "gun",       8 },
		{ "bomb",      9 },
		{ "bounce",   10 },
		{ "thrust",   11 },
		{ "speed",    12 },
		{ "shutdown", 14 },
		{ "multi",    15 },
		{ "prox",     16 },
		{ "super",    17 },
		{ "shield",   18 },
		{ "shrap",    19 },
		{ "anti",     20 },
		{ "rep",      21 },
		{ "burst",    22 },
		{ "decoy",    23 },
		{ "thor",     24 },
		{ "mprize",   25 },
		{ "brick",    26 },
		{ "rocket",   27 },
		{ "port",     28 },
	};

	while (strsplit(params, " ,", word, sizeof(word), &tmp))
		if ((t = strtol(word, NULL, 0)) != 0)
		{
			/* this is a count */
			count = t;
			last = last_count;
		}
		else /* try a word */
		{
			/* negative prizes are marked with negative counts, for now */
			if (word[0] == '-')
				count = -count;

			/* now try to find the word */
			type = BAD_TYPE;
			if (word[0] == '#')
				type = strtol(word+1, NULL, 0);
			else
				for (i = 0; i < sizeof(lookup)/sizeof(lookup[0]); i++)
					if (strstr(word, lookup[i].string))
						type = lookup[i].type;

			if (type != BAD_TYPE)
			{
				/* but for actually sending them, they must be marked with
				 * negative types and positive counts. */
				if (count < 0)
				{
					type = -type;
					count = -count;
				}

				game->GivePrize(target, type, count);

				/* reset count to 1 once we hit a successful word */
				count = 1;
			}

			last = last_word;
		}

	if (last == last_count)
		/* if the line ends in a count, do that many of random */
		game->GivePrize(target, 0, count);

#undef BAD_TYPE
}


/* locking commands */

local helptext_t lock_help =
"Targets: player, freq, or arena\n"
"Args: [-n] [-s] [-t <timeout>]\n"
"Locks the specified targets so that they can't change ships. Use ?unlock\n"
"to unlock them. By default, ?lock won't change anyone's ship. If {-s} is\n"
"present, it will spec the targets before locking them. If {-n} is present,\n"
"it will notify players of their change in status. If {-t} is present, you\n"
"can specify a timeout in seconds for the lock to be effective.\n";

local void Clock(const char *tc, const char *params, Player *p, const Target *target)
{
	const char *t = strstr(params, "-t");
	game->Lock(
			target,
			strstr(params, "-n") != NULL,
			strstr(params, "-s") != NULL,
			t ? strtol(t+2, NULL, 0) : 0);
}


local helptext_t unlock_help =
"Targets: player, freq, or arena\n"
"Args: [-n]\n"
"Unlocks the specified targets so that they can now change ships. An optional\n"
"{-n} notifies players of their change in status.\n";

local void Cunlock(const char *tc, const char *params, Player *p, const Target *target)
{
	game->Unlock(target, strstr(params, "-n") != NULL);
}


local helptext_t lockarena_help =
"Targets: arena\n"
"Args: [-n] [-a] [-i] [-s]\n"
"Changes the default locked state for the arena so entering players will be locked\n"
"to spectator mode. Also locks everyone currently in the arena to their ships. The {-n}\n"
"option means to notify players of their change in status. The {-a} options means to\n"
"only change the arena's state, and not lock current players. The {-i} option means to\n"
"only lock entering players to their initial ships, instead of spectator mode. The {-s}\n"
"means to spec all players before locking the arena.\n";

local void Clockarena(const char *tc, const char *params, Player *p, const Target *target)
{
	if (target->type != T_ARENA) return;
	game->LockArena(target->u.arena,
			strstr(params, "-n") != NULL,
			strstr(params, "-a") != NULL,
			strstr(params, "-i") != NULL,
			strstr(params, "-s") != NULL);
}


local helptext_t unlockarena_help =
"Targets: arena\n"
"Args: [-n] [-a]\n"
"Changes the default locked state for the arena so entering players will not be\n"
"locked to spectator mode. Also unlocks everyone currently in the arena to their ships\n"
"The {-n} options means to notify players of their change in status. The {-a} option\n"
"means to only change the arena's state, and not unlock current players.\n";

local void Cunlockarena(const char *tc, const char *params, Player *p, const Target *target)
{
	if (target->type != T_ARENA) return;
	game->UnlockArena(target->u.arena,
			strstr(params, "-n") != NULL,
			strstr(params, "-a") != NULL);
}


local helptext_t flaginfo_help =
"Targets: none\n"
"Args: none\n"
"Displays information (status, location, carrier) about all the flags in\n"
"the arena.\n";

local void Cflaginfo(const char *tc, const char *params, Player *p, const Target *target)
{
#define MAXFLAGSTOPRINT 20
	FlagInfo flags[MAXFLAGSTOPRINT];
	int i, n;

	if (target->type != T_ARENA)
		return;

	n = flagcore->GetFlags(p->arena, 0, flags, MAXFLAGSTOPRINT);

	for (i = 0; i < n; i++)
		switch (flags[i].state)
		{
			case FI_NONE:
				chat->SendMessage(p, "flag %d: doesn't exist", i);
				break;

			case FI_ONMAP:
				{
					unsigned short x = flags[i].x * 20 / 1024;
					unsigned short y = flags[i].y * 20 / 1024;

					chat->SendMessage(p,
							"flag %d: on the map at %c%d (%d,%d), owned by freq %d",
							i, 'A'+x, y+1, flags[i].x, flags[i].y, flags[i].freq);
				}
				break;

			case FI_CARRIED:
				if (flags[i].carrier)
					chat->SendMessage(p,
							"flag %d: carried by %s, freq %d",
							i, flags[i].carrier->name, flags[i].carrier->p_freq);
				break;
		}
}


local helptext_t neutflag_help =
"Targets: none\n"
"Args: <flag id>\n"
"Neuts the specified flag in the middle of the arena.\n";

local void Cneutflag(const char *tc, const char *params, Player *p, const Target *target)
{
	Arena *arena = p->arena;
	int flagid, n;
	char *next;
	FlagInfo fi;

	flagid = strtol(params, &next, 0);
	if (next == params ||
	    (n = flagcore->GetFlags(arena, flagid, &fi, 1)) != 1)
	{
		chat->SendMessage(p, "Bad flag id!");
		return;
	}

	/* undocumented flag lets you force a flag away from a player. */
	if (fi.state == FI_ONMAP || strcmp(next, "force") == 0)
	{
		/* set flag state to none, so that the flag timer will neut it
		 * next time it runs. */
		fi.state = FI_NONE;
		flagcore->SetFlags(arena, flagid, &fi, 1);
	}
	else
		chat->SendMessage(p, "That flag isn't currently on the map!");
}


local helptext_t moveflag_help =
"Targets: none\n"
"Args: <flag id> <owning freq> [<x coord> <y coord>]\n"
"Moves the specified flag. You must always specify the freq that will own\n"
"the flag. The coordinates are optional: if they are specified, the flag\n"
"will be moved there, otherwise it will remain where it is.\n";

local void Cmoveflag(const char *tc, const char *params, Player *p, const Target *target)
{
	Arena *arena = p->arena;
	char *next, *next2;
	int flagid, n, x, y;
	FlagInfo fi;

	flagid = strtol(params, &next, 0);
	if (next == params ||
	    (n = flagcore->GetFlags(arena, flagid, &fi, 1)) != 1)
	{
		chat->SendMessage(p, "Bad flag id!");
		return;
	}

	if (fi.state != FI_ONMAP)
	{
		chat->SendMessage(p, "Flag %d isn't on the map!", flagid);
		return;
	}

	fi.freq = strtol(next, &next2, 0);
	if (next == next2 || fi.freq < 0 || fi.freq > 9999)
	{
		chat->SendMessage(p, "Bad freq!");
		return;
	}

	x = strtol(next2, &next, 0);
	while (*next == ',' || *next == ' ') next++;
	y = strtol(next, NULL, 0);
	if (x > 0 && x < 1024 && y > 0 && y < 1024)
	{
		if (mapdata)
			mapdata->FindEmptyTileNear(arena, &x, &y);
		fi.x = x;
		fi.y = y;
	}

	flagcore->SetFlags(arena, flagid, &fi, 1);
}


local helptext_t flagreset_help =
"Targets: none\n"
"Args: none\n"
"Causes the flag game to immediately reset.\n";

local void Cflagreset(const char *tc, const char *params, Player *p, const Target *target)
{
	flagcore->FlagReset(p->arena, -1, 0);
}


local void send_msg_cb(const char *line, void *clos)
{
	chat->SendMessage((Player*)clos, "  %s", line);
}

local helptext_t reloadconf_help =
"Targets: none\n"
"Args: [-f] [path]\n"
"With no args, causes the server to reload any config files that have\n"
"been modifed since they were loaded. With {-f}, forces a reload of all\n"
"open files. With a string, forces a reload of all files whose pathnames\n"
"contain that string.\n";

local void Creloadconf(const char *tc, const char *params, Player *p, const Target *target)
{
	if (strcmp(params, "-f") == 0)
	{
		chat->SendMessage(p, "Reloading all config files:");
		cfg->ForceReload(NULL, send_msg_cb, p);
	}
	else if (*params)
	{
		chat->SendMessage(p, "Reloading config files containing '%s':", params);
		cfg->ForceReload(params, send_msg_cb, p);
	}
	else
	{
		chat->SendMessage(p, "Reloading all modified config files.");
		cfg->CheckModifiedFiles();
	}
}



local helptext_t jackpot_help =
"Targets: none\n"
"Args: none or <arena name> or {all}\n"
"Displays the current jackpot for this arena, the named arena, or all arenas.\n";

local void Cjackpot(const char *tc, const char *params, Player *p, const Target *target)
{
	if (!strcasecmp(params, "all"))
	{
		Arena *arena;
		Link *link;
		int jp, seehid = capman->HasCapability(p, CAP_SEEPRIVARENA);

		aman->Lock();
		FOR_EACH_ARENA(arena)
			if (arena->status == ARENA_RUNNING &&
			    (arena->name[0] != '#' || seehid || p->arena == arena) &&
			    (jp = jackpot->GetJP(arena)) > 0)
				chat->SendMessage(p, "jackpot in %s: %d", arena->name, jp);
		aman->Unlock();
	}
	else if (*params)
	{
		Arena *arena = aman->FindArena(params, NULL, NULL);
		if (arena)
			chat->SendMessage(p, "The jackpot in %s is %d.", arena->name, jackpot->GetJP(arena));
		else
			chat->SendMessage(p, "Arena '%s' doesn't exist.", params);
	}
	else
		chat->SendMessage(p, "The jackpot is %d.", jackpot->GetJP(p->arena));
}


local helptext_t setjackpot_help =
"Targets: none\n"
"Args: <new jackpot value>\n"
"Sets the jackpot for this arena to a new value.\n";

local void Csetjackpot(const char *tc, const char *params, Player *p, const Target *target)
{
	char *next;
	int new = strtol(params, &next, 0);

	if (next != params && new >= 0)
	{
		jackpot->SetJP(p->arena, new);
		chat->SendMessage(p, "The jackpot is %d.", jackpot->GetJP(p->arena));
	}
	else
		chat->SendMessage(p, "setjackpot: bad value");
}


local helptext_t uptime_help =
"Targets: none\n"
"Args: none\n"
"Displays how long the server has been running.\n";

local void Cuptime(const char *tc, const char *params, Player *p, const Target *target)
{
	ticks_t secs = TICK_DIFF(current_ticks(), startedat) / 100;
	int days, hours, mins;
	char day_string[20];
	char hour_string[20];
	char min_string[20];
	char sec_string[20];

	days = secs / 86400;
	secs %= 86400;
	hours = secs / 3600;
	secs %= 3600;
	mins = secs / 60;
	secs %= 60;

	day_string[0] = hour_string[0] = min_string[0] = sec_string[0] = '\0';

	if (days)
	{
		if (days == 1)
			sprintf(day_string, "1 day, ");
		else
			sprintf(day_string, "%d days, ", days);
	}
	if (hours || days)
	{
		if (hours == 1)
			sprintf(hour_string, "1 hour, ");
		else
			sprintf(hour_string, "%d hours, ", hours);
	}
	if (mins || hours || days)
	{
		if (mins == 1)
			sprintf(min_string, "1 minute and ");
		else
			sprintf(min_string, "%d minutes and ", mins);
	}
	if (secs || mins || hours || days)
	{
		if (secs == 1)
			sprintf(sec_string, "1 second.");
		else
			sprintf(sec_string, "%d seconds.", secs);
	}

	chat->SendMessage(p, "This server has been online for %s%s%s%s",
			day_string, hour_string, min_string, sec_string);
}


/* lag commands */

local helptext_t lag_help =
"Targets: none or player\n"
"Args: [{-v}]\n"
"Displays lag information about you or a target player.\n"
"Use {-v} for more detail. The format of the ping fields is\n"
"\"last average (min-max)\".\n";

local void Clag(const char *tc, const char *params, Player *p, const Target *target)
{
	struct PingSummary pping, cping, rping;
	struct PLossSummary ploss;
	int avg;
	Player *t = target->type == T_PLAYER ? target->u.p : p;
	const char *prefix = t == p ? "lag" : t->name;

	if (!IS_STANDARD(t))
	{
		chat->SendMessage(p, "%s %s using a game client.",
				t == p ? "You" : t->name,
				t == p ? "aren't" : "isn't");
		return;
	}

	lagq->QueryPPing(t, &pping);
	lagq->QueryCPing(t, &cping);
	lagq->QueryRPing(t, &rping);
	lagq->QueryPLoss(t, &ploss);

	/* weight reliable ping twice the s2c and c2s */
	/* FIXME: remove code duplication with lagaction.c */
	avg = (pping.avg + cping.avg + 2*rping.avg) / 4;

	if (!strstr(params, "-v"))
	{
		chat->SendMessage(p,
				"%s: avg ping: %d  ploss: s2c: %.2f c2s: %.2f",
				prefix, avg, 100.0*ploss.s2c, 100.0*ploss.c2s);
	}
	else
	{
		struct ReliableLagData rlag;
		ticks_t tm = TICK_DIFF(current_ticks(), t->connecttime);

		lagq->QueryRelLag(t, &rlag);

		chat->SendMessage(p, "%s: s2c ping: %d %d (%d-%d) (reported by client)",
				prefix, cping.cur, cping.avg, cping.min, cping.max);
		chat->SendMessage(p, "%s: c2s ping: %d %d (%d-%d) (from position pkt times)",
				prefix, pping.cur, pping.avg, pping.min, pping.max);
		chat->SendMessage(p, "%s: rel ping: %d %d (%d-%d) (reliable ping)",
				prefix, rping.cur, rping.avg, rping.min, rping.max);
		chat->SendMessage(p, "%s: effective ping: %d (average of above)",
				prefix, avg);

		chat->SendMessage(p, "%s: ploss: s2c: %.2f c2s: %.2f s2cwpn: %.2f",
				prefix, 100.0*ploss.s2c, 100.0*ploss.c2s, 100.0*ploss.s2cwpn);
		chat->SendMessage(p, "%s: reliable dups: %.2f%%  reliable resends: %.2f%%",
				prefix, 100.0*(double)rlag.reldups/(double)rlag.c2sn,
				100.0*(double)rlag.retries/(double)rlag.s2cn);
		chat->SendMessage(p, "%s: s2c slow: %d/%d  s2c fast: %d/%d",
				prefix, cping.s2cslowcurrent, cping.s2cslowtotal,
				cping.s2cfastcurrent, cping.s2cfasttotal);
		do_common_bw_stuff(p, t, tm, prefix, FALSE);
	}
}


local helptext_t laghist_help =
"Targets: none or player\n"
"Args: [{-r}]\n"
"Displays lag histograms. If a {-r} is given, do this histogram for\n"
"\"reliable\" latency instead of c2s pings.\n";

local void Claghist(const char *tc, const char *params, Player *p, const Target *target)
{
	/* FIXME: write this */
}


local helptext_t listarena_help =
"Targets: none\n"
"Args: <arena name>\n"
"Lists the players in the given arena.\n";

local void Clistarena(const char *tc, const char *params, Player *p, const Target *target)
{
	StringBuffer sb;
	int total = 0, playing = 0;
	Arena *a;
	Player *p2;
	Link *link;

	if (params[0] == '#' && !capman->HasCapability(p, CAP_SEEPRIVARENA))
	{
		chat->SendMessage(p, "You don't have permission to view private arenas.");
		return;
	}

	if (params[0] == '\0')
		params = p->arena->name;

	a = aman->FindArena(params, NULL, NULL);
	if (!a)
	{
		chat->SendMessage(p, "Arena '%s' doesn't exist.", params);
		return;
	}

	SBInit(&sb);
	pd->Lock();
	FOR_EACH_PLAYER(p2)
		if (p2->status == S_PLAYING && p2->arena == a)
		{
			total++;
			if (p2->p_ship != SHIP_SPEC)
				playing++;
			SBPrintf(&sb, ", %s", p2->name);
		}
	pd->Unlock();

	chat->SendMessage(p, "Arena '%s': %d total, %d playing", a->name, total, playing);
	chat->SendWrappedText(p, SBText(&sb, 2));
	SBDestroy(&sb);
}


local helptext_t endinterval_help =
"Targets: none\n"
"Args: [-g] [-a <arena group name>] <interval name>\n"
"Causes the specified interval to be reset. If {-g} is specified, reset the interval\n"
"at the global scope. If {-a} is specified, use the named arena group. Otherwise, use\n"
"the current arena's scope. Interval names can be \"game\", \"reset\", or \"maprotation\".\n";

local void Cendinterval(const char *tc, const char *params, Player *p, const Target *target)
{
	char word[128];
	const char *tmp = NULL;
	int interval = -1, dasha = 0;
	char ag[MAXAGLEN] = { '\0' };

	while (strsplit(params, " \t", word, sizeof(word), &tmp))
		if (dasha)
		{
			astrncpy(ag, word, sizeof(ag));
			dasha = 0;
		}
		else if (!strcmp(word, "-g"))
			strcpy(ag, AG_GLOBAL);
		else if (!strcmp(word, "-a"))
			dasha = 1;
		else if (!strcmp(word, "game"))
			interval = INTERVAL_GAME;
		else if (!strcmp(word, "reset"))
			interval = INTERVAL_RESET;
		else if (!strcmp(word, "maprotation"))
			interval = INTERVAL_MAPROTATION;
		else
		{
			chat->SendMessage(p, "Bad argument: %s", word);
			return;
		}

	if (dasha)
	{
		chat->SendMessage(p, "You must specify an arena group name after -a.");
		return;
	}

	if (interval == -1)
	{
		chat->SendMessage(p, "You must specify an interval to reset.");
		return;
	}

	if (ag[0])
		persist->EndInterval(ag, NULL, interval);
	else if (p->arena)
		persist->EndInterval(NULL, p->arena, interval);
}


local helptext_t scorereset_help =
"Targets: none or player\n"
"Args: none\n"
"Resets your own score, or the target player's score.\n";

local void Cscorereset(const char *tc, const char *params, Player *p, const Target *target)
{
	/* for now, only reset INTERVAL_RESET scores, since those are
	 * the only ones that cont sees. */
	if (target->type == T_ARENA)
	{
		/* cfghelp: Misc:SelfScoreReset, arena, bool, def: 0
		 * Whether players can reset their own scores using ?scorereset.
		 */
		if (cfg->GetInt(p->arena->cfg, "Misc", "SelfScoreReset", 0))
		{
			stats->ScoreReset(p, INTERVAL_RESET);
			stats->SendUpdates(NULL);
			chat->SendMessage(p, "Your score has been reset.");
		}
		else
			chat->SendMessage(p,
					"This arena doesn't allow you to reset your own scores.");
	}
	else if (target->type == T_PLAYER)
	{
		stats->ScoreReset(target->u.p, INTERVAL_RESET);
		stats->SendUpdates(NULL);
		chat->SendMessage(p, "Player '%s' has had their score reset.", target->u.p->name);
	}
}


local helptext_t points_help =
"Targets: any\n"
"Args: <points to add>"
"Adds the specified number of points to the targets' flag points.\n";

local void Cpoints(const char *tc, const char *params, Player *p, const Target *target)
{
	char *next;
	long n = strtol(params, &next, 0);

	if (next != params)
	{
		LinkedList set = LL_INITIALIZER;
		Link *l;

		pd->TargetToSet(target, &set);
		for (l = LLGetHead(&set); l; l = l->next)
			stats->IncrementStat(l->data, STAT_FLAG_POINTS, n);
		LLEmpty(&set);
		stats->SendUpdates(NULL);
	}
}


local helptext_t mapinfo_help =
"Targets: none\n"
"Args: none\n"
"Displays some information about the map in this arena.\n";

local void mapinfo_count_rgns(void *clos, Region *reg)
{
	(*(int*)clos)++;
}

local void Cmapinfo(const char *tc, const char *params, Player *p, const Target *target)
{
	struct mapdata_memory_stats_t stats;
	const char *name, *vers, *mapc, *tsetc, *prog;
	char fname[128];
	int regs = 0;
	int hasfn = mapdata->GetMapFilename(p->arena, fname, sizeof(fname), NULL);

	chat->SendMessage(p, "LVL file loaded from '%s'.", hasfn ? fname : "<nowhere>");

	name = mapdata->GetAttr(p->arena, "NAME");
	vers = mapdata->GetAttr(p->arena, "VERSION");
	mapc = mapdata->GetAttr(p->arena, "MAPCREATOR");
	tsetc = mapdata->GetAttr(p->arena, "TILESETCREATOR");
	prog = mapdata->GetAttr(p->arena, "PROGRAM");

	chat->SendMessage(p,
			"name: %s, "
			"version: %s,",
			name ? name : "<not set>",
			vers ? vers : "<not set>");
	chat->SendMessage(p,
			"map creator: %s, "
			"tileset creator: %s, "
			"program: %s,",
			mapc ? mapc : "<not set>",
			tsetc ? tsetc : "<not set>",
			prog ? prog : "<not set>");

	mapdata->EnumContaining(p->arena, -1, -1, mapinfo_count_rgns, &regs);
	chat->SendMessage(p, "regions: %d", regs);

	mapdata->GetMemoryStats(p->arena, &stats);
	chat->SendMessage(p, "memory (bytes/blocks): lvl=%d/%d  region=%d/%d.",
			stats.lvlbytes, stats.lvlblocks, stats.rgnbytes, stats.rgnblocks);
}


local helptext_t redirect_help =
"Targets: any\n"
"Args: <redirect alias> | <ip>:<port>[:<arena>]\n"
"Module: redirect\n"
"Redirects the target to a different zone.\n";

local void Credirect(const char *tc, const char *params, Player *p, const Target *t)
{
	Target nt;
	if (t->type == T_ARENA)
	{
		nt.type = T_PLAYER;
		nt.u.p = p;
		t = &nt;
	}
	redir->AliasRedirect(t, params);
}


/* command group system */

/* declarations */
struct cmd_info
{
	const char *cmdname;
	CommandFunc func;
	helptext_t *phelptext;
};

struct interface_info
{
	void **ptr;
	const char *iid;
};

struct cmd_group
{
	const char *groupname;
	const struct interface_info *ifaces;
	const struct cmd_info *cmds;
	int loaded;
};


/* loading/unloading funcs */

local int load_cmd_group(struct cmd_group *grp)
{
	const struct interface_info *ii;
	const struct cmd_info *ci;

	for (ii = grp->ifaces; ii->iid; ii++)
	{
		*(ii->ptr) = mm->GetInterface(ii->iid, ALLARENAS);
		if (*(ii->ptr) == NULL)
		{
			/* if we can't get one, roll back all the others */
			for (ii--; ii >= grp->ifaces; ii--)
				mm->ReleaseInterface(*(ii->ptr));
			return MM_FAIL;
		}
	}

	for (ci = grp->cmds; ci->cmdname; ci++)
		cmd->AddCommand(ci->cmdname, ci->func, ALLARENAS, *ci->phelptext);

	grp->loaded = 1;

	return MM_OK;
}

local void unload_cmd_group(struct cmd_group *grp)
{
	const struct interface_info *ii;
	const struct cmd_info *ci;

	if (!grp->loaded)
		return;

	for (ii = grp->ifaces; ii->iid; ii++)
		mm->ReleaseInterface(*(ii->ptr));

	for (ci = grp->cmds; ci->cmdname; ci++)
		cmd->RemoveCommand(ci->cmdname, ci->func, ALLARENAS);

	grp->loaded = 0;
}

/* loading/unloading commands */

local struct cmd_group *find_group(const char *name);

local helptext_t enablecmdgroup_help =
"Targets: none\n"
"Args: <command group>\n"
"Enables all the commands in the specified command group. This is only\n"
"useful after using ?disablecmdgroup.\n";

local void Cenablecmdgroup(const char *tc, const char *params, Player *p, const Target *target)
{
	struct cmd_group *grp = find_group(params);
	if (grp)
	{
		if (grp->loaded)
			chat->SendMessage(p, "Command group %s already enabled.", params);
		else if (load_cmd_group(grp) == MM_OK)
			chat->SendMessage(p, "Command group %s enabled.", params);
		else
			chat->SendMessage(p, "Error enabling command group %s.", params);
	}
	else
		chat->SendMessage(p, "Command group %s not found.", params);
}


local helptext_t disablecmdgroup_help =
"Targets: none\n"
"Args: <command group>\n"
"Disables all the commands in the specified command group and released the\n"
"modules that they require. This can be used to release interfaces so that\n"
"modules can be unloaded or upgraded without unloading playercmd (which would\n"
"be irreversible).\n";

local void Cdisablecmdgroup(const char *tc, const char *params, Player *p, const Target *target)
{
	struct cmd_group *grp = find_group(params);
	if (grp)
	{
		if (grp->loaded)
		{
			unload_cmd_group(grp);
			chat->SendMessage(p, "Command group %s disabled.", params);
		}
		else
			chat->SendMessage(p, "Command group %s not loaded.", params);
	}
	else
		chat->SendMessage(p, "Command group %s not found.", params);
}


/* actual group definitions */

#define CMD(x) {#x, C ## x, & x ## _help},
#define CMDALIAS(x, y) {#x, C ## y, & x ## _help},
#define CMD_GROUP(x) {#x, x ## _requires, x ## _commands, 0},
#define REQUIRE(name, iid) {(void**)&name, iid},
#define END() {0}

local const struct interface_info core_requires[] =
{
	REQUIRE(aman, I_ARENAMAN)
	REQUIRE(net, I_NET)
	REQUIRE(ml, I_MAINLOOP)
	END()
};
local const struct cmd_info core_commands[] =
{
	CMD(enablecmdgroup)
	CMD(disablecmdgroup)
	CMD(arena)
	CMD(shutdown)
	CMD(recyclezone)
	CMD(owner)
	CMD(zone)
	CMD(version)
	CMD(uptime)
	CMD(lsmod)
	CMD(modinfo)
#ifndef CFG_NO_RUNTIME_LOAD
	CMD(insmod)
#endif
	CMD(rmmod)
	CMD(attmod)
	CMD(detmod)
	CMD(info)
	CMD(a)
	CMD(aa)
	CMD(z)
	CMD(az)
	CMD(warn)
	CMD(reply)
	CMD(netstats)
	CMD(send)
	CMD(recyclearena)
	CMD(where)
	END()
};


local const struct interface_info game_requires[] =
{
	REQUIRE(net, I_NET)
	REQUIRE(aman, I_ARENAMAN)
	REQUIRE(game, I_GAME)
	REQUIRE(cfg, I_CONFIG)
	END()
};
local const struct cmd_info game_commands[] =
{
	CMD(setfreq)
	CMD(setship)
	CMD(specall)
	CMD(warpto)
	CMD(shipreset)
	CMD(prize)

	CMD(lock)
	CMD(unlock)
	CMD(lockarena)
	CMD(unlockarena)

	END()
};


local const struct interface_info jackpot_requires[] =
{
	REQUIRE(aman, I_ARENAMAN)
	REQUIRE(jackpot, I_JACKPOT)
	END()
};
local const struct cmd_info jackpot_commands[] =
{
	CMD(jackpot)
	CMD(setjackpot)
	END()
};


local const struct interface_info config_requires[] =
{
	REQUIRE(aman, I_ARENAMAN)
	REQUIRE(cfg, I_CONFIG)
	END()
};
local const struct cmd_info config_commands[] =
{
	CMDALIAS(setg, set_generic)
	CMDALIAS(getg, get_generic)
	CMDALIAS(seta, set_generic)
	CMDALIAS(geta, get_generic)
	CMD(reloadconf)
	END()
};


local const struct interface_info flag_requires[] =
{
	REQUIRE(flagcore, I_FLAGCORE)
	END()
};
local const struct cmd_info flag_commands[] =
{
	CMD(flagreset)
	CMD(flaginfo)
	CMD(neutflag)
	CMD(moveflag)
	END()
};


local const struct interface_info ball_requires[] =
{
	REQUIRE(balls, I_BALLS)
	END()
};
local const struct cmd_info ball_commands[] =
{
	CMD(ballcount)
	CMD(ballinfo)
	CMD(giveball)
	CMD(moveball)
	CMD(spawnball)
	END()
};


local const struct interface_info lag_requires[] =
{
	REQUIRE(lagq, I_LAGQUERY)
	END()
};
local const struct cmd_info lag_commands[] =
{
	CMD(lag)
	CMD(laghist)
	END()
};


local const struct interface_info stats_requires[] =
{
	REQUIRE(persist, I_PERSIST)
	REQUIRE(stats, I_STATS)
	END()
};

local const struct cmd_info stats_commands[] =
{
	CMD(scorereset)
	CMD(endinterval)
	CMD(points)
	END()
};


local const struct interface_info misc_requires[] =
{
	REQUIRE(capman, I_CAPMAN)
	REQUIRE(groupman, I_GROUPMAN)
	REQUIRE(aman, I_ARENAMAN)
	REQUIRE(lm, I_LOGMAN)
	REQUIRE(cfg, I_CONFIG)
	REQUIRE(mapdata, I_MAPDATA)
	END()
};
local const struct cmd_info misc_commands[] =
{
	CMD(getgroup)
	CMD(setgroup)
	CMD(rmgroup)
	CMD(grplogin)
	CMD(listmod)
	CMD(find)
	CMD(setcm)
	CMD(getcm)
	CMD(listarena)
	CMD(sheep)
	CMD(mapinfo)
	END()
};


local const struct interface_info external_requires[] =
{
	REQUIRE(redir, I_REDIRECT)
	END()
};
local const struct cmd_info external_commands[] =
{
	CMD(redirect)
	END()
};


/* list of groups */
local struct cmd_group all_cmd_groups[] =
{
	CMD_GROUP(core)
	CMD_GROUP(game)
	CMD_GROUP(jackpot)
	CMD_GROUP(config)
	CMD_GROUP(flag)
	CMD_GROUP(ball)
	CMD_GROUP(lag)
	CMD_GROUP(stats)
	CMD_GROUP(misc)
	CMD_GROUP(external)
	END()
};

#undef CMD
#undef CMD_GROUP
#undef REQUIRE
#undef END

struct cmd_group *find_group(const char *name)
{
	struct cmd_group *grp;
	for (grp = all_cmd_groups; grp->groupname; grp++)
		if (!strcasecmp(grp->groupname, name))
			return grp;
	return NULL;
}

EXPORT const char info_playercmd[] = CORE_MOD_INFO("playercmd");

EXPORT int MM_playercmd(int action, Imodman *mm_, Arena *arena)
{
	struct cmd_group *grp;

	if (action == MM_LOAD)
	{
		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);

		if (!pd || !chat || !cmd) return MM_FAIL;

		startedat = current_ticks();

		for (grp = all_cmd_groups; grp->groupname; grp++)
			load_cmd_group(grp);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		for (grp = all_cmd_groups; grp->groupname; grp++)
			unload_cmd_group(grp);

		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(cmd);
		return MM_OK;
	}
	return MM_FAIL;
}




