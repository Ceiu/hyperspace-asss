# dist: public

import sys, cStringIO, asss

def try_interface(name):
	try:
		return asss.get_interface(name)
	except:
		return None

def arenaToPlayerList(arena):
	def each(player):
		if player.arena == arena:
			playerList.append(player)

	playerList = asss.PlayerListType()
	asss.for_each_player(each)
	return playerList

env = {}
# `CB_SOMETHING` and `asss.CB_SOMETHING` are both correct
env.update(asss.__dict__)
env['asss'] = asss
env['arenaman'] = try_interface(asss.I_ARENAMAN)
env['balls'] = try_interface(asss.I_BALLS)
env['bricks'] = try_interface(asss.I_BRICKS)
env['brickwriter'] = try_interface(asss.I_BRICKWRITER)
env['capman'] = try_interface(asss.I_CAPMAN)
env['chat'] = try_interface(asss.I_CHAT)
env['clientset'] = try_interface(asss.I_CLIENTSET)
env['config'] = try_interface(asss.I_CONFIG)
env['fake'] = try_interface(asss.I_FAKE)
env['filetrans'] = try_interface(asss.I_FILETRANS)
env['flagcore'] = try_interface(asss.I_FLAGCORE)
env['game'] = try_interface(asss.I_GAME)
env['jackpot'] = try_interface(asss.I_JACKPOT)
env['logman'] = try_interface(asss.I_LOGMAN)
env['mapdata'] = try_interface(asss.I_MAPDATA)
env['objects'] = try_interface(asss.I_OBJECTS)
env['playerdata'] = try_interface(asss.I_PLAYERDATA)
env['redirect'] = try_interface(asss.I_REDIRECT)
env['stats'] = try_interface(asss.I_STATS)
env['arenaToPlayerList'] = arenaToPlayerList

chat = asss.get_interface(asss.I_CHAT)
orig_sys_stdout = sys.stdout

def c_py(cmd, params, player, targ):
	"""\
Module: <py> exec
Targets: any
Args: <python code>
Executes arbitrary python code. The code runs in a namespace containing
all of the asss module, plus three helpful preset variables: {me} is
yourself, {t} is the target of the command, and {a} is the current
arena. You can write multi-line statements by separating lines with
semicolons (be sure to get the indentation correct). Output written to
stdout (e.g., with print) is captured and displayed to you, as are any
exceptions raised in your code.
"""
	params = params.replace(';', '\n')
	output = sys.stdout = cStringIO.StringIO()
	env['me'] = player
	env['t'] = targ
	env['a'] = player.arena

	try:
		exec params in env
	except:
		info = sys.exc_info()
		output.write('%s: %s\n' % \
				(getattr(info[0], '__name__', info[0]), info[1]))
		del info

	if hasattr(sys, 'exc_clear'): sys.exc_clear()
	sys.stdout = orig_sys_stdout
	del env['me']
	del env['t']
	del env['a']

	for l in output.getvalue().splitlines():
		chat.SendMessage(player, l)

ref = asss.add_command('py', c_py)
