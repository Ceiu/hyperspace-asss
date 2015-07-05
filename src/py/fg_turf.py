
# dist: public

import asss

flagcore = asss.get_interface(asss.I_FLAGCORE)
mapdata = asss.get_interface(asss.I_MAPDATA)
cfg = asss.get_interface(asss.I_CONFIG)

KEY_TURF_OWNERS = 20


# flag game

class TurfFlagGame:
	iid = asss.I_FLAGGAME

	def Init(me, a):
		# get settings
		a.fg_turf_fc = fc = mapdata.GetFlagCount(a)
		a.fg_turf_persist = cfg.GetInt(a.cfg, "Flag", "PersistentTurfOwners", 1)

		# set up turf game
		flagcore.SetCarryMode(a, asss.CARRY_NONE)
		flagcore.ReserveFlags(a, fc)

		# set up initial flags
		f = asss.flaginfo()
		f.state = asss.FI_ONMAP
		f.freq = -1
		for i in xrange(fc):
			flagcore.SetFlags(a, i, f)

	def FlagTouch(me, a, p, fid):
		n, f = flagcore.GetFlags(a, fid)
		assert n == 1
		oldfreq = f.freq
		f.state = asss.FI_ONMAP
		newfreq = f.freq = p.freq
		flagcore.SetFlags(a, fid, f)
		asss.call_callback(asss.CB_TURFTAG, (a, p, fid, oldfreq, newfreq), a)

	def Cleanup(me, a, fid, reason, carrier, freq):
		pass


# persistent stuff

class OwnershipAPD:
	key = KEY_TURF_OWNERS
	interval = asss.INTERVAL_FOREVER_NONSHARED
	scope = asss.PERSIST_ALLARENAS

	def get(me, a):
		if getattr(a, 'fg_turf_persist', 0):
			owners = []
			for i in xrange(a.fg_turf_fc):
				n, f = flagcore.GetFlags(a, i)
				assert n == 1
				owners.append(f.freq)
			return owners
		else:
			return None

	def set(me, a, owners):
		# only set owners from persistent data if the setting says so, and
		# if the setting agrees:
		if getattr(a, 'fg_turf_persist', 0) and len(owners) == a.fg_turf_fc:
			f = asss.flaginfo()
			f.state = asss.FI_ONMAP
			for i in xrange(a.fg_turf_fc):
				f.freq = owners[i]
				flagcore.SetFlags(a, i, f)

	def clear(me, a):
		pass # the init action does this already

my_apd_ref = asss.reg_arena_persistent(OwnershipAPD())


# attaching/detaching

def mm_attach(a):
	a.fg_turf_ref = asss.reg_interface(TurfFlagGame(), a)

def mm_detach(a):
	for attr in ['ref', 'fc', 'persist']:
		try: delattr(a, 'fg_turf_' + attr)
		except: pass

