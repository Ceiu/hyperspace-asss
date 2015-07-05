
# asss_bprot.py
# asss biller server protocol
# dist: public

import sys
from socket import inet_aton

import vie_bprot, util

log = util.log

# config stuff
maxlogins = 10

# globals
sock = None

serverid = 5000
groupid = 1
scoreid = 5000

# maps pids to Players
pidmap = {}
# whether we got a connect command from the server
gotconnect = None
# how many outstanding login requests we have
curlogins = 0


class Player:
	def __init__(me, name):
		me.name = name
		me.connected = util.ticks()


def set_sock(s):
	global sock
	sock = s


def handle_s2b_setids(line):
	global serverid, groupid, scoreid
	serverid, groupid, scoreid = line.split(':')
	serverid = int(serverid)
	groupid = int(groupid)
	scoreid = int(scoreid)


def handle_s2b_connect(line):
	global serverid, groupid, scoreid, gotconnect

	version, swname, zonename, network, password = line.split(':', 4)

	version = int(version)
	if version != 1:
		log("local server specified wrong protocol version!")
		vie_bprot.disconnect()
		util.exit(1)

	if network:
		sendname = network + ' ' + zonename
	else:
		sendname = zonename

	log("local zone %s running on %s" % (sendname, swname))

	args = [serverid, groupid, scoreid, sendname, password]
	def do_send_stuff(args=args):
		log("logging in to remote biller")
		vie_bprot.send_s2b_login(*args)
		send_b2s_connectok('bproxy %s' % util.version)

	# this is a little messy. we check to see if we've contacted the vie
	# server yet. if so, just send the connectok. if not, wait until we
	# do.
	if vie_bprot.stage == vie_bprot.s_connected:
		do_send_stuff()
	else:
		gotconnect = do_send_stuff


def handle_s2b_plogin(line):
	global curlogins

	pid, flag, name, pw, ip, macid, contid = line.split(':')

	pid = int(pid)
	flag = int(flag)
	macid = long(macid)

	if curlogins >= maxlogins:
		send_b2s_pbad(pid, 'The server is currently busy processing other login requests, please try again in a few moments.')
		return

	curlogins = curlogins + 1

	if contid:
		contid = util.hex_to_bin(contid)

	log("player login: " + name)

	vie_bprot.send_s2b_playerlogin(flag, inet_aton(ip), name, pw, pid,
		macid, 0, contid)


def handle_s2b_bnr(line):
	# FIXME: banners
	pass


def handle_s2b_pleave(line):
	pid = line
	pid = int(pid)
	if pidmap.has_key(pid):
		vie_bprot.send_s2b_playerleave(pid)
		del pidmap[pid]
	else:
		log("leaving pid %d not in pidmap" % pid)


def handle_s2b_chat(line):
	pid, channel, sound, text = line.split(':', 3)
	pid = int(pid)
	sound = int(sound)

	vie_bprot.send_s2b_chat(pid, channel, text)


def handle_s2b_rmt(line):
	pid, dest, sound, text = line.split(':', 3)
	pid = int(pid)
	sound = int(sound)

	# grab the sender's name from our local map. this is pretty hacky.
	if pidmap.has_key(pid):
		sendername = pidmap[pid].name
		vie_bprot.send_s2b_remotepriv(pid, ':%s:(%s)>%s' %
			(dest, sendername, text))
	else:
		log("pid %d not found in pidmap for remote priv message" % pid)


def handle_s2b_rmtsqd(line):
	pid, destsqd, sound, text = line.split(':', 3)
	pid = int(pid)
	sound = int(sound)

	# grab the sender's name from our local map. this is pretty hacky.
	if pidmap.has_key(pid):
		sendername = pidmap[pid].name
		vie_bprot.send_s2b_remotepriv(pid, ':#%s:(%s)>%s' %
			(destsqd, sendername, text))
	else:
		log("pid %d not found in pidmap for remote squad message" % pid)


def handle_s2b_cmd(line):
	pid, cmdname, args = line.split(':', 2)
	pid = int(pid)
	vie_bprot.send_s2b_command(pid, '?%s %s' % (cmdname, args))


def handle_s2b_log(line):
	pid, text = line.splt(':', 1)
	pid = int(pid)
	vie_bprot.send_s2b_warning(pid, text)


s2b_dispatch = \
{
	'SETIDS': handle_s2b_setids,
	'CONNECT': handle_s2b_connect,
	'PLOGIN': handle_s2b_plogin,
	'BNR': handle_s2b_bnr,
	'PLEAVE': handle_s2b_pleave,
	'CHAT': handle_s2b_chat,
	'RMT': handle_s2b_rmt,
	'RMTSQD': handle_s2b_rmtsqd,
	'CMD': handle_s2b_cmd,
	'LOG': handle_s2b_log
}


def process_incoming(line):
	type, rest = line.split(':', 1)
	if s2b_dispatch.has_key(type):
		try:
			s2b_dispatch[type](rest)
		except:
			import traceback
			log("error while processing message from local server")
			traceback.print_exc()
	else:
		log("bad message type from game server: '%s'" % type)


def send_line(line):
	sock.sendall(line + '\n')


def send_connected():
	global gotconnect
	if gotconnect:
		gotconnect()
		gotconnect = None


inbuf = ''
def try_read():
	global inbuf

	try:
		r = sock.recv(1024)
	except:
		# probably ewouldblock
		return

	if r:
		inbuf = inbuf + r
	else:
		log("lost connection to local game server")
		vie_bprot.disconnect()
		util.exit(1)

	lines = inbuf.splitlines(1)
	inbuf = ''
	for l in lines:
		if l.endswith('\n') or l.endswith('\r'):
			process_incoming(l.strip())
		else:
			inbuf = l


# sending

def send_b2s_connectok(billername):
	send_line('CONNECTOK:%s' % billername)

def send_b2s_connectbad(billername, reason):
	send_line('CONNECTBAD:%s:%s' % (billername, reason))

def send_b2s_pok(pid, rtext, name, squad, billerid, usage, firstused):
	global pidmap, curlogins
	curlogins = curlogins - 1
	pid = int(pid)
	pidmap[pid] = Player(name)
	send_line('POK:%s:%s:%s:%s:%s:%s:%s' %
		(pid, rtext, name, squad, billerid, usage, firstused))

def send_b2s_pbad(pid, rtext):
	global curlogins
	curlogins = curlogins - 1
	send_line('PBAD:%s:%s' % (pid, rtext))

def send_b2s_bnr(pid, banner):
	send_line('BNR:%s:%s' % (pid, banner))

def send_b2s_chattxt(channel, sender, sound, text):
	send_line('CHATTXT:%s:%s:%s:%s' % (channel, sender, sound, text))

def send_b2s_chat(pid, number):
	send_line('CHAT:%s:%s' % (pid, number))

def send_b2s_rmt(pid, sender, sound, text):
	send_line('RMT:%s:%s:%s:%s' % (pid, sender, sound, text))

def send_b2s_rmtsqd(destsquad, sender, sound, text):
	send_line('RMTSQD:%s:%s:%s:%s' % (destsquad, sender, sound, text))

def send_b2s_msg(pid, sound, text):
	send_line('MSG:%s:%s:%s' % (pid, sound, text))

def send_b2s_staffmsg(sender, sound, text):
	send_line('STAFFMSG:%s:%s:%s' % (sender, sound, text))

def send_b2s_broadcast(sender, sound, text):
	send_line('BROADCAST:%s:%s:%s' % (sender, sound, text))

