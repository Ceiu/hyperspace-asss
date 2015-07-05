 
#include <stdlib.h>
#include <string.h>

#include "asss.h"


// Modules...
local Imodman *mm;
local Ichat *chat;
local Icmdman *cmdman;
local Iplayerdata *pd;
local Igame *game;



// Util functions...
////////////////////////////////////////////////////////////////////////////////////////////////////
local void sendMessage(const char *cmd, const char *params, Player *player, const Target *target) {
    LinkedList plist, cnlist;

    int type = 0;		// Verify later
	int cntype = 0;
	
    char *next, *msg = 0;

    Link *link;         // Linked list stuff...
    Player *p;


	// Process input...
    while(params) {
        switch(*params) {
            case '-':   // Process parameter
                switch(*++params) {
                    case 't':
                        params = strchr(params, ' ');
                        if(!params) return; // Invalid usage.

                        type = strtol(++params, &next, 0) & 0xFF;
                        if(next == params) return; // Invalid type

                        params = next;
                        break;
					
					case 'm':
						params = strchr(params, ' ');
						if(!params) return; // Invalid usage.

						msg = (char*)(params + 1);
						params = 0;
						break;
						
					default:
                        return; // Invalid usage.
                }
                break;

            case ' ':
                ++params;
                break;

			default:
                return; // Invalid usage.
        }
    }

	// Verify input...
	if(!type) return;	// Type wasn't set.
	if(!msg) return;	// Message wasn't set.

	// Set chatnet type...
	//	6	= Green/Fuschia
	//	33	= Gray/Fushchia
	//	79	= Purple/Fuschia
	if(type > 9 || type == 6)
		cntype = MSG_PUB;
	else
		cntype = type;

    // Build player lists...
    LLInit(&plist);
    LLInit(&cnlist);

    switch(target->type) {
        case T_PLAYER:
            if(IS_STANDARD(target->u.p))
                LLAdd(&plist, target->u.p);
            else if(IS_CHAT(target->u.p))
                LLAdd(&cnlist, target->u.p);
            break;

        case T_FREQ:
            pd->Lock();
            FOR_EACH_PLAYER(p) {
                if(p->arena == target->u.freq.arena) {
                    if(p->pkt.freq == target->u.freq.freq) {
                        if(IS_STANDARD(p))
                            LLAdd(&plist, p);
                        else if(IS_CHAT(p))
                            LLAdd(&cnlist, p);
                    }
                }
            }
            pd->Unlock();
            break;

		default:
        case T_ARENA:
			pd->Lock();
			FOR_EACH_PLAYER(p) {
				if(p->arena == target->u.arena) {
					if(IS_STANDARD(p))
						LLAdd(&plist, p);
					else if(IS_CHAT(p))
						LLAdd(&cnlist, p);
				}
			}
			pd->Unlock();
			break;
			
		case T_ZONE:
			pd->Lock();
            FOR_EACH_PLAYER(p) {
				if(IS_STANDARD(p))
					LLAdd(&plist, p);
				else if(IS_CHAT(p))
					LLAdd(&cnlist, p);
            }
            pd->Unlock();
			break;
	}

    chat->SendAnyMessage(&plist, type, 0, player, "%s", msg);
    chat->SendAnyMessage(&cnlist, cntype, 0, player, "%s", msg);

    LLEmpty(&plist);
    LLEmpty(&cnlist);
}

// Commands...
////////////////////////////////////////////////////////////////////////////////////////////////////
// Color text
local void cmd_ct(const char *cmd, const char *params, Player *player, const Target *target) {
	sendMessage(cmd, params, player, target);
}

// Zone color text
local void cmd_zct(const char *cmd, const char *params, Player *player, const Target *target) {

	Target t;
	t.type = T_ZONE;
	
	sendMessage(cmd, params, player, &t);
}

