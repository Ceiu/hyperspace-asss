/*
	AKD_LAG
	Hyperspace Branch
	15 Feb 2009
	Author:
	- Justin "Arnk Kilo Dylie" Schwartz
	Contributors:

*/
/*
	All Hockey Zone Code is the property of Hockey Zone and the code authors.
*/

#include "asss.h"
#include "akd_lag.h"

#include <string.h>

#define MODULENAME akd_lag
#define SZMODULENAME "akd_lag"
#define INTERFACENAME Iakd_lag

#define NOT_USING_SHIP_NAMES 1
#include "akd_asss.h"
//#include "cmdlist.h"
//#include "hz_util.h"

#define AKD_FLUX_MIN_PING 90
#define AKD_PLOSS_SAMPLE_PACKETS_MIN 30

#define LAG_CATEGORIES_COUNT 9
#define LAG_CATEGORIES_PING_INTERVAL 30
#define LAG_CATEGORIES_LOSS_INTERVAL 20
#define LAG_CATEGORIES_ETC_INTERVAL 2

local const char *LAG_CATEGORIES[LAG_CATEGORIES_COUNT] = {
	"Excellent",	//0-29		0.00-0.24%	0-1
	"Great",		//30-59		0.25-0.49%	2-3
	"Good",			//60-89		0.50-0.74%	4-5
	"Okay",			//90-119	0.75-0.99%	6-7
	"Mediocre",		//120-149	1.00-1.24%	8-9
	"Poor",			//150-179	1.25-1.49%	10-11
	"Bad",			//180-209	1.50-1.74%	12-13
	"Awful",		//210-239	1.75-1.99%	14-15
	"Terrible",		//240+		2.00%+		16+
};

local const char *AKD_LAG_TYPE_STRINGS[9] = {
	"s2cping",
	"c2sping",
	"relping",
	"flux",
	"fluxb",
	"drift",
	"s2closs",
	"c2sloss",
	"wpnloss"
};

//other interfaces we want to use besides the usual
//local Icmdlist *cl;

//config values

//other globals
local pthread_mutex_t globalmutex;

//prototype all functions we will be using in the interface here. then define the interface, then prototype that other stuff.

typedef enum
{
	cm_regular,
	cm_escape,
	cm_switch,
} getParamMode_t;


#define PARAM_BUFFER 64
local char *getParam(const char *params, char key, char *buffer)
{
	char activekey = 0;
	getParamMode_t mode = cm_regular;
	int i = 0;
	char activechar = *params;
	char lastchar = ' ';
	int keybuf = 0;

	while (activechar)
	{
		switch(mode)
		{
		case cm_regular:
			if ((activechar == '-') && (lastchar == ' '))
			{
				mode = cm_switch;
				break;
			}
			if ((activechar == '=') && (!keybuf))
			{
				break;
			}

			if (activechar == ' ')
			{
				if (activekey)
				{
					activekey = 0;
					keybuf = 0;
					mode = cm_regular;
					break;
				}
			}
			else if (activechar == '\\')
			{
				mode = cm_escape;
				break;
			}
			else if ((activechar == '_') && activekey) //allow -a=bc_de
			{
				activechar = ' ';
			}

			//No break here, because we want to still write to the buffer if we haven't broken yet
			//cm_escape just bypasses all of these checks on special characters
		case cm_escape:
			if ((activekey == key) && buffer)
			{
				buffer[i++] = activechar;
			}

			++keybuf;

			mode = cm_regular;
			break;

		case cm_switch:
			activekey = activechar;
			keybuf = 0;
			mode = cm_regular;
			break;
		}

		lastchar = *params;
		params += 1;
		activechar = *params;

		//stop reading after we read too many bytes.
		if (i >= PARAM_BUFFER - 1)
		{
			break;
		}
	}

	if (buffer)
		buffer[i] = 0;
	return buffer;
}

local int getBoolParam(const char *params, char key)
{
	char activekey = 0;
	getParamMode_t mode = cm_regular;
	char activechar = *params;
	char lastchar = ' ';

	while (activechar)
	{
		switch(mode)
		{
		case cm_regular:
			if ((activechar == '-') && (lastchar == ' '))
			{
				mode = cm_switch;
				break;
			}
			if (activechar == '=')
			{
				break;
			}

			if (activechar == ' ')
			{
				if (activekey)
				{
					activekey = 0;
					mode = cm_regular;
					break;
				}
			}
			else if (activechar == '\\')
			{
				mode = cm_escape;
				break;
			}
			else if ((activechar == '_') && activekey)
			{
				activechar = ' ';
			}

			//No break here, because we want to still write to the buffer if we haven't broken yet
			//cm_escape just bypasses all of these checks on special characters
		case cm_escape:

			mode = cm_regular;
			break;

		case cm_switch:
			if (activechar == key)
				return 1;

			activekey = activechar;
			mode = cm_regular;
			break;
		}

		params += 1;
		activechar = *params;
	}
	return 0;

}

DEF_GLOBALINTERFACE;

DEF_PARENA_TYPE
	float combinedLossToSpec;
	float lossToSpec;
	short pingToSpec;
	short fluxToSpec;
	short driftToSpec;
	short spikeToSpec;
ENDDEF_PARENA_TYPE;

DEF_PPLAYER_TYPE
	akd_lag_data data;

	struct TimeSyncData lastTSD, secondLastTSD;
	int syncs;
	int measureDelay;

	i8 checkSkip;
ENDDEF_PPLAYER_TYPE;

//prototype internal functions here.

local void Init(Player *p);

local void Position(Player *p, int ping, int cliping, unsigned int wpnsent);
local void RelDelay(Player *p, int ping);
local void ClientLatency(Player *p, struct ClientLatencyData *d);
local void RelStats(Player *p, struct ReliableLagData *d);
local void TimeSync(Player *p, struct TimeSyncData *d);
local int TimeSyncTimer(void *_data);

local Ilagcollect lcint =
{
	INTERFACE_HEAD_INIT(I_LAGCOLLECT, "akd_lag")
	Position, RelDelay, ClientLatency,
	TimeSync, RelStats
};

local helptext_t lag_help =
"Targets: self, player\n"
"Syntax:\n"
"  ?lag [-a]\n"
"-or- ?lag [-a] [player]\n"
"-or- /?lag [-a]\n"
"Displays a summary of collected lag data.\n"
"Using -a shows all data, even for obscure things that usually don't come into play.\n";
local void Clag(const char *cmd, const char *params, Player *p, const Target *target);

local helptext_t limits_help =
"Targets: arena\n"
"Syntax:\n"
"  ?limits [-a]\n"
"Displays the lag limits for ping, packetloss, combined packetloss, flux, and drift.\n"
"Using -a shows all of the limits, even for obscure things that usually don't come into play.\n";
local void Climits(const char *cmd, const char *params, Player *p, const Target *target);

local helptext_t laggraph_help =
"Targets: self, player\n"
"Syntax:\n"
"  ?laggraph <-t=type>\n"
"-or- ?laggraph <-t=type> [player]\n"
"-or- /?laggraph <-t=type>\n"
"Shows a graph of data in order of how it is stored. The asterisk shows the latest point.\n"
"Valid types of graphs are s2cping, c2sping, relping, s2closs, c2sloss, wpnloss, fluxa, fluxb, drift.\n";
local void Claggraph(const char *cmd, const char *params, Player *p, const Target *target);

