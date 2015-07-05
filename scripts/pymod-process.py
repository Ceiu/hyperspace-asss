#!/usr/bin/env python
# dist: public

import sys, os, re, string, glob

DEFBUFLEN = 1024

# lots of precompiled regular expressions

# include directives
re_pyinclude = re.compile(r'\s*/\* pyinclude: (.*) \*/')

# constants
re_pyconst_dir = re.compile(r'\s*/\* pyconst: (.*), "(.*)" \*/')

# callbacks
re_pycb_cbdef = re.compile(r'#define (CB_[A-Z_0-9]*)')
re_pycb_typedef = re.compile(r'typedef void \(\*([A-Za-z_0-9]*)\)')
re_pycb_dir = re.compile(r'/\* pycb: (.*?)(\*/)?$')

# interfaces
re_pyint_intdef = re.compile(r'#define (I_[A-Z_0-9]*)')
re_pyint_typedef = re.compile(r'typedef struct (I[a-z_0-9]*)')
re_pyint_func = re.compile(r'([ ]{4}|\t)[A-Za-z].*?\(\*([A-Za-z_0-9]*)\)')
re_pyint_dir = re.compile(r'\s*/\* pyint: (.*?)(\*/)?$')
re_pyint_done = re.compile(r'^}')

# advisers
re_pyadv_advdef = re.compile(r'#define (A_[A-Z_0-9]*)')
re_pyadv_typedef = re.compile(r'typedef struct (A[a-z_0-9]*)')
re_pyadv_func = re.compile(r'([ ]{4}|\t)[A-Za-z].*?\(\*([A-Za-z_0-9]*)\)')
re_pyadv_dir = re.compile(r'\s*/\* pyadv: (.*?)(\*/)?$')
re_pyadv_done = re.compile(r'^}')

# types
re_pytype = re.compile(r'\s*/\* pytype: (.*) \*/')


# utility functions for constant translation

def const_int(n):
	const_file.write('INT(%s)\n' % n);

def const_string(n):
	const_file.write('STRING(%s)\n' % n);

def const_one(n):
	const_file.write('ONE(%s)\n' % n);

def const_callback(n):
	const_file.write('PYCALLBACK(%s)\n' % n);

def const_interface(n):
	const_file.write('PYINTERFACE(%s)\n' % n);
	
def const_adviser(n):
	const_file.write('PYADVISER(%s)\n' % n);


def tokenize_signature(s):
	out = []
	t = ''
	dash = 0
	for c in s:
		if c in string.letters or c in string.digits or c == '_':
			t += c
		else:
			if t:
				out.append(t)
				t = ''

			if dash and c == '>':
				out.append('->')
				dash = 0
			else:
				dash = 0

			if c in ',()':
				out.append(c)
			elif c == '-':
				dash = 1

	assert not dash

	if t:
		out.append(t)

	return out

class Arg:
	def __init__(me, tp, flags):
		me.tp = tp
		me.flags = flags
	def __str__(me):
		return str(me.tp) + '[' + ', '.join(map(str, me.flags)) + ']'

class Func:
	def __init__(me, args, out):
		me.args = args
		me.out = out
	def __str__(me):
		return '(' + ', '.join(map(str, me.args)) + ' -> ' + str(me.out) + ')'

def is_func(o):
	return getattr(o, '__class__', None) is Func

def parse_arg(tokens):
	flags = []
	if tokens[0] == '(':
		tokens.pop(0)
		tp = parse_func(tokens)
		assert tokens[0] == ')'
		tokens.pop(0)
	else:
		tp = tokens.pop(0)
	while tokens and tokens[0] not in [',', '->', ')']:
		flags.append(tokens.pop(0))

	return Arg(tp, flags)

def parse_func(tokens):
	args = []
	while tokens[0] != '->':
		assert tokens[0] != ')'
		args.append(parse_arg(tokens))
		if tokens[0] == ',':
			tokens.pop(0)
	tokens.pop(0)
	out = parse_arg(tokens)
	return Func(args, out)


def genid(state = [100]):
	state[0] += 1
	return 'py_genid_%d' % state[0]

class type_gen:
	def format_char(me):
		raise Exception()
	# these two should throw exceptions unless the format char is O&
	def build_converter(me):
		raise Exception()
	def parse_converter(me):
		raise Exception()
	def decl(me):
		raise Exception()
	def buf_decl(me, s):
		return me.decl(s)
	def buf_decl_def(me, s):
		return me.buf_decl(s) + ' = %s' % me.buf_init()
	def parse_decl(me, s):
		return me.buf_decl(s)
	def ptr_decl(me, s):
		return '%s*%s' % (me.decl(''), s)
	def conv_to_buf(me, buf, val):
		return '\t*%s = %s;' % (buf, val)
	def buf_init(me):
		return '0'
	def buf_prefix(me):
		return '&'
	def out_prefix(me):
		return ''
	def ptr_val(me):
		return '*'

class type_void(type_gen):
	def decl(me, s):
		return 'void ' + s
	def to_obj(me, s):
		return 'PyCObject_FromVoidPtr(ptr, NULL)'
	def from_obj(me, s):
		return '*(int *)PyCObject_AsVoidPtr(%s)' % s

class type_null(type_gen):
	def format_char(me):
		return ''
	def decl(me, s):
		return 'void *%s = NULL' % s

class type_zero(type_gen):
	def format_char(me):
		return ''
	def decl(me, s):
		return 'int %s = 0' % s

class type_one(type_gen):
	def format_char(me):
		return ''
	def decl(me, s):
		return 'int %s = 1' % s

class type_int(type_gen):
	def format_char(me):
		return 'i'
	def decl(me, s):
		return 'int ' + s
	def to_obj(me, s):
		return 'PyInt_FromLong(%s)' % s
	def from_obj(me, s):
		return 'PyInt_AsLong(%s)' % s

class type_short(type_int):
	def format_char(me):
		return 'h'
	def decl(me, s):
		return 'short ' + s

class type_uint(type_int):
	def format_char(me):
		return 'I'
	def decl(me, s):
		return 'unsigned int ' + s

class type_ushort(type_int):
	def format_char(me):
		return 'H'
	def decl(me, s):
		return 'unsigned short ' + s

class type_double(type_gen):
	def format_char(me):
		return 'd'
	def decl(me, s):
		return 'double ' + s
	def to_obj(me, s):
		return 'PyFloat_FromDouble(%s)' % s
	def from_obj(me, s):
		return 'PyFloat_AsDouble(%s)' % s

