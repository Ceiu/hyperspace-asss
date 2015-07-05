#!/usr/bin/env python

import sys
import os

from ConfigParser import SafeConfigParser
from optparse import OptionParser

from lvl import LVLFile
from region import Region
from bmp import BMPFile


all_simple_flags = ['isbase', 'noanti', 'noweapons', 'noflags']


def mq(s):
	if s.find(' ') == -1 and s.find("'") == -1:
		return s
	else:
		return "'%s'" % s.replace("'", "'\\''")


def print_lvl_info(lvl):
	if lvl.bmp:
		print "  tileset present: %dx%d, %d bpp" % \
			(lvl.bmp.width, lvl.bmp.height, lvl.bmp.depth)
	else:
		print "  tileset absent"

	print "  tile data: %d tiles present" % len(lvl.tilelist)

	hasmeta = lvl.chunks or lvl.regions or lvl.attrs
	if hasmeta:
		print "  metadata present:"
		for k, v in lvl.attrs.items():
			print "    attribute: %s=%s" % (mq(k), mq(v))
		idx = 0
		for r in lvl.regions:
			print "    region %d:" % idx
			print "      name: %s" % mq(r.name)
			if r.isbase:
				print "      isbase: true"
			if r.noanti:
				print "      noanti: true"
			if r.noweapons:
				print "      noweapons: true"
			if r.noflags:
				print "      noflags: true"
			if r.autowarp and not r.autowarp[2]:
				print "      autowarp to: %d, %d" % r.autowarp[0:2]
			if r.autowarp and r.autowarp[2]:
				print "      autowarp to: %d, %d in arena %s" % r.autowarp
			print "      tile data: %d tiles included" % len(r.tiles)
			if r.chunks:
				for t, d in r.chunks:
					print "      unknown chunk: type %s, %d bytes" % (t, len(d))
			idx += 1
		for t, d in lvl.chunks:
			print "    unknown chunk: type %s, %d bytes" % (t, len(d))


