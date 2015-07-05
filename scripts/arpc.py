#!/usr/bin/env python
#
# arpc - the asss rpc system
#
# arpc generates stubs for remote invocation of functions in asss
# modules.

SRCDIR = '../src/include'

import re

class arpc_func:
	def __init__(me, iface, name, line):
		me.iface = iface
		me.name = name
		me.line = line
		me.attrs = []
		me.serialid = arpc_func.serialid
		arpc_func.serialid = arpc_func.serialid + 1

	def addattr(me, a):
		me.attrs.append(a)

	def hasattr(me, a):
		return a in me.attrs

	def __str__(me):
		pre = '%s: %s: ' % (me.iface, me.name)
		if me.hasattr('null'):
			return pre + '(null)'
		else:
			pre += 'func: returns %s, args %s' % \
				(me.retval, ' '.join(map(str, me.args)))
			if me.hasattr('noop'):
				pre += ' (noop)'
			return pre

	def makesendname(me):
		return 'Arpc_s_%s_%s' % (me.iface, me.name)

	def makerecvname(me):
		return 'Arpc_r_%s_%s' % (me.iface, me.name)

	serialid = 100


class arpc_arg:
	def __init__(me, name):
		me.name = name
		me.attrs = []

	def addattr(me, a):
		me.attrs.append(a)

	def hasattr(me, a):
		return a in me.attrs

	def __str__(me):
		pre = me.name
		if me.hasattr('callback'):
			pre += ' (callback)'
		return pre

def parse_arg(arg):
	p = arg.split()
	a = arpc_arg(p[0])
	for attr in p[1:]:
		a.addattr(attr)
	return a

def parse_arpc_info(iface, func, line):
	a = arpc_func(iface, func, line)

	if line.find('null') != -1:
		a.addattr('null')

	if line.find('noop') != -1:
		a.addattr('noop')

	m = re.search(r'([a-zA-Z]*)\s*\((.*)\)', line)
	if m:
		a.retval = m.group(1)
		args = map(lambda s: parse_arg(s.strip()), m.group(2).split(','))
		a.args = args
		for arg in args:
			if arg.name == 'formatstr':
				a.addattr('hasformatstr')
			if arg.name in ['string', 'intset', 'BallData']:
				a.addattr('needfree')

	return a


def getfuncs():
	import os

	headers = []
	for f in os.listdir(SRCDIR):
		if f.endswith('.h'):
			headers.append(f)

	funcs = []
	iface = ''
	for h in headers:
		f = open(SRCDIR + '/' + h).readlines()
		iface = func = arpc = ''
		for l in f:
			# try to find interface declaration
			m = re.search(r'struct I([a-zA-Z]*)$', l)
			if m:
				iface = m.group(1)
				continue
			# try to find function name
			m = re.search(r'\(\*([a-zA-Z]*)\)', l)
			if m:
				name = m.group(1)
				continue
			# try to find data pointer name
			m = re.search(r'\*([a-zA-Z]*);', l)
			if m:
				name = m.group(1)
				continue
			# look for arpc info
			m = re.search(r'/\*\s*arpc:(.*)\*/', l)
			if m:
				arpc = m.group(1).strip()
				if iface and name and arpc:
					arpc = parse_arpc_info(iface, name, arpc)
					funcs.append(arpc)

	return funcs


def cmd_listfuncs():
	for f in getfuncs():
		print f

def getctype(name):
	if name == 'int':
		type = 'int'
	elif name == 'string' or name == 'formatstr':
		type = 'const char*'
	elif name == 'intptr':
		type = 'int*'
	elif name == 'voidptr':
		type = 'void*'
	elif name == 'void':
		type = 'void'
	elif name == 'char':
		type = 'char'
	elif name == 'intset':
		type = 'int*'
	elif name == 'ConfigHandle':
		type = 'ConfigHandle'
	elif name == 'BallData':
		type = 'struct BallData*'
	elif name == 'CommandFunc':
		type = 'CommandFunc'
	elif name == 'LinkedList':
		type = 'LinkedList*'
	elif name == 'etc':
		type = 'etc'
	else:
		type = '/*typefixme*/ ' + name
	return type

def getcuker(name):
	return 'w_%s' % name

def getuncuker(name):
	return 'r_%s' % name

def makeargs(args):
	ret = ''
	idx = 0
	for a in args:
		type = getctype(a.name)
		if type == 'void':
			ret += 'void,'
		elif type == 'etc':
			ret += '...,'
		else:
			ret += '%s arg%d,' % (type, idx)
		idx = idx + 1
	return ret[0:-1] # strip off last comma


MAXFORMATSTRBUF = 1024

def makesendbody(f):
	b = ''
	if f.hasattr('hasformatstr'):
		b += """\
	char buf[%d];
	va_list args;
""" % MAXFORMATSTRBUF
	if f.retval != 'void':
		b += """\
	%s ret;
""" % getctype(f.retval)
	b += """\
	CukeState *cuke = new_cuke();
	w_uint(cuke, %d); /* serialid */
""" % f.serialid
	idx = 0
	for a in f.args:
		aname = 'arg%d' % idx
		if a.name == 'formatstr':
			b += """\
	va_start(args, %s);
	vsnprintf(buf, %d, %s, args);
	va_end(args);
	w_string(cuke, buf);
""" % (aname, MAXFORMATSTRBUF, aname)
		elif a.name == 'etc' or a.name == 'void':
			pass
		else:
			cuker = getcuker(a.name)
			b += "\t%s(cuke, %s);\n" % (cuker, aname)
		idx = idx + 1
	# send off the cuked args
	b += "\tsend_cuke_default(cuke);\n\tfree_cuke(cuke);\n"
	if f.retval != 'void':
		b += """
	/* wait for result */
	cuke = wait_for_resp_default(-%d);
	ret = %s(cuke); 
	free_cuke(cuke);
	return ret;
""" % (f.serialid, getuncuker(f.retval))
	return b[0:-1] # strip last newline

