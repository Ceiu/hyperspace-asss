
import os, time, struct, random, math

import prot
import timer
import util
log = util.log
from pkt_types import *


def make_ppk(rot, x, y, xspeed, yspeed, status, bty, nrg):
	tm = int(util.ticks() & 0x7fffffff)
	if random.random() < 0.05:
		wpn = 1
	else:
		wpn = 0
	ppk1 = struct.pack('< B b I h h',
		C2S_POSITION, rot, tm, xspeed, y)
	ppk2 = struct.pack('< b h h H h h',
		status, x, yspeed, bty, nrg, wpn)
	epd = ''
	cksum = 0
	for c in ppk1:
		cksum ^= ord(c)
	for c in ppk2:
		cksum ^= ord(c)
	return ppk1 + chr(cksum) + ppk2 + epd


class Pilot:
	MAX = 1024 * 16
	VFACTOR = 10 # ss speeds are pixels / 10 seconds

	def __init__(me):
		me.x = Pilot.MAX/2
		me.y = Pilot.MAX/2

	def getxy(me):
		return me.x, me.y

	def getdxdy(me):
		return 0, 0

	def getrot(me):
		return 0

	def getbty(me):
		return random.choice([20, 40, 60, 100, 200, 280, 500])

	def update(me):
		pass


class RandomWalk(Pilot):
	# simple random walk
	def __init__(me, jump = 30):
		Pilot.__init__(me)
		me.jump2 = 2 * jump

	def update(me):
		me.x += me.jump2 * (random.random() - 0.5)
		me.y += me.jump2 * (random.random() - 0.5)
		if me.x < 0 or me.x >= Pilot.MAX:
			me.x = Pilot.MAX/2
		if me.y < 0 or me.y >= Pilot.MAX:
			me.y = Pilot.MAX/2


class Circler(Pilot):
	def __init__(me):
		Pilot.__init__(me)
		me.newparams()

	def newparams(me):
		me.x = Pilot.MAX/2 + (Pilot.MAX/2 * (random.random() - 0.5))
		me.y = Pilot.MAX/2 + (Pilot.MAX/2 * (random.random() - 0.5))
		me.r = max(100.0, Pilot.MAX/4 * random.random() - 150)
		me.dtheta = (100.0 * (random.random() + 2.0)) / me.r
		me.theta = 0.0
		me.timebase = time.time()
		me.expire = 15.0 * (random.random() + 2.0)

	def update(me):
		dt = time.time() - me.timebase
		me.theta = me.dtheta * dt
		if me.expire <= dt:
			me.newparams()

	def getxy(me):
		return int(me.x + me.r * math.cos(me.theta)), \
		       int(me.y + me.r * math.sin(me.theta))

	def getdxdy(me):
		return -int(me.dtheta * me.r * math.sin(me.theta) * Pilot.VFACTOR), \
		       int(me.dtheta * me.r * math.cos(me.theta) * Pilot.VFACTOR)

	def getrot(me):
		return int((me.theta + math.pi) / math.pi * 20) % 40


class Client(prot.Connection):
	def __init__(me, name = None, pwd = '', defarena = 0, ploss=0):
		prot.Connection.__init__(me)
		me.packetloss = ploss

		me.add_handler(0x0200, me.handle_connected)
		me.add_handler(S2C_WHOAMI, me.handle_whoami)
		me.add_handler(S2C_ENTERINGARENA, me.handle_inarena)
		me.add_handler(S2C_LOGINRESPONSE, me.handle_loginresponse)
		me.add_handler(S2C_WEAPON, me.handle_weapon)
		me.add_handler(S2C_POSITION, me.handle_position)
		me.add_handler(S2C_CHAT, me.handle_chat)

		if name:
			me.name = name
		else:
			me.name = 'loadgen-%06d' % random.randint(0, 999999)
		me.pwd = pwd
		me.defarena = defarena

		me.pilot = Circler() #RandomWalk()
		me.pid = None

		me.reset_stats()

		me.timers = timer.Timers()

		me.timers.add(500, me.print_stats)

	def iter(me):
		me.timers.iter()

	def handle_connected(me, pkt):
		log("sending login packet")
		login = struct.pack('< B x 32s 32s I x 2x 2x h 8x 4x 12x',
			C2S_LOGIN, me.name, me.pwd, 0x12345678, 134)
		me.send(login, 1)

	def handle_loginresponse(me, pkt):
		log("got login response, entering arena")
		me.goto_arena(me.defarena)

	def handle_whoami(me, pkt):
		(me.pid,) = struct.unpack('< x H', pkt)
		log("i have pid %d" % me.pid)

	def handle_inarena(me, pkt):
		log("in arena, starting ppks")
		me.timers.add(10 + 10*random.random(), me.send_ppk)
		me.timers.add(20 + 50*random.random(), me.send_chat)

	def handle_weapon(me, pkt):
		#log("got weapon")
		me.wpn_rcvd += 1

	def handle_position(me, pkt):
		#log("got position")
		me.pos_rcvd += 1

	def handle_chat(me, pkt):
		#log("got chat msg")
		me.chat_rcvd += 1

	def goto_arena(me, arena):
		me.pid = None
		me.timers.remove(me.send_ppk)
		me.timers.remove(me.send_chat)
		if type(arena) == type(0):
			goarena = struct.pack('< B B 2x h h h 16s',
				C2S_GOTOARENA, 0, 1024, 768, arena, '')
		else:
			goarena = struct.pack('< B B 2x h h h 16s',
				C2S_GOTOARENA, 0, 1024, 768, -3, arena)
		me.send(goarena, 1)

	def send_ppk(me):
		me.pilot.update()
		bty = me.pilot.getbty()
		x, y = me.pilot.getxy()
		dx, dy = me.pilot.getdxdy()
		rot = me.pilot.getrot()
		ppk = make_ppk(rot, x, y, dx, dy, 0, bty, 1700)
		me.send(ppk)
		me.pos_sent += 1
		#log("sent ppk: (%d,%d) :%d" % (x, y, bty))

	def send_chat(me):
		me.chat_sent += 1
		for i in range(random.choice([1, 1, 1, 2, 2, 3])):
			pkt = struct.pack('< B B x h', C2S_CHAT, 2, -1)
			pkt += 'msg %d-%d from %d' % (
				i, random.randint(0, 99999), me.pid)
			pkt += chr(0)
			me.send(pkt, 1)

	def print_stats(me):
		log("pos_sent=%2d  pos_rcvd=%4d  wpn_rcvd=%3d  "
			"chat_sent=%2d  chat_rcvd=%3d  inq_len=%2d" %
			(me.pos_sent, me.pos_rcvd, me.wpn_rcvd,
			 me.chat_sent, me.chat_rcvd, me.get_inq_len()))
		me.reset_stats()

	def reset_stats(me):
		me.pos_sent = me.pos_rcvd = me.wpn_rcvd = 0
		me.chat_sent = me.chat_rcvd = 0


