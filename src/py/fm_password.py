
# dist: public

# this module implements password-protected freqs.
# anyone on a private freq can set a password for it, which can be empty.
# anyone can also set their own "joining password".
# to join a freq with a non-empty password, your joining password must
# match the freq's password.

import asss

chat = asss.get_interface(asss.I_CHAT)
cfg = asss.get_interface(asss.I_CONFIG)


# utilities

def count_freq(a, f):
	count = [0]
	def each_player(p):
		if p.arena == a and p.freq == f:
			count[0] += 1
	asss.for_each_player(each_player)
	return count[0]


# freq manager

def initial(p, ship, freq):
	return p.arena.fpwd_oldfm.InitialFreq(p, ship, freq)

def shipch(p, ship, freq):
	return p.arena.fpwd_oldfm.ShipChange(p, ship, freq)

def freqch(p, ship, freq):
	if count_freq(p.arena, freq) == 0:
		# reset join password
		try: del p.arena.fpwd_fpwds[freq]
		except: pass

	freqpwd = p.arena.fpwd_fpwds.get(freq, '')
	joinpwd = getattr(p, 'fpwd_joinpwd', '')
	if freqpwd and freqpwd != joinpwd:
		chat.SendMessage(p,
				"Freq %d requires a password. "
				"Set your joining password with ?joinpwd "
				"and try joining it again." % freq)
		return p.ship, p.freq
	else:
		return p.arena.fpwd_oldfm.FreqChange(p, ship, freq)


# commands

def c_joinpwd(tc, params, p, target):
	"""\
Module: fm_password
Targets: none
Args: <password>
Sets your joining password, which will be used to check if you can join
password-protected freqs.
"""
	p.fpwd_joinpwd = params
	chat.SendMessage(p, "Join password set.")

def c_freqpwd(tc, params, p, target):
	"""\
Module: fm_password
Targets: none
Args: <password>
Sets a password for your freq. Public freqs and the spec freq cannot
have passwords.
"""
	privfreqstart = cfg.GetInt(p.arena.cfg, "Team", "PrivFreqStart", 100)
	if p.freq >= privfreqstart and p.freq != p.arena.specfreq:
		p.arena.fpwd_fpwds[p.freq] = params
		chat.SendMessage(p, "Freq password set.")
	else:
		chat.SendMessage(p, "You can't set a password on a public freq.")


# attaching

def mm_attach(a):
	# set up interface
	a.fpwd_oldfm = asss.get_interface(asss.I_FREQMAN, a)
	a.fpwd_myint = asss.reg_interface(asss.I_FREQMAN, (initial, shipch, freqch), a)

	# set up command
	a.fpwd_joinpwd_cmd = asss.add_command("joinpwd", c_joinpwd, a)
	a.fpwd_freqpwd_cmd = asss.add_command("freqpwd", c_freqpwd, a)

	# data
	a.fpwd_fpwds = {}

def mm_detach(a):
	for attr in ['myint', 'oldfm', 'joinpwd_cmd', 'freqpwd_cmd', 'fpwds']:
		try: delattr(a, 'fpwd_' + attr)
		except: pass