class type_string(type_gen):
	def format_char(me):
		return 's'
	def decl(me, s):
		return 'const char *' + s
	def parse_decl(me, s):
		return me.decl(s)
	def buf_decl(me, s):
		return 'charbuf %s' % s
	def ptr_decl(me, s):
		return 'char *%s' % s
	def buf_init(me):
		return '{0}'
	def conv_to_buf(me, buf, val):
		return '\tastrncpy(%s, %s, buflen);' % (buf, val)
	def buf_prefix(me):
		return ''

class type_zstring(type_string):
	def format_char(me):
		return 'z'

class type_retstring(type_string):
	def format_char(me):
		return 's#'

class type_player(type_gen):
	def format_char(me):
		return 'O&'
	def decl(me, s):
		return 'Player *' + s
	def build_converter(me):
		return 'cvt_c2p_player'
	def parse_converter(me):
		return 'cvt_p2c_player'

class type_arena(type_gen):
	def format_char(me):
		return 'O&'
	def decl(me, s):
		return 'Arena *' + s
	def build_converter(me):
		return 'cvt_c2p_arena'
	def parse_converter(me):
		return 'cvt_p2c_arena'

class type_config(type_gen):
	def format_char(me):
		return 'O&'
	def decl(me, s):
		return 'ConfigHandle ' + s
	def build_converter(me):
		return 'cvt_c2p_config'
	def parse_converter(me):
		return 'cvt_p2c_config'

class type_banner(type_gen):
	def format_char(me):
		return 'O&'
	def decl(me, s):
		return 'Banner *' + s
	def build_converter(me):
		return 'cvt_c2p_banner'
	def parse_converter(me):
		return 'cvt_p2c_banner'

class type_target(type_gen):
	def format_char(me):
		return 'O&'
	def decl(me, s):
		return 'Target *' + s
	def buf_decl(me, s):
		return 'Target ' + s
	def build_converter(me):
		return 'cvt_c2p_target'
	def parse_converter(me):
		return 'cvt_p2c_target'
	def out_prefix(me):
		return '&'
	def conv_to_buf(me, buf, val):
		raise Exception()

class type_bufp(type_gen):
	def format_char(me):
		return 's#'
	def decl(me, s):
		raise Exception()
	def buf_decl(me, s):
		return 'const void *%s' % s
	def buf_init(me):
		return 'NULL'
	def conv_to_buf(me, buf, val):
		raise Exception()

class type_playerlist(type_gen):
	def format_char(me):
		return 'O&'
	def decl(me, s):
		return 'LinkedList *%s = LLAlloc()' % s
	def build_converter(me):
		return 'cvt_c2p_playerlist'
	def parse_converter(me):
		return 'cvt_p2c_playerlist'

class type_dict(type_gen):
	def format_char(me):
		return 'O&'
	def decl(me, s):
		return 'HashTable *%s = HashAlloc()' %s
	def build_converter(me):
		return 'cvt_c2p_dict'
	def parse_converter(me):
		return 'cvt_p2c_dict'

def get_type(tp):
	try:
		cname = 'type_' + tp
		cls = globals()[cname]
		return cls()
	except:
		return None


def create_c_to_py_func(name, func):
	args = func.args
	out = func.out

	informat = []
	outformat = []
	inargs = []
	outargs = []
	decls = []
	extras3 = []
	allargs = []
	av_arena = None
	av_player = None

	if out.tp == 'void':
		retorblank = ''
		rettype = type_void()
		defretval = ''
	elif out.tp == 'retstring':
		assert not out.flags
		typ = get_type(out.tp)
		argname = 'tmp'
		decls.append('\tconst char *tmp;')
		decls.append('\tint buflen;')
		decls.append('\tchar *ret;')
		outformat.append("s#")
		outargs.append('&tmp')
		outargs.append('&buflen')
		retorblank = 'ret'
		rettype = typ
		defretval = 'NULL'
		extras3.append('\tif(buflen != 0)\n\t{')
		extras3.append('\t\tbuflen++;')
		extras3.append('\t\tret = amalloc(sizeof(char) * buflen);')
		extras3.append('\t\tastrncpy(ret, tmp, buflen);\n\t}')
	else:
		assert not out.flags
		typ = get_type(out.tp)
		argname = 'ret'
		decls.append('\t%s;' % typ.decl(argname))
		outformat.append(typ.format_char())
		try:
			outargs.append(typ.build_converter())
		except:
			pass
		outargs.append('&%s' % argname)
		retorblank = 'ret'
		rettype = typ
		defretval = '0'

	idx = 0
	for arg in args:
		idx = idx + 1
		argname = 'arg%d' % idx

		opts = arg.flags
		typ = get_type(arg.tp)

		if arg.tp == 'void':
			continue

		elif arg.tp == 'clos':
			decls.append('\tPyObject *closobj = clos;')
			allargs.append('void *clos')

		elif 'in' in opts or not opts:
			# this is an incoming arg
			argname += '_in'
			informat.append(typ.format_char())
			try:
				inargs.append(typ.build_converter())
			except:
				pass
			inargs.append(argname)
			allargs.append(typ.decl(argname))

			# the arena value can only be an inarg
			if av_arena is None and arg.tp == 'arena':
				av_arena = argname
			elif av_player is None and arg.tp == 'player':
				av_player = argname + '->arena'

		elif 'out' in opts:
			# this is an outgoing arg
			argname += '_out'
			vargname = argname + '_v'
			decls.append('\t%s;' % (typ.parse_decl(vargname)))
			outformat.append(typ.format_char())
			try:
				outargs.append(typ.parse_converter())
			except:
				pass
			outargs.append('&%s' % vargname)
			allargs.append(typ.ptr_decl(argname))
			extras3.append(typ.conv_to_buf(argname, vargname))

		elif 'inout' in opts:
			# this is both incoming and outgoing
			argname += '_inout'
			vargname = argname + '_v'
			if arg.tp == 'string':
				decls.append('\t%s;' % (typ.decl(vargname)))
				decls.append('\tint len_%s;' % vargname)
				outformat.append('s#')
			else:
				decls.append('\t%s;' % (typ.buf_decl(vargname)))
				outformat.append(typ.format_char())
			informat.append(typ.format_char())			
			try:
				inargs.append(typ.parse_converter())
			except:
				pass
			if isinstance(typ, type_string):
				inargs.append(argname)
			else:
				inargs.append(typ.ptr_val() + argname)
			try:
				outargs.append(typ.build_converter())
			except:
				pass
			outargs.append('&%s' % vargname)
			allargs.append(typ.ptr_decl(argname))
			if arg.tp == 'string':
				outargs.append('&len_%s' % vargname)
				extras3.append('\tif(++len_%s < buflen) buflen = len_%s;' % (vargname, vargname))
			extras3.append(typ.conv_to_buf(argname, vargname))


		elif 'buflen' in opts:
			# this arg is a buffer length
			allargs.append('int %s' % argname)
			new = []
			for e in extras3:
				new.append(e.replace('buflen', argname))
			extras3 = new

	if inargs:
		inargs = ', ' + ', '.join(inargs)
	else:
		inargs = ''
	if outargs:
		outargs = ', ' + ', '.join(outargs)
	else:
		outargs = ''
	if allargs:
		allargs = ', '.join(allargs)
	else:
		allargs = 'void'
	informat = ''.join(informat)
	outformat = ''.join(outformat)
	decls = '\n'.join(decls)
	extras3 = '\n'.join(extras3)
	retdecl = rettype.decl('')
	if isinstance(rettype, type_playerlist):
		retdecl = "LinkedList *"

	if av_arena:
		arenaval = av_arena
	elif av_player:
		arenaval = av_player
		if name:
			print "warning: %s: guessing arena from player argument" % name
	else:
		arenaval = 'ALLARENAS'
	del av_arena
	del av_player
	del args
	del out
	del rettype

	return vars()


