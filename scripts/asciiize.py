#!/usr/bin/env python

import sys

def dig(d):
	if d in 'abcdef':
		return ord(d) - ord('a') + 10
	else:
		return ord(d) - ord('0')

def val(ds):
	v = 0
	for d in ds:
		v = v * 16 + dig(d)
	return v

for l in sys.stdin.readlines():
	bytes = l.split()

	sys.stdout.write(l)

	for b in bytes:
		v = val(b)
		if v > 31 and v < 127:
			sys.stdout.write('%c  ' % chr(v))
		else:
			sys.stdout.write('   ')

	print
