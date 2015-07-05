
/* dist: public */

/* this module authenticates players based on hashed passwords from a
 * file. the file should look rougly like:

[General]
AllowUnknown = yes

[Users]
Grelminar = 561fb382be64fdfd6c4bc4450577b966
other.player = 37b51d194a7513e45b56f6524f2d51f2
bad-person = locked
someone = any

 * the file should be in conf/passwd.conf.
 *
 * for players listed in users: if the key has the value "locked", they
 * will be denied entry. if it's set to "any", they will be allowed in
 * with any password. otherwise, it's a md5 hash of their player name
 * and password, set by the ?passwd command.
 */

#include <string.h>
#include <stdio.h>

#include "asss.h"

#include "md5.h"

#define PWHASHLEN 32

typedef struct pdata
{
	int set;
	char pwhash[PWHASHLEN+1];
} pdata;

local Imodman *mm;
local Iplayerdata *pd;
local Iconfig *cfg;
local Icmdman *cmd;
local Ichat *chat;
local ConfigHandle pwdfile;
local int pdkey;


local void newplayer(Player *p, int new)
{
	pdata *pdata = PPDATA(p, pdkey);
	pdata->set = FALSE;
}


local void hash_password(const char *name, const char *pwd, char
		out[PWHASHLEN+1])
{
	static const char table[16] = "0123456789abcdef";
	struct MD5Context ctx;
	unsigned char hash[16];
	char msg[56];
	int i;

	memset(msg, 0, 56);
	astrncpy(msg, name, 24);
	ToLowerStr(msg);
	astrncpy(msg + 24, pwd, 32);

	MD5Init(&ctx);
	MD5Update(&ctx, (unsigned char *)msg, 56);
	MD5Final(hash, &ctx);

	for (i = 0; i < PWHASHLEN/2; i++)
	{
		out[i*2+0] = table[(hash[i] & 0xf0) >> 4];
		out[i*2+1] = table[(hash[i] & 0x0f) >> 0];
	}
	out[PWHASHLEN] = 0;
}


local void authenticate(Player *p, struct LoginPacket *lp, int lplen,
		void (*done)(Player *p, AuthData *data))
{
	pdata *pdata = PPDATA(p, pdkey);
	AuthData ad;
	const char *line;
	char name[32];
	char pass[32];

	/* copy to local storage in case it's not null terminated */
	astrncpy(name, lp->name, sizeof(name));

	/* ignore zone password in player password */
	delimcpy(pass, lp->password, sizeof(pass), '*');

	/* setup basic authdata */
	memset(&ad, 0, sizeof(ad));
	ad.authenticated = FALSE;
	astrncpy(ad.name, name, sizeof(ad.name));
	astrncpy(ad.sendname, name, sizeof(ad.sendname));

	/* hash password into pdata */
	hash_password(name, pass, pdata->pwhash);
	pdata->set = TRUE;

	/* check if this user has an entry */
	line = cfg->GetStr(pwdfile, "users", name);

	if (line)
	{
		if (!strcmp(line, "lock"))
			ad.code = AUTH_NOPERMISSION;
		else if (!strcmp(line, "any"))
			ad.code = AUTH_OK;
		else
		{
			if (strcmp(pdata->pwhash, line))
				ad.code = AUTH_BADPASSWORD;
			else
			{
				/* only a correct password gets marked as authenticated */
				ad.authenticated = TRUE;
				ad.code = AUTH_OK;
			}
		}
	}
	else
	{
		/* no match found */

		/* cfghelp: General:AllowUnknown, passwd.conf, bool, def: 1, \
		 * mod: auth_file
		 * Determines whether to allow players not listed in the
		 * password file. */
		int allow = cfg->GetInt(pwdfile, "General", "AllowUnknown", TRUE);

		if (allow)
			ad.code = AUTH_OK;
		else
			ad.code = AUTH_NOPERMISSION2;
	}

	done(p, &ad);
}


local helptext_t local_password_help =
"Module: auth_file\n"
"Targets: none\n"
"Args: <new password>\n"
"Changes your local server password. Note that this command only changes\n"
"the password used by the auth_file authentication mechanism (used when the\n"
"billing server is disconnected). This command does not involve the billing\n"
"server.\n";

local void Cpasswd(const char *tc, const char *params, Player *p, const Target *target)
{
	char hex[PWHASHLEN+1];

	if (!*params)
		chat->SendMessage(p, "You must specify a password.");
	else
	{
		/* cfghelp: General:RequireAuthenticationToSetPassword, \
		 * passwd.conf, bool, def: 1, mod: auth_file
		 * If true, you must be authenticated (have used a correct password)
		 * according to this module or some other module before using
		 * ?local_password to change your local password. */
		if (cfg->GetInt(pwdfile, "General", "RequireAuthenticationToSetPassword", TRUE) &&
		    !p->flags.authenticated)
			chat->SendMessage(p, "You must be authenticated to change your local password.");
		else
		{
			hash_password(p->name, params, hex);
			cfg->SetStr(pwdfile, "users", p->name, hex, NULL, TRUE);
			chat->SendMessage(p, "Password set");
		}
	}
}