local helptext_t laganalysis_help =
"Targets: zone\n"
"Syntax:\n"
"  ?laganalysis [-c] [-d]\n"
"Does some analysis of players lag data.\n"
"-c shows raw counts instead of percentages.\n"
"-d shows descriptions of the categories.\n";
local void Claganalysis(const char *cmd, const char *params, Player *p, const Target *target);

local void Ctestping(const char *cmd, const char *params, Player *p, const Target *target);

local void reportPingData(akd_lag_pingsubdata *d, int ping);
local void reportDriftData(akd_lag_etcsubdata *d, int etc);
local void reportEtcData(akd_lag_etcsubdata *d, int etc);
local void reportLossData(akd_lag_losssubdata *d, int loss, int sent);
local int calculatePingAverage(akd_lag_pingsubdata *d);
local int calculateEtcAverage(akd_lag_etcsubdata *d);
local int calculateLossAverage(akd_lag_losssubdata *d);
local int calculateBroadAverage(akd_lag_broadsubdata *d);

//callback examples
local void playeraction(Player *, int action, Arena *);		//CB_PLAYERACTION callback prototype
local void arenaaction(Arena *, int action);

local int enforcer(void *);

local akd_lag_report *lagReport(Player *t, akd_lag_report *s);

MYINTERFACE =
{
	INTERFACE_HEAD_INIT(I_AKD_LAG, "akd_lag")
	lagReport
};


EXPORT const char info_akd_lag[] = "v1.4 by Justin \"Arnk Kilo Dylie\" Schwartz <kilodylie@rshl.org>";
EXPORT int MM_akd_lag(int action, Imodman *mm_, Arena *arena)
{
	int fail = 0;

	if (action == MM_LOAD)
	{
		//store the provided Imodman interface.
		mm = mm_;

		GETINT(pd, I_PLAYERDATA);
		GETINT(ml, I_MAINLOOP);
		GETINT(cfg, I_CONFIG);
		GETINT(prng, I_PRNG);
		GETINT(lm, I_LOGMAN);

		//register per-arena and per-player data.
		BREG_PPLAYER_DATA();

		//malloc and init anything else.

		//init a global mutex if you need one. you only need one if you have a global linkedlist, hashtable, or something cool like that.
		INIT_MUTEX(globalmutex);

		mm->RegCallback(CB_PLAYERACTION, playeraction, ALLARENAS);
		mm->RegCallback(CB_ARENAACTION, arenaaction, ALLARENAS);

		mm->RegInterface(&lcint, 0);

		INIT_GLOBALINTERFACE();
		REG_GLOBALINTERFACE();

		//finally, return success.
		return MM_OK;
	}
	else if (action == MM_POSTLOAD)
	{
		GETINT(chat, I_CHAT);
		GETINT(game, I_GAME);
		GETINT(cmd, I_CMDMAN);
		GETINT(net, I_NET);
		//OGETINT(cl, I_CMDLIST);

		GETINT(aman, I_ARENAMAN);
		BREG_PARENA_DATA();

		ml->SetTimer(enforcer, 40, 40, 0, 0);

		cmd->AddCommand("lag", Clag, ALLARENAS, lag_help);
		cmd->AddCommand("limits", Climits, ALLARENAS, limits_help);
		cmd->AddCommand("laggraph", Claggraph, ALLARENAS, laggraph_help);
		cmd->AddCommand("laganalysis", Claganalysis, ALLARENAS, laganalysis_help);
		cmd->AddCommand("testping", Ctestping, ALLARENAS, NULL);

		/*if (cl)
		{
			cl->AddCmdSection("lag");
			cl->AddCmd("lag", "lag", "Shows lag information about the target.", TARGETS_PLAYER|TARGETS_SELF);
			cl->AddCmd("lag", "limits", "Shows the values of various lag measurements at which people are specced.", TARGETS_ARENA);
			cl->AddCmd("lag", "laggraph", "Shows a graph of recorded lag data.", TARGETS_PLAYER|TARGETS_SELF);
			cl->AddCmd("lag", "laganalysis", "Displays statistics about player lag data.", 0);
		}*/
		return MM_OK;
	}
	else if (action == MM_PREUNLOAD)
	{
		UNREG_GLOBALINTERFACE();

		/*if (cl)
		{
			cl->DelCmdSection(SZMODULENAME);
		}*/

		cmd->RemoveCommand("limits", Climits, ALLARENAS);
		cmd->RemoveCommand("lag", Clag, ALLARENAS);
		cmd->RemoveCommand("laggraph", Claggraph, ALLARENAS);
		cmd->RemoveCommand("laganalysis", Claganalysis, ALLARENAS);
		cmd->RemoveCommand("testping", Ctestping, ALLARENAS);

		ml->ClearTimer(enforcer, 0);

		UNREG_PARENA_DATA();
		RELEASEINT(aman);
		RELEASEINT(net);
		RELEASEINT(chat);
		RELEASEINT(game);
		RELEASEINT(cmd);
		//RELEASEINT(cl);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		//first try to unregister the interface if exposing one.
		mm->RegInterface(&lcint, 0);

		ml->ClearTimer(TimeSyncTimer, 0);

		mm->UnregCallback(CB_ARENAACTION, arenaaction, ALLARENAS);
		mm->UnregCallback(CB_PLAYERACTION, playeraction, ALLARENAS);

		//clear the mutex if we were using it
		DESTROY_MUTEX(globalmutex);

		//free any other malloced data

		//unregister per-arena and per-player data
		UNREG_PPLAYER_DATA();

		//release interfaces last.
		//this is where GETINT jumps to if it fails.
Lfailload:
		RELEASEINT(cfg);
		RELEASEINT(ml);
		RELEASEINT(pd);
		RELEASEINT(prng);
		RELEASEINT(lm);

		//returns MM_FAIL if we jumped from a failed GETINT or other MM_LOAD action, returns MM_OK if not.
		DO_RETURN();
	}

	return MM_FAIL;

	(void)persist;
}



