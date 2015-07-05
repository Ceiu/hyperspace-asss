#!/usr/bin/env python
# dist: public

import sys, re, string, glob


re_helptext = re.compile(r"^local helptext_t ([a-z_]*)_help =$")
re_crap = re.compile(r'"?(.*?)(\\n)?"?;?$')
re_targets = re.compile(r"Targets: (.*)")
re_args = re.compile(r"Args: (.*)")
re_module = re.compile(r"Module: (.*)")
re_braces = re.compile(r"({.*?})")

re_py_cmddef = re.compile(r"^def c_([a-z_]*)\(")
re_py_quote = re.compile(r"'''" + r'|' + r'"""')


def rem_crap(l):
	m = re_crap.match(l)
	if m:
		return m.group(1)
	else:
		return l


checks = [
	(re_targets, 'targets'),
	(re_args, 'args'),
	(re_module, 'requiremod')
]

def escape(s):
	# replace < and > with math symbols
	s = s.replace('<', '\\lt{}')
	s = s.replace('>', '\\gt{}')

	# handle other punctuation
	s = s.replace('_', '\\_')
	s = s.replace('#', '\\#')
	s = s.replace('$', '\\$')
	s = s.replace('^', '\\^')
	s = s.replace('%', '\\%')
	s = s.replace('|', '\\vbar{}')

	return s


def print_line(line):
	# replace braces with texttt expressions
	line = re_braces.sub(r"\\texttt\1", line)

	line = escape(line)

	# check itemized lists
	if line.startswith(' * '):
		return '\\item ' + line + '\n'

	# now check for targets/args/module
	for ex, repl in checks:
		m = ex.match(line)
		if m:
			return '\\%s{%s}\n' % (repl, m.group(1))

	# otherwise, just plain text
	return line + '\n'


def print_doc(cmd, text):
	initem = 0
	out = ''
	out += "\\command{%s}\n" % escape(cmd)
	for line in text:
		if line.startswith(' * '):
			line = '\\item ' + line[3:]
			if not initem:
				out += '\\begin{itemize}\n'
				initem = 1
		elif initem:
			out += '\\end{itemize}\n'
			initem = 0
		out += print_line(line)
	if initem:
		out += '\\end{itemize}\n'
	out += '\n'
	return out


def extract_docs(lines):
	docs = {}
	i = 0
	while i < len(lines):
		l = lines[i]

		m = re_helptext.match(l)
		if m:
			# found a command
			cmdname = m.group(1)

			# extract lines until one ends in ;
			text = []
			i = i + 1
			while not lines[i].endswith(';'):
				text.append(lines[i])
				i = i + 1
			text.append(lines[i])
			i = i + 1

			# remove crap
			text = map(rem_crap, text)

			# output docs
			docs[cmdname] = print_doc(cmdname, text)

		m = re_py_cmddef.match(l)
		if m:
			# found a command, in python
			cmdname = m.group(1)

			# make sure we have a multi-line quote
			i = i + 1
			if not re_py_quote.search(lines[i]):
				continue

			text = [lines[i]]
			i = i + 1
			while not re_py_quote.search(lines[i]):
				text.append(lines[i])
				i = i + 1
			text.append(lines[i])
			i = i + 1

			text = eval('\n'.join(text)).splitlines()
			docs[cmdname] = print_doc(cmdname, text)

		i = i + 1

	return docs


if __name__ == '__main__':
	# open output
	sys.stdout = open(sys.argv[1], 'w')

	# get input
	lines = []
	for pat in sys.argv[2:]:
		for f in glob.glob(pat):
			lines.extend(map(string.strip, open(f).readlines()))

	docs = extract_docs(lines).items()
	docs.sort()
	for c, t in docs:
		sys.stdout.write(t)