def create_py_to_c_func(func):
	args = func.args
	out = func.out

	informat = []
	outformat = []
	inargs = []
	outargs = []
	decls = []
	extras1 = []
	extras2 = []
	extras3 = []
	extracode = []
	allargs = []

	if out.tp == 'void':
		asgntoret = ''
	else:
		assert not out.flags
		typ = get_type(out.tp)
		argname = 'ret'
		decls.append('\t%s;' % typ.decl(argname))
		outformat.append(typ.format_char())
		try:
			outargs.append(typ.build_converter())
		except:
			pass
		outargs.append(argname)
		asgntoret = '%s = ' % argname

	idx = 0
	for arg in args:
		idx = idx + 1
		argname = 'arg%d' % idx

		opts = arg.flags

		typ = get_type(arg.tp)

		if arg.tp == 'void':
			continue

		elif arg.tp == 'formatted':
			# these are a little weird
			if idx != len(args):
				raise Exception, "formatted arg isn't last!"
			typ = get_type('string')
			argname += '_infmt'
			informat.append(typ.format_char())
			decls.append('\t%s;' % typ.decl(argname))
			inargs.append('&%s' % argname)
			allargs.append('"%s"')
			allargs.append('%s' % argname)

		elif arg.tp == 'clos':
			# hardcoded value. this depends on the existence of exactly
			# one function argument.
			allargs.append('cbfunc')

		elif is_func(arg.tp):
			cbfuncname = genid()
			informat.append('O')
			decls.append('\tPyObject *cbfunc;')
			inargs.append('&cbfunc')
			allargs.append(cbfuncname)
			extras1.append("""
	if (!PyCallable_Check(cbfunc))
	{
		PyErr_SetString(PyExc_TypeError, "func isn't callable");
		return NULL;
	}
""")
			if 'dynamic' in opts:
				extras1.append('\tPy_INCREF(cbfunc);')
				if 'failval' in opts:
					# provides a way to clean up the extra reference if
					# the return value from the function indicates that
					# the callback won't be called. otherwise, we expect
					# the callback to be called exactly once.
					failval = opts[opts.index('failval')+1]
					extras2.append(
							'\tif (ret == (%s)) { Py_DECREF(cbfunc); }' % failval)
				decref = '\tPy_DECREF(closobj);'
			else:
				decref = ''

			cbdict = create_c_to_py_func(None, arg.tp)
			cbdict['cbfuncname'] = cbfuncname
			cbdict['decref'] = decref
			cbcode = []
			cbcode.append("""
local %(retdecl)s %(cbfuncname)s(%(allargs)s)
{
	PyObject *args, *out;
%(decls)s
	args = Py_BuildValue("(%(informat)s)"%(inargs)s);
	if (!args)
	{
		log_py_exception(L_ERROR, "python error building args for "
				"interface argument function");
		return %(defretval)s;
	}
	out = PyObject_Call(closobj, args, NULL);
	Py_DECREF(args);
%(decref)s
	if (!out)
	{
		log_py_exception(L_ERROR, "python error calling "
				"interface argument function");
		return %(defretval)s;
	}
""")
			if cbdict['outargs']:
				cbcode.append("""
	if (!PyArg_ParseTuple(out, "%(outformat)s"%(outargs)s))
	{
		Py_DECREF(out);
		log_py_exception(L_ERROR, "python error unpacking results of "
				"interface argument function");
		return %(defretval)s;
	}
""")
			else:
				cbcode.append("""
	if (out != Py_None)
	{
		Py_DECREF(out);
		log_py_exception(L_ERROR, "interface argument function didn't return None as expected");
		return %(defretval)s;
	}
""")
			cbcode.append("""
%(extras3)s
	Py_DECREF(out);
}
""")
			cbcode = ''.join(cbcode) % cbdict
			extracode.append(cbcode)

		elif 'in' in opts or not opts:
			# this is an incoming arg
			argname += '_in'
			informat.append(typ.format_char())
			decls.append('\t%s;' % (typ.parse_decl(argname)))
			try:
				inargs.append(typ.parse_converter())
			except:
				pass
			if typ.format_char():
				inargs.append('&' + argname)
			allargs.append(typ.out_prefix() + argname)

		elif 'out' in opts:
			# this is an outgoing arg
			argname += '_out'
			if 'buflen' not in opts:
				outformat.append(typ.format_char())
			decls.append('\t%s;' % (typ.buf_decl_def(argname)))
			try:
				outargs.append(typ.build_converter())
			except:
				pass
			outargs.append(typ.out_prefix() + argname)
			allargs.append(typ.buf_prefix() + argname)

		elif 'inout' in opts:
			# this is both incoming and outgoing
			argname += '_inout'
			informat.append(typ.format_char())
			outformat.append(typ.format_char())
			decls.append('\t%s;' % (typ.buf_decl_def(argname)))
			if typ.format_char():
				try:
					inargs.append(typ.parse_converter())
				except:
					pass
				inargs.append('&' + argname)
			try:
				outargs.append(typ.build_converter())
			except:
				pass
			outargs.append(typ.out_prefix() + argname)
			allargs.append(typ.buf_prefix() + argname)

		elif 'buflen' in opts:
			# this arg is a buffer length
			# it must be passed in, so it's sort of like an
			# inarg, but it doesn't get parsed.
			allargs.append('%s' % DEFBUFLEN)

	decls = '\n'.join(decls)
	if inargs:
		inargs = ', ' + ', '.join(inargs)
	else:
		inargs = ''
	if outargs:
		outargs = ', ' + ', '.join(outargs)
	else:
		outargs = ''
	informat = ''.join(informat)
	outformat = ''.join(outformat)
	allargs = ', '.join(allargs)
	extras1 = '\n'.join(extras1)
	extras2 = '\n'.join(extras2)
	extras3 = '\n'.join(extras3)
	extracode = '\n'.join(extracode)

	del args
	del out
	del idx

	return vars()



