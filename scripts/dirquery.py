#!/usr/bin/env python

import sys, socket


def query(host, minp = 1, printstatus = 0):
	import select, struct
	import util, prot

	def send_req(p):
		pkt = struct.pack('<BI', 1, minp)
		conn.send(pkt, 1)
		if printstatus:
			sys.stdout.write('querying')
			sys.stdout.flush()

	def onepkt(p):
		if printstatus:
			sys.stdout.write('.')
			sys.stdout.flush()

	def response(p):
		conn.disconnect(2)

		p = p[1:]
		servers = []
		while p:
			ip, port, players, scoresp, vers, title = \
					struct.unpack('<4sHHHI64s', p[:78])
			title = title.strip('\x00')
			p = p[78:]
			idx = p.index('\x00')
			descr = p[:idx]
			p = p[idx+1:]
			servers.append((ip, port, players, scoresp, vers, title, descr))

		results.append(servers)

	results = []

	# disable logging
	util.log = lambda l: None

	conn = prot.Connection()
	conn.add_handler(0x0200, send_req)
	conn.add_handler(0x0a00, onepkt)
	conn.add_handler(0x01, response)
	conn.connect(host, 4990, useenc=1)

	while not results:

		try:
			ready, _, _ = select.select([conn.sock], [], [], 1)
		except KeyboardInterrupt:
			break
		except:
			ready = []

		# read/process some data
		if conn.sock in ready:
			try:
				conn.try_read()
			except prot.Disconnected:
				results.append(None)

		# try sending
		try:
			conn.try_sending_outqueue()
		except prot.Disconnected:
			results.append(None)

	return results[0]


def print_servers(servers):
	import textwrap

	print "\nservers:"
	totals = 0
	totalp = 0
	for ip, port, players, scoresp, vers, title, descr in servers:
		totals += 1
		totalp += players
		ip = socket.inet_ntoa(ip)
		if scoresp:
			scoresp = 'scores'
		else:
			scoresp = 'no scores'
		print "%s (%s:%d) (%d players) (v %d) (%s)" % \
				(title, ip, port, players, vers, scoresp)
		for l in textwrap.wrap(descr):
			print '  ' + l

	print "\ntotal servers: %d, total players: %d" % (totals, totalp)


def main():
	host = sys.argv[1]
	try:
		minp = int(sys.argv[2])
	except:
		minp = 1

	servers = query(host, minp, 1)
	print_servers(servers)


if __name__ == '__main__':
	main()