//body:Init
local void Init(Player *p)
{
	BDEF_PD(p);

	pdat->data.s2cping.broadtype = AKD_PING_TYPE;	pdat->data.s2cping.type = AKD_S2CPING;
	pdat->data.c2sping.broadtype = AKD_PING_TYPE;	pdat->data.c2sping.type = AKD_C2SPING;
	pdat->data.relping.broadtype = AKD_PING_TYPE;	pdat->data.relping.type = AKD_RELPING;
	pdat->data.s2closs.broadtype = AKD_LOSS_TYPE;	pdat->data.s2closs.type = AKD_S2CLOSS;
	pdat->data.c2sloss.broadtype = AKD_LOSS_TYPE;	pdat->data.c2sloss.type = AKD_C2SLOSS;
	pdat->data.wpnloss.broadtype = AKD_LOSS_TYPE;	pdat->data.wpnloss.type = AKD_WPNLOSS;
	pdat->data.flux.broadtype = AKD_ETC_TYPE;		pdat->data.flux.type = AKD_FLUX;
	pdat->data.fluxB.broadtype = AKD_ETC_TYPE;		pdat->data.fluxB.type = AKD_FLUXB;
	pdat->data.drift.broadtype = AKD_ETC_TYPE;		pdat->data.drift.type = AKD_DRIFT;
	pdat->data.testping.broadtype = AKD_PING_TYPE;	pdat->data.testping.type = AKD_TESTPING;

	pdat->data.s2cping.low = pdat->data.s2cping.high = -1;
	pdat->data.c2sping.low = pdat->data.c2sping.high = -1;
	pdat->data.relping.low = pdat->data.relping.high = -1;
	pdat->data.flux.high = -1;
	pdat->data.fluxB.high = -1;
	pdat->data.drift.high = -1;

	pdat->data.s2cping.current = AKD_PING_SAMPLESIZE - 1;
	pdat->data.c2sping.current = AKD_PING_SAMPLESIZE - 1;
	pdat->data.relping.current = AKD_PING_SAMPLESIZE - 1;

	pdat->data.flux.current = AKD_ETC_SAMPLESIZE - 1;
	pdat->data.fluxB.current = AKD_ETC_SAMPLESIZE - 1;
	pdat->data.drift.current = AKD_DRIFT_SAMPLESIZE - 1;

	pdat->data.s2closs.current = AKD_PLOSS_SAMPLESIZE - 1;
	pdat->data.c2sloss.current = AKD_PLOSS_SAMPLESIZE - 1;
	pdat->data.wpnloss.current = AKD_PLOSS_SAMPLESIZE - 1;

	pdat->data.testping.current = AKD_PING_SAMPLESIZE - 1;

	pdat->syncs = 0;
	pdat->checkSkip = prng?prng->Number(0,20):1;
	pdat->measureDelay = 2;
}

//body:reportPingData
local void reportPingData(akd_lag_pingsubdata *d, int ping)
{
	int next = (d->current + 1)%AKD_PING_SAMPLESIZE;

	if (ping < 0)
		ping = 0;
	else if (ping > AKD_LAG_MAXPING)
		ping = AKD_LAG_MAXPING;

	//slowly phase out old highs because spikes shouldn't ruin anyone's record.
	if ((d->high > 0) && (++d->sinceHigh > AKD_PING_SAMPLESIZE))
	{
		--d->high;
	}

	if ((ping < d->low) || (d->low == -1))
		d->low = ping;
	if (ping > d->high)
	{
		d->high = ping;
		d->sinceHigh = 0;
	}


	d->samples[next] = ping;
	d->current = next;

	//update the count, which is never higher than AKD_PING_SAMPLESIZE
	(d->taken < AKD_PING_SAMPLESIZE)?(++d->taken):(d->taken = AKD_PING_SAMPLESIZE);
}

//body:reportEtcData
local void reportEtcData(akd_lag_etcsubdata *d, int ping)
{
	int next = (d->current + 1)%AKD_ETC_SAMPLESIZE;

	if (ping < 0)
		ping = 0;
	else if (ping > AKD_LAG_MAXPING)
		ping = AKD_LAG_MAXPING;

	//slowly phase out old highs because spikes shouldn't ruin anyone's record.
	if ((d->high > 0) && (++d->sinceHigh > AKD_ETC_SAMPLESIZE))
	{
		--d->high;
	}
	if ((d->NAhigh > 0) && (++d->NAsinceHigh > AKD_ETC_SAMPLESIZE))
	{
		--d->NAhigh;
	}

	d->samples[next] = ping;
	d->current = next;

	if (ping > d->NAhigh)
	{
		d->NAhigh = ping;
		d->NAsinceHigh = 0;
	}

	//update the count, which is never higher than AKD_ETC_SAMPLESIZE
	(d->taken < AKD_ETC_SAMPLESIZE)?(++d->taken):(d->taken = AKD_ETC_SAMPLESIZE);

	if (d->taken == AKD_ETC_SAMPLESIZE)
	{
		int ave = calculateEtcAverage(d);
		if (ave > d->high)
			d->high = ave;
	}
}

//body:reportDriftData
local void reportDriftData(akd_lag_etcsubdata *d, int ping)
{
	int next = (d->current + 1)%AKD_DRIFT_SAMPLESIZE;

	if (ping < 0)
		ping = 0;
	else if (ping > AKD_LAG_MAXPING)
		ping = AKD_LAG_MAXPING;

	//slowly phase out old highs because spikes shouldn't ruin anyone's record.
	if ((d->high > 0) && (++d->sinceHigh > AKD_DRIFT_SAMPLESIZE))
	{
		--d->high;
	}
	if ((d->NAhigh > 0) && (++d->NAsinceHigh > AKD_DRIFT_SAMPLESIZE))
	{
		--d->NAhigh;
	}

	d->samples[next] = ping;
	d->current = next;

	if (ping > d->NAhigh)
	{
		d->NAhigh = ping;
		d->NAsinceHigh = 0;
	}

	//update the count, which is never higher than AKD_DRIFT_SAMPLESIZE
	(d->taken < AKD_DRIFT_SAMPLESIZE)?(++d->taken):(d->taken = AKD_DRIFT_SAMPLESIZE);

	if (d->taken == AKD_DRIFT_SAMPLESIZE)
	{
		int ave = calculateEtcAverage(d);
		if (ave > d->high)
			d->high = ave;
	}
}

//body:reportLossData
local void reportLossData(akd_lag_losssubdata *d, int loss, int sent)
{
	int next = (d->current + 1)%AKD_PLOSS_SAMPLESIZE;

	//slowly phase out old highs because spikes shouldn't ruin anyone's record.
	if ((d->high > 0) && (++d->sinceHigh > AKD_PLOSS_SAMPLESIZE))
	{
		d->high -= 10;
		if (d->high < 0)
			d->high = 0;
	}
	if ((d->NAhigh > 0) && (++d->NAsinceHigh > AKD_PLOSS_SAMPLESIZE))
	{
		d->NAhigh -= 10;
		if (d->NAhigh < 0)
			d->NAhigh = 0;
	}

	d->samples[next] = loss;
	d->weights[next] = sent;
	d->current = next;

	if (loss > d->NAhigh)
	{
		d->NAhigh = loss;
		d->NAsinceHigh = 0;
	}

	//update the count, which is never higher than AKD_PLOSS_SAMPLESIZE
	(d->taken < AKD_PLOSS_SAMPLESIZE)?(++d->taken):(d->taken = AKD_PLOSS_SAMPLESIZE);

	//adjust extremes
	if (d->taken > 1)
	{
		int aveloss = calculateLossAverage(d);

		if (aveloss > d->high)
			d->high = loss;
	}
}

//body:calculatePingAverage
local int calculatePingAverage(akd_lag_pingsubdata *d)
{
	int i;
	int total = 0;
	int min = -1;
	int max = -1;
	int actual = d->taken - 2;
	//no data collected yet, let's not divide by 0 or average from just 1
	if (actual < 2)
		return 0;

	for (i = 0; i < d->taken; ++i)
	{
		if (min == -1 || d->samples[i] < min)
			min = d->samples[i];
		if (max == -1 || d->samples[i] > max)
			max = d->samples[i];

		total += d->samples[i];
	}

	//drop the highest and lowest
	total = total - min - max;

	return total / actual;
}

//body:calculateEtcAverage
local int calculateEtcAverage(akd_lag_etcsubdata *d)
{
	int i;
	int total = 0;
	int min = -1;
	int max = -1;
	int actual = d->taken - 2;
	//no data collected yet, let's not divide by 0 or average from just 1
	if (actual < 2)
		return 0;

	for (i = 0; i < d->taken; ++i)
	{
		if (min == -1 || d->samples[i] < min)
			min = d->samples[i];
		if (max == -1 || d->samples[i] > max)
			max = d->samples[i];

		total += d->samples[i];
	}

	//drop the highest and lowest
	total = total - min - max;

	return total / actual;
}