# functions for asss<->python callback translation

pycb_cb_names = []

def translate_pycb(name, ctype, line):
	pycb_cb_names.append((name, ctype))

	tokens = tokenize_signature(line + '->void')
	func = parse_func(tokens)

	funcname = 'py_cb_%s' % name
	cbvars = create_c_to_py_func(name, func)
	cbvars.update(vars())

	# an optimization: if we have outargs, we need to build the arg
	# tuple every time to get the updated values, but if we don't, we
	# can reuse the tuple.
	outargs = cbvars['outargs']

	buildcode = """
	args = Py_BuildValue("(%(informat)s)"%(inargs)s);
	if (!args)
	{
		log_py_exception(L_ERROR, "python error building args for "
			"callback %(name)s");
		return;
	}
"""
	decref = """
	Py_DECREF(args);
"""

	code = []
	code.append("""
local %(retdecl)s %(funcname)s(%(allargs)s)
{
	PyObject *args, *out;
	LinkedList cbs = LL_INITIALIZER;
	Link *l;
%(decls)s
	mm->LookupCallback(PYCBPREFIX %(name)s, %(arenaval)s, &cbs);
	if (LLIsEmpty(&cbs))
		return;
""")
	if not outargs:
		code.append(buildcode)
	code.append("""
	for (l = LLGetHead(&cbs); l; l = l->next)
	{
""")
	if outargs:
		code.append(buildcode)
	code.append("""
		out = PyObject_Call(l->data, args, NULL);
""")
	if outargs:
		code.append(decref)
	code.append("""
		if (!out)
		{
			log_py_exception(L_ERROR, "python error calling "
				"callback %(name)s");
			continue;
		}
""")
	if outargs:
		code.append("""
		if (!PyArg_ParseTuple(out, "%(outformat)s"%(outargs)s))
		{
			Py_DECREF(out);
			log_py_exception(L_ERROR, "python error unpacking results of "
				"callback %(name)s");
			continue;
		}
""")
	else:
		code.append("""
		if (out != Py_None)
		{
			Py_DECREF(out);
			log_py_exception(L_ERROR, "callback %(name)s didn't return None as expected");
			continue;
		}
""")
	code.append("""
		Py_DECREF(out);
%(extras3)s
	}
""")
	if not outargs:
		code.append(decref)
	code.append("""
	LLEmpty(&cbs);
}
""")
	code = ''.join(code) % cbvars
	callback_file.write(code)

	cbvars = create_py_to_c_func(func)
	cbvars['name'] = name
	cbvars['ctype'] = ctype
	code = """
local PyObject * py_cb_call_%(name)s(Arena *arena, PyObject *args)
{
	PyObject *out;
%(decls)s
	if (!PyArg_ParseTuple(args, "%(informat)s"%(inargs)s))
		return NULL;
%(extras1)s
	DO_CBS(%(name)s, arena, %(ctype)s, (%(allargs)s));
%(extras2)s
	out = Py_BuildValue("%(outformat)s"%(outargs)s);
%(extras3)s
	return out;
}

""" % cbvars
	callback_file.write(code)


def finish_pycb():
	callback_file.write("""
typedef PyObject * (*py_cb_caller)(Arena *arena, PyObject *args);
local HashTable *py_cb_callers;

local void init_py_callbacks(void)
{
	py_cb_callers = HashAlloc();
""")
	for n, ctype in pycb_cb_names:
		callback_file.write("	{ %s typecheck = py_cb_%s; (void)typecheck; }\n" % (ctype, n))
		callback_file.write("	mm->RegCallback(%s, py_cb_%s, ALLARENAS);\n" % (n, n))
		callback_file.write("	HashReplace(py_cb_callers, PYCBPREFIX %s, py_cb_call_%s);\n" % (n, n))
	callback_file.write("""\
}

local void deinit_py_callbacks(void)
{
""")
	for n, _ in pycb_cb_names:
		callback_file.write("	mm->UnregCallback(%s, py_cb_%s, ALLARENAS);\n" % (n, n))
	callback_file.write("""\
	HashFree(py_cb_callers);
}
""")


# translating interfaces asss<->python

def init_pyint():
	out = int_file
	out.write("""
/* pyint declarations */

local void pyint_generic_dealloc(pyint_generic_interface_object *self)
{
	mm->ReleaseInterface(self->i);
	PyObject_Del(self);
}

""")

pyint_init_code = []

