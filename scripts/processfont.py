#!/usr/bin/env python
# dist: public

import sys

STARTAT = ord(' ')
ENDAT = ord('~')
LETTERS = ENDAT - STARTAT + 1
YES = '#'
MAYBE = '@' # only used internally
NO = '.'


def gethoriz(mat, min):
	height = len(mat)
	width = len(mat[0])
	bricks = []
	for y in range(height):
		x = 0
		while x < width:
			if mat[y][x] == YES:
				nx = x
				while nx < width and mat[y][nx] != NO:
					nx = nx + 1
				if (nx - x) >= min:
					for x2 in range(nx - x):
						mat[y][x+x2] = MAYBE
					bricks.append((x, y, nx-1, y))
				x = nx
			else:
				x = x + 1
	return bricks


def getvert(mat, min):
	height = len(mat)
	width = len(mat[0])
	bricks = []
	for x in range(width):
		y = 0
		while y < height:
			if mat[y][x] == YES:
				ny = y
				while ny < height and mat[ny][x] != NO:
					ny = ny + 1
				if (ny - y) >= min:
					for y2 in range(ny - y):
						mat[y+y2][x] = MAYBE
					bricks.append((x, y, x, ny-1))
				y = ny
			else:
				y = y + 1
	return bricks


def makemutable(mat):
	h = len(mat)
	w = len(mat[0])
	m = []
	for y in range(h):
		m.append([])
		for x in range(w):
			m[y].append(mat[y][x])
	return m


def processfile(name):
	from string import strip
	data = map(strip, open(name).readlines())
	l = len(data)
	perlet = l / LETTERS

	letters = []
	widths = []

	while data:
		let = makemutable(data[:perlet])
		del data[:perlet]

		m = max(len(let), len(let[0]))
		bricks = []
		for n in range(m, 0, -1):
			bricks.extend(gethoriz(let, n))
			bricks.extend(getvert(let, n))
		letters.append(bricks)
		widths.append(len(let[0]))

	return letters, widths, perlet



def emit_prelude(o):
	o.write(
"""
/* declarations */

struct bl_brick
{
	unsigned char x1, y1, x2, y2;
};

struct bl_letter
{
	int width, bricknum;
	struct bl_brick *bricks; /* can be null if bricknum == 0 */
};

""")


def emit_letters(o, letters, widths):
	o.write("\n/* letters */\n\n")
	for idx in range(len(letters)):
		l = letters[idx]
		o.write("/* '%c' */\n" % chr(idx + STARTAT))
		if len(l):
			o.write("static struct bl_brick bl_letter_%d[] =\n{\n" % \
				(idx + STARTAT))
			for b in l:
				o.write("\t{ %d, %d, %d, %d },\n" % b)
			o.write("};\n\n")
		else:
			o.write("/* no bricks */\n\n")

	o.write("\n/* big array */\n\n")
	o.write("static struct bl_letter letterdata[] =\n{\n")
	for idx in range(len(letters)):
		l = len(letters[idx])
		if l:
			o.write("\t{ %d, %d, bl_letter_%d },\n" % \
				( widths[idx], l, idx + STARTAT ))
		else:
			o.write("\t{ %d, %d, 0 },\n" % \
				( widths[idx], l ))
	o.write("};\n\n")

	o.write("\n/* done */\n\n")


def main():
	if len(sys.argv) < 3:
		print "\nUsage: %s banner.font letters.inc\n"
		sys.exit()
	else:
		o = open(sys.argv[2], 'w')
		emit_prelude(o)
		l, w, h = processfile(sys.argv[1])
		o.write("static const int letterheight = %d;\n" % h)
		emit_letters(o, l, w)

if __name__ == '__main__': main()

