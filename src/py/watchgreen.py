
# dist: public

import asss

chat = asss.get_interface(asss.I_CHAT)

greenmap = {
	1: "Recharge",
	2: "Energy",
	3: "Rotation",
	4: "Stealth",
	5: "Cloak",
	6: "XRadar",
	7: "Warp",
	8: "Gun",
	9: "Bomb",
	10: "Bounce",
	11: "Thrust",
	12: "Speed",
	13: "FullCharge",
	14: "Shutdown",
	15: "MultiFire",
	16: "Prox",
	17: "Super",
	18: "Shield",
	19: "Shrap",
	20: "AntiWarp",
	21: "Repel",
	22: "Burst",
	23: "Decoy",
	24: "Thor",
	25: "MultiPrize",
	26: "Brick",
	27: "Rocket",
	28: "Portal",
}

watchmap = {}


def clear_watches(p):
	for watchee, watchers in watchmap.iteritems():
		if p in watchers:
			watchers.remove(p)


def c_watchgreen(cmd, params, p, targ):
	"""\
Module: <py> watchgreen
Targets: player or arena
Args: none
If send to a player, turns on green watching for that player. If sent as
a public message, turns off all your green watching."""
	if type(targ) == asss.ArenaType:
		clear_watches(p)
	elif type(targ) == asss.PlayerType and \
	     asss.is_standard(targ):
		watchers = watchmap.setdefault(targ, [])
		if p not in watchers:
			watchers.append(p)
	else:
		chat.SendMessage(p, "Bad target")

cmd1 = asss.add_command('watchgreen', c_watchgreen)


def my_green(p, x, y, type):
	watchers = watchmap.get(p)
	if watchers:
		msg = '%s picked up %s' % (p.name, greenmap.get(type, 'Unknown'))
		for watcher in watchers:
			chat.SendMessage(watcher, msg)

cb1 = asss.reg_callback(asss.CB_GREEN, my_green)


def my_paction(p, action, arena):
	if action == asss.PA_LEAVEARENA:
		try: del watchmap[p]
		except: pass
		clear_watches(p)

cb2 = asss.reg_callback(asss.CB_PLAYERACTION, my_paction)

