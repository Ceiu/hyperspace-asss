##
## Automated Team Picker
## D1st0rt, SSCE Hyperspace
## License: MIT/X11
## Last Updated 2008-08-03
##
import asss

chat = asss.get_interface(asss.I_CHAT)
game = asss.get_interface(asss.I_GAME)
pd = asss.get_interface(asss.I_PLAYERDATA)

def shipchange(p, ship, freq):
	if p.name in p.arena.hs_picker_picks: #they have been picked
		pfreq = p.arena.hs_picker_picks[p.name]
		if freq != pfreq:
			chat.SendMessage(p, 'Sorry, you have been picked to play on frequency %d' % pfreq)
			game.SetFreq(p, pfreq)

def clear(a):
	a.hs_picker_capts = []
	a.hs_picker_picks = {}
	a.hs_picker_turn = None

def c_capt(cmd, params, p, target):
	"""Module: hs_picker
Targets: player or arena
Args: <frequency> (only if sent to player)
If sent to arena, displays current captains. If sent to player,
assigns them as captain of the specified frequency
"""
	a = p.arena
	if type(target) == asss.PlayerType:   #assign captain
		if params.isdigit():
			pfreq = int(params)
			if pfreq >= 0 and pfreq <= 9999:
				for name in a.hs_picker_capts:
					if a.hs_picker_picks[name] == pfreq:
						a.hs_picker_capts.remove(name)
						break
				if target.name not in a.hs_picker_capts:
					a.hs_picker_capts.append(target.name)
				a.hs_picker_picks[target.name] = pfreq
				chat.SendArenaMessage(a, '%s assigned %s as captain of frequency %d' % (p.name, target.name, pfreq))
			else:
				chat.SendMessage(p, 'Invalid frequency')
		else:
			chat.SendMessage(p, 'Invalid frequency')
	elif type(target) == asss.ArenaType: #show captains
		str = 'Captains: '
		for name in a.hs_picker_capts:
			pfreq = a.hs_picker_picks[name]
			str = str + '[%d] %s, ' % (pfreq, name)
		str = str.strip(', ')
		chat.SendMessage(p, str)

def c_pick(cmd, params, p, target):
	"""Module: hs_picker
Targets: player or arena
Args: <captain freq or none> (only if sent to arena)
For captains, picks a player to be on your team if available.
For hosts, sets the pick rotation to the specified frequency
from ?capt. If blank, disables picking.
"""
	a = p.arena
	if type(target) == asss.PlayerType and a.hs_picker_turn is not None:
		if p.name == a.hs_picker_capts[a.hs_picker_turn]:
			if target.name not in a.hs_picker_picks:
				pfreq = a.hs_picker_picks[p.name]
				a.hs_picker_picks[target.name] = pfreq
				game.SetFreq(target, pfreq)
				chat.SendArenaMessage(a, '%s picked %s to play on frequency %d.' % (p.name, target.name, pfreq))

				a.hs_picker_turn += 1
				if a.hs_picker_turn >= len(a.hs_picker_capts):
					a.hs_picker_turn = 0
				name = a.hs_picker_capts[a.hs_picker_turn]
				capt = pd.FindPlayer(name)
				if capt is not None and capt.arena == a:
					chat.SendMessage(capt, 'It is your turn to /?pick a player')
				else:
					chat.SendModMessage('Player %s is not present for their turn to pick in arena %s' % (name, a.name))

			else:
				chat.SendMessage(p, 'Sorry, that player has already been picked')
	elif type(target) == asss.ArenaType and a.hs_picker_turn is None: #start picking
		if params.isdigit() and int(params) in a.hs_picker_picks.values():
			pfreq = int(params)
			found = False
			for name in a.hs_picker_capts:
				if pfreq == a.hs_picker_picks[name]:
					found = True
					break
			a.hs_picker_turn = a.hs_picker_capts.index(name)
			if found:
				capt = pd.FindPlayer(name)
				if capt is not None and capt.arena == a:
					chat.SendMessage(capt, 'It is your turn to /?pick a player')
					chat.SendMessage(p, 'It is %s\'s turn to pick' % capt.name)
				else:
					chat.SendModMessage('Player %s is not present for their turn to pick in arena %s' % (name, a.name))
			else:
				chat.SendMessage(p, 'Team %d is set to pick, but there is no captain' % (pfreq))
	elif type(target) == asss.ArenaType and params == '':
			a.hs_picker_turn = None
			chat.SendMessage(p, 'Picking disabled')

def c_resetpick(cmd, params, p, target):
	"""Module: hs_picker
Targets: player or arena
Args: none
If sent to arena, clears all captains and picks (ie. starting a new game)
If sent to player, frees them from having to stay on their picked team.
"""
	if type(target) == asss.ArenaType: #reset arena
		clear(p.arena)
		chat.SendMessage(p, 'Picked teams reset')
	elif type(target) == asss.PlayerType and target.name in p.arena.hs_picker_picks: #reset player
		p.arena.hs_picker_picks.remove(target.name)
		chat.SendMessage(p, '%s reset' % target.name)

def mm_attach(a):
	try:
		clear(a)
		a.hs_picker_cmd1 = asss.add_command("capt", c_capt, a)
		a.hs_picker_cmd2 = asss.add_command("pick", c_pick, a)
		a.hs_picker_cmd3 = asss.add_command("resetpick", c_resetpick, a)
	except:
		mm_detach(a)

def mm_detach(a):
	for attr in ['capts', 'picks', 'turn', 'cmd1', 'cmd2', 'cmd3']:
		try:
			delattr(a, 'hs_picker_' + attr)
		except:
			pass

