#!/usr/bin/env python

import asss

chat = asss.getInterface(asss.I_CHAT)
game = asss.getInterface(asss.I_GAME)

#############
# callbacks #
############

def kill(arena, killer, killed, bty, flags, pts, green):
    pts = scores.get(killer, 0)
    pts += bty    
    scores[killer] = pts
    
def shipchange(p, ship, freq):
    if ship == asss.SHIP_LANCASTER and p.name not in tw_caps:
        game.SetShip(p, asss.SHIP_SPIDER)

############
# commands #
############

def c_start(tc, params, p, target):
    """\
Module: hs_turretwar
Targets: none
Args: none
Starts a new TurretWar game
    """
    a.tw_scores.clear()        
    chat.SendArenaSoundMessage(p.arena, 104, "GOGOGOGOGOOOOOO!")
    
def c_addcap(tc, params, p, target):
    """\
Module: hs_turretwar
Targets: Player
Args: none
Starts a new TurretWar game
    """
    if type(target) == asss.PlayerType:
        if target.p.name not in a.tw_caps:
            a.tw_caps.add(target.p.name)
            game.SetShip(target.p, asss.SHIP_LANCASTER)
        else:
            a.tw_caps.remove(target.p.name)
            game.SetShip(target.p, asss.SHIP_SPIDER)

def c_score(tc, params, p, target):
    """\
Module: hs_turretwar
Targets: none
Args: none
Displays current TurretWar game score
    """
    freq = [0, 0]
    for s in a.tw_scores.keys():
        freq[s.freq] += a.tw_scores[s]

    chat.SendArenaMessage(p.arena, "Scores: Team 0="+ freq[0] +"Team 1="+ freq[1])

def c_end(tc, params, p, target):
    """\
Module: hs_turretwar    
Target: none
Args: none
Ends the current game of TurretWar
    """
    freq = [0, 0]
    mvp = p
    mvpscore = 0
    chat.SendArenaMessage(p.arena, "Game Over!")
    chat.SendArenaMessage(p.arena, "-------------------------------------")
    chat.SendArenaMessage(p.arena, "Player Scores:")
    for s in a.tw_scores.keys():
        score = a.tw_scores[s]
        if score > mvpscore:
            mvp = s
            mvpscore = score
        freq[s.freq] += score
        chat.SendArenaMessage(p.arena, s.name +"("+ s.freq +") - "+ score)
    chat.SendArenaMessage(p.arena, "-------------------------------------")
    chat.SendArenaMessage(p.arena, "Team Scores:")
    for x in range(2):
        chat.SendArenaMessage(p.arena, "Team "+ x +" - "+ freq[x])
    chat.SendArenaMessage(p.arena, "-------------------------------------")
    if freq[0] > freq[1]:
        chat.SendArenaMessage(p.arena, "Team 0 Wins!")
    elif freq[1] > freq[0]:
        chat.SendArenaMessage(p.arena, "Team 1 Wins!")
    else:
        chat.SendArenaMessage(p.arena, "OMG TIE!")
    chat.SendArenaMessage(p.arena, "MVP: "+ mvp.name)

#############
# attaching #
#############

def mm_attach(a):
    #commands
    a.tw_Cstart = asss.add_command("start", c_start, a)
    a.tw_Csetcap = asss.add_command("addcap", c_addcap, a)
    a.tw_Cscore = asss.add_command("score", c_score, a)
    a.tw_Cend = asss.add_command("end", c_end, a)
    
    #callbacks
    cb1 = reg_callback(CB_KILL, kill)
    cb2 = reg_callback(CB_SHIPCHANGE, shipchange)
    
    #data
    a.tw_caps = []
    a.tw_freq0 = []
    a.tw_freq1 = []
    a.tw_scores = {'key':'value'}
    
def mm_detach(a):
        for attr in ['Cstart', 'Csetcap', 'caps', 'scores', 'Cscore', 'Cend',
                     'freq0', 'freq1']:
            try: delattr(a, 'tw_' + attr)
            except: pass
                