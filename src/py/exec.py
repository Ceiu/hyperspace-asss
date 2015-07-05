
# dist: public

import sys, cStringIO, asss

env = {}
env.update(asss.__dict__)

chat = asss.get_interface(asss.I_CHAT)
orig_sys_stdout = sys.stdout

def c_py(cmd, params, player, targ):
	"""\
Module: <py> exec
Targets: any
Args: <python code>
Executes arbitrary python code. The code runs in a namespace containing
all of the asss module, plus three helpful preset variables: {me} is
yourself, {t} is the target of the command, and {a} is the current
arena. You can write multi-line statements by separating lines with
semicolons (be sure to get the indentation correct). Output written to
stdout (e.g., with print) is captured and displayed to you, as are any
exceptions raised in your code.
"""
	params = params.replace(';', '\n')
	output = sys.stdout = cStringIO.StringIO()
	env['me'] = player
	env['t'] = targ
	env['a'] = player.arena

	try:
		exec params in env
	except:
		info = sys.exc_info()
		output.write('%s: %s\n' % \
				(getattr(info[0], '__name__', info[0]), info[1]))
		del info

	if hasattr(sys, 'exc_clear'): sys.exc_clear()
	sys.stdout = orig_sys_stdout
	del env['me']
	del env['t']
	del env['a']

	for l in output.getvalue().splitlines():
		chat.SendMessage(player, l)

ref = asss.add_command('py', c_py)

