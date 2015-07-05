
import util, random

class Timer:
	def __init__(me, iv, func):
		me.interval = iv
		me.func = func
		me.last = util.ticks() + iv * random.random()

class Timers:
	def __init__(me):
		me.clear()

	def clear(me):
		me.timers = []

	def add(me, iv, func):
		me.timers.append(Timer(iv, func))

	def remove(me, func):
		for t in me.timers:
			if t.func == func:
				me.timers.remove(t)

	def iter(me):
		now = util.ticks()
		for t in me.timers:
			if (now - t.last) > t.interval:
				t.last = now
				t.func()

