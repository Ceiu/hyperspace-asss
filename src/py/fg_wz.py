
# dist: public

import math
import asss

MAXFLAGS = 255

flagcore = asss.get_interface(asss.I_FLAGCORE)
mapdata = asss.get_interface(asss.I_MAPDATA)
cfg = asss.get_interface(asss.I_CONFIG)
lm = asss.get_interface(asss.I_LOGMAN)
prng = asss.get_interface(asss.I_PRNG)


# utils

def clip(v, bottom, top):
	if v < bottom:
		return bottom
	elif v > top:
		return top
	else:
		return v

def wrap(v, bottom = 0, top = 1023):
	while 1:
		if v < bottom:
			v = 2 * bottom - v
		elif v > top:
			v = 2 * top - v
		else:
			return v

def circular_random(cx, cy, radius):
	r = prng.Uniform() * float(radius)
	cx = float(cx) + 0.5
	cy = float(cy) + 0.5
	theta = prng.Uniform() * 2 * math.pi
	return wrap(int(cx + r * math.cos(theta))), \
	       wrap(int(cy + r * math.sin(theta)))


# flag game

class WZSets:
	def __init__(me, a):
		me.load(a)

	def load(me, a):
		c = a.cfg

		# cfghelp: Flag:ResetDelay, arena, int, def: 0
		# The length of the delay between flag games.
		me.resetdelay = cfg.GetInt(c, "Flag", "ResetDelay", 0)
		# cfghelp: Flag:SpawnX, arena, int, def: 512
		# The X coordinate that new flags spawn at (in tiles).
		me.spawnx = cfg.GetInt(c, "Flag", "SpawnX", 512)
		# cfghelp: Flag:SpawnY, arena, int, def: 512
		# The Y coordinate that new flags spawn at (in tiles).
		me.spawny = cfg.GetInt(c, "Flag", "SpawnY", 512)
		# cfghelp: Flag:SpawnRadius, arena, int, def: 50
		# How far from the spawn center that new flags spawn (in
		# tiles).
		me.spawnr = cfg.GetInt(c, "Flag", "SpawnRadius", 50)
		# cfghelp: Flag:DropRadius, arena, int, def: 2
		# How far from a player do dropped flags appear (in tiles).
		me.dropr = cfg.GetInt(c, "Flag", "DropRadius", 2)
		# cfghelp: Flag:FriendlyTransfer , arena, bool, def: 1
		# Whether you get a teammates flags when you kill him.
		me.friendlytransfer = cfg.GetInt(c, "Flag", "FriendlyTransfer", 1)
		# cfghelp for this one is in clientset.def
		me.carryflags = cfg.GetInt(c, "Flag", "CarryFlags", asss.CARRY_ALL)

		# cfghelp: Flag:DropOwned, arena, bool, def: 1
		# Whether flags you drop are owned by your team.
		me.dropowned = cfg.GetInt(c, "Flag", "DropOwned", 1)
		# cfghelp: Flag:DropCenter, arena, bool, def: 0
		# Whether flags dropped normally go in the center of the map, as
		# opposed to near the player.
		me.dropcenter = cfg.GetInt(c, "Flag", "DropCenter", 0)

		# cfghelp: Flag:NeutOwned, arena, bool, def: 0
		# Whether flags you neut-drop are owned by your team.
		me.neutowned = cfg.GetInt(c, "Flag", "NeutOwned", 0)
		# cfghelp: Flag:NeutCenter, arena, bool, def: 0
		# Whether flags that are neut-droped go in the center, as
		# opposed to near the player who dropped them.
		me.neutcenter = cfg.GetInt(c, "Flag", "NeutCenter", 0)

		# cfghelp: Flag:TKOwned, arena, bool, def: 1
		# Whether flags dropped by a team-kill are owned by your team,
		# as opposed to neutral.
		me.tkowned = cfg.GetInt(c, "Flag", "TKOwned", 1)
		# cfghelp: Flag:TKCenter, arena, bool, def: 0
		# Whether flags dropped by a team-kill spawn in the center, as
		# opposed to near the killed player.
		me.tkcenter = cfg.GetInt(c, "Flag", "TKCenter", 0)

		# cfghelp: Flag:SafeOwned, arena, bool, def: 1
		# Whether flags dropped from a safe zone are owned by your
		# team, as opposed to neutral.
		me.safeowned = cfg.GetInt(c, "Flag", "SafeOwned", 1)
		# cfghelp: Flag:SafeCenter, arena, bool, def: 0
		# Whether flags dropped from a safe zone spawn in the center,
		# as opposed to near the safe zone player.
		me.safecenter = cfg.GetInt(c, "Flag", "SafeCenter", 0)

		# cfghelp: Flag:WinDelay, arena, int, def: 200
		# The delay between dropping the last flag and winning (ticks).
		me.windelay = cfg.GetInt(c, "Flag", "WinDelay", 200)

		# cfghelp: Flag:FlagCount, arena, rng, range: 0-256, def: 0
		# How many flags are present in this arena.
		count = cfg.GetStr(c, "Flag", "FlagCount")
		try:
			me.min, me.max = map(int, count.split('-'))
		except:
			try:
				me.min = me.max = int(count)
			except:
				me.min = me.max = 0

		me.max = clip(me.max, 0, MAXFLAGS)
		me.min = clip(me.min, 0, me.max)