//body:calculateLossAverage
local int calculateLossAverage(akd_lag_losssubdata *d)
{
	int i;
	int total = 0;
	int totalweight = 0;
	int result;
	int min = -1;
	int max = -1;
	int minweight = 0;
	int maxweight = 0;
	int actual = d->taken - 2;
	//no data collected yet, let's not divide by 0 or average from just 1
	if (actual < 2)
		return 0;

	for (i = 0; i < d->taken; ++i)
	{
		int weightedsample = d->samples[i] * d->weights[i];
		if (min == -1 || weightedsample < min)
		{
			min = weightedsample;
			minweight = 10 * d->weights[i];
		}
		if (max == -1 || weightedsample > max)
		{
			max = weightedsample;
			maxweight = 10 * d->weights[i];
		}

		totalweight += 10 * d->weights[i];
		total += weightedsample;
	}

	//drop the highest and lowest magnitude samples. actual is already accounted for
	total = total - min - max;
	totalweight = totalweight - minweight - maxweight;

	result = total / actual;

	if ((totalweight / actual) < 10)
		return 0;

	result /= ((totalweight / actual) / 10);

	return result;
}

//body:calculateBroadAverage
local int calculateBroadAverage(akd_lag_broadsubdata *d)
{
	switch(d->broadtype)
	{
	case AKD_PING_TYPE: return calculatePingAverage((akd_lag_pingsubdata *)d);
	case AKD_LOSS_TYPE: return calculateLossAverage((akd_lag_losssubdata *)d);
	case AKD_ETC_TYPE: return calculateEtcAverage((akd_lag_etcsubdata *)d);
	default: return 0;
	}
}

//body:playeraction
local void playeraction(Player *p, int action, Arena *a)
{
	if (p->type == T_FAKE) return;

	if (action == PA_CONNECT)
	{
		Init(p);
	}
}

//body:arenaaction
local void arenaaction(Arena *a, int action)
{
	/*if (action == AA_CREATE)
	{
		cl->AddSectionToArena(a, "lag");
	}
	else if (action == AA_DESTROY)
	{
		cl->RemoveSectionFromArena(a, "lag");
	}*/

	if (action == AA_CREATE || action == AA_CONFCHANGED)
	{
		BDEF_AD(a);
		/* cfghelp: AKD_Lag:SpikeToSpec, arena, int, def: 2000, mod: akd_lag
		 How long the server can go (in ms) without receiving data from a player before that player is specced.*/
		ad->spikeToSpec = cfg->GetInt(a->cfg, "AKD_Lag", "SpikeToSpec", 2000);

		/* cfghelp: AKD_Lag:PingToSpec, arena, int, def: 250, mod: akd_lag
		 The average ping (in ms) of any type at which a player is locked in spec. The current ping of any type at which a player is specced temporarily.*/
		ad->pingToSpec = cfg->GetInt(a->cfg, "AKD_Lag", "PingToSpec", 250);

		/* cfghelp: AKD_Lag:CombinedLossToSpec, arena, int, def: 55, mod: akd_lag
		 The average combined positive packetloss (in 0.1%) at which a player is locked in spec. The current combined positive packetloss at which a player is specced temporarily.*/
		ad->combinedLossToSpec = cfg->GetInt(a->cfg, "AKD_Lag", "CombinedLossToSpec", 55) / 10.f;

		/* cfghelp: AKD_Lag:LossToSpec, arena, int, def: 45, mod: akd_lag
		 The average packetloss of any type (in 0.1%) at which a player is locked in spec. The current packetloss of any type at which a player is specced temporarily.*/
		ad->lossToSpec = ((float)cfg->GetInt(a->cfg, "AKD_Lag", "LossToSpec", 45)) / 10.f;

		/* cfghelp: AKD_Lag:FluxToSpec, arena, int, def: 50, mod: akd_lag
		 The average ping fluctuation (in ms) at which a player is locked in spec.*/
		ad->fluxToSpec = cfg->GetInt(a->cfg, "AKD_Lag", "FluxToSpec", 30);

		/* cfghelp: AKD_Lag:DriftToSpec, arena, int, def: 15, mod: akd_lag
		 The average timer drift (in god knows what units) at which a player is locked in spec.*/
		ad->driftToSpec = cfg->GetInt(a->cfg, "AKD_Lag", "DriftToSpec", 15);
	}
}

