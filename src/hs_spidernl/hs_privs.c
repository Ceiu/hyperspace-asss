#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "asss.h"
#include "hscore_spawner.h"

//Interfaces
local Imodman *mm;
local Iarenaman *aman;
local Iconfig *cfg;
local Ichat *chat;
local Icmdman *cmd;
local Igame *game;

#define MAX_PASSWORD_LENGTH 16

typedef struct Priv
{
    char password[MAX_PASSWORD_LENGTH];
    bool passwordProtected;
    
    int freq;    
    int players; //Players left in this frequency. If this reaches 0, remove frequency.
} Priv;

#define MAX_PRIVS 100

typedef struct ArenaData
{   
    struct
    {
        int startFrequency;  
        int maxPlayers;
    } Config;
    
    Priv *privs[MAX_PRIVS];
} ArenaData;
local int adkey = -1;
local struct ArenaData *getArenaData(Arena *arena)
{
    ArenaData *adata = P_ARENA_DATA(arena, adkey);
    return adata;
}

//Prototypes
local void Ccreatepriv(const char *cmd, const char *params, Player *p, const Target *target);
local void Cjoin(const char *cmd, const char *params, Player *p, const Target *target);

local void shipFreqChangeCB(Player *p, int newship, int oldship, int newfreq, int oldfreq);
local void playerActionCB(Player *p, int action, Arena *arena);

local void readConfig(ConfigHandle ch, ArenaData *adata);
local int hasFullEnergy(Player *p);
local void shipReset(Player *p);

local void getInterfaces();
local bool checkInterfaces();
local void releaseInterfaces();

//Commands
local void Ccreatepriv(const char *cmd, const char *params, Player *p, const Target *target)
{
    if (p->p_ship == SHIP_SPEC)
    {
        chat->SendMessage(p, "You can't create a private frequency while in spectator mode."); 
        return;          
    }  
    
    ArenaData *adata = getArenaData(p->arena);  
    
    if (!hasFullEnergy(p))
	{
        chat->SendMessage(p, "Not enough energy to change frequencies.");   
        return;         
    }    
      
    int i;
    int freq = -1;
    
    for (i = 0; i < MAX_PRIVS; i++)
    {
        if (!adata->privs[i])
        {
            freq = adata->Config.startFrequency + i; 
            break;               
        }
    }
    
    if (freq < 0)
    {
        chat->SendMessage(p, "No private frequencies are currently available.");
        return;
    }
    
    Priv *priv = amalloc(sizeof(Priv));
    
    if (params && *params)
    {
        astrncpy(priv->password, params, MAX_PASSWORD_LENGTH);      
        priv->passwordProtected = true; 
    }
    else
    {
        priv->passwordProtected = false;
    }
    
    priv->freq = freq;
    
    adata->privs[i] = priv;
    
    game->SetFreq(p, freq);
    shipReset(p);
}

local void Cjoin(const char *cmd, const char *params, Player *p, const Target *target)
{
    if (target->type != T_PLAYER)
    {
        chat->SendMessage(p, "This command must be sent to a player on the frequency you want to join.");             
        return;          
    }
    
    int freq = target->u.p->p_freq;
    
    if (freq == p->p_freq)
    {
        chat->SendMessage(p, "You're already on that frequency!");             
        return;             
    }
    
    ArenaData *adata = getArenaData(p->arena);
    
    if (freq < adata->Config.startFrequency ||
       freq >= adata->Config.startFrequency + MAX_PRIVS)
    {
        chat->SendMessage(p, "That player isn't on a private frequency."); 
        return;
    }
    
    Priv *priv = adata->privs[freq - adata->Config.startFrequency]; 
    if (!priv)
    {
        chat->SendMessage(p, "Error: Frequency undefined. Please report this error to SpiderNL.");   
        return;    
    }
    
    if (priv->players >= adata->Config.maxPlayers)
    {
        chat->SendMessage(p, "That private frequency is full. The maximum size for a private frequency is %i players.", adata->Config.maxPlayers); 
        return;              
    }
    
    if (priv->passwordProtected)
    {
        if (!params)
        {
            chat->SendMessage(p, "That private frequency is password-protected. You need to specify the password.");           
        } 
        else
        {
            if (strcmp(params, priv->password) == 0)
            {
                game->SetFreq(p, freq);   
                shipReset(p);
            }
            else
            {
                chat->SendMessage(p, "Access denied: wrong password."); 
            }
        }                       
    }
    else
    {
        game->SetFreq(p, freq);
        shipReset(p);
    }
}

