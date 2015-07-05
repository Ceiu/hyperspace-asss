#!/usr/bin/env python

import sys, os, time, random, select, signal, optparse
import util, prot, ui, pilot, timer

clients = []
def_ploss = 0.01

def set_signal():
	def sigfunc(signum, frame):
		util.log("caught signal, disconnecting")
		for client in clients:
			client.disconnect()
		os._exit(0)
	signal.signal(signal.SIGTERM, sigfunc)
	signal.signal(signal.SIGINT, sigfunc)


def new_client(name = None):
	dest = random.randint(0, opts.arenas - 1)
	client = pilot.Client(name=name, defarena=dest,
			ploss=def_ploss)
	client.connect(opts.server, opts.port)
	clients.append(client)


def login_event():
	if len(clients) == 1:
		add = 1
	elif len(clients) >= 2 * opts.n:
		add = 0
	else:
		add = random.random() > 0.5
	if add:
		new_client()
		print "*** new connection -> %d" % len(clients)
	else:
		cp = random.choice(clients)
		clients.remove(cp)
		cp.disconnect()
		print "*** dropping connection -> %d" % len(clients)


def arena_event():
	client = random.choice(clients)
	if client.pid is not None:
		dest = random.randint(0, opts.arenas - 1)
		print "*** arena change pid %d -> arena %d" % (client.pid, dest)
		client.goto_arena(dest)


def main():
	parser = optparse.OptionParser()
	parser.add_option('-s', '--server', type='string', dest='server', default='127.0.0.1')
	parser.add_option('-p', '--port', type='int', dest='port', default=5000)
	parser.add_option('-n', '--num', type='int', dest='n', default=1)
	parser.add_option('-a', '--arenas', type='int', dest='arenas', default=1)
	parser.add_option('-L', '--loginiv', type='int', dest='loginiv', default=0)
	parser.add_option('-A', '--arenaiv', type='int', dest='arenaiv', default=0)

	global opts
	(opts, args) = parser.parse_args()

	set_signal()

	for i in range(opts.n):
		new_client('loadgen-%02d-%03d' % (os.getpid() % 99, i))

	myui = None

	mytimers = timer.Timers()
	if opts.loginiv:
		mytimers.add(opts.loginiv, login_event)
	if opts.arenaiv:
		mytimers.add(opts.arenaiv, arena_event)

	while clients:

		socks = [0]
		for client in clients:
			socks.append(client.sock)

		try:
			ready, _, _ = select.select(socks, [], [], 0.01)
		except:
			ready = []

		# read/process some data
		for client in clients:
			if client.sock in ready:
				try:
					client.try_read()
				except prot.Disconnected:
					clients.remove(client)
					continue
			if myui and 0 in ready:
				line = sys.stdin.readline().strip()
				myui.process_line(line)

			# try sending
			try:
				client.try_sending_outqueue()
			except prot.Disconnected:
				clients.remove(client)
				continue

			# move pilot
			client.iter()

		mytimers.iter()


if __name__ == '__main__':
	main()


