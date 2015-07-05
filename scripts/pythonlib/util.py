
import sys, time, os

logfile = None

prefix = '%02d| ' % (os.getpid() % 100)


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

	#l = '%s %s\n' % (time.ctime()[4:], s)
	l = '%s %s\n' % (time.ctime()[11:19], s)
	sys.stdout.write(prefix)
	sys.stdout.write(l)
	if logfile:
		logfile.write(l)