//body:enforcer
local int enforcer(void *x)
{
	Player *p;
	Link *link;
	MYGLOCK;
	PDLOCK;

	FOR_EACH_PLAYER(p)
	{
		if ((p->type != T_CONT) && (p->type != T_VIE))
			continue;
		if (!p->arena)
			continue;
		if (p->status != S_PLAYING)
			continue;
		else
		{
			int a;
			BDEF_PD(p);
			BDEF_AD(p->arena);
			char message[40];

			if ((a = net->GetLastPacketTime(p) * 10) > ad->spikeToSpec)
			{
				if (!IS_SPEC(p))
				{
					snprintf(message, sizeof(message), "spike of %ims", a);
					chat->SendMessage(p, " You have been specced for a large spike: %i ms breaks the limit of %i", a, ad->spikeToSpec);
					lm->LogP(L_INFO, "akd_lag", p, "lagout: %s", message);
					DO_CBS(CB_LAGOUT, p->arena, LagoutFunc, (p, p->p_freq, message));
					game->SetShipAndFreq(p, SHIP_SPEC, p->arena->specfreq);
				}
				continue;
			}

			if (pdat->checkSkip > 0)
			{
				--pdat->checkSkip;
				continue;
			}
			else
			{
				int c2spingave;
				float b, c, d, e;
				message[0] = 0;

				pdat->checkSkip = 0;

				//do not fear the goto, embrace it.

				//averages.
				if ((a = c2spingave = calculatePingAverage(&pdat->data.c2sping)) > ad->pingToSpec)
				{
					if (!IS_SPEC(p))
					{
						snprintf(message, sizeof(message), "%ims c2sping", a);
						chat->SendMessage(p, " You have been specced for excessive average client-to-server ping: %i ms breaks the limit of %i", a, ad->pingToSpec);
					}
					goto enforcement_lock;
				}
				if ((a = calculateEtcAverage(&pdat->data.flux)) > ad->fluxToSpec)
				{
					if (!IS_SPEC(p))
					{
						snprintf(message, sizeof(message), "%ims flux (alpha)", a);
						chat->SendMessage(p, " You have been specced for excessive average ping fluctuation: %i ms breaks the limit of %i", a, ad->fluxToSpec);
					}
					goto enforcement_lock;
				}
				if ((a = calculateEtcAverage(&pdat->data.fluxB)) > ad->fluxToSpec)
				{
					if (!IS_SPEC(p))
					{
						snprintf(message, sizeof(message), "%ims flux (beta)", a);
						chat->SendMessage(p, " You have been specced for excessive average ping fluctuation: %i ms breaks the limit of %i", a, ad->fluxToSpec);
					}
					goto enforcement_lock;
				}
				if ((a = calculatePingAverage(&pdat->data.s2cping)) > ad->pingToSpec)
				{
					if (!IS_SPEC(p))
					{
						snprintf(message, sizeof(message), "%ims s2cping", a);
						chat->SendMessage(p, " You have been specced for excessive average server-to-client ping: %i ms breaks the limit of %i", a, ad->pingToSpec);
					}
					goto enforcement_lock;
				}
				if ((b = ((float)calculateLossAverage(&pdat->data.s2closs))/100.f) > ad->lossToSpec)
				{
					if (!IS_SPEC(p))
					{
						snprintf(message, sizeof(message), "%.2f%% s2closs", b);
						chat->SendMessage(p, " You have been specced for excessive average server-to-client packetloss: %.2f%% breaks the limit of %.2f%%", b, ad->lossToSpec);
					}
					goto enforcement_lock;
				}
				if ((b = ((float)calculateLossAverage(&pdat->data.c2sloss))/100.f) > ad->lossToSpec)
				{
					if (!IS_SPEC(p))
					{
						snprintf(message, sizeof(message), "%.2f%% c2sloss", b);
						chat->SendMessage(p, " You have been specced for excessive average client-to-server packetloss: %.2f%% breaks the limit of %.2f%%", b, ad->lossToSpec);
					}
					goto enforcement_lock;
				}
				if ((a = calculateEtcAverage(&pdat->data.drift)) > ad->driftToSpec)
				{
					if (!IS_SPEC(p))
					{
						snprintf(message, sizeof(message), "drift of %i", a);
						chat->SendMessage(p, " You have been specced for excessive average timer drift: %i breaks the limit of %i", a, ad->driftToSpec);
					}
					goto enforcement_lock;
				}
				if ((b = ((float)calculateLossAverage(&pdat->data.wpnloss))/100.f) > ad->lossToSpec)
				{
					if (!IS_SPEC(p))
					{
						snprintf(message, sizeof(message), "%.2f%% wpnloss", b);
						chat->SendMessage(p, " You have been specced for excessive average weapons packetloss: %.2f%% breaks the limit of %.2f%%", b, ad->lossToSpec);
					}
					goto enforcement_lock;
				}
				if ((a = calculatePingAverage(&pdat->data.relping)) > ad->pingToSpec)
				{
					if (!IS_SPEC(p))
					{
						snprintf(message, sizeof(message), "%ims relping", a);
						chat->SendMessage(p, " You have been specced for excessive average reliable ping: %i ms breaks the limit of %i", a, ad->pingToSpec);
					}
					goto enforcement_lock;
				}

				c = ((float)calculateLossAverage(&pdat->data.s2closs))/100.f;
				d = ((float)calculateLossAverage(&pdat->data.c2sloss))/100.f;
				e = ((float)calculateLossAverage(&pdat->data.wpnloss))/100.f;
				if (c < 0.f) c = 0.f;
				if (d < 0.f) d = 0.f;
				if (e < 0.f) e = 0.f;
				if ((b = c + d + e) > ad->combinedLossToSpec)
				{
					if (!IS_SPEC(p))
					{
						snprintf(message, sizeof(message), "%.2f%% combined loss", b);
						chat->SendMessage(p, " You have been specced for excessive average combined packetloss: %.2f%% breaks the limit of %.2f%%", b, ad->combinedLossToSpec);
					}
					goto enforcement_lock;
				}

				p->flags.no_ship = 0;
				pdat->checkSkip += 8;

				goto enforcement_next;


				enforcement_lock:
				p->flags.no_ship = 1;
				pdat->checkSkip += 1;

				if (!IS_SPEC(p))
				{
					pdat->checkSkip += 4;
					lm->LogP(L_INFO, "akd_lag", p, "lagout: %s", message);
					DO_CBS(CB_LAGOUT, p->arena, LagoutFunc, (p, p->p_freq, message));
					game->SetShipAndFreq(p, SHIP_SPEC, p->arena->specfreq);
				}

				enforcement_next:

				;
			}
		}
	}

	PDUNLOCK;
	MYGUNLOCK;
	return 1;
}


//body:Position
local void Position(Player *p, int ping, int cliping, unsigned int wpnsent)
{
	BDEF_PD(p);

	if (p->status != S_PLAYING)
		return;
	if (!p->flags.sent_ppk)
		return;

	MYGLOCK;
		if (&pdat->data.c2sping.taken > 0)
		{
			BDEF_AD(p->arena);
			int a = (ping < AKD_FLUX_MIN_PING)?(AKD_FLUX_MIN_PING):(ping);
			int b = (pdat->data.c2sping.samples[pdat->data.c2sping.current] < AKD_FLUX_MIN_PING)?(AKD_FLUX_MIN_PING):(pdat->data.c2sping.samples[pdat->data.c2sping.current]);
			int fluxA = abs(a - b);
			int fluxB = fluxA;

			fluxA /= 2;
			//determine consistant spiking and blips too.

			reportEtcData(&pdat->data.flux, fluxA);

			if (ad && ad->fluxToSpec)
			{
				fluxB -= ad->fluxToSpec;
				if (fluxB < 0)
					fluxB = 0;
				reportEtcData(&pdat->data.fluxB, fluxB);
			}
		}

		reportPingData(&pdat->data.c2sping, ping);
		reportPingData(&pdat->data.testping, ping);
	MYGUNLOCK;
}

local void Ctestping(const char *cmd, const char *params, Player *p, const Target *target)
{
	BDEF_PD(p);

	int ping = atoi(params);

	if (p->status != S_PLAYING)
		return;
	if (!p->flags.sent_ppk)
		return;

	MYGLOCK;
		reportPingData(&pdat->data.c2sping, ping);
		reportPingData(&pdat->data.testping, ping);
	MYGUNLOCK;
}

//body:RelDelay
local void RelDelay(Player *p, int ping)
{
	BDEF_PD(p);

	if (p->status != S_PLAYING)
		return;
	if (!p->flags.sent_ppk)
		return;

	MYGLOCK;
		reportPingData(&pdat->data.relping, ping/2);
	MYGUNLOCK;
}

//body:ClientLatency
local void ClientLatency(Player *p, struct ClientLatencyData *d)
{
	BDEF_PD(p);

	if (p->status != S_PLAYING)
		return;
	if (!p->flags.sent_ppk)
		return;

	MYGLOCK;
		reportPingData(&pdat->data.s2cping, d->lastping * 10);
	MYGUNLOCK;
}

//body: RelStats
local void RelStats(Player *p, struct ReliableLagData *d)
{

}

typedef struct akdlag_timesyncdata
{
	Player *p;
	struct TimeSyncData d;
} akdlag_timesyncdata;

local int TimeSyncTimer(void *_data)
{
	akdlag_timesyncdata *data = (akdlag_timesyncdata *)_data;
	Player *p = data->p;
	struct TimeSyncData *d = &data->d;
	BDEF_PD(p);


	MYGLOCK;
	{
		//calculate drift
		int drift = abs((d->s_time - d->c_time) - (pdat->secondLastTSD.s_time - pdat->secondLastTSD.c_time));

		//calculate the s2c packetloss (units are .01%)
		int received = d->c_pktrcvd - pdat->secondLastTSD.c_pktrcvd;
		int sent = d->s_pktsent - pdat->secondLastTSD.s_pktsent;
		int loss;

		//report drift now that we're not declaring variables..
		if (pdat->data.s2closs.taken > 0)
			reportDriftData(&pdat->data.drift, drift);


		if (sent)
		{
			loss = (10000 * (sent - received)) / sent;
			reportLossData(&pdat->data.s2closs, loss, sent);
		}

		//calculate the c2s packetloss (units are .01%)
		received = d->s_pktrcvd - pdat->secondLastTSD.s_pktrcvd;
		sent = d->c_pktsent - pdat->secondLastTSD.c_pktsent;

		if (sent)
		{
			loss = (10000 * (sent - received)) / sent;
			reportLossData(&pdat->data.c2sloss, loss, sent);
		}
	}

	MYGUNLOCK;

	afree(data);
	return 0;
}

