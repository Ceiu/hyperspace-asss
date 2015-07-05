#!/usr/bin/env python

import sys, os, base64, re, textwrap

BRANCH = 'asss.asss.main'

def run(*words):
	return os.popen('monotone ' + ' '.join(words)).read()

def parse_certs(certdata):
	certs = {}
	cur_certname = ''
	cur_signer = ''
	cur_data = ''
	state = 'nocert'
	for l in certdata.splitlines():
		l = l.strip()
		if state == 'nocert':
			assert l.startswith('[rcert ')
			state = 'getcertname'
		elif state == 'getcertname':
			cur_certname = l
			state = 'getsigner'
		elif state == 'getsigner':
			cur_signer = l
			state = 'getdata'
		elif state == 'getdata':
			if l.endswith(']'):
				cur_data += l[:-1]
				certs[cur_certname] = \
						base64.decodestring(cur_data).strip()
				cur_certname = ''
				cur_signer = ''
				cur_data = ''
				state = 'getsig'
			else:
				cur_data += l
		elif state == 'getsig':
			if l == '[end]':
				state = 'nocert'
		else:
			assert not 'uh oh'

	assert state == 'nocert'
	return certs

def parse_revision(revdata):
	def get_filename(line):
		return re.search('"([^"]*)"', line).group(1)
	files = []
	seen = {}
	for l in revdata.splitlines():
		if l.startswith('add_file'):
			fn = get_filename(l)
			files.append('+%s' % fn)
			seen[fn] = 1
		elif l.startswith('delete_file'):
			fn = get_filename(l)
			files.append('-%s' % fn)
			seen[fn] = 1
		elif l.startswith('patch'):
			fn = get_filename(l)
			if fn not in seen:
				files.append('%s' % fn)
		elif l.startswith('rename_file'):
			renamefrom = get_filename(l)
		elif l.startswith('         to'):
			files.append('%s->%s' % (renamefrom, get_filename(l)))
	return files

def author_hook(certs):
	if '@' not in certs['author'] and certs['author'].startswith('d'):
		certs['author'] = 'grelminar@yahoo.com'

entries = {}

heads = run('automate heads', BRANCH).splitlines()

revs = heads + run('automate ancestors', *heads).splitlines()
every = int(len(revs)/80)

wrapper = textwrap.TextWrapper(width=72, initial_indent='files: ',
		subsequent_indent='  ')

for n, rev in enumerate(revs):
	certs = parse_certs(run('certs', rev))
	author_hook(certs)
	certs['rev'] = rev

	files = parse_revision(run('cat revision', rev))
	certs['files'] = wrapper.fill(' '.join(files))

	entry = """\
%(date)s    %(author)s    %(branch)s    %(rev)s

%(changelog)s

%(files)s


""" % certs
	entries[certs['date']] = entry

	# simple progress display
	if n % every == 0:
		sys.stderr.write('=')
sys.stderr.write('\n')

entries = entries.items()
entries.sort()
entries.reverse()
for _, entry in entries:
	sys.stdout.write(entry)