def translate_pyint(iid, ifstruct, dirs):

	_, allowed = dirs.pop(0)
	allowed = map(string.strip, allowed.split(','))
	if 'use' in allowed:

		objstructname = 'pyint_obj_%s' % iid

		methods = []
		methoddecls = []
		members = []
		memberdecls = []

		for name, thing in dirs:
			try:
				tokens = tokenize_signature(thing)
				func = parse_func(tokens)
			except:
				print "couldn't parse '%s'" % thing
				continue

			dict = create_py_to_c_func(func)
			mthdname = 'pyint_method_%s_%s' % (iid, name)
			dict.update(vars())
			mthd = """
%(extracode)s
local PyObject *
%(mthdname)s(%(objstructname)s *me, PyObject *args)
{
	PyObject *out;
%(decls)s
	if (!PyArg_ParseTuple(args, "%(informat)s"%(inargs)s))
		return NULL;
%(extras1)s
	%(asgntoret)sme->i->%(name)s(%(allargs)s);
%(extras2)s
	out = Py_BuildValue("%(outformat)s"%(outargs)s);
%(extras3)s
	return out;
}
""" % dict
			methods.append(mthd)

			decl = """\
	{"%(name)s", (PyCFunction)%(mthdname)s, METH_VARARGS, NULL },
""" % vars()
			methoddecls.append(decl)


		objstructdecl = """
typedef struct {
	PyObject_HEAD
	%(ifstruct)s *i;
} %(objstructname)s;
""" % vars()
		methoddeclname = 'pyint_%s_methods' % iid
		methoddeclstart = """
local PyMethodDef %(methoddeclname)s[] = {
""" % vars()
		methoddeclend = """\
	{NULL}
};
""" % vars()
		memberdeclname = 'pyint_%s_members' % iid
		memberdeclstart = """
local PyMemberDef %(memberdeclname)s[] = {
""" % vars()
		memberdeclend = """\
	{NULL}
};
""" % vars()
		typestructname = 'pyint_%s_type' % iid
		typedecl = """
local PyTypeObject %(typestructname)s = {
	PyObject_HEAD_INIT(NULL)
	0,                         /*ob_size*/
	"asss.%(ifstruct)s",       /*tp_name*/
	sizeof(%(objstructname)s), /*tp_basicsize*/
	0,                         /*tp_itemsize*/
	(destructor)pyint_generic_dealloc, /*tp_dealloc*/
	0,                         /*tp_print*/
	0,                         /*tp_getattr*/
	0,                         /*tp_setattr*/
	0,                         /*tp_compare*/
	0,                         /*tp_repr*/
	0,                         /*tp_as_number*/
	0,                         /*tp_as_sequence*/
	0,                         /*tp_as_mapping*/
	0,                         /*tp_hash */
	0,                         /*tp_call*/
	0,                         /*tp_str*/
	0,                         /*tp_getattro*/
	0,                         /*tp_setattro*/
	0,                         /*tp_as_buffer*/
	Py_TPFLAGS_DEFAULT,        /*tp_flags*/
	0,                         /*tp_doc */
	0,                         /*tp_traverse */
	0,                         /*tp_clear */
	0,                         /*tp_richcompare*/
	0,                         /*tp_weaklistoffset*/
	0,                         /*tp_iter*/
	0,                         /*tp_iternext*/
	%(methoddeclname)s,        /*tp_methods*/
	%(memberdeclname)s,        /*tp_members*/
	/* rest are null */
};
""" % vars()
		doinit = """\
	if (PyType_Ready(&%(typestructname)s) < 0) return;
	HashReplace(pyint_ints, %(iid)s, &%(typestructname)s);
""" % vars()
		pyint_init_code.append(doinit)

		int_file.write('\n/* using interface %(iid)s from python {{{ */\n' % vars())
		int_file.write(objstructdecl)
		for m in methods:
			int_file.write(m)
		int_file.write(methoddeclstart)
		for m in methoddecls:
			int_file.write(m)
		int_file.write(methoddeclend)
		for m in members:
			int_file.write(m)
		int_file.write(memberdeclstart)
		for m in memberdecls:
			int_file.write(m)
		int_file.write(memberdeclend)
		int_file.write(typedecl)
		int_file.write('\n/* }}} */\n')

	if 'impl' in allowed:

		ifacename = 'pyint_int_%s' % iid

		funcs = []
		funcnames = []
		lastout = None

		for name, thing in dirs:
			try:
				tokens = tokenize_signature(thing)
				func = parse_func(tokens)
			except:
				print "bad declaration '%s'" % thing
				continue
			funcname = 'pyint_func_%s_%s' % (iid, name)
			funcdict = create_c_to_py_func('%s::%s' % (iid, name), func)
			funcdict.update(vars())
			code = []
			code.append("""
local %(retdecl)s %(funcname)s(%(allargs)s)
{
	PyObject *args, *out;
%(decls)s
	args = Py_BuildValue("(%(informat)s)"%(inargs)s);
	if (!args)
	{
		log_py_exception(L_ERROR, "python error building args for "
				"function %(name)s in interface %(iid)s");
		return %(defretval)s;
	}
	out = call_gen_py_interface(PYINTPREFIX %(iid)s, "%(name)s", args, %(arenaval)s);
	if (!out)
	{
		log_py_exception(L_ERROR, "python error calling "
				"function %(name)s in interface %(iid)s");
		return %(defretval)s;
	}
""")
			if funcdict['outargs']:
				code.append("""
	if (!PyArg_ParseTuple(out, "%(outformat)s"%(outargs)s))
	{
		Py_DECREF(out);
		log_py_exception(L_ERROR, "python error unpacking results of "
				"function %(name)s in interface %(iid)s");
		return %(defretval)s;
	}
""")
			else:
				code.append("""
	if (out != Py_None)
	{
		Py_DECREF(out);
		log_py_exception(L_ERROR, "interface func %(name)s didn't return None as expected");
		return %(defretval)s;
	}
""")
			code.append("""
%(extras3)s
	Py_DECREF(out);
	return %(retorblank)s;
}
""")
			code = ''.join(code) % funcdict
			funcs.append(code)
			funcnames.append(funcname)

		funcnames = ',\n\t'.join(funcnames)
		ifstructdecl = """
local struct %(ifstruct)s %(ifacename)s = {
	INTERFACE_HEAD_INIT(%(iid)s, "pyint-%(iid)s")
	%(funcnames)s
};

""" % vars()
		init = "\tHashReplace(pyint_impl_ints, PYINTPREFIX %(iid)s, &%(ifacename)s);\n" % vars()
		pyint_init_code.append(init)

		int_file.write('\n/* implementing interface %(iid)s in python {{{ */\n' % vars())
		for func in funcs:
			int_file.write(func)
		int_file.write(ifstructdecl)
		int_file.write('\n/* }}} */\n')



def finish_pyint():
	out = int_file
	out.write("""
local HashTable *pyint_ints;
local HashTable *pyint_impl_ints;

local void init_py_interfaces(void)
{
	pyint_ints = HashAlloc();
	pyint_impl_ints = HashAlloc();
""")
	for line in pyint_init_code:
		out.write(line)
	out.write("""\
}

local void deinit_py_interfaces(void)
{
""")
	out.write("""\
	HashFree(pyint_ints);
	HashFree(pyint_impl_ints);
}
""")

# translating advisers asss<->python

def init_pyadv():
	out = adv_file
	out.write("""
/* pyadv declarations */

local void pyadv_generic_dealloc(pyint_generic_adviser_object *self)
{
	mm->UnregAdviser(self->adv);/*TODO i -> adv*/
	PyObject_Del(self);
}

""")

pyadv_init_code = []