def modify_lvl(lvl, opts):
	# tileset
	if opts.newtileset:
		if not os.path.exists(opts.newtileset):
			print "*** tileset file '%s' does not exist" % opts.newtileset
			sys.exit(1)

		try:
			nt = BMPFile(file(opts.newtileset))
		except:
			print "*** error reading tileset"
			sys.exit(1)

		lvl.bmp = nt
		print "using tileset from %s" % mq(opts.newtileset)
		if nt.width != 304 or nt.height != 160 or nt.depth != 8:
			print "WARNING: new tileset isn't 304x160 at 8 bpp"

	# tiles
	for tilespec in opts.newtiles:
		try:
			x, y, t = map(int, tilespec.split(','))
		except:
			print "*** bad tile option: '%s' (should be 'x,y,type')" % tilespec
			sys.exit(1)

		lvl.set_tile(x, y, t)
		print "set tile at %d,%d to %d" % (x, y, t)

	# attributes
	for attr in opts.attrs:
		try:
			k, v = attr.split('=', 1)
		except:
			print "*** bad attribute option: '%s' (should be 'key=value')" % attr
			sys.exit(1)

		lvl.set_attr(k, v)
		print "setting attribute %s to %s" % (mq(k), mq(v))

	for k in opts.attrs_to_del:
		lvl.del_attr(k)
		print "removing attribute %s" % (mq(k))

	# regions
	for rname in opts.regs_to_del:
		r = lvl.find_region(rname)
		if r:
			lvl.regions.remove(r)
			print "removing region %s" % (mq(rname))
		elif opts.allowmissing:
			print "can't find region %s, continuing" % mq(rname)
		else:
			print "*** can't find region %s" % mq(rname)
			sys.exit(1)

	for rname in opts.newregs:
		r = Region([('rNAM', rname)])
		lvl.regions.append(r)
		print "created region %s" % (mq(rname))

	for spec in opts.regs_to_ren:
		try:
			old, new = spec.split('=')
		except:
			print "*** bad renregion option: '%s' (should be 'oldname=newname')" % spec
			sys.exit(1)

		r = lvl.find_region(old)
		if not r:
			print "*** can't find region %s" % mq(old)
			sys.exit(1)

		r.name = new
		print "renaming region %s to %s" % (mq(old), mq(new))

	for flag in all_simple_flags:
		for spec in getattr(opts, 'regs_to_set_' + flag):
			try:
				rname, state = spec.split('=')
				state = int(state)
			except:
				print "*** bad %s option: '%s' (should be 'region=0/1')" % (flag, spec)
				sys.exit(1)

			r = lvl.find_region(rname)
			if not r:
				print "*** can't find region %s" % mq(rname)
				sys.exit(1)

			setattr(r, flag, state)
			if state:
				print "turned on %s for region %s" % (flag, mq(rname))
			else:
				print "turned off %s for region %s" % (flag, mq(rname))

	for spec in opts.regs_to_set_aw:
		try:
			rname, xyarena = spec.split('=')
			xyarena = xyarena.split(',')
			if len(xyarena) == 2:
				x, y = xyarena
				arena = None
			elif len(xyarena) == 3:
				x, y, arena = xyarena

			r = lvl.find_region(rname)
			x = int(x)
			y = int(y)
			r.autowarp = (x, y, arena)
			if arena:
				print "set autowarp destination for region %s to %d,%d, arena %s" % \
					(mq(rname), x, y, arena)
			else:
				print "set autowarp destination for region %s to %d,%d" % \
					(mq(rname), x, y)
		except:
			print "*** bad autowarp option: '%s' (should be 'region=x,y[,arena]')" % spec
			sys.exit(1)

	for spec in opts.regs_to_set_tiles:
		try:
			rname, cspec = spec.split('=', 1)
			bmpfile, rgb = cspec.split(':')
			r, g, b = map(int, rgb.split(','))
		except:
			print "*** bad setregiontiles option: '%s' (should be 'region=bmpfile:r,g,b')" % spec
			sys.exit(1)

		rgn = lvl.find_region(rname)
		if not rgn:
			print "*** can't find region %s" % mq(rname)
			sys.exit(1)

		try:
			bmp = BMPFile(bmpfile)
		except:
			print "*** error loading bmp file %s" % mq(bmpfile)
			sys.exit(1)

		if bmp.width != 1024 or bmp.height != 1024:
			print "*** bmp %s has wrong dimensions (must be 1024x1024)" % mq(bmpfile)
			sys.exit(1)

		target = bmp.make_pixel(r, g, b)

		newtiles = {}
		for x in xrange(1024):
			for y in xrange(1024):
				if bmp.get_pixel(x, y) == target:
					newtiles[(x,y)] = 1

		rgn.tiles = newtiles
		print "set region %s tiles from %s with color %d,%d,%d" % \
			(mq(rname), bmpname, r, g, b)

	for rgnv1file in opts.rgnv1files:
		try:
			lines = file(rgnv1file).readlines()
		except:
			print "*** can't read version 1 region file '%s'" % rgnv1file
			sys.exit(1)

		if lines[0].strip() != 'asss region file version 1':
			print "*** bad region file header"
			sys.exit(1)

		def decode_rect(s):
			def char_to_val(c):
				c = ord(c)
				if c >= ord('a') and c <= ord('z'):
					return c - ord('a')
				elif c >= ord('1') and c <= ord('6'):
					return c - ord('1') + 26
			return (char_to_val(s[0]) << 5 | char_to_val(s[1]),
			        char_to_val(s[2]) << 5 | char_to_val(s[3]),
			        char_to_val(s[4]) << 5 | char_to_val(s[5]),
			        char_to_val(s[6]) << 5 | char_to_val(s[7]))

		r = None
		for l in lines:
			l = l.strip().lower()
			if l.startswith('name: '):
				rname = l[6:].strip()
				r = lvl.find_region(rname)
				if not r:
					r = Region([('rNAM', rname)])
					lvl.regions.append(r)
					print "created region %s" % (mq(rname))
			elif l.startswith('| ') and r:
				x0, y0, w, h = decode_rect(l[2:])
				print "adding rect (%d,%d)-(%d,%d) to region %s" % \
					(x0, y0, x0+w, y0+h, r.name)
				for x in xrange(x0, x0+w):
					for y in xrange(y0, y0+h):
						r.tiles[(x, y)] = 1


def process_script(s):
	out = ['--allowmissing']

	cfg = SafeConfigParser()
	cfg.readfp(file(s))

	for k, v in cfg.items('args'):
		out.append('--%s' % k)
		out.append(v)

	for rname in cfg.sections():
		if rname == 'args':
			continue

		out.append('--delregion')
		out.append(rname)

		out.append('--newregion')
		out.append(rname)

		for k, v in cfg.items(rname):
			if k == 'tiles':
				k = 'setregiontiles'
			out.append('--%s' % k)
			out.append('%s=%s' % (rname, v))

	return out