local helptext_t addallowed_help =
"Module: auth_file\n"
"Targets: none\n"
"Args: <player name>\n"
"Adds a player to passwd.conf with no set password. This will allow them\n"
"to log in when AllowUnknown is set to false, and has no use otherwise.\n";

local void Caddallowed(const char *tc, const char *params, Player *p, const Target *target)
{
	if (!*params)
		chat->SendMessage(p, "You must specify a player name.");
	else
	{
		const char *pwd = cfg->GetStr(pwdfile, "users", params);
		if (pwd)
			chat->SendMessage(p, "%s has already set a local password.", params);
		else
		{
			char buf[128];
			time_t _time;
			struct tm _tm;

			time(&_time);
			alocaltime_r(&_time, &_tm);
			snprintf(buf, sizeof(buf), "added by %s on ", p->name);
			strftime(buf + strlen(buf), sizeof(buf) - strlen(buf),
					"%a %b %d %H:%M:%S %Y", &_tm);

			cfg->SetStr(pwdfile, "users", params, "any", buf, TRUE);
			chat->SendMessage(p, "Added %s to the allowed player list.", params);
		}
	}
}


local helptext_t set_local_password_help =
"Module: auth_file\n"
"Targets: player\n"
"Args: none\n"
"If used on a player that has no local password set, it will set their\n"
"local password to the password they used to log in to this session.\n";

local void Cset_local_password(const char *tc, const char *params, Player *p, const Target *target)
{
	if (target->type != T_PLAYER)
		chat->SendMessage(p, "You must use this on a player.");
	else
	{
		Player *t = target->u.p;
		pdata *pdata = PPDATA(t, pdkey);
		const char *pwd = cfg->GetStr(pwdfile, "users", t->name);

		if (pwd)
			chat->SendMessage(p, "%s has already set a local password.", t->name);
		else if (!pdata->set)
			/* maybe they logged in with another auth module? */
			chat->SendMessage(p, "Hashed password missing.");
		else
		{
			char buf[128];
			time_t _time;
			struct tm _tm;

			time(&_time);
			alocaltime_r(&_time, &_tm);

			snprintf(buf, sizeof(buf), "added by %s on ", p->name);
			strftime(buf + strlen(buf), sizeof(buf) - strlen(buf),
					"%a %b %d %H:%M:%S %Y", &_tm);

			cfg->SetStr(pwdfile, "users", t->name, pdata->pwhash, buf, TRUE);
			chat->SendMessage(p, "Set local password for %s.", t->name);
			chat->SendMessage(t, "Your password has been set as a local password by %s.", p->name);
		}
	}
}



local Iauth myauth =
{
	INTERFACE_HEAD_INIT(I_AUTH, "auth-file")
	authenticate
};

EXPORT const char info_auth_file[] = CORE_MOD_INFO("auth_file");

EXPORT int MM_auth_file(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);

		if (!cfg || !cmd || !pd || !chat) return MM_FAIL;

		pdkey = pd->AllocatePlayerData(sizeof(pdata));
		if (pdkey == -1) return MM_FAIL;

		pwdfile = cfg->OpenConfigFile(NULL, "passwd.conf", NULL, NULL);

		if (!pwdfile) return MM_FAIL;

		cmd->AddCommand("passwd", Cpasswd, ALLARENAS, local_password_help);
		cmd->AddCommand("local_password", Cpasswd, ALLARENAS, local_password_help);
		cmd->AddCommand("addallowed", Caddallowed, ALLARENAS, addallowed_help);
		cmd->AddCommand("set_local_password", Cset_local_password, ALLARENAS, set_local_password_help);
		cmd->AddUnlogged("passwd");
		cmd->AddUnlogged("local_password");

		mm->RegCallback(CB_NEWPLAYER, newplayer, ALLARENAS);
		mm->RegInterface(&myauth, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&myauth, ALLARENAS))
			return MM_FAIL;
		mm->UnregCallback(CB_NEWPLAYER, newplayer, ALLARENAS);
		cmd->RemoveCommand("passwd", Cpasswd, ALLARENAS);
		cmd->RemoveCommand("local_password", Cpasswd, ALLARENAS);
		cmd->RemoveCommand("addallowed", Caddallowed, ALLARENAS);
		cmd->RemoveCommand("set_local_password", Cset_local_password, ALLARENAS);
		cmd->RemoveUnlogged("passwd");
		cmd->RemoveUnlogged("local_password");
		cfg->CloseConfigFile(pwdfile);
		pd->FreePlayerData(pdkey);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(pd);
		return MM_OK;
	}
	return MM_FAIL;
}


