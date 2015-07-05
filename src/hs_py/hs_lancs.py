##
## Hyperspace Team Lancaster Notification
## D1st0rt, monkey, SSCE Hyperspace
## License: MIT/X11
## Last Updated 2013-11-04
##
## Provides a command that allows players to see which of their team
## members are currently in a lancaster, and also if they have a
## summoner equipped.
##
import asss

chat = asss.get_interface(asss.I_CHAT)
items = asss.get_interface(asss.I_HSCORE_ITEMS)

def c_lancs(cmd, params, p, targ):
    """Module: <py> hs_lancs
Targets: none
Args: none
Tells you the names of the lancasters on your team. (S) indicates summoner."""
    f = p.freq
    a = p.arena
    lancs = []
   
    if not a.hs_lanc_item_summoner:
        a.hs_lanc_item_summoner = items.getItemByName('Summoner', a)
       
    if not a.hs_lanc_item_evoker:
        a.hs_lanc_item_evoker = items.getItemByName('Evoker', a)
   
    def print_lancs(lanc):
        if lanc.type != asss.T_CONT and lanc.type != asss.T_VIE:
            return
 
        if lanc.arena == a and lanc.freq == f and lanc.ship != asss.SHIP_SPEC:
            if a.hs_lanc_item_evoker is not None and items.getItemCount(lanc, a.hs_lanc_item_evoker, lanc.ship) > 0:
                msg = '(E) ' + lanc.name
                lancs.append(msg)
                return # don't list lancs with evoker twice
               
        if lanc.arena == a and lanc.freq == f and lanc.ship == asss.SHIP_LANCASTER:
            msg = None
            if a.hs_lanc_item_summoner is not None and items.getItemCount(lanc, a.hs_lanc_item_summoner, lanc.ship) > 0:
                msg = '(S) ' + lanc.name
            else:
                msg = lanc.name
            if msg:
                lancs.append(msg)
 
    asss.for_each_player(print_lancs)
 
    if len(lancs) > 0:
        chat.SendMessage(p, ', '.join(lancs))
    else:
        chat.SendMessage(p, 'There are no Lancasters on your team.')

def mm_attach(a):
    try:
        a.hs_lanc_cmd1 = asss.add_command('lancs', c_lancs, a)
        a.hs_lanc_item_summoner = items.getItemByName('Summoner', a)
        a.hs_lanc_item_evoker = items.getItemByName('Evoker', a)
    except:
        mm_detach(a)

def mm_detach(a):
    for attr in ['item_evoker', 'item_summoner', 'cmd1']:
        try:
            delattr(a, 'hs_lanc_' + attr)
        except:
            pass
