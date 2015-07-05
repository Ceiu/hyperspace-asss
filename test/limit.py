#!/usr/bin/env python

rel = 0.1

limit = 1000
bytessince = 0
tm = 0
last = 0

tosend = []

while 1:
	tm += 1
	if (tm - last) > 1000/8:
		bytessince = bytessince * 7 / 8
		last = tm

	mylimit = int(bytessince + (limit - bytessince) * rel)
	bts = 10

	# send rel
	if tm % 100 == 0:
		print "want to send %d byte rel. %d queued. limit=%d  bts=%d mylimit=%d" % \
			(bts, len(tosend), limit, bytessince, mylimit)

		tosend.append(bts)

	for s in tosend[:]:
		if (bytessince + s) <= mylimit:
			bytessince += s
			tosend.remove(s)
			#print "SENT"
		else:
			#print "POSTPONED"
			pass

	# send unrel
	if (bytessince+10) < limit:
		bytessince += 10