// Set ship & freq
local void cmd_ssf(const char *cmd, const char *params, Player *player, const Target *target) {
	
	int ship = -1;
	int freq = -1;
	int lock_time = 0;

	char *next;
	
	// Linked list junk
	LinkedList set = LL_INITIALIZER;
	Link *link;

	
	// Check parameters...
    while(params) {
        switch(*params) {
            case '-':   // Process parameter
                switch(*++params) {
                    case 's':
                        params = strchr(params, ' ');
                        if(!params) return; // Invalid usage.

                        ship = strtol(++params, &next, 0) & 0xF;
                        if(next == params) return; // Invalid type

						params = next;
						break;
					
					case 'f':
                        params = strchr(params, ' ');
                        if(!params) return; // Invalid usage.

                        freq = strtol(++params, &next, 0) & 0x3FFF;
                        if(next == params) return; // Invalid type

						params = next;
						break;
						
					case 't':
                        params = strchr(params, ' ');
                        if(!params) return; // Invalid usage.

                        lock_time = strtol(++params, &next, 0) & 0x7FFF;
                        if(next == params) return; // Invalid type

						params = next;
						break;
						
					default:
						params = 0;
                        break;
                }
                break;

            case ' ':
                ++params;
                break;

			default:
                params = 0;
				break;
        }
    }
	
	// Validate input...
	if(ship == -1 && freq == -1)
		return; // Neither were set.
		

	// Set ship and/or freq...
	pd->Lock();
	pd->TargetToSet(target, &set);
	
	for (link = LLGetHead(&set); link; link = link->next) {
		Player *p = link->data;
		
		if(ship >= 0 && ship <= 8)		game->SetShip(p, ship);
		if(freq >= 0 && freq <= 9999)	game->SetFreq(p, freq);
	}

	LLEmpty(&set);
	
	// Lock the target(s), if necessary...
	if(lock_time > 0)
		game->Lock(target, 0, 0, lock_time);
	
	pd->Unlock();
	

}


// Module stuff...
////////////////////////////////////////////////////////////////////////////////////////////////////
EXPORT const char info_cl_botutil[] = "Bot utilities v1.0 -- Ceiu";

EXPORT int MM_cl_botutil(int action, Imodman *_mm, Arena *arena) {

    switch(action) {
        case MM_LOAD:
            mm = _mm;

            // Get interfaces...
            chat = mm->GetInterface(I_CHAT, ALLARENAS);
            cmdman = mm->GetInterface(I_CMDMAN, ALLARENAS);
            pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
			game = mm->GetInterface(I_GAME, ALLARENAS);

            if(!chat || !cmdman || !pd || !game) {
                mm->ReleaseInterface(chat);
                mm->ReleaseInterface(cmdman);
                mm->ReleaseInterface(pd);
				mm->ReleaseInterface(game);

                return MM_FAIL;
            }

            // Register commands...
            cmdman->AddCommand("+ct", cmd_ct, ALLARENAS, NULL);
			cmdman->AddCommand("+zct", cmd_zct, ALLARENAS, NULL);
			cmdman->AddCommand("+ssf", cmd_ssf, ALLARENAS, NULL);

            return MM_OK;

    //////////////////////////////////////////////////

        case MM_UNLOAD:

            // Release commands...
            cmdman->RemoveCommand("+ct", cmd_ct, ALLARENAS);
			cmdman->RemoveCommand("+zct", cmd_zct, ALLARENAS);
			cmdman->RemoveCommand("+ssf", cmd_ssf, ALLARENAS);

            // Release interfaces...
            mm->ReleaseInterface(chat);
            mm->ReleaseInterface(cmdman);
            mm->ReleaseInterface(pd);
			mm->ReleaseInterface(game);

            return MM_OK;

    //////////////////////////////////////////////////

        case MM_ATTACH:
            return MM_OK;

    //////////////////////////////////////////////////

        case MM_DETACH:
            return MM_OK;

    }

    return MM_FAIL;
}