//Callbacks
local void shipFreqChangeCB(Player *p, int newship, int oldship, int newfreq, int oldfreq)
{
    if (newfreq == oldfreq) return;  
      
    ArenaData *adata = getArenaData(p->arena);    
    
    if (oldfreq >= adata->Config.startFrequency)
    {
        if (oldfreq < adata->Config.startFrequency + MAX_PRIVS)
        {
            //Player left private freq 
            int index = oldfreq - adata->Config.startFrequency;
            Priv *priv = adata->privs[index]; 
            
            if (priv)
            {
                priv->players--;
                
                if (priv->players <= 0)
                {
                    adata->privs[index] = NULL;              
                    afree(priv);              
                }     
            }   
        }       
    }
    
    if (newfreq >= adata->Config.startFrequency)
    {
        if (newfreq < adata->Config.startFrequency + MAX_PRIVS)
        {
            //Player entered private freq
            Priv *priv = adata->privs[newfreq - adata->Config.startFrequency];
            
            if (priv)
            {
                priv->players++;     
            }        
        }       
    }
}

local void playerActionCB(Player *p, int action, Arena *arena)
{
    if (action == PA_LEAVEARENA)
	{   
		shipFreqChangeCB(p, SHIP_SPEC, SHIP_SPEC, -1, p->p_freq);   
	}  
}

//Misc
local void readConfig(ConfigHandle ch, ArenaData *adata)
{   
    adata->Config.startFrequency = cfg->GetInt(ch, "HS_Privs", "StartFrequency", 100);
    adata->Config.maxPlayers = cfg->GetInt(ch, "HS_Privs", "MaxPlayers", 3);
}

local int hasFullEnergy(Player *p)
{
	int full = 1;
	if (p->p_ship == SHIP_SPEC)
	   return full;
	
	Ihscorespawner *spawner = mm->GetInterface(I_HSCORE_SPAWNER, p->arena);
	if (spawner)
	{
		int max = spawner->getFullEnergy(p);
		if (max != p->position.energy)
		{
			full = 0;
		}
	}
	mm->ReleaseInterface(spawner);

	return full;
}

local void shipReset(Player *p)
{
    Target target;
    target.type = T_PLAYER;    
    target.u.p = p;
    
    game->ShipReset(&target);
}

//Used interfaces, etc. -> Module stuff..
local void getInterfaces()
{
    aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);  
    cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
    chat = mm->GetInterface(I_CHAT, ALLARENAS);
    cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
    game = mm->GetInterface(I_GAME, ALLARENAS);   
}
local bool checkInterfaces()
{
    if (aman && cfg && chat && cmd && game)
       return true;
    return false;
}
local void releaseInterfaces()
{
    mm->ReleaseInterface(aman);   
    mm->ReleaseInterface(cfg);       
    mm->ReleaseInterface(chat);    
    mm->ReleaseInterface(cmd);          
    mm->ReleaseInterface(game);   
}

local helptext_t Ccreatepriv_help =
    "Arguments: password (leave blank for an unprotected private frequency)\n"
	"Puts you on a new private frequency that other players may join.\n"
	"If a password is specified, other players will not be able to join unless\n"
	"they know the password.";

local helptext_t Cjoin_help =
	"Targets: player\n"
	"Arguments: password (leave blank if unprotected)\n"
	"Puts you on the private frequency of the player this command is sent to.";

EXPORT const char info_hs_privs[] = "hs_privs v1.1 by Spidernl\n";
EXPORT int MM_hs_privs(int action, Imodman *mm_, Arena *arena)
{
    if (action == MM_LOAD)
	{
		mm = mm_;
		
		getInterfaces();
		if (!checkInterfaces())
		{
            releaseInterfaces();
            return MM_FAIL;                 
        }
        
        adkey = aman->AllocateArenaData(sizeof(struct ArenaData));

        if (adkey == -1) //Memory check
        {
            releaseInterfaces();
            return MM_FAIL;
        }
		
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{	
        aman->FreeArenaData(adkey);
        
        releaseInterfaces();
        
		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
        ArenaData *adata = getArenaData(arena); 
         
        readConfig(arena->cfg, adata); 
        
        mm->RegCallback(CB_PLAYERACTION, playerActionCB, arena);  
        mm->RegCallback(CB_SHIPFREQCHANGE, shipFreqChangeCB, arena);
        
        cmd->AddCommand("createpriv", Ccreatepriv, arena, Ccreatepriv_help);
        cmd->AddCommand("join", Cjoin, arena, Cjoin_help);
 
        return MM_OK; 
    }
    else if (action == MM_DETACH)
    {
        cmd->RemoveCommand("createpriv", Ccreatepriv, arena);
        cmd->RemoveCommand("join", Cjoin, arena);
         
        mm->UnregCallback(CB_SHIPFREQCHANGE, shipFreqChangeCB, arena); 
        mm->UnregCallback(CB_PLAYERACTION, playerActionCB, arena);  
        
        ArenaData *adata = getArenaData(arena); 
        int i;
        for (i = 0; i < MAX_PRIVS; i++)
        {
            afree(adata->privs[i]);
        }
         
        return MM_OK; 
    }   
      
	return MM_FAIL;
}