def translate_pyadv(aid, ifstruct, dirs):

	_, allowed = dirs.pop(0)
	allowed = map(string.strip, allowed.split(','))
	if 'use' in allowed:

		objstructname = 'pyadv_obj_%s' % aid

		methods = []
		methoddecls = []
		members = []
		memberdecls = []

		for name, thing in dirs:
			try:
				tokens = tokenize_signature(thing)
				func = parse_func(tokens)
			except:
				print "couldn't parse '%s'" % thing
				continue

			dict = create_py_to_c_func(func)
			mthdname = 'pyadv_method_%s_%s' % (aid, name)
			dict.update(vars())
			mthd = """
%(extracode)s
local PyObject *
%(mthdname)s(%(objstructname)s *me, PyObject *args)
{
	PyObject *out;
%(decls)s
	if (!PyArg_ParseTuple(args, "%(informat)s"%(inargs)s))
		return NULL;
%(extras1)s
	%(asgntoret)sme->i->%(name)s(%(allargs)s);
%(extras2)s
	out = Py_BuildValue("%(outformat)s"%(outargs)s);
%(extras3)s
	return out;
}
""" % dict
			methods.append(mthd)

			decl = """\
	{"%(name)s", (PyCFunction)%(mthdname)s, METH_VARARGS, NULL },
""" % vars()
			methoddecls.append(decl)


		objstructdecl = """
typedef struct {
	PyObject_HEAD
	%(ifstruct)s *i;
} %(objstructname)s;
""" % vars()
		methoddeclname = 'pyadv_%s_methods' % aid
		methoddeclstart = """
local PyMethodDef %(methoddeclname)s[] = {
""" % vars()
		methoddeclend = """\
	{NULL}
};
""" % vars()
		memberdeclname = 'pyadv_%s_members' % aid
		memberdeclstart = """
local PyMemberDef %(memberdeclname)s[] = {
""" % vars()
		memberdeclend = """\
	{NULL}
};
""" % vars()
		typestructname = 'pyadv_%s_type' % aid
		typedecl = """
local PyTypeObject %(typestructname)s = {
	PyObject_HEAD_INIT(NULL)
	0,                         /*ob_size*/
	"asss.%(ifstruct)s",       /*tp_name*/
	sizeof(%(objstructname)s), /*tp_basicsize*/
	0,                         /*tp_itemsize*/
	(destructor)pyadv_generic_dealloc, /*tp_dealloc*/
	0,                         /*tp_print*/
	0,                         /*tp_getattr*/
	0,                         /*tp_setattr*/
	0,                         /*tp_compare*/
	0,                         /*tp_repr*/
	0,                         /*tp_as_number*/
	0,                         /*tp_as_sequence*/
	0,                         /*tp_as_mapping*/
	0,                         /*tp_hash */
	0,                         /*tp_call*/
	0,                         /*tp_str*/
	0,                         /*tp_getattro*/
	0,                         /*tp_setattro*/
	0,                         /*tp_as_buffer*/
	Py_TPFLAGS_DEFAULT,        /*tp_flags*/
	0,                         /*tp_doc */
	0,                         /*tp_traverse */
	0,                         /*tp_clear */
	0,                         /*tp_richcompare*/
	0,                         /*tp_weaklistoffset*/
	0,                         /*tp_iter*/
	0,                         /*tp_iternext*/
	%(methoddeclname)s,        /*tp_methods*/
	%(memberdeclname)s,        /*tp_members*/
	/* rest are null */
};
""" % vars()
		doinit = """\
	if (PyType_Ready(&%(typestructname)s) < 0) return;
	HashReplace(pyadv_advs, %(aid)s, &%(typestructname)s);
""" % vars()
		pyadv_init_code.append(doinit)

		adv_file.write('\n/* using adviser %(aid)s from python {{{ */\n' % vars())
		adv_file.write(objstructdecl)
		for m in methods:
			adv_file.write(m)
		adv_file.write(methoddeclstart)
		for m in methoddecls:
			adv_file.write(m)
		adv_file.write(methoddeclend)
		for m in members:
			adv_file.write(m)
		adv_file.write(memberdeclstart)
		for m in memberdecls:
			adv_file.write(m)
		adv_file.write(memberdeclend)
		adv_file.write(typedecl)
		adv_file.write('\n/* }}} */\n')

	if 'impl' in allowed:

		advisname = 'pyadv_adv_%s' % aid

		funcs = []
		funcnames = []
		lastout = None

		for name, thing in dirs:
			try:
				tokens = tokenize_signature(thing)
				func = parse_func(tokens)
			except:
				print "bad declaration '%s'" % thing
				continue
			funcname = 'pyadv_func_%s_%s' % (aid, name)
			funcdict = create_c_to_py_func('%s::%s' % (aid, name), func)
			funcdict.update(vars())
			code = []
			code.append("""
local %(retdecl)s %(funcname)s(%(allargs)s)
{
	PyObject *args, *out;
%(decls)s
	args = Py_BuildValue("(%(informat)s)"%(inargs)s);
	if (!args)
	{
		log_py_exception(L_ERROR, "python error building args for "
				"function %(name)s in adviser %(aid)s");
		return %(defretval)s;
	}
	out = call_gen_py_adviser(PYADVPREFIX %(aid)s, "%(name)s", args, %(arenaval)s);
	if (!out)
	{
		log_py_exception(L_ERROR, "python error calling "
				"function %(name)s in adviser %(aid)s");
		return %(defretval)s;
	}
""")
			if funcdict['outargs']:
				code.append("""
	if (!PyArg_ParseTuple(out, "%(outformat)s"%(outargs)s))
	{
		Py_DECREF(out);
		log_py_exception(L_ERROR, "python error unpacking results of "
				"function %(name)s in adviser %(aid)s");
		return %(defretval)s;
	}
""")
			else:
				code.append("""
	if (out != Py_None)
	{
		Py_DECREF(out);
		log_py_exception(L_ERROR, "adviser func %(name)s didn't return None as expected");
		return %(defretval)s;
	}
""")
			code.append("""
%(extras3)s
	Py_DECREF(out);
	return %(retorblank)s;
}
""")
			code = ''.join(code) % funcdict
			funcs.append(code)
			funcnames.append(funcname)

		funcnames = ',\n\t'.join(funcnames)
		ifstructdecl = """
local struct %(ifstruct)s %(advisname)s = {
	ADVISER_HEAD_INIT(%(aid)s)
	%(funcnames)s
};

""" % vars()
		init = "\tHashReplace(pyadv_impl_ints, PYADVPREFIX %(aid)s, &%(advisname)s);\n" % vars()
		pyadv_init_code.append(init)

		adv_file.write('\n/* implementing adviser %(aid)s in python {{{ */\n' % vars())
		for func in funcs:
			adv_file.write(func)
		adv_file.write(ifstructdecl)
		adv_file.write('\n/* }}} */\n')



def finish_pyadv():
	out = adv_file
	out.write("""
local HashTable *pyadv_advs;
local HashTable *pyadv_impl_advs;

local void init_py_advisers(void)
{
	pyadv_advs = HashAlloc();
	pyadv_impl_advs = HashAlloc();
""")
	for line in pyadv_init_code:
		out.write(line)
	out.write("""\
}

local void deinit_py_advisers(void)
{
""")
	out.write("""\
	HashFree(pyadv_advs);
	HashFree(pyadv_impl_advs);
}
""")


