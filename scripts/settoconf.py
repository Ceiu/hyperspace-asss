#!/usr/bin/env python
# dist: public

import sys

data = {}

for l in sys.stdin.readlines():
	f = l.strip().split(':', 5)

	while len(f) < 6:
		f.append('')

	sec, key, val, min, max, desc = f

	data.setdefault(sec, {})[key] = val

for sec, vals in data.items():
	print '[%s]' % sec
	for key, val in vals.items():
		print '%s=%s' % (key, val)
	print

