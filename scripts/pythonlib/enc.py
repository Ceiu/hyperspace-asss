# enc.py
# vie encryption routines, in python
# dist: public

import array, struct

import util


class EncState:
	def __init__(me):
		# keystream is either None, or (int, int array)
		me.keystream = None

	def encrypt(me, pkt):
		def enc_work(edata, arr):
			work, table = edata
			for i in range(len(arr)):
				work = arr[i] ^ table[i] ^ work
				arr[i] = work
		if me.keystream is None and pkt[:2] == '\x00\x01':
			(me.sentkey,) = struct.unpack('<i', pkt[2:6])
			return pkt
		return me.do_stuff(pkt, enc_work)

	def decrypt(me, pkt):
		def dec_work(edata, arr):
			work, table = edata
			for i in range(len(arr)):
				tmp = arr[i]
				arr[i] = table[i] ^ work ^ tmp
				work = tmp
		if me.keystream is None and pkt[:2] == '\x00\x02':
			(gotkey,) = struct.unpack('<i', pkt[2:6])
			if gotkey != me.sentkey:
				me.keystream = gen_keystream(gotkey)
				util.log('got key response, using vie encryption')
			else:
				util.log('got key response, no encryption')
			return pkt
		return me.do_stuff(pkt, dec_work)

	def do_stuff(me, pkt, do_work):
		# test zero key
		if me.keystream is None:
			return pkt

		origlen = len(pkt)

		# strip off type byte(s)
		if pkt[0] == '\0':
			prepend = pkt[:2]
			mydata = pkt[2:]
		else:
			prepend = pkt[:1]
			mydata = pkt[1:]

		# pad with zeros
		pad = (4 - (len(mydata) & 3)) & 3
		mydata = mydata + '\0' * pad

		# get into array
		arr = array.array('i')
		arr.fromstring(mydata)

		# do the work
		do_work(me.keystream, arr)

		# put back into string
		out = prepend + arr.tostring()
		return out[:origlen]


def gen_keystream(key):
	# key 0 means no encryption
	if key == 0:
		return None
	# minor hacks to work around python's integer semantics
	elif key > 0x7fffffff:
		key = -int(0x100000000-key)

	origkey = key

	tab = []
	for l in range(0, 260):
		t = (key * (-2092037281)) >> 32
		t = (t+key) >> 16
		t = t + (t>>31)
		t = ((((((t * 9) << 3) - t) * 5) << 1) - t) << 2
		rem = key % 0x1F31D
		if key < 0:
			rem = rem - 0x1F31D
		key = ((rem * 0x41A7) - t) + 0x7B
		if key == 0 or key < 0:
			key = key + 0x7fffffff
		tab.append(key & 0xffff)

	arr = array.array('H')
	arr.fromlist(tab)
	str = arr.tostring()
	arr = array.array('i')
	arr.fromstring(str)
	return (origkey, arr)


### testing stuff
#
# import sys
# k = long(sys.argv[1])
# s = gen_keystream(k)
# #_, str = s
# #sys.stdout.write(str.tostring())
#
# lst = []
# for i in range(32):
# 	lst.append(i)
# test = array.array('B')
# test.fromlist(lst)
# test = test.tostring()
#
# sys.stdout.write(encrypt(s, test))
# sys.stdout.write(decrypt(s, test))
#
### to test:
### for a in 1 2 3 7777 -4554 2882382797 4008636142 4294967295; do diff <(../../test/genkey $a) <(python enc.py $a | od -t x1 -A n | sed 's/^ //'); done