def make_op():
	usage = """usage: lvltool <command> <options>

commands:
  print                 displays various information about input lvl file.
  write                 reads an lvl file, and write it out after
                        modifying it as directed by the command line
                        options.\
"""

	op = OptionParser(usage = usage)

	# basic options
	op.add_option('--in', '-i', action = 'store', type = 'string', dest = 'infile',
		metavar = 'FILE', help = 'the lvl file to read from')
	op.add_option('--out', '-o', action = 'store', type = 'string', dest = 'outfile',
		metavar = 'FILE', help = 'the lvl file to write to')

	# modifying options
	op.add_option('--tileset', '-t', action = 'store', type = 'string', dest = 'newtileset',
		metavar = 'FILE', help = 'bmp or lvl to get a new tileset from')
	op.add_option('--attr', '-a', action = 'append', type = 'string', dest = 'attrs',
		default = [], metavar = 'KEY=VALUE', help = 'adds an attribute to the lvl metadata')
	op.add_option('--delattr', action = 'append', type = 'string', dest = 'attrs_to_del',
		default = [], metavar = 'KEY', help = 'removes an attribute from the lvl metadata')
	op.add_option('--tile', action = 'append', type = 'string', dest = 'newtiles',
		default = [], metavar = 'X,Y,TYPE', help = 'modifies the map tiles, use type 0 to remove a tile')

	# regions
	op.add_option('--newregion', action = 'append', type = 'string', dest = 'newregs',
		default = [], metavar = 'REGIONNAME', help = 'adds a new region')
	op.add_option('--delregion', action = 'append', type = 'string', dest = 'regs_to_del',
		default = [], metavar = 'REGIONNAME', help = 'deletes a region')
	op.add_option('--renregion', action = 'append', type = 'string', dest = 'regs_to_ren',
		default = [], metavar = 'OLDNAME=NEWNAME', help = 'renames a region')
	for flag in all_simple_flags:
		op.add_option('--' + flag, action = 'append', type = 'string',
			dest = 'regs_to_set_' + flag, default = [], metavar = 'REGIONNAME=0/1',
			help = 'changes the %s flag in the specified region' % flag)
	op.add_option('--autowarp', action = 'append', type = 'string', dest = 'regs_to_set_aw',
		default = [], metavar = 'REGIONNAME=x,y[,arena]',
		help = 'sets an autowarp for the specified region')
	op.add_option('--setregiontiles', action = 'append', type = 'string',
		dest = 'regs_to_set_tiles', default = [], metavar = 'REGIONNAME=BMPFILE:r,g,b',
		help = 'sets the tiles for the specified region based on a bmp file')
	op.add_option('--loadv1file', action = 'append', type = 'string',
		dest = 'rgnv1files', default = [], metavar = 'RGNFILE',
		help = 'loads the contents of the specified version 1 region file')

	# options from file
	op.add_option('--script', action = 'append', type = 'string', dest = 'scripts',
		default = [], metavar = 'SCRIPTFILE',
		help = 'specifies a script to read region definitions from')

	# misc
	op.add_option('--allowmissing', action = 'store_true', default = False)

	return op


def main():

	argv = sys.argv[1:]
	(opts, args) = make_op().parse_args(argv)

	for s in opts.scripts:
		try:
			newargs = process_script(s)
			argv.extend(newargs)
			print "processed script %s, appending args: %s" % (mq(s), ' '.join(newargs))
		except:
			print "*** error processing script %s" % mq(s)
			sys.exit(1)

	# reprocess args to pick up new ones from any scripts
	(opts, args) = make_op().parse_args(argv)

	if not args:
		if not opts.infile:
			make_op().print_help()
			sys.exit(0)
		else:
			args = ['print']

	cmd = args[0].lower()

	if not opts.infile:
		print "*** no input file specified"
		sys.exit(1)

	if not os.path.exists(opts.infile):
		print "*** file '%s' doesn't exist" % opts.infile
		sys.exit(1)

	if cmd == 'print':
		try:
			lvl = LVLFile(opts.infile)
		except:
			print "*** errors reading lvl file"
			sys.exit(1)

		print "read lvl file '%s':" % opts.infile
		print_lvl_info(lvl)
	elif cmd == 'write':
		if not opts.outfile:
			print "*** no destination file specified"
			sys.exit(1)

		try:
			lvl = LVLFile(opts.infile)
		except:
			print "*** errors reading lvl file"
			sys.exit(1)

		try:
			modify_lvl(lvl, opts)
		except SystemExit:
			raise
		except:
			print "*** errors modifying lvl file"
			sys.exit(1)

		try:
			lvl.write_to_file(opts.outfile)
		except:
			print "*** errors writing lvl file"
			sys.exit(1)

		print "successfully wrote %s" % opts.outfile

	else:
		make_op().print_help()


if __name__ == '__main__':
	main()