//body:TimeSync
local void TimeSync(Player *p, struct TimeSyncData *d)
{
	akdlag_timesyncdata *data;
	BDEF_PD(p);

	if (pdat->measureDelay > 0)
	{
		--pdat->measureDelay;
	}

	if (p->status != S_PLAYING)
		return;
	if (!p->flags.sent_ppk)
		return;

	++pdat->syncs;

	//ignore early syncs which tend to suffer
	if (pdat->syncs > 3)
	{
		if (d->s_pktsent - AKD_PLOSS_SAMPLE_PACKETS_MIN > pdat->lastTSD.s_pktsent)
		{
			pdat->secondLastTSD = pdat->lastTSD;
			pdat->lastTSD = *d;
		}
		else
		{
			return;
		}
	}
	else
	{
		pdat->secondLastTSD = pdat->lastTSD;
		pdat->lastTSD = *d;
		return;
	}

	if (pdat->measureDelay > 0)
	{
		return;
	}

	data = amalloc(sizeof(*data));
	data->p = p;
	data->d = *d;
	ml->SetTimer(TimeSyncTimer, 0, 0, data, data);
}


//?limits
//body:Climits
local void Climits(const char *cmd, const char *params, Player *p, const Target *target)
{
	BDEF_AD(p->arena);
	if (!p->arena)
		return;

	if (getBoolParam(params, 'a'))
	{
		chat->SendMessage(p, "limits: ping: %ims  loss: %.1f%%  spike: %ims  flux: %ims  drift: %i  combinedloss: %.2f%%",
			ad->pingToSpec, ad->lossToSpec, ad->spikeToSpec, ad->fluxToSpec, ad->driftToSpec, ad->combinedLossToSpec);
	}
	else
	{
		chat->SendMessage(p, "limits: ping: %ims  loss: %.1f%%  spike: %ims  flux: %ims",
			ad->pingToSpec, ad->lossToSpec, ad->spikeToSpec, ad->fluxToSpec);
	}
}

local int formatHighLow(int x)
{
	if (x < 0)
		x = 0;
	return x;
}

//?lag
//body:Clag
local void Clag(const char *cmd, const char *params, Player *p, const Target *target)
{
	Player *t = 0;

	if (target->type == T_PLAYER)
		t = target->u.p;
	else if (target->type == T_ARENA)
	{
		char buffer[256];

		getParam(params, 0, buffer);

		if (buffer[0] > 32)
			t = pd->FindPlayer(buffer);

		if (!t && (buffer[0] > 32))
		{
			chat->SendMessage(p, "Player '%s' not found.", buffer);
			return;
		}
		if (!t)
			t = p;
	}
	else
		return;

	if (!t)
		return;

	
	if ((t->type != T_CONT) && (t->type != T_VIE))
	{
		chat->SendMessage(p, "lag: Data is not collected for internal players, chat clients, or any other non-standard connection.");
		return;
	}

	MYGLOCK;
	if (getBoolParam(params, 'a'))
	{
		BDEF_PD(t);

		short sp_low, sp_high, sp_ave, cp_low, cp_high, cp_ave, rp_low, rp_high, rp_ave, fx_high, fx_ave, fxb_high, fxb_ave, dr_high, dr_ave;
		float sl_high, sl_ave, cl_high, cl_ave, wl_high, wl_ave;

		if (pdat->measureDelay)
		{
			chat->SendMessage(p, "lag: Not enough data has been collected to report a summary.");
			MYGUNLOCK;
			return;
		}

		sp_low = formatHighLow(pdat->data.s2cping.low);
		sp_high = formatHighLow(pdat->data.s2cping.high);
		sp_ave = calculatePingAverage(&pdat->data.s2cping);

		cp_low = formatHighLow(pdat->data.c2sping.low);
		cp_high = formatHighLow(pdat->data.c2sping.high);
		cp_ave = calculatePingAverage(&pdat->data.c2sping);

		rp_low = formatHighLow(pdat->data.relping.low);
		rp_high = formatHighLow(pdat->data.relping.high);
		rp_ave = calculatePingAverage(&pdat->data.relping);

		fx_high = formatHighLow(pdat->data.flux.high);
		fx_ave = calculateEtcAverage(&pdat->data.flux);
		fxb_high = formatHighLow(pdat->data.fluxB.high);
		fxb_ave = calculateEtcAverage(&pdat->data.fluxB);

		dr_high = formatHighLow(pdat->data.drift.high);
		dr_ave = calculateEtcAverage(&pdat->data.drift);

		sl_high = ((float)pdat->data.s2closs.high)/100.f;
		sl_ave = ((float)calculateLossAverage(&pdat->data.s2closs))/100.f;
		cl_high = ((float)pdat->data.c2sloss.high)/100.f;
		cl_ave = ((float)calculateLossAverage(&pdat->data.c2sloss))/100.f;
		wl_high = ((float)pdat->data.wpnloss.high)/100.f;
		wl_ave = ((float)calculateLossAverage(&pdat->data.wpnloss))/100.f;

		chat->SendMessage(p, "lag: ping: s2c: %i (%i-%i)  c2s: %i (%i-%i)  rel: %i (%i-%i)    loss: s2c: %.2f (%.2f)  c2s: %.2f (%.2f)  wpn: %.2f (%.2f)    etc: flux: %i,%i (%i,%i)  drift: %i (%i)",
			sp_ave, sp_low, sp_high, cp_ave, cp_low, cp_high, rp_ave, rp_low, rp_high,
			sl_ave, sl_high, cl_ave, cl_high, wl_ave, wl_high,
			fx_ave, fxb_ave, fx_high, fxb_high, dr_ave, dr_high);

	}
	else
	{
		BDEF_PD(t);

		short sp_low, sp_high, sp_ave, cp_low, cp_high, cp_ave, fx_high, fx_ave, fxb_high, fxb_ave;
		float sl_high, sl_ave, cl_high, cl_ave;

		if (pdat->measureDelay)
		{
			chat->SendMessage(p, "lag: Not enough data has been collected to report a summary.");
			MYGUNLOCK;
			return;
		}

		sp_low = formatHighLow(pdat->data.s2cping.low);
		sp_high = formatHighLow(pdat->data.s2cping.high);
		sp_ave = calculatePingAverage(&pdat->data.s2cping);

		cp_low = formatHighLow(pdat->data.c2sping.low);
		cp_high = formatHighLow(pdat->data.c2sping.high);
		cp_ave = calculatePingAverage(&pdat->data.c2sping);

		fx_high = formatHighLow(pdat->data.flux.high);
		fx_ave = calculateEtcAverage(&pdat->data.flux);
		fxb_high = formatHighLow(pdat->data.fluxB.high);
		fxb_ave = calculateEtcAverage(&pdat->data.fluxB);

		sl_high = ((float)pdat->data.s2closs.high)/100.f;
		sl_ave = ((float)calculateLossAverage(&pdat->data.s2closs))/100.f;
		cl_high = ((float)pdat->data.c2sloss.high)/100.f;
		cl_ave = ((float)calculateLossAverage(&pdat->data.c2sloss))/100.f;


		chat->SendMessage(p, "lag: ping: s2c: %i (%i-%i)  c2s: %i (%i-%i)    loss: s2c: %.2f (%.2f)  c2s: %.2f (%.2f)    etc: flux: %i,%i (%i,%i)",
			sp_ave, sp_low, sp_high, cp_ave, cp_low, cp_high, sl_ave, sl_high, cl_ave, cl_high, fx_ave, fxb_ave, fx_high, fxb_high);

	}
	MYGUNLOCK;
}

