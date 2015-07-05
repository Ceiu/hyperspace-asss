#include <alloca.h>
#include "asss.h"
#include "hs_flagutil.h"

// Modules...
local Iflagcore *fc;
local Ilogman *lm;
local Imodman *mm;
local Inet *net;

typedef struct
{
    u8  packet_id;  // 0x13
    u16 flag_id;
    u16 player_id;
} S2CFlagPickup;

typedef struct
{
    u8  packet_id;  // 0x16
    u16 player_id;
} S2CDropFlags;

// Commands
////////////////////////////////////////////////////////////////////////////////////////////////////
local void ResetFlagTimer(Player *p)
{
    int flags = fc->CountPlayerFlags(p);

    if(!flags)
        return;

    int size = 3 + sizeof(S2CDropFlags) + flags + flags * sizeof(S2CFlagPickup);
    byte* packet = alloca( size );
    if(packet)
    {
        FlagInfo flag_info;

        int offset;
        S2CDropFlags* pkt_drop;
        S2CFlagPickup* pkt_pickup;

        // Build!
        packet[0] = 0x00;
        packet[1] = 0x0E;
        packet[2] = sizeof(S2CDropFlags);

        pkt_drop = (S2CDropFlags*)&packet[3];
        pkt_drop->packet_id = 0x16;
        pkt_drop->player_id = p->pid;

        int i = 0;
        for(int fid = 0; fid < 256; ++fid)
        {
            if(fc->GetFlags(p->arena, fid, &flag_info, 1))
            {
                if(flag_info.state == FI_CARRIED && flag_info.carrier->pid == p->pid)
                {
                    offset = 3 + sizeof(S2CDropFlags) + i + sizeof(S2CFlagPickup) * i;

                    packet[offset] = sizeof(S2CFlagPickup);
                    pkt_pickup = (S2CFlagPickup*)&packet[offset + 1];

                    pkt_pickup->packet_id = 0x13;
                    pkt_pickup->flag_id = fid;
                    pkt_pickup->player_id = p->pid;

                    if(++i >= flags)
                    {
                        net->SendToOne(p, packet, size, NET_RELIABLE);
                        break; // Fuck you, grel.
                    }
                }
            } else {
                break; // Fuck you again, grel.
            }
        }
    } else {
        lm->Log(L_ERROR, "<flagutil> No memory =(");
    }
}


// Interface Nonsense
////////////////////////////////////////////////////////////////////////////////////////////////////
local Iflagutil interface =
{
    INTERFACE_HEAD_INIT(I_FLAGUTIL, "FlagUtil")
    ResetFlagTimer
};

// Module nonsense.
////////////////////////////////////////////////////////////////////////////////////////////////////
EXPORT const char info_flagutil[] = "v1.00 - Chris \"Cerium\" Rog";

EXPORT int MM_flagutil(int action, Imodman *_mm, Arena *arena)
{
    switch(action)
    {
        case MM_LOAD:
            mm = _mm;

            // Register interfaces...
            fc = mm->GetInterface(I_FLAGCORE, ALLARENAS);
            lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
            net = mm->GetInterface(I_NET, ALLARENAS);


            if(!fc || !lm || !net)
            {
                mm->ReleaseInterface(fc);
                mm->ReleaseInterface(lm);
                mm->ReleaseInterface(net);

                return MM_FAIL;
            }

            mm->RegInterface(&interface, ALLARENAS);

            // YAY!
            return MM_OK;

    //////////////////////////////////////////////////

        case MM_UNLOAD:
            if(mm->UnregInterface(&interface, ALLARENAS))
                return MM_FAIL;

            // Release interfaces...
            mm->ReleaseInterface(fc);
            mm->ReleaseInterface(lm);
            mm->ReleaseInterface(net);

            return MM_OK;

    //////////////////////////////////////////////////

        case MM_ATTACH:

            return MM_FAIL;

    //////////////////////////////////////////////////

        case MM_DETACH:


            return MM_FAIL;

    //////////////////////////////////////////////////

        //default:
            // watf?
    }

	return MM_FAIL;
}
