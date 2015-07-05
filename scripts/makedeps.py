#!/usr/bin/env python
# like gcc -MM, but stupider, in python, and with a prefix feature.
# dist: public

import sys, os, glob, re, posixpath as path

re_inc = re.compile(r'\s*#include\s+"([^"]+)"\s*')


def warn(fn, alreadydone={}):
	if not alreadydone.has_key(fn):
		alreadydone[fn] = 1
		# print >>sys.stderr, 'makedeps: warning: "%s" not found' % fn


def search(base, paths):
	for p in paths:
		fn = path.join(p, base)
		if os.access(fn, os.F_OK):
			return fn


def format_deps(out, fn, paths, prefix):
	root, _ = path.splitext(path.basename(fn))
	ofn = prefix + root + '.o'
	deps = {}

	def find_deps(fn, deps):
		dir, base = path.split(fn)

		try:
			lines = file(fn).readlines()
		except EnvironmentError:
			print >>sys.stderr, "makedeps: can't open: %s" % fn
			sys.exit(1)

		for line in lines:
			line = line.strip()
			m = re_inc.match(line)
			if m:
				incbase = m.group(1)
				incfile = search(incbase, [dir] + paths)
				if incfile:
					deps[incfile] = 1
					# recur!
					find_deps(incfile, deps)
				elif incbase.endswith('.inc') and prefix:
					# special case: unfound inc files go in prefix
					incfile = prefix + incbase
					deps[incfile] = 1
				else:
					warn(incbase)

	find_deps(fn, deps)

	out.write('%s: %s \\\n' % (ofn, fn))
	l = '  '
	for d in deps.keys():
		if (len(l) + len(d) + 1) >= 80:
			out.write(l + '\\\n')
			l = '  '
		l += d + ' '
	out.write(l + '\n')


def main():
	args = sys.argv[1:]

	paths = []
	files = []
	prefix = ''
	outfile = sys.stdout

	i = 0
	while i < len(args):
		arg = args[i]
		i += 1
		if arg.startswith('-o'):
			try:
				outfile = file(args[i], 'w')
			except EnvironmentError:
				print >>sys.stderr, "makedeps: can't write to: %s" % args[i]
				sys.exit(1)
			i += 1
		elif arg.startswith('-I'):
			if arg == '-I':
				arg = args[i]
				i += 1
			else:
				arg = arg[2:]
			if path.isabs(arg):
				pass
				# print >>sys.stderr, "makedeps: ignoring option: -I%s" % arg
			else:
				paths.append(arg)
		elif arg.startswith('-P'):
			prefix = args[i]
			i += 1
		elif arg.startswith('-'):
			pass
			# print >>sys.stderr, "makedeps: ignoring option: %s" % arg
		else:
			for f in glob.glob(arg):
				files.append(f)

	for fn in files:
		format_deps(outfile, fn, paths, prefix)

	sys.exit(0)

if __name__ == '__main__':
	main()