def makerecvbody(f):
	b = ''
	arglist = '  '
	# delcare vars
	if f.retval != 'void':
		b += "\t%s ret;\n" % getctype(f.retval)
	idx = 0
	for a in f.args:
		b += "\t%s arg%d;\n" % (getctype(a.name), idx)
		if a.name == 'formatstr':
			arglist += '"%s", '
		if a.name == 'etc':
			pass
		else:
			arglist += "arg%d, " % idx
		idx = idx + 1
	arglist = arglist[0:-2]
	# uncuke
	idx = 0
	for a in f.args:
		b += "\targ%d = %s(cuke);\n" % (idx, getuncuker(a.name))
		idx = idx + 1
	# call
	if f.retval == 'void':
		b += "\tarpc_use_%s->%s(%s);\n" % (f.iface, f.name, arglist)
	else:
		b += "\tret = arpc_use_%s->%s(%s);\n" % (f.iface, f.name, arglist)
	# free
	idx = 0
	for a in f.args:
		if a.hasattr('needfree'):
			b += "\tfree_%s(arg%d);\n" % (a.name, idx)
		idx = idx + 1
	# send result back
	if f.retval != 'void':
		b += """\
	cuke = new_cuke();
	set_cuke_type(cuke, -%d);
	%s(cuke, ret);
	send_cuke(con, cuke);
	free_cuke(cuke);
""" % (f.serialid, getcuker(f.retval))

	return b[0:-1] # strip newline

def cmd_makestubs():
	funcs = getfuncs()

	# get ifaces
	ifaces = {}
	for f in funcs:
		ifaces[f.iface] = 1
	ifaces = ifaces.keys()

	print """
#include <stdarg.h>
#include <stdio.h>
#include "asss.h"
#include "remote.h"
"""

	# send stubs
	for f in funcs:
		if not f.hasattr('null'):
			name = f.makesendname()
			if f.hasattr('noop'):
				body = '\t/* noop */'
			else:
				body = makesendbody(f)
			args = makeargs(f.args)
			print \
"""
static %(ret)s %(name)s(%(args)s)
{
%(body)s
}
""" % \
	{
		'name': name, 'args': args,
		'body': body, 'ret': getctype(f.retval)
	}

	reglines = ''
	unreglines = ''
	for i in ifaces:
		print 'static I%s arpc_int_%s = {\n' \
			'\tINTERFACE_HEAD_DECL("%s", "arpc-stubs-%s")' % (i, i, i, i)

		for f in funcs:
			if f.iface == i:
				if f.hasattr('null'):
					print "\t0,"
				else:
					print "\t%s," % f.makesendname()

		print "};\n"

		reglines += """\
		mm->RegInterface(&arpc_int_%s, ALLARENAS);
""" % (i, i)
		unreglines += """\
		mm->UnregInterface(&arpc_int_%s, ALLARENAS);
""" % (i, i)


	print """\
EXPORT int MM_arpc_send_stubs(int action, Imodman *mm, int arena)
{
	if (action == MM_LOAD)
	{
%s
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
%s
		return MM_OK;
	}
	return MM_FAIL;
}
""" % (reglines, unreglines)


	# recv stubs
	print "\n\n/* recv stubs */"

	for i in ifaces:
		print "static I%s arpc_use_%s;" % (i, i)
	print

	for f in funcs:
		if not f.hasattr('null') and not f.hasattr('noop'):
			name = f.makerecvname()
			body = makerecvbody(f)
			print \
"""
static void %(name)s(CukeState *cuke, ConnectionData *con)
{
%(body)s
}
""" % \
	{
		'name': name,
		'body': body, 'ret': getctype(f.retval)
	}


	getlines = ''
	rellines = ''
	for i in ifaces:
		getlines += """\
		arpc_use_%s = mm->GetInterface("%s", ALLARENAS);
""" % (i, i)
		rellines += """\
		mm->ReleaseInterface(arpc_use_%s);
""" % i

	print """
EXPORT int MM_arpc_recv_stubs(int action, Imodman *mm, int arena)
{
	if (action == MM_LOAD)
	{
%s
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
%s
		return MM_OK;
	}
	return MM_FAIL;
}
""" % (getlines, rellines)


# main

if __name__ == '__main__':
	import sys
	try:
		cmd = sys.argv[1]
	except:
		print "You must specify a command on the command line:"
		for f in dir():
			if f.startswith('cmd_'):
				print "\t%s" % f[4:]
		sys.exit(1)
	g = globals()
	try:
		func = g['cmd_' + cmd]
		apply(func, sys.argv[2:])
		sys.exit(0)
	except:
		raise
		print "Bad command name or bad arguments"
		sys.exit(2)

