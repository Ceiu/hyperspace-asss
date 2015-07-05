
"""

this deals with extended lvl files

"""

import struct
import cStringIO

import bmp
import chunks
import region


METADATA_MAGIC = 0x6c766c65


class LVLFile:
	def __init__(me, name = None):
		if name is not None:
			me.load_from_file(name)


	def load_from_file(me, name):
		f = file(name, 'r')

		try:
			me.bmp = bmp.BMPFile(f)
		except bmp.NotABMPFile:
			me.bmp = None
			me.chunks = []
			me.regions = []
			me.attrs = {}
			f.seek(0)
			me.read_tile_data(f)
			return

		if me.bmp.bf_res1 != 0:
			f.seek(me.bmp.bf_res1)
			me.read_metadata(f)
		else:
			me.chunks = []
			me.regions = []
			me.attrs = {}

		f.seek(me.bmp.bf_size)
		me.read_tile_data(f)


	def read_tile_data(me, f):
		me.tilelist = []
		tile = f.read(4)
		while len(tile) == 4:
			b1, b2, b3, type = struct.unpack('<BBBB', tile)
			x = b1 | ((b2 & 0x0f) << 8)
			y = (b2 >> 4) | (b3 << 4)
			me.tilelist.append((y, x, type))
			tile = f.read(4)


	def write_tile_data(me, f):
		me.tilelist.sort()
		for y, x, type in me.tilelist:
			if type != 0:
				b1 = x & 0xff
				b2 = ((y & 0x0f) << 4) | ((x & 0xf00) >> 8)
				b3 = y >> 4
				f.write(struct.pack('<BBBB', b1, b2, b3, type))


	def set_tile(me, nx, ny, ntype):
		for tile in me.tilelist:
			y, x, _ = tile
			if nx == x and ny == y:
				idx = me.tilelist.index(tile)
				me.tilelist[idx] = (ny, nx, ntype)
				return
		me.tilelist.append((ny, nx, ntype))


	def read_metadata(me, f):
		magic, totalsize, _ = struct.unpack('<III', f.read(12))
		if magic != METADATA_MAGIC:
			raise Exception('bad metadata magic')

		rawchunks = chunks.read_chunks(f, totalsize - 12)

		me.chunks = []
		me.regions = []
		me.attrs = {}

		for t, d in rawchunks:
			if t == 'ATTR':
				k, v = d.split('=', 1)
				me.attrs[k] = v
			elif t == 'REGN':
				rgnchunks = chunks.read_chunks(cStringIO.StringIO(d), len(d))
				rgn = region.Region(rgnchunks)
				me.regions.append(rgn)
			elif t == 'TSET':
				raise Exception("can't handle TSET chunk yet")
			elif t == 'TILE':
				raise Exception("can't handle TILE chunk yet")
			else:
				# add it to the unparsed chunks list
				me.chunks.append((t, d))


	def metadata_to_string(me):
		hasmeta = me.chunks or me.regions or me.attrs
		if not hasmeta:
			return ''

		rawchunks = []
		for k, v in me.attrs.items():
			rawchunks.append(('ATTR', '%s=%s' % (k, v)))

		for rgn in me.regions:
			rawchunks.append(('REGN', rgn.to_string()))

		rawchunks.extend(me.chunks)

		f = cStringIO.StringIO()
		totalsize = chunks.write_chunks(f, rawchunks)

		header = struct.pack('<III', METADATA_MAGIC, totalsize + 12, 0)
		return header + f.getvalue()


	def set_attr(me, k, v):
		me.attrs[k] = v


	def del_attr(me, k):
		try: del me.attrs[k]
		except: pass


	def get_attr(me, k, default = None):
		return me.attrs.get(k, default)


	def add_region(me, rgn):
		me.regions.append(rgn)


	def find_region(me, rname):
		for r in me.regions:
			if r.name == rname:
				return r
		return None


	def write_to_file(me, name):
		f = file(name, 'wb')

		meta = me.metadata_to_string()

		if me.bmp:
			me.bmp.write_to_file(f, len(meta))
			f.write(meta)
			assert (f.tell() & 3) == 0
		elif meta:
			print "WARNING: metadata can't be saved because no tileset is present"

		me.write_tile_data(f)



if __name__ == '__main__':
	# test code
	l = LVLFile('testin.lvl')
	l.set_attr('NAME', 'lvl for testing metadata')
	l.set_attr('TESTFIELD', 'blah blah blah')
	d = '\xa1\xf3!\xf3I"\x01\xa0\xc6 c`c#7\xe0b\xa0\xc7\t@#\xf4\x96'
	rgninit = [('rTIL', d), ('rFOO', 'blahrg'), ('rNAM', 'test region #1')]
	rgn = region.Region(rgninit)
	l.add_region(rgn)
	l.chunks.append(('CFOO', 'custom data goes here'))
	l.write_to_file('testout.lvl')
	l = LVLFile('testout.lvl')
	l.write_to_file('testout2.lvl')

