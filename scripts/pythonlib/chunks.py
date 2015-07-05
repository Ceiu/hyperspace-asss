
import struct


def read_chunks(f, bytesleft):
	# read all chunks
	chunks = []
	while bytesleft > 0:
		ctype, csize = struct.unpack('< 4s I', f.read(8))
		bytesleft -= (csize + 8)
		cdata = f.read(csize)
		if len(cdata) != csize:
			raise Exception('not enough bytes')
		chunks.append((ctype, cdata))
		# chunks are padded up to 4 bytes
		if (csize & 3) != 0:
			padding = 4 - (csize & 3)
			f.read(padding)
			bytesleft -= padding

	if bytesleft < 0:
		raise Exception("chunk sizes don't add up correctly!")

	return chunks


def write_chunks(f, chunks):
	totalsize = 0

	for ctype, cdata in chunks:
		csize = len(cdata)
		header = struct.pack('< 4s I', ctype, csize)
		f.write(header)
		f.write(cdata)
		totalsize += (csize + 8)
		# chunks are padded up to 4 bytes
		if (csize & 3) != 0:
			padding = 4 - (csize & 3)
			f.write(chr(0) * padding)
			totalsize += padding

	return totalsize

