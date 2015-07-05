
import util


class UI:
	def __init__(me, pilot):
		me.pilot = pilot

	def process_line(me, line):
		util.log('got line: %s' % line)