# useful flag things

def neut_flag(a, i, x, y, r, freq):
	# record it as a neut so we can behave differently when spawning it
	a.fg_wz_neuts.append(i)
	# move it to none, but record x, y, freq
	f = asss.flaginfo()
	f.state = asss.FI_NONE
	f.x, f.y = x, y
	f.freq = freq
	f.carrier = None
	flagcore.SetFlags(a, i, f)


def spawn_flag(a, i, cx, cy, r, freq, fallback = 1):
	good = 0
	tries = 0
	while not good and tries < 30:
		# assume this will work
		good = 1
		# pick a random point
		x, y = circular_random(cx, cy, r + tries)
		# and move off of any tiles
		x, y = mapdata.FindEmptyTileNear(a, x, y)
		# check if it's hitting another flag:
		for j in xrange(a.fg_wz_current):
			if i != j:
				n2, f2 = flagcore.GetFlags(a, j)
				if n2 == 1 and f2.state == asss.FI_ONMAP and \
				   f2.x == x and f2.y == y:
					good = 0
		# and check for no-flag regions:
		if good:
			foundnoflags = []
			def check_region(r):
				if mapdata.RegionChunk(r, asss.RCT_NOFLAGS) is not None:
					foundnoflags.append(1)
			mapdata.EnumContaining(a, x, y, check_region)
			if foundnoflags:
				good = 0
		# if we did hit another flag, bump up the radius so we don't get
		# stuck here.
		tries += 1

	if good:
		f = asss.flaginfo()
		f.state = asss.FI_ONMAP
		f.x, f.y = x, y
		f.freq = freq
		f.carrier = None
		flagcore.SetFlags(a, i, f)
	elif fallback:
		# we have one fallback option: centering it
		lm.LogA(asss.L_WARN, 'flagcore', a,
				("failed to place a flag at (%d,%d)-%d, "
				 "falling back to center") % (cx, cy, r))
		sets = a.fg_wz_sets
		spawn_flag(a, i, sets.spawnx, sets.spawny, sets.spawnr, freq, 0)
	else:
		# couldn't place in original location, and couldn't place in
		# center either. this is really bad.
		lm.LogA(asss.L_ERROR, 'flagcore', a,
				("failed to place a flag at (%d,%d)-%d, "
				 "fallback disabled")% (cx, cy, r))


# the spawn timer

def spawn_timer(a):
	current = a.fg_wz_current
	sets = a.fg_wz_sets
	neuts = a.fg_wz_neuts

	# pick a new flag count?
	if current < sets.min:
		a.fg_wz_current = current = prng.Number(sets.min, sets.max)

	# spawn flags?
	for i in xrange(current):
		n, f = flagcore.GetFlags(a, i)
		if n != 1: continue
		if f.state == asss.FI_NONE:
			# handle neuted flags specially. their x, y, freq will have
			# been set correctly when they were neuted (by cleanup).
			if i in neuts:
				neuts.remove(i)
				x, y = f.x, f.y
				if sets.neutcenter:
					r = sets.spawnr
				else:
					r = sets.dropr
				freq = f.freq
			else:
				x = sets.spawnx
				y = sets.spawny
				r = sets.spawnr
				freq = -1

			spawn_flag(a, i, x, y, r, freq)


# winning

