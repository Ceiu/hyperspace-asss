
"""

handle regions

"""

import struct
import cStringIO

import chunks


class Region:
	def __init__(me, rawchunks):
		me.load_from_chunks(rawchunks)


	def load_from_chunks(me, rawchunks):
		me.name = None
		me.isbase = 0
		me.noanti = 0
		me.noweapons = 0
		me.noflags = 0
		me.autowarp = None
		me.chunks = []
		me.tiles = {}

		for t, d in rawchunks:
			if t == 'rNAM':
				me.name = d
			elif t == 'rTIL':
				me.load_tile_data(d)
			elif t == 'rBSE':
				me.isbase = 1
			elif t == 'rNAW':
				me.noanti = 1
			elif t == 'rNWP':
				me.noweapons = 1
			elif t == 'rNFL':
				me.noflags = 1
			elif t == 'rAWP':
				if len(d) == 4:
					x, y = struct.unpack('<hh', d)
					arena = None
				else:
					x, y, arena = struct.unpack('< h h 16s', d)
				me.autowarp = (x, y, arena)
			else:
				me.chunks.append((t, d))


	def load_tile_data(me, d):
		tiles = {}

		cx = 0
		cy = 0

		i = 0
		while i < len(d):
			assert cx >= 0 and cx < 1024 and cy >= 0 and cy < 1024

			b = ord(d[i])
			i += 1

			op = (b & 192) >> 6
			d1 = b & 31
			n = d1 + 1

			if b & 32:
				# two-byte ops
				d2 = ord(d[i])
				i += 1
				n = (d1 << 8) + d2 + 1

			if op == 0:
				# n empty in a row
				cx += n
			elif op == 1:
				# n present in a row
				for x in xrange(n):
					tiles[(cx + x, cy)] = 1
				cx += n
			elif op == 2:
				# n rows of empty
				assert cx == 0
				cy += n
			elif op == 3:
				# repeat last row n times
				assert cx == 0 and cy > 0
				for y in xrange(n):
					for x in xrange(1024):
						if tiles.has_key((x, cy-1)):
							tiles[(x, cy+y)] = 1
				cy += n

			if cx == 1024:
				cx = 0
				cy += 1

		assert cy == 1024

		me.tiles = tiles


	def get_tile(me, x, y):
		return me.tiles.get((x, y), 0)


	def set_tile(me, x, y, on = 1):
		if on:
			me.tiles[(x, y)] = 1
		else:
			try: del me.tiles[(x, y)]
			except: pass


	def get_tile_data(me):
		rows = []

		cx = 0
		cy = 0
		crow = ''

		while cy < 1024:
			x = cx
			y = cy
			cur = me.tiles.get((x, y), 0)

			while me.tiles.get((x, y), 0) == cur and x < 1024:
				x += 1

			n = x - cx
			assert n > 0

			# special case
			if n == 1024 and cur == 0:
				assert cx == 0
				crow = None
			elif n <= 32:
				crow += chr((cur << 6) | (n-1))
			else:
				crow += chr((cur << 6) | 32 | ((n-1) >> 8))
				crow += chr((n-1) & 255)

			cx += n
			if cx == 1024:
				cx = 0
				cy += 1
				rows.append(crow)
				crow = ''

		assert len(rows) == 1024

		out = []
		cy = 0
		while cy < 1024:
			y = cy
			cur = rows[cy]

			while y < 1024 and rows[y] == cur:
				y += 1

			n = y - cy
			assert n > 0

			if n == 1:
				cy += 1
				out.append(cur)
			elif rows[cy] is None:
				cy += n
				if n <= 32:
					out.append(chr(128 | (n-1)))
				else:
					out.append(chr(128 | 32 | ((n-1) >> 8)))
					out.append(chr((n-1) & 255))
			else:
				cy += n
				out.append(cur)
				n -= 1
				if n <= 32:
					out.append(chr(128 | 64 | (n-1)))
				else:
					out.append(chr(128 | 64 | 32 | ((n-1) >> 8)))
					out.append(chr((n-1) & 255))

		return ''.join(out)


	def to_string(me):
		rawchunks = []

		if me.name is not None:
			rawchunks.append(('rNAM', me.name))
		if me.isbase:
			rawchunks.append(('rBSE', ''))
		if me.noanti:
			rawchunks.append(('rNAW', ''))
		if me.noweapons:
			rawchunks.append(('rNWP', ''))
		if me.noflags:
			rawchunks.append(('rNFL', ''))
		if me.autowarp:
			x, y, arena = me.autowarp
			if arena:
				data = struct.pack('<hh16s', x, y, arena)
			else:
				data = struct.pack('<hh', x, y)
			rawchunks.append(('rAWP', data))

		rawchunks.append(('rTIL', me.get_tile_data()))

		rawchunks.extend(me.chunks)

		f = cStringIO.StringIO()
		chunks.write_chunks(f, rawchunks)
		return f.getvalue()



if __name__ == '__main__':
	# some testing code

	d = [
		# 500 empty rows
		161, 243,
		# 500 empty tiles
		33, 243,
		# 10 present tiles
		73,
		# 514 empty tiles
		34, 1,
		# 523 empty rows
		162, 10]

	d = ''.join(map(chr, d))
	#print `d`

	rgninit = [('rTIL', d), ('rFOO', 'blahrg'), ('rNAM', 'test region #1')]
	rgn = Region(rgninit)

	rgn.set_tile(10, 1000)

	for x in range(100, 200):
		for y in range(700+x/10-100, 800):
			rgn.set_tile(x, y)

	#print rgn.tiles.keys()

	d2 = rgn.get_tile_data()
	#print `d2`, len(d2)

	assert d2 == Region([('rTIL', d2)]).get_tile_data()

	#print `rgn.to_string()`