//?laggraph
local void Claggraph(const char *cmd, const char *params, Player *p, const Target *target)
{
	Player *t = 0;
	akd_lag_broadsubdata *d;
	char buffer[256];

	if (target->type == T_PLAYER)
		t = target->u.p;
	else if (target->type == T_ARENA)
	{
		getParam(params, 0, buffer);

		if (buffer[0] > 32)
			t = pd->FindPlayer(buffer);

		if (!t && (buffer[0] > 32))
		{
			chat->SendMessage(p, "Player '%s' not found.", buffer);
			return;
		}
		if (!t)
			t = p;
	}
	else
		return;

	if (!t)
		return;

	getParam(params, 't', buffer);



	{
		BDEF_PD(t);
		int step;
		int halfstep;
		char graph[AKD_MAX_SAMPLESIZE + 10];
		int i;
		int current;
		int next;
		int minstep = 2;

		if (!buffer[0] || !strcasecmp(buffer, "c2sping"))
		{
			d = (akd_lag_broadsubdata *)&pdat->data.c2sping;
			minstep = 10;
		}
		else if (!strcasecmp(buffer, "s2cping"))
		{
			d = (akd_lag_broadsubdata *)&pdat->data.s2cping;
			minstep = 10;
		}
		else if (!strcasecmp(buffer, "relping"))
		{
			d = (akd_lag_broadsubdata *)&pdat->data.relping;
		}
		else if (!strcasecmp(buffer, "s2closs"))
		{
			d = (akd_lag_broadsubdata *)&pdat->data.s2closs;
		}
		else if (!strcasecmp(buffer, "c2sloss"))
		{
			d = (akd_lag_broadsubdata *)&pdat->data.c2sloss;
		}
		else if (!strcasecmp(buffer, "wpnloss"))
		{
			d = (akd_lag_broadsubdata *)&pdat->data.wpnloss;
		}
		else if (!strcasecmp(buffer, "flux") || !strcasecmp(buffer, "fluxa"))
		{
			d = (akd_lag_broadsubdata *)&pdat->data.flux;
			minstep = 5;
		}
		else if (!strcasecmp(buffer, "fluxb"))
		{
			d = (akd_lag_broadsubdata *)&pdat->data.fluxB;
			minstep = 10;
		}
		else if (!strcasecmp(buffer, "drift"))
		{
			d = (akd_lag_broadsubdata *)&pdat->data.drift;
		}
		else if (!strcasecmp(buffer, "testping"))
		{
			d = (akd_lag_broadsubdata *)&pdat->data.testping;
		}
		else
		{
			chat->SendMessage(p, "Invalid type specified. Use ?help laggraph for syntax.");
			return;
		}

		chat->SendMessage(p, "laggraph: Requested '%s'  Samples:%i  High:%i (has been high for %i sample(s))", buffer, d->taken, d->high, d->sinceHigh);


		MYGLOCK;

			step = d->high / 10;
			if (step < minstep)
			{
				step = minstep;
			}
			halfstep = step/2;

			current = d->high + (step - (d->high % step));
			next = current - step;
			for (i = 0; ; ++i)
			{
				int x = 0;
				int y = 5;
				sprintf(graph, "%4i ", current);

				while (x < d->taken)
				{
					int number = d->samples[x];

					if ((number >= (current - step)) && (number < (current + step)))
					{
						if ((number >= (current - halfstep)) && (number < (current + halfstep)))
						{
							if (d->current == x)
							{
								graph[y] = '*';
							}
							else
							{
								graph[y] = '=';
							}
						}
						else if (number % step)
						{
							graph[y] = '-';
						}
						else
						{
							graph[y] = ' ';
						}
					}
					else
					{
						graph[y] = ' ';
					}

					++y;

					if (d->current == x)
					{
						//make a break to indicate this is the latest part
						graph[y] = ' ';
						++y;
					}

					++x;
				}

				graph[y] = 0;

				chat->SendMessage(p, "%s", graph);

				current = next;
				if (current < 0)
					break;
				next -= step;
				if (current && next < 0)
					next = 0;
			}

		MYGUNLOCK;

	}

}