def check_win(a):
	current = a.fg_wz_current
	ownedflags = 0
	winfreq = -1

	for i in xrange(current):
		n, f = flagcore.GetFlags(a, i)
		assert n == 1
		if f.state == asss.FI_ONMAP:
			if f.freq == winfreq:
				ownedflags += 1
			else:
				winfreq = f.freq
				ownedflags = 1

	# just to check
	for i in xrange(current, a.fg_wz_sets.max):
		n, f = flagcore.GetFlags(a, current)
		assert n == 0 or f.state == asss.FI_NONE

	if ownedflags == current and winfreq != -1:
		# we have a winner!
		points = asss.call_callback(asss.CB_WARZONEWIN, (a, winfreq, 0), a)
		# hardcoded default
		if points == 0: points = 1000
		flagcore.FlagReset(a, winfreq, points)
		a.fg_wz_current = 0


def schedule_win_check(a):
	def check_win_tmr():
		check_win(a)
		del a.fg_wz_checkwintmr
	# note that this releases the old reference, if one existed, which
	# will cancel any previously-set timer, so there is guaranteed to be
	# no win checks before windelay ticks after calling this function.
	a.fg_wz_checkwintmr = asss.set_timer(check_win_tmr, a.fg_wz_sets.windelay)


def flagonmap(a, fid, x, y, freq):
	# any time a flag lands on the map, we should check for a win
	schedule_win_check(a)


# the flaggame interface

class WZFlagGame:
	iid = asss.I_FLAGGAME

	def Init(me, a):
		# get settings
		a.fg_wz_sets = sets = WZSets(a)

		if sets.carryflags < asss.CARRY_ALL:
			lm.LogA(asss.L_ERROR, 'fg_wz', a, 'invalid Flag:CarryFlags for warzone-style game')
			del a.fg_wz_sets
			return

		# set up flag game
		flagcore.SetCarryMode(a, sets.carryflags)
		flagcore.ReserveFlags(a, sets.max)

		# set up more stuff
		a.fg_wz_neuts = []
		a.fg_wz_current = 0

		a.fg_wz_spawntmr = asss.set_timer(lambda: spawn_timer(a), 500)


	def FlagTouch(me, a, p, fid):
		sets = a.fg_wz_sets
		if sets.carryflags == asss.CARRY_ALL:
			cancarry = MAXFLAGS
		else:
			cancarry = sets.carryflags - 1

		if p.flagscarried >= cancarry:
			return

		# assign him the flag
		f = asss.flaginfo()
		f.state = asss.FI_CARRIED
		f.carrier = p
		flagcore.SetFlags(a, fid, f)


	def Cleanup(me, a, fid, reason, carrier, freq):
		sets = a.fg_wz_sets

		def spawn(owned, center, func=spawn_flag):
			if owned:
				myfreq = freq
			else:
				myfreq = -1
			if center:
				x = sets.spawnx
				y = sets.spawny
				r = sets.spawnr
			else:
				x = carrier.position[0] >> 4
				y = carrier.position[1] >> 4
				r = sets.dropr
			func(a, fid, x, y, r, myfreq)

		if reason == asss.CLEANUP_DROPPED or \
		   reason == asss.CLEANUP_KILL_CANTCARRY or \
		   reason == asss.CLEANUP_KILL_FAKE:
			# this acts like a normal drop
			spawn(sets.dropowned, sets.dropcenter)

		elif reason == asss.CLEANUP_INSAFE:
			# similar, but use safe zone settings
			spawn(sets.safeowned, sets.safecenter)

		elif reason == asss.CLEANUP_KILL_TK:
			# only spawn if we're not transferring
			if not sets.friendlytransfer:
				spawn(sets.tkowned, sets.tkcenter)

		elif reason == asss.CLEANUP_SHIPCHANGE or \
			 reason == asss.CLEANUP_FREQCHANGE or \
			 reason == asss.CLEANUP_LEFTARENA or \
			 reason == asss.CLEANUP_OTHER:
			# neuts
			spawn(sets.neutowned, sets.neutcenter, neut_flag)


# attaching/detaching

def mm_attach(a):
	try:
		a.fg_wz_intref = asss.reg_interface(WZFlagGame(), a)
		a.fg_wz_cbref1 = asss.reg_callback(asss.CB_FLAGONMAP, flagonmap, a)
	except:
		mm_detach(a)

def mm_detach(a):
	for attr in ['intref', 'cbref1', 'current', 'sets', 'neuts', 'checkwintmr', 'spawntmr']:
		try: delattr(a, 'fg_wz_' + attr)
		except: pass