def handle_pyconst_directive(tp, pat):
	def clear():
		del extra_patterns[:]

	pat = pat.replace('*', '[A-Z_0-9]*')

	if tp == 'enum':
		# these are always ints, and last until a line with a close-brace
		pat = r'\s*(%s)' % pat
		newre = re.compile(pat)
		extra_patterns.append((newre, const_int))
		extra_patterns.append((re.compile(r'.*}.*;.*'), clear))
	elif tp.startswith('define'):
		# these can be ints or strings, and last until a blank line
		pat = r'#define (%s)' % pat
		newre = re.compile(pat)
		subtp = tp.split()[1].strip()
		func = { 'int': const_int, 'string': const_string, }[subtp]
		extra_patterns.append((newre, func))
		extra_patterns.append((re.compile(r'^$'), clear))
	elif tp == 'config':
		pat = r'#define (%s)' % pat
		extra_patterns.append((re.compile(pat + ' "'), const_string))
		extra_patterns.append((re.compile(pat + ' [0-9]'), const_int))
		extra_patterns.append((re.compile(pat + '$'), const_one))
		extra_patterns.append((re.compile(r'/\* pyconst: config end \*/'), clear))


def add_converted_type(ctype, name, isstruct):
	class mytype(type_gen):
		def format_char(me):
			return 'O&'
		if isstruct:
			def decl(me, s):
				return '%s *%s' % (ctype, s)
			def ptr_decl(me, s):
				return '%s *%s' % (ctype, s)
		else:
			def decl(me, s):
				return '%s %s' % (ctype, s)
		if isstruct:
			def buf_decl(me, s):
				return '%s %s' % (ctype, s)
		if isstruct:
			def buf_init(me):
				return '{ 0 }';
		else:
			def buf_init(me):
				raise Exception()
		def build_converter(me):
			return 'cvt_c2p_' + name
		def parse_converter(me):
			return 'cvt_p2c_' + name
		if isstruct:
			def out_prefix(me):
				return '&'
		def ptr_val(me):
			return ''

	globals()['type_' + name] = mytype


def make_opaque_type(ctype, name):
	code = """
/* dummy value for uniqueness */
static int pytype_desc_%(name)s;

ATTR_UNUSED()
local PyObject * cvt_c2p_%(name)s(void *p)
{
	if (p == NULL)
	{
		Py_INCREF(Py_None);
		return Py_None;
	}
	else
		return PyCObject_FromVoidPtrAndDesc(p, &pytype_desc_%(name)s, NULL);
}

ATTR_UNUSED()
local int cvt_p2c_%(name)s(PyObject *o, void **pp)
{
	if (o == Py_None)
	{
		*pp = NULL;
		return TRUE;
	}
	else if (PyCObject_Check(o) &&
	         PyCObject_GetDesc(o) == &pytype_desc_%(name)s)
	{
		*pp = PyCObject_AsVoidPtr(o);
		return TRUE;
	}
	else
	{
		PyErr_Format(PyExc_TypeError, "arg isn't a '%%s'", "%(name)s");
		return FALSE;
	}
}
""" % vars()
	type_file.write(code)

	add_converted_type(ctype, name, 0)


def map_struct_type(tp):
	return {
		'Player': 'player',
		'Arena': 'arena',
		'ticks_t': 'int',
	}.get(tp, tp)

def getter_name(tp):
	return 'pytype_getter_%s' % tp

def setter_name(tp):
	return 'pytype_setter_%s' % tp

pytype_made_getsetters = {}
pytype_objects = []

def generate_getset_code(tp):
	if pytype_made_getsetters.has_key(tp):
		return []
	pytype_made_getsetters[tp] = 1

	typ = get_type(tp)
	if not typ:
		return []

	ctype = typ.decl('')
	getname = getter_name(tp)
	setname = setter_name(tp)

	getter = """
local PyObject * %(getname)s(PyObject *obj, void *v)
{
	int offset = (long)v;
	%(ctype)s *ptr = (%(ctype)s*)(((char*)obj)+offset);
""" % vars()
	try:
		getter += """\
	return %s(*ptr);
}
""" % typ.build_converter()
	except:
		getter += """\
	return %s;
}
""" % typ.to_obj('*ptr')

	setter = """
local int %(setname)s(PyObject *obj, PyObject *newval, void *v)
{
	int offset = (long)v;
	%(ctype)s *ptr = (%(ctype)s*)(((char*)obj)+offset);
""" % vars()

	if isinstance(typ,type_void):
		setter = """
local int %(setname)s(PyObject *obj, PyObject *newval, void *v)
{
	int offset = (long)v;
	int *ptr = (int*)(((char*)obj)+offset);
""" % vars()

	try:
		setter += """\
	return %s(newval, ptr) ? 0 : -1;
}
""" % typ.parse_converter()
	except:
		setter += """\
	*ptr = %s;
	return 0;
}
""" % typ.from_obj('newval')

	return [getter, setter]


class struct_context:
	def __init__(me, ctype, name):
		me.ctype = ctype
		me.name = name
		me.fields = []

	def finish(me):
		del extra_patterns[:]

		typeobj = 'pytype_%s_typeobj' % me.name
		pytype_objects.append((me.name, typeobj))
		objtype = 'pytype_%s_struct' % me.name
		getsetters = 'pytype_%s_getset' % me.name

		code = []
		code.append("""
typedef struct %(objtype)s
{
	PyObject_HEAD
	%(ctype)s data;
} %(objtype)s;
""")

		for tp, vname in me.fields:
			code.extend(generate_getset_code(tp))

		code.append("""
local PyGetSetDef %(getsetters)s[] =
{
""")

		for tp, vname in me.fields:
			code.append('\t{ "%s", %s, %s, NULL, (void*)offsetof(%s, data.%s) },\n' % (
				vname,
				getter_name(tp),
				setter_name(tp),
				objtype,
				vname))

		code.append("""
	{NULL}
};

local PyTypeObject %(typeobj)s =
{
	PyObject_HEAD_INIT(NULL)
	0,                          /* ob_size */
	"asss.%(name)s_wrapper",    /* tp_name */
	sizeof(%(objtype)s),        /* tp_basicsize */
	0,                          /* tp_itemsize */
	0,                          /* tp_dealloc */
	0,                          /* tp_print */
	0,                          /* tp_getattr */
	0,                          /* tp_setattr */
	0,                          /* tp_compare */
	0,                          /* tp_repr */
	0,                          /* tp_as_number */
	0,                          /* tp_as_sequence */
	0,                          /* tp_as_mapping */
	0,                          /* tp_hash  */
	0,                          /* tp_call */
	0,                          /* tp_str */
	0,                          /* tp_getattro */
	0,                          /* tp_setattro */
	0,                          /* tp_as_buffer */
	Py_TPFLAGS_DEFAULT,         /* tp_flags */
	"type object for %(name)s", /* tp_doc */
	0,                          /* tp_traverse */
	0,                          /* tp_clear */
	0,                          /* tp_richcompare */
	0,                          /* tp_weaklistoffset */
	0,                          /* tp_iter */
	0,                          /* tp_iternext */
	0,                          /* tp_methods */
	0,                          /* tp_members */
	%(getsetters)s,             /* tp_getset */
	0,                          /* tp_base */
	0,                          /* tp_dict */
	0,                          /* tp_descr_get */
	0,                          /* tp_descr_set */
	0,                          /* tp_dictoffset */
	0,                          /* tp_init */
	0,                          /* tp_alloc */
	0,                          /* tp_new */
};

ATTR_UNUSED()
local PyObject * cvt_c2p_%(name)s(void *p)
{
	if (p == NULL)
	{
		Py_INCREF(Py_None);
		return Py_None;
	}
	else
	{
		struct %(objtype)s *o = PyObject_New(%(objtype)s, &%(typeobj)s);
		if (o)
			o->data = *(%(ctype)s*)p;

		return (PyObject*)o;
	}
}

ATTR_UNUSED()
local int cvt_p2c_%(name)s(PyObject *o, void **pp)
{
	if (o->ob_type == &%(typeobj)s)
	{
		*(%(ctype)s*)pp = ((%(objtype)s*)o)->data;
		return TRUE;
	}
	else
	{
		PyErr_Format(PyExc_TypeError, "arg isn't a '%%s'", "%(name)s");
		return FALSE;
	}
}
""")
		dict = vars()
		dict['name'] = me.name
		dict['ctype'] = me.ctype
		type_file.write(''.join(code) % dict)

	def handle_line(me, unsigned, tp, vs):
		if unsigned:
			tp = 'u' + tp
		tp = map_struct_type(tp)
		vs = [v.strip(' \t*') for v in vs.split(',')]
		for v in vs:
			me.fields.append((tp, v))


