#!/usr/bin/env python

import os

# get deps
cmd = "cd ../src ; grep '^local I.*\*' */*.c | sed 's/\.c:local I/:/' | cut -d' ' -f1"
file = os.popen(cmd)
depdata = map(lambda l: l.strip().split(':'), file.readlines())

# organize them more nicely
deps = {}
for a, b in depdata:
	if not deps.has_key(a):
		deps[a] = []
	if not deps.has_key(b):
		deps[b] = []
	deps[a].append(b)

def cyclecheck(m, tocheck, seen):
	for i in tocheck:
		if i == m:
			seen.append(i)
			print "cycle: %s" % ' '.join(seen)
		elif deps.has_key(i) and i not in seen:
			seen2 = seen[:]
			seen2.append(i)
			cyclecheck(m, deps[i], seen2)

# look for cycles
for m, d in deps.items():
	cyclecheck(m, d, [m])

# find load order
def containedin(a, b):
	for i in a:
		if i not in b: return 0
	return 1

print "possible load order:"

loaded = []
left = deps.keys()
gotone = 1
while gotone:
	gotone = 0
	gottwo = 1
	loadnow = []
	while gottwo:
		gottwo = 0
		for m, d in deps.items():
			if m not in loaded and m not in loadnow and containedin(d, loaded):
				loadnow.append(m)
				gotone = 1
				gottwo = 1
	for m in loadnow:
		print m
		loaded.append(m)
		left.remove(m)
	print

if left: print "couldn't load: %s" % ' '.join(left)

