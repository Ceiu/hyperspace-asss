#!/usr/bin/env python

import sys

NP = 5

el = '[K'
clr = '[H'
el = ''
clr = ''

class A:
	"""separate sent counters plus borrowing"""
	L = {
		0: 0.2,
		1: 0.4,  # regular unrel
		2: 0.2,
		3: 0.15, # rel
		4: 0.05  # ack
	}

	def __init__(me):
		me.current_sent = [0, 0, 0, 0, 0]

	def iter(me, dt):
		for p in range(NP):
			me.current_sent[p] -= dt * A.L[p] * limit
			if me.current_sent[p] < 0:
				me.current_sent[p] = 0

	def try_send(me, bytes, pri):
		copy = me.current_sent[:]
		for p in range(pri, -1, -1):
			slack = int(A.L[p] * limit - me.current_sent[p])
			if slack > 0:
				me.current_sent[p] += min(bytes, slack)
				bytes = max(0, bytes - slack)
		if bytes == 0:
			return 0
		else:
			me.current_sent = copy
			return 1

	def display(me):
		print '         %s%s' % ('-' * 72, el)
		for p in range(NP):
			print "%d: %5d %s%s" % (p, int(me.current_sent[p]),
				'*' * int(me.current_sent[p] * 72 / limit), el)

class C:
	"""single sent counter"""
	L = {
		0: 0.2,
		1: 0.6,  # regular unrel
		2: 0.8,
		3: 0.95, # rel
		4: 1.0   # acl
	}

	def __init__(me):
		me.current_sent = 0

	def iter(me, dt):
		me.current_sent -= dt * limit
		if me.current_sent < 0:
			me.current_sent = 0
		#print "refilling %d to %d" % (dt * limit, me.current_sent)

	def try_send(me, bytes, pri):
		if (me.current_sent + bytes) < C.L[pri] * limit:
			me.current_sent += bytes
			#print "sending %d to %d" % (bytes, me.current_sent)
			return 0
		#print "NOT sending %d (%d >= %d)" % (bytes, me.current_sent+bytes, C.L[pri]*limit)
		return 1

	def display(me):
		print '%s%s' % ('-' * 72, el)
		print me.current_sent
		print "%s%s" % ('*' * int(me.current_sent * 72 / limit), el)


limit = 10000.0

m = A()
#m = C()

t = 0
dt = 0.1
dropped3 = dropped1 = 0

dropped3 += m.try_send(0.3 * limit, 3)
dropped1 += m.try_send(0.6 * limit, 1)

for qq in xrange(1000):
	t += 1
	# add tokens
	mod = 7
	if (t % mod) == 0:
		m.iter(dt * mod)
	# try sending
	n = 15
	for i in range(n):
		dropped1 += m.try_send(int(0.70 * limit * dt / n), 1)
	for i in range(n):
		dropped3 += m.try_send(int(0.35 * limit * dt / n), 3)
	# display
	#print clr
	#m.display()
	#print "dropped=%d/%d" % (dropped3, dropped1)
	sys.stdout.flush()

print "dropped=%d/%d" % (dropped3, dropped1)



#class B:
#	L = {
#		0: 0.2,
#		1: 0.6,
#		2: 0.8,
#		3: 0.95,
#		4: 1.0
#	}
#
#	def __init__(me):
#		me.current_sent = [0, 0, 0, 0, 0]
#
#	def iter(me, dt):
#		for p in range(NP):
#			me.current_sent[p] -= dt * B.L[p] * limit
#			if me.current_sent[p] < 0:
#				me.current_sent[p] = 0
#
#	def try_send(me, bytes, pri):
#		if (me.current_sent[pri] + bytes) < B.L[pri] * limit:
#			me.current_sent[pri] += bytes
#			return 0
#		return 1
#
#	def display(me):
#		print '   %s' % ('-' * 72)
#		#print me.current_sent
#		for p in range(NP):
#			print "%d: %s" % (p, '*' * int(me.current_sent[p] * 72 / limit))