def start_struct_type(ctype, name):
	ctx = struct_context(ctype, name)

	extra_patterns.append((re.compile(r'\s*(unsigned\s+)?([A-Za-z_0-9]+)\s+([^;]+);'),
		ctx.handle_line))
	extra_patterns.append((re.compile(r'.*}.*;.*'), ctx.finish))

	add_converted_type(ctype, name, 1)


def handle_pytype(line):
	things = map(string.strip, line.split(','))
	if things[0] == 'opaque':
		make_opaque_type(things[1], things[2])
	elif things[0] == 'struct':
		start_struct_type(things[1], things[2])

def finish_pytype():
	type_file.write("""

local int ready_generated_types(void)
{
""")
	for n, t in pytype_objects:
		type_file.write("""\
	%(t)s.tp_dealloc = (destructor)PyObject_Del;
	%(t)s.tp_new = PyType_GenericNew;
	if (PyType_Ready(&%(t)s) < 0) return -1;
""" % {'t': t})
	type_file.write("""
	return 0;
}

""")

	type_file.write("""

local void add_type_objects_to_module(PyObject *m)
{
""")
	for n, t in pytype_objects:
		type_file.write('\tPy_INCREF(&%s);\n' % (t))
		type_file.write('\tPyModule_AddObject(m, "%s", (PyObject*)&%s);\n' % (n, t))
	type_file.write("""
}

""")

# output files
prefix = sys.argv[1]
const_file = open(os.path.join(prefix, 'py_constants.inc'), 'w')
callback_file = open(os.path.join(prefix, 'py_callbacks.inc'), 'w')
int_file = open(os.path.join(prefix, 'py_interfaces.inc'), 'w')
adv_file = open(os.path.join(prefix, 'py_advisers.inc'), 'w')
type_file = open(os.path.join(prefix, 'py_types.inc'), 'w')
include_file = open(os.path.join(prefix, 'py_include.inc'), 'w')

warning = """
/* THIS IS AN AUTOMATICALLY GENERATED FILE */
"""

const_file.write(warning)
callback_file.write(warning)
int_file.write(warning)
type_file.write(warning)
include_file.write(warning)

type_file.write("""
typedef char charbuf[%d];
""" % DEFBUFLEN)

lines = []
for pat in sys.argv[2:]:
	for f in glob.glob(pat):
		lines.extend(map(lambda l: l.rstrip("\r\n"), open(f).readlines()))

# default constants
const_string('ASSSVERSION')
const_string('BUILDDATE')
const_int('ASSSVERSION_NUM')

const_int('TRUE')
const_int('FALSE')

init_pyint()
init_pyadv()

extra_patterns = []

# now process file
intdirs = []
advdirs = []
lastfunc = ''
lastadvfunc = ''

for l in lines:
	# includes
	m = re_pyinclude.match(l)
	if m:
		include_file.write('#include "%s"\n' % m.group(1).strip())

	# constants
	m = re_pyconst_dir.match(l)
	if m:
		handle_pyconst_directive(m.group(1), m.group(2))

	# callbacks
	m = re_pycb_cbdef.match(l)
	if m:
		lastcbdef = m.group(1)
	m = re_pycb_typedef.match(l)
	if m:
		lasttypedef = m.group(1)
	m = re_pycb_dir.match(l)
	if m:
		const_callback(lastcbdef)
		translate_pycb(lastcbdef, lasttypedef, m.group(1))

	# interfaces
	m = re_pyint_intdef.match(l)
	if m:
		lastintdef = m.group(1)
		intdirs = []
	m = re_pyint_typedef.match(l)
	if m:
		lasttypedef = m.group(1)
	m = re_pyint_func.match(l)
	if m:
		lastfunc = m.group(2)
	m = re_pyint_dir.match(l)
	if m:
		intdirs.append((lastfunc, m.group(1)))
	m = re_pyint_done.match(l)
	if m:
		if intdirs:
			const_interface(lastintdef)
			translate_pyint(lastintdef, lasttypedef, intdirs)
			intdirs = []
			
	# advisers
	m = re_pyadv_advdef.match(l)
	if m:
		lastadvdef = m.group(1)
		advdirs = []
	m = re_pyadv_typedef.match(l)
	if m:
		lastadvtypedef = m.group(1)
	m = re_pyadv_func.match(l)
	if m:
		lastadvfunc = m.group(2)
	m = re_pyadv_dir.match(l)
	if m:
		advdirs.append((lastadvfunc, m.group(1)))
	m = re_pyadv_done.match(l)
	if m:
		if advdirs:
			const_adviser(lastadvdef)
			translate_pyadv(lastadvdef, lastadvtypedef, advdirs)
			advdirs = []
	
	# types
	m = re_pytype.match(l)
	if m:
		handle_pytype(m.group(1))

	# generic stuff
	for myre, func in extra_patterns:
		m = myre.match(l)
		if m:
			apply(func, m.groups())


finish_pycb()
finish_pyint()
finish_pyadv()
finish_pytype()

