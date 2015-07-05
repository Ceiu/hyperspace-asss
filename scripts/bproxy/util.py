
# utility stuff
# dist: public

import sys, time

logfile = None

version = '0.1'


def ticks():
	return long(time.time() * 100)


def open_logfile(f):
	global logfile
	try:
		logfile = open(f, 'a', 1)
	except:
		log("can't open log file")


def log(s):
	global logfile

	l = '%s %s\n' % (time.ctime()[4:], s)
	sys.stderr.write('bproxy: ' + l)
	if logfile:
		logfile.write(l)

def exit(s):
	import os
	os._exit(s)
	log("aaaaaah! shouldn't get here!")


def snull(s):
	i = s.find('\0')
	if i == -1:
		return s
	else:
		return s[0:i]


def hex_to_bin(s):
	def val(c):
		c = ord(c)
		if c >= 97 and c <= 102:
			return c-87
		else:
			return c-48

	res = []
	s = s.lower()
	for i in range(0, len(s)/2, 2):
		v = (val(s[i]) << 4) + val(s[i+1])
		res.append(chr(v))

	return ''.join(res)