#define LAG_ANALYSIS_TYPES 5
local void Claganalysis(const char *cmd, const char *params, Player *p, const Target *target)
{
	int switchC;
	int switchD;
	char buffer[255];
	akd_lag_type map[LAG_ANALYSIS_TYPES] = {AKD_S2CPING, AKD_C2SPING, AKD_FLUX, AKD_S2CLOSS, AKD_C2SLOSS};

	Link *link;
	Player *x;
	akd_lag_broadsubdata *d;
	int i;
	int count = 0;
	int samples[LAG_ANALYSIS_TYPES];
	int sum[LAG_ANALYSIS_TYPES];
	int min[LAG_ANALYSIS_TYPES];
	int countByCat[LAG_ANALYSIS_TYPES][LAG_CATEGORIES_COUNT];

	switchD = strstr(params, "-d")?1:0;
	switchC = strstr(params, "-c")?1:0;

	for (i = 0; i < LAG_ANALYSIS_TYPES; ++i)
	{
		int j;
		samples[i] = 0;
		sum[i] = 0;
		min[i] = 10000;
		for (j = 0; j < LAG_CATEGORIES_COUNT; ++j)
			countByCat[i][j] = 0;
	}

	MYGLOCK;
	PDLOCK;
		FOR_EACH_PLAYER(x)
		{
			BDEF_PD(x);

			if (x->type != T_VIE && x->type != T_CONT)
				continue;

			for (i = 0; i < LAG_ANALYSIS_TYPES; ++i)
			{
				int ave;
				int cat;

				switch(i)
				{
				case 0: d = (akd_lag_broadsubdata *)&pdat->data.s2cping;	break;
				case 1: d = (akd_lag_broadsubdata *)&pdat->data.c2sping;	break;
				case 2: d = (akd_lag_broadsubdata *)&pdat->data.flux;		break;
				case 3: d = (akd_lag_broadsubdata *)&pdat->data.s2closs;	break;
				case 4: d = (akd_lag_broadsubdata *)&pdat->data.c2sloss;	break;
				}

				if (d->taken < 3)
				{
					continue;
				}

				samples[i] += d->taken;
				ave = calculateBroadAverage(d);
				sum[i] += ave;
				if (ave < min[i])
					min[i] = ave;

				if (d->broadtype == AKD_PING_TYPE)
				{
					cat = ave / LAG_CATEGORIES_PING_INTERVAL;
				}
				else if (d->broadtype == AKD_LOSS_TYPE)
				{
					cat = ave / LAG_CATEGORIES_LOSS_INTERVAL;
				}
				else if (d->broadtype == AKD_ETC_TYPE)
				{
					cat = ave / LAG_CATEGORIES_ETC_INTERVAL;
				}

				if (cat >= LAG_CATEGORIES_COUNT)
						cat = LAG_CATEGORIES_COUNT - 1;

				++countByCat[i][cat];
			}

			++count;
		}
	PDUNLOCK;
	MYGUNLOCK;

	chat->SendMessage(p, "Lag Analysis");
	chat->SendMessage(p, " Players:%i", count);

	if (count < 1)
		return;

	buffer[0] = 0;
	sprintf(buffer, "  %-12s", "Type");
	for (i = 0; i < LAG_ANALYSIS_TYPES; ++i)
	{
		char buffer2[20];
		snprintf(buffer2, sizeof(buffer2), "%-18s", AKD_LAG_TYPE_STRINGS[map[i]]);
		strncat(buffer, buffer2, sizeof(buffer));
	}

	chat->SendMessage(p, "%s", buffer);

	sprintf(buffer, "  %-12s", "  Min");
	for (i = 0; i < LAG_ANALYSIS_TYPES; ++i)
	{
		char buffer2[32];

		if (IS_PING_TYPE(map[i]))
			snprintf(buffer2, sizeof(buffer2), "%6i%-12s", min[i], "ms");
		else if (IS_LOSS_TYPE(map[i]))
			snprintf(buffer2, sizeof(buffer2), "%6.2f%-12s", ((float)min[i])/100.f, "%");
		else//if (IS_ETC_TYPE(map[i]))
			snprintf(buffer2, sizeof(buffer2), "%6i%-12s", min[i], "");
		strncat(buffer, buffer2, sizeof(buffer));
	}

	chat->SendMessage(p, "%s", buffer);

	sprintf(buffer, "  %-12s", "  Ave");
	for (i = 0; i < LAG_ANALYSIS_TYPES; ++i)
	{
		char buffer2[32];

		if (IS_PING_TYPE(map[i]))
			snprintf(buffer2, sizeof(buffer2), "%6i%-12s", sum[i]/count, "ms");
		else if (IS_LOSS_TYPE(map[i]))
			snprintf(buffer2, sizeof(buffer2), "%6.2f%-12s", ((float)(sum[i]/count))/100.f, "%");
		else//if (IS_ETC_TYPE(map[i]))
			snprintf(buffer2, sizeof(buffer2), "%6i%-12s", sum[i]/count, "");
		strncat(buffer, buffer2, sizeof(buffer));
	}

	chat->SendMessage(p, "%s", buffer);

	for (i = 0; i < LAG_CATEGORIES_COUNT; ++i)
	{
		int doContinue = 1;
		int j;
		sprintf(buffer, "   %-11s", LAG_CATEGORIES[i]);
		for (j = 0; j < LAG_ANALYSIS_TYPES; ++j)
		{
			char buffer2[32];
			if (countByCat[j][i] > 0)
				doContinue = 0;

			if (switchC)
			{
				if (switchD)
				{
					if (IS_PING_TYPE(map[j]))
						snprintf(buffer2, sizeof(buffer2), " %4i > %-4i%-6s", countByCat[j][i], LAG_CATEGORIES_PING_INTERVAL * i, "ms");
					else if (IS_LOSS_TYPE(map[j]))
						snprintf(buffer2, sizeof(buffer2), " %4i > %-6.2f%-4s", countByCat[j][i], ((float)(LAG_CATEGORIES_LOSS_INTERVAL * i))/100.f, "%");
					else//if (IS_ETC_TYPE(map[j]))
						snprintf(buffer2, sizeof(buffer2), " %4i > %-10i", countByCat[j][i], LAG_CATEGORIES_ETC_INTERVAL * i);
				}
				else
				{
					sprintf(buffer2, " %4i%-13s", countByCat[j][i], "");
				}
			}
			else
			{
				if (switchD)
				{
					if (IS_PING_TYPE(map[j]))
						snprintf(buffer2, sizeof(buffer2), " %5.1f%% > %-4i%-4s", 100.f*((float)countByCat[j][i])/((float)count), LAG_CATEGORIES_PING_INTERVAL * i, "ms");
					else if (IS_LOSS_TYPE(map[j]))
						snprintf(buffer2, sizeof(buffer2), " %5.1f%% > %-6.2f%-2s",100.f*((float)countByCat[j][i])/((float)count), ((float)(LAG_CATEGORIES_LOSS_INTERVAL * i))/100.f, "%");
					else//if (IS_ETC_TYPE(map[j]))
						snprintf(buffer2, sizeof(buffer2), " %5.1f%% > %-8i", 100.f*((float)countByCat[j][i])/((float)count), LAG_CATEGORIES_ETC_INTERVAL * i);
				}
				else
				{
					sprintf(buffer2, " %-5.1f%%%-11s", 100.f*((float)countByCat[j][i])/((float)count), "");
				}
			}

			buffer2[18] = 0;

			strncat(buffer, buffer2, sizeof(buffer));
			buffer[104] = 0;
		}

		if (doContinue)
			continue;

		chat->SendMessage(p, "%s", buffer);
	}

	sprintf(buffer, "  %-12s", "  SPP");
	for (i = 0; i < LAG_ANALYSIS_TYPES; ++i)
	{
		char buffer2[32];

		sprintf(buffer2, "%-18i", samples[i]/count);
		strncat(buffer, buffer2, sizeof(buffer));
	}
	chat->SendMessage(p, "%s", buffer);
}


//body:lagReport
local akd_lag_report *lagReport(Player *t, akd_lag_report *s)
{
	MYGLOCK;
	{
		BDEF_PD(t);

		s->s2c_ping_low = pdat->data.s2cping.low;
		if (s->s2c_ping_low < 0) s->s2c_ping_low = 0;
		s->s2c_ping_high = pdat->data.s2cping.high;
		if (s->s2c_ping_high < 0) s->s2c_ping_high = 0;
		s->s2c_ping_ave = calculatePingAverage(&pdat->data.s2cping);
		s->c2s_ping_low = pdat->data.c2sping.low;
		s->c2s_ping_high = pdat->data.c2sping.high;
		s->c2s_ping_ave = calculatePingAverage(&pdat->data.c2sping);
		s->rel_ping_low = pdat->data.relping.low;
		s->rel_ping_high = pdat->data.relping.high;
		s->rel_ping_ave = calculatePingAverage(&pdat->data.relping);
		s->flux_high = pdat->data.flux.high;
		s->flux_ave = calculateEtcAverage(&pdat->data.flux);
		s->fluxb_high = pdat->data.fluxB.high;
		s->fluxb_ave = calculateEtcAverage(&pdat->data.fluxB);
		s->drift_high = pdat->data.drift.high;
		s->drift_ave = calculateEtcAverage(&pdat->data.drift);
		s->s2c_loss_high = ((float)pdat->data.s2closs.high)/100.f;
		s->s2c_loss_ave = ((float)calculateLossAverage(&pdat->data.s2closs))/100.f;
		s->c2s_loss_high = ((float)pdat->data.c2sloss.high)/100.f;
		s->c2s_loss_ave = ((float)calculateLossAverage(&pdat->data.c2sloss))/100.f;
	MYGUNLOCK;
	}

	return s;
}

