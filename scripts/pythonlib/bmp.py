
"""

this deals with bmp files

"""


import struct
import array


BMFH_FMT = '< H I H H I'
BMIH_FMT = '< I i i h h I I i i I I'


class NotABMPFile(Exception): pass


class BMPFile:
	def __init__(me, f = None):
		if f is not None:
			me.load_from_file(f)


	def load_from_file(me, f):
		bmfh = f.read(14)
		if len(bmfh) != 14:
			raise NotABMPFile('too short')

		bf_type, bf_size, bf_res1, bf_res2, bf_offbits = struct.unpack(BMFH_FMT, bmfh)

		if bf_type != 19778:
			raise NotABMPFile('bad BMP header')

		bmih = f.read(40)
		bi_size, bi_width, bi_height, bi_planes, bi_bitcount, \
			bi_compress, bi_sizeimage, bi_xres, bi_yres, bi_clrused, \
			bi_clrimportant = struct.unpack(BMIH_FMT, bmih)

		if bi_size != 40:
			raise Exception('bad BMP header')

		if bi_compress != 0:
			raise Exception("can't handle compressed bitmaps")

		if bi_planes != 1:
			raise Exception('bad plane count')

		if bi_bitcount != 8 and bi_bitcount != 24:
			raise Exception('bad color depth (only 8 and 24 supported)')

		if bi_bitcount == 8 and bf_offbits != 1078:
			raise Exception('unexpected data offset')
		if bi_bitcount == 24 and bf_offbits != 54:
			raise Exception('unexpected data offset')

		me.width = bi_width
		me.height = bi_height
		me.depth = bi_bitcount
		me.bf_size = bf_size
		me.bf_res1 = bf_res1

		if bi_bitcount == 8:
			me.rowbytes = (bi_width + 3) & ~3
			# read color table. note these are in bgr_ order within each
			# word.
			me.tab = array.array('L')
			me.tab.fromfile(f, 256)
		else:
			me.rowbytes = (bi_width * 3 + 3) & ~3
			# pixel data is rgb, left to right, bottom to top

		pixelbytes = me.rowbytes * bi_height
		me.pixeldata = f.read(pixelbytes)
		if len(me.pixeldata) != pixelbytes:
			raise Exception('not enough bytes, or other read error')


	def get_pixel(me, x, y):
		if x < 0 or x >= me.width or y < 0 or y >= me.height:
			raise Exception('pixel out of range')
		if me.depth == 8:
			offset = (me.height - y - 1) * me.rowbytes + x
			idx = ord(me.pixeldata[offset:offset+1])
			val = me.tab[idx]
			return val
		else:
			offset = (me.height - y - 1) * me.rowbytes + x * 3
			val = me.pixeldata[offset:offset+4]
			r, g, b = struct.unpack(val, '<BBB')
			# convert to bgr_, to match RGBQUAD
			return b | g<<8 | r<<16


	def make_pixel(me, r, g, b):
		return b | g<<8 | r<<16


	def write_to_file(me, f, extrabytes = 0):

		if me.depth == 8:
			size = me.rowbytes * me.height + 1024 + 54
			offbits = 1024 + 54
		else:
			size = me.rowbytes * me.height + 54
			offbits = 54

		if extrabytes:
			padding = ((size + 3) & ~3) - size
			size += padding
			res1 = size
			size += extrabytes
		else:
			res1 = 0
			padding = 0

		bmfh = struct.pack(BMFH_FMT, 19778, size, res1, 0, offbits)
		f.write(bmfh)

		bmih = struct.pack(BMIH_FMT, 40, me.width, me.height, 1,
			me.depth, 0, 0, 0, 0, 0, 0)
		f.write(bmih)

		if me.depth == 8:
			me.tab.tofile(f)

		f.write(me.pixeldata)
		f.write('\0' * padding)


if __name__ == '__main__':
	# test code
	b = BMPFile(file('testin.bmp'))
	b.write_to_file(file('testout.bmp', 'wb'))

