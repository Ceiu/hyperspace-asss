##
## Pub Team Spec Remover
## D1st0rt, SSCE Hyperspace
## License: MIT/X11
## Last Updated 2008-12-20
##
import datetime
import asss

chat = asss.get_interface(asss.I_CHAT)
game = asss.get_interface(asss.I_GAME)
pd = asss.get_interface(asss.I_PLAYERDATA)

def shipfreqchange(p, ship, oldship, freq, oldfreq):
    a = p.arena
    if hasattr(a,'hs_specremove_times'):
        if p.type != asss.T_FAKE and ship == asss.SHIP_SPEC and freq < 100:
            if not a.hs_specremove_times.has_key(p.name):
                a.hs_specremove_times[p.name] = datetime.datetime.now()
        elif a.hs_specremove_times.has_key(p.name):
            del a.hs_specremove_times[p.name]

def tick(a):
    for name in a.hs_specremove_times.copy() :
        p = pd.FindPlayer(name)
        if p is None:
            del a.hs_specremove_times[name]
        elif p.ship == asss.SHIP_SPEC:
            delta = (datetime.datetime.now() - a.hs_specremove_times[name]).seconds
            if delta == 45:
                game.SetFreq(p, a.specfreq)
                chat.SendMessage(p, 'You have been moved to the spectator frequency for idling too long.')
            elif delta == 30:
                chat.SendMessage(p, 'Please enter the game if you want to be on this frequency. You have 15 seconds before you are automatically moved to the spectator frequency.')
        else:
            del a.hs_specremove_times[name]

def mm_attach(a):
    try:
        a.hs_specremove_times = {}
        a.hs_specremove_cb = asss.reg_callback(asss.CB_SHIPFREQCHANGE, shipfreqchange)
        a.hs_specremove_timer = asss.set_timer(lambda: tick(a), 100)
    except:
        mm_detach(a)

def mm_detach(a):
    for attr in ['times', 'cb', 'timer']:
        try:
            delattr(a, 'hs_specremove_' + attr)
        except:
            pass

