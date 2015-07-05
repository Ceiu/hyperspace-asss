
/* dist: public */

#include <signal.h>
#include <pthread.h>
#include <assert.h>

/* nasty hack to avoid warning when using python versions 2.4 and above */
#undef _POSIX_C_SOURCE

/* another nasty hack */
#undef _XOPEN_SOURCE
#include "Python.h"
#include "structmember.h"

/* add define for Py_ssize_t on versions before Python2.5 */
#if PY_VERSION_HEX < 0x02050000
#include <unistd.h>
typedef ssize_t         Py_ssize_t;
#endif 

#include "asss.h"

#include "persist.h"
#include "log_file.h"
#include "net-client.h"
#include "filetrans.h"
#include "fake.h"
#include "clientset.h"
#include "cfghelp.h"
#include "banners.h"
#include "billing.h"
#include "jackpot.h"
#include "koth.h"
#include "watchdamage.h"
#include "objects.h"
#include "reldb.h"
#include "fg_wz.h"
#include "fg_turf.h"
#include "idle.h"
#include "redirect.h"
#include "brickwriter.h"

#include "turf/turf_reward.h"
#include "py_include.inc"

#define PYCBPREFIX "PY-"
#define PYINTPREFIX "PY-"

#ifndef WIN32
#define CHECK_SIGS
#endif


/* forward decls */

typedef struct PlayerObject
{
	PyObject_HEAD
	Player *p;
	PyObject *dict;
} PlayerObject;

typedef struct ArenaObject
{
	PyObject_HEAD
	Arena *a;
	PyObject *dict;
} ArenaObject;

typedef struct PlayerListObject
{
//blank?
} PlayerListObject;

local PyTypeObject PlayerType;
local PyTypeObject ArenaType;
local PyTypeObject PlayerListType;

typedef struct pdata
{
	PlayerObject *obj;
} pdata;

typedef struct adata
{
	ArenaObject *obj;
} adata;


/* our locals */
local Imodman *mm;
local Iplayerdata *pd;
local Iarenaman *aman;
local Ilogman *lm;
local Iconfig *cfg;
local Icmdman *cmd;
local Ipersist *persist;
local Imainloop *mainloop;

local int pdkey, adkey;
local int mods_loaded;

local PyObject *cPickle;
local PyObject *log_code;

pthread_mutex_t pymtx;
#define LOCK() pthread_mutex_lock(&pymtx)
#define UNLOCK() pthread_mutex_unlock(&pymtx)

/* utility functions */

local void init_log_py_code(void)
{
	PyObject *py_main;
	const char *runstring =
		"import traceback" "\n"
		"def asss_format_exception(e, v=None, tb=None): " "\n\t"
		"lines = traceback.format_exception(e, v, tb)" "\n\t"
		"lines = [line.rstrip('\\n') for line in lines]" "\n\t"
		"return '\\0'.join(lines).replace('\\n', '\\0')"
		;

	log_code = NULL;

	py_main = PyImport_AddModule("__main__");

	if (NULL == py_main)
	{
		lm->Log(L_ERROR, "<pymod> initialization of log code failed. "
				"error msgs will be limited (check stderr)");
		return;
	}

	PyRun_SimpleString(runstring);
	if (PyErr_Occurred()) PyErr_PrintEx(0);

	log_code = PyObject_GetAttrString(py_main, "asss_format_exception");
	if (PyErr_Occurred()) PyErr_PrintEx(0);
}

local void log_py_exception(char lev, const char *msg)
{
	PyObject *result, *e, *v, *tb, *args;
	char *line;
	Py_ssize_t str_size;
	int i;

	if (!PyErr_Occurred())
		return;

	if (msg)
		lm->Log(lev, "<pymod> EXC: MSG: %s", msg);

	if (NULL == log_code)
	{
		PyErr_PrintEx(0);
		return;
	}

	/* fetching also clears the error */
	PyErr_Fetch(&e, &v, &tb);
	PyErr_NormalizeException(&e, &v, &tb);

	if (v && tb)
	{
		args = Py_BuildValue("(OOO)", e, v, tb);
		result = PyObject_CallObject(log_code, args);
	}
	else
	{
		PyObject *kw_args = PyDict_New();
		int kw_fail = 0;

		if (!kw_args) return;

		args = Py_BuildValue("(O)", e);

		if (v)
		{
			if (-1 == PyDict_SetItemString(kw_args, "v", v))
				kw_fail = 1;
		}
		if (tb)
		{
			if (-1 == PyDict_SetItemString(kw_args, "tb", tb))
				kw_fail = 1;
		}

		if (kw_fail)
			result = PyObject_CallObject(log_code, args);
		else
			result = PyObject_Call(log_code, args, kw_args);

		Py_DECREF(kw_args);
	}

	Py_DECREF(e);
	Py_XDECREF(v);
	Py_XDECREF(tb);
	Py_DECREF(args);

	if (!result) return;

	line = PyString_AsString(result);
	str_size = PyString_Size(result);
	i = 0;
	while (i < str_size)
	{
		int k = strlen(line) + 1;

		lm->Log(lev, "<pymod> EXC: %s", line);

		i += k;
		line += k;
	}

	Py_DECREF(result);
}


/* converters */

local PyObject * cvt_c2p_player(Player *p)
{
	PyObject *o;
	if (p)
		o = (PyObject*)((pdata*)PPDATA(p, pdkey))->obj;
	else
		o = Py_None;
	Py_XINCREF(o);
	return o;
}

local int cvt_p2c_player(PyObject *o, Player **pp)
{
	if (o == Py_None)
	{
		*pp = NULL;
		return TRUE;
	}
	else if (o->ob_type == &PlayerType)
	{
		*pp = ((PlayerObject*)o)->p;
		return TRUE;
	}
	else
	{
		PyErr_SetString(PyExc_TypeError, "arg isn't a player object");
		return FALSE;
	}
}


local PyObject * cvt_c2p_arena(Arena *a)
{
	PyObject *o;
	if (a)
		o = (PyObject*)((adata*)P_ARENA_DATA(a, adkey))->obj;
	else
		o = Py_None;
	Py_XINCREF(o);
	return o;
}

local int cvt_p2c_arena(PyObject *o, Arena **ap)
{
	if (o == Py_None)
	{
		*ap = NULL;
		return TRUE;
	}
	else if (o->ob_type == &ArenaType)
	{
		*ap = ((ArenaObject*)o)->a;
		return TRUE;
	}
	else
	{
		PyErr_SetString(PyExc_TypeError, "arg isn't a arena object");
		return FALSE;
	}
}


local PyObject * cvt_c2p_banner(Banner *b)
{
	return PyString_FromStringAndSize((const char *)b, sizeof(*b));
}

local int cvt_p2c_banner(PyObject *o, Banner **bp)
{
	char *buf;
	Py_ssize_t len = -1;
	PyString_AsStringAndSize(o, &buf, &len);
	if (len == sizeof(Banner))
	{
		*bp = (Banner*)buf;
		return TRUE;
	}
	else
		return FALSE;
}

local PyObject * cvt_c2p_playerlist(LinkedList *list)
{
	PyObject *o;
	if(list)
	{
		o = PyObject_CallObject((PyObject *) &PlayerListType, NULL);
		Link *link;
		Player *p;

		LOCK();
		FOR_EACH(list, p, link)
		{
			if(p)
			{
				PyObject *po = cvt_c2p_player(p);
				Py_XDECREF(po);
				if(po && po != Py_None)
				{
					PyList_Append(o, po);
				}
			}
		}
		UNLOCK();
	}
	else
	{
		o = Py_None;
	}
	return o;
}



local int cvt_p2c_playerlist(PyObject *o, LinkedList **list)
{
	if(o && o->ob_type == &PlayerListType)
	{
		int size = PyList_Size(o);
		LOCK();
		for(int i = 0; i < size; i++)
		{
			PyObject *obj = PyList_GET_ITEM(o, i);
			Player *p;
			if(cvt_p2c_player(obj, &p) && p)
			{
				LLAdd(*list, p);
			}
		}
		UNLOCK();
		return TRUE;
	}
	else
	{
		PyErr_SetString(PyExc_TypeError, "arg isn't a player list object");
		return FALSE;
	}
}

local PyObject * cvt_c2p_target(Target *t)
{
	/* the representation of a target in python depends on which sort of
	 * target it is, and is generally lax. none is none. players and
	 * arenas are represented as themselves. freqs are (arena, freq)
	 * tuples. the zone is a special string. a list is a python list of
	 * players. */
	switch (t->type)
	{
		case T_NONE:
			Py_INCREF(Py_None);
			return Py_None;

		case T_PLAYER:
			return cvt_c2p_player(t->u.p);

		case T_ARENA:
			return cvt_c2p_arena(t->u.arena);

		case T_FREQ:
		{
			PyObject *obj = PyTuple_New(2);
			if (NULL == obj)
				return NULL;

			PyTuple_SET_ITEM(obj, 0, cvt_c2p_arena(t->u.freq.arena));
			PyTuple_SET_ITEM(obj, 1, PyInt_FromLong(t->u.freq.freq));
			return obj;
		}

		case T_ZONE:
			return PyString_FromString("zone");

		case T_LIST:
			return cvt_c2p_playerlist(&t->u.list);

		default:
			PyErr_SetString(PyExc_ValueError,
					"type tag for target is unknown. "
					"You probably need to re(compile|load) pymod.so");
			return NULL;
	}
}

local int cvt_p2c_target(PyObject *o, Target *t)
{
	if (o->ob_type == &PlayerType)
	{
		t->type = T_PLAYER;
		t->u.p = ((PlayerObject*)o)->p;
		return TRUE;
	}
	else if (o->ob_type == &ArenaType)
	{
		t->type = T_ARENA;
		t->u.arena = ((ArenaObject*)o)->a;
		return TRUE;
	}
	else if (PyTuple_Check(o))
	{
		t->type = T_FREQ;
		t->u.arena = ((ArenaObject*)o)->a;
		return PyArg_ParseTuple(o, "O&i",
				cvt_p2c_arena,
				&t->u.freq.arena,
				&t->u.freq.freq);
	}
	else if (PyString_Check(o))
	{
		const char *c = PyString_AsString(o);
		if (strcmp(c, "zone") == 0)
		{
			t->type = T_ZONE;
			return TRUE;
		}
		else
			return FALSE;
	}
	else if (o->ob_type == &PlayerListType)
	{
		t->type = T_LIST;
		LinkedList *list = (LinkedList *)&t->u.list;
		LLInit(list);
		return cvt_p2c_playerlist(o, &list);
	}
	else if (o == Py_None)
	{
		t->type = T_NONE;
		return TRUE;
	}
	else
		return FALSE;
}

local void close_config_file(void *v)
{
	cfg->CloseConfigFile(v);
}

local PyObject * cvt_c2p_config(ConfigHandle ch)
{
	return PyCObject_FromVoidPtr(ch, close_config_file);
}

local int cvt_p2c_config(PyObject *o, ConfigHandle *chp)
{
	if (o == Py_None)
	{
		*chp = GLOBAL;
		return TRUE;
	}
	else if (PyCObject_Check(o))
	{
		*chp = PyCObject_AsVoidPtr(o);
		return TRUE;
	}
	else
	{
		PyErr_SetString(PyExc_TypeError, "arg isn't a config handle object");
		return FALSE;
	}
}

/* these two functions are unused */
/*local PyObject * cvt_c2p_dict(HashTable *table)
{
	PyObject *o;
	if(table)
	{
		o = PyDict_New();
		const char *key;
		Link *l, *l2;
		void *value;
		CType *type;
		PyObject *v;

		LinkedList *keys = HashGetKeys(table);

		LOCK();
		FOR_EACH(keys, key, l)
		{
			LinkedList *values = HashGet(table, key);
			if(values)
			{
				LOCK();
				l2 = LLGetHead(values);
				if(l2)
				{
					type = (CType *)l2->data;
					l2 = l2->next;
					if(l2)
					{
						value = l2->data;

						switch(*type)
						{
							case CT_INTEGER:
								v = Py_BuildValue("i", *(long *)value);
								break;

							case CT_FLOAT:
								v = Py_BuildValue("f", *(double *)value);
								break;

							case CT_STRING:
								v = Py_BuildValue("s", value);
								break;

							case CT_VOID:
								v = PyCObject_FromVoidPtr(value, NULL);
								break;

							case CT_ARENA:
								v = cvt_c2p_arena(value);
								break;

							case CT_PLAYER:
								v = cvt_c2p_player(value);
								break;

							case CT_PLAYERLIST:
								v = cvt_c2p_playerlist(value);
								break;

							case CT_TARGET:
								v = cvt_c2p_target(value);
								break;

							default:
								v = Py_None;
								lm->Log(L_WARN, "Unknown CType given for key %s, using Py_None", key);
								break;
						}

						if(v)
						{
							PyDict_SetItemString(o, key, v);
							Py_XDECREF(v);
						}
						else
						{
							lm->Log(L_ERROR, "Error building value for key %s", key);
						}
					}
					else //skip
					{
						lm->Log(L_WARN, "No value given for key %s, skipping", key);
					}
				}
				else //skip
				{
					lm->Log(L_WARN, "Null CType given for key %s, skipping", key);
				}
				UNLOCK();
			}
		}
		UNLOCK();
	}
	else
	{
		o = Py_None;
	}
	return o;
}

local int cvt_p2c_dict(PyObject *o, HashTable **table)
{
	if(o == Py_None || !PyDict_Check(o))
	{
		PyErr_SetString(PyExc_ValueError, "Not a Dictionary object");
		return FALSE;
	}

	if(!*table)
	{
		PyErr_SetString(PyExc_ValueError, "Null HashTable provided");
		return FALSE;
	}

	PyObject *entry, *key, *value;
	PyObject *items = PyDict_Items(o);
	CType *type;
	void *data;
	const char *k;

	Py_ssize_t len = PyList_GET_SIZE(items);
	int i, ok = TRUE;
	for(i = 0; i < len; i++)
	{
		entry = PyList_GET_ITEM(items, i);
		key = PyTuple_GET_ITEM(entry, 0);
		value = PyTuple_GET_ITEM(entry, 1);

		type = amalloc(sizeof(CType));

		k = PyString_AsString(key);
		if(!k)
		{
			lm->Log(L_WARN, "Error parsing key, skipping");
			continue;
		}

		if(PyInt_Check(value))
		{
			*type = CT_INTEGER;
			data = amalloc(sizeof(long));
			*((long *)data) = PyInt_AsLong(value);
			ok = !PyErr_Occurred();
		}
		else if(PyFloat_Check(value))
		{
			*type = CT_FLOAT;
			data = amalloc(sizeof(double));
			*((double *)data) = PyFloat_AsDouble(value);
			ok = !PyErr_Occurred();
		}
		else if(PyString_Check(value))
		{
			*type = CT_STRING;
			Py_ssize_t buflen = PyString_Size(value);
			data = amalloc(sizeof(char) * buflen);
			char *temp = PyString_AsString(value);
			astrncpy(data, temp, buflen);
		}
		else if(PyCObject_Check(value))
		{
			*type = CT_VOID;
			data = PyCObject_AsVoidPtr(value);
		}
		else if (value->ob_type == &ArenaType)
		{
			*type = CT_ARENA;
			data = amalloc(sizeof(Arena));
			ok = cvt_p2c_arena(value, (Arena**)&data);
		}
		else if (value->ob_type == &PlayerType)
		{
			*type = CT_PLAYER;
			data = amalloc(sizeof(Player));
			ok = cvt_p2c_player(value, (Player**)&data);
		}
		else if (value->ob_type == &PlayerListType)
		{
			*type = CT_PLAYERLIST;
			data = LLAlloc();
			ok = cvt_p2c_playerlist(value, (LinkedList**)&data);
		}


		if(value == Py_None)
		{
			*type = CT_VOID;
			data = NULL;
		}
		else if(!data || !ok)
		{
			lm->Log(L_WARN, "Error parsing value for key %s, skipping", k);
			afree(data);
			afree(type);
			continue;
		}
		//target will appear as one of the above unless its a freq

		LOCK();
		HashAdd(*table, k, type);
		HashAdd(*table, k, data);
		UNLOCK();
	}

	return TRUE;
}*/


/* defining the asss module */

/* players */

local void Player_dealloc(PlayerObject *obj)
{
	Py_XDECREF(obj->dict);
	PyObject_Del(obj);
}

#define GET_AND_CHECK_PLAYER(var) \
	Player *var = ((PlayerObject*)obj)->p; \
	if (!var) { \
		PyErr_SetString(PyExc_ValueError, "stale player object"); \
		return NULL; \
	}

local PyObject *Player_get_pid(PyObject *obj, void *v)
{
	GET_AND_CHECK_PLAYER(p)
	return PyInt_FromLong(p->pid);
}

local PyObject *Player_get_status(PyObject *obj, void *v)
{
	GET_AND_CHECK_PLAYER(p)
	return PyInt_FromLong(p->status);
}

local PyObject *Player_get_type(PyObject *obj, void *v)
{
	GET_AND_CHECK_PLAYER(p)
	return PyInt_FromLong(p->type);
}

local PyObject *Player_get_arena(PyObject *obj, void *v)
{
	PyObject *ao;
	GET_AND_CHECK_PLAYER(p)
	ao = p->arena ?
		(PyObject*)(((adata*)P_ARENA_DATA(p->arena, adkey))->obj) :
		Py_None;
	Py_XINCREF(ao);
	return ao;
}

local PyObject *Player_get_name(PyObject *obj, void *v)
{
	GET_AND_CHECK_PLAYER(p)
	return PyString_FromString(p->name);
}

local PyObject *Player_get_squad(PyObject *obj, void *v)
{
	GET_AND_CHECK_PLAYER(p)
	return PyString_FromString(p->squad);
}

local PyObject *Player_get_ship(PyObject *obj, void *v)
{
	GET_AND_CHECK_PLAYER(p)
	return PyInt_FromLong(p->p_ship);
}

local PyObject *Player_get_freq(PyObject *obj, void *v)
{
	GET_AND_CHECK_PLAYER(p)
	return PyInt_FromLong(p->p_freq);
}

local PyObject *Player_get_res(PyObject *obj, void *v)
{
	GET_AND_CHECK_PLAYER(p)
	return Py_BuildValue("ii", p->xres, p->yres);
}

local PyObject *Player_get_onfor(PyObject *obj, void *v)
{
	int tm;
	GET_AND_CHECK_PLAYER(p)
	tm = TICK_DIFF(current_ticks(), p->connecttime);
	return PyLong_FromLong(tm/100);
}

local PyObject *Player_get_position(PyObject *obj, void *v)
{
	GET_AND_CHECK_PLAYER(p)
	return Py_BuildValue("iiiiiii",
			p->position.x,
			p->position.y,
			p->position.xspeed,
			p->position.yspeed,
			p->position.rotation,
			p->position.bounty,
			p->position.status);
}

local PyObject *Player_get_macid(PyObject *obj, void *v)
{
	GET_AND_CHECK_PLAYER(p)
	return PyLong_FromLong(p->macid);
}

local PyObject *Player_get_ipaddr(PyObject *obj, void *v)
{
	GET_AND_CHECK_PLAYER(p)
	return PyString_FromString(p->ipaddr);
}

local PyObject *Player_get_connectas(PyObject *obj, void *v)
{
	GET_AND_CHECK_PLAYER(p)
	if (p->connectas)
		return PyString_FromString(p->connectas);
	else
	{
		Py_INCREF(Py_None);
		return Py_None;
	}
}

local PyObject *Player_get_authenticated(PyObject *obj, void *v)
{
	GET_AND_CHECK_PLAYER(p)
	return PyInt_FromLong(p->flags.authenticated);
}

local PyObject *Player_get_flagscarried(PyObject *obj, void *v)
{
	GET_AND_CHECK_PLAYER(p)
	return PyInt_FromLong(p->pkt.flagscarried);
}

local PyGetSetDef Player_getseters[] =
{
#define SIMPLE_GETTER(n, doc) { #n, Player_get_ ## n, NULL, doc, NULL },
	SIMPLE_GETTER(pid, "player id")
	SIMPLE_GETTER(status, "current status")
	SIMPLE_GETTER(type, "client type (e.g., cont, 1.34, chat)")
	SIMPLE_GETTER(arena, "current arena")
	SIMPLE_GETTER(name, "player name")
	SIMPLE_GETTER(squad, "player squad")
	SIMPLE_GETTER(ship, "player ship")
	SIMPLE_GETTER(freq, "player freq")
	SIMPLE_GETTER(res, "screen resolution, returns tuple (x,y)")
	SIMPLE_GETTER(onfor, "seconds since login")
	SIMPLE_GETTER(position, "current position, returns tuple (x, y, v_x, v_y, rotation, bounty, status)")
	SIMPLE_GETTER(macid, "machine id")
	SIMPLE_GETTER(ipaddr, "client ip address")
	SIMPLE_GETTER(connectas, "which virtual server the client connected to")
	SIMPLE_GETTER(authenticated, "whether the client has been authenticated")
	SIMPLE_GETTER(flagscarried, "how many flags the player is carrying")
#undef SIMPLE_GETTER
	{NULL}
};

local PyTypeObject PlayerType =
{
	PyObject_HEAD_INIT(NULL)
	0,                         /*ob_size*/
	"asss.Player",             /*tp_name*/
	sizeof(PlayerObject),      /*tp_basicsize*/
	0,                         /*tp_itemsize*/
	(destructor)Player_dealloc,/*tp_dealloc*/
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
	"Player object",           /* tp_doc */
	0,                         /* tp_traverse */
	0,                         /* tp_clear */
	0,                         /* tp_richcompare */
	0,                         /* tp_weaklistoffset */
	0,                         /* tp_iter */
	0,                         /* tp_iternext */
	0,                         /* tp_methods */
	0,                         /* tp_members */
	Player_getseters,          /* tp_getset */
	0,                         /* tp_base */
	0,                         /* tp_dict */
	0,                         /* tp_descr_get */
	0,                         /* tp_descr_set */
	offsetof(PlayerObject, dict),  /* tp_dictoffset */
	0,                         /* tp_init */
	0,                         /* tp_alloc */
	0,                         /* tp_new */
};


/* arenas */

local void Arena_dealloc(ArenaObject *obj)
{
	Py_XDECREF(obj->dict);
	PyObject_Del(obj);
}

#define GET_AND_CHECK_ARENA(var) \
	Arena *var = ((ArenaObject*)obj)->a; \
	if (!var) { \
		PyErr_SetString(PyExc_ValueError, "stale arena object"); \
		return NULL; \
	}

local PyObject *Arena_get_status(PyObject *obj, void *v)
{
	GET_AND_CHECK_ARENA(a)
	return PyInt_FromLong(a->status);
}

local PyObject *Arena_get_name(PyObject *obj, void *v)
{
	GET_AND_CHECK_ARENA(a)
	return PyString_FromString(a->name);
}

local PyObject *Arena_get_basename(PyObject *obj, void *v)
{
	GET_AND_CHECK_ARENA(a)
	return PyString_FromString(a->basename);
}

local PyObject *Arena_get_cfg(PyObject *obj, void *v)
{
	GET_AND_CHECK_ARENA(a)
	return cvt_c2p_config(cfg->AddRef(a->cfg));
}

local PyObject *Arena_get_specfreq(PyObject *obj, void *v)
{
	GET_AND_CHECK_ARENA(a)
	return PyInt_FromLong(a->specfreq);
}

local PyObject *Arena_get_playing(PyObject *obj, void *v)
{
	GET_AND_CHECK_ARENA(a)
	return PyInt_FromLong(a->playing);
}

local PyObject *Arena_get_total(PyObject *obj, void *v)
{
	GET_AND_CHECK_ARENA(a)
	return PyInt_FromLong(a->total);
}


local PyGetSetDef Arena_getseters[] =
{
#define SIMPLE_GETTER(n, doc) { #n, Arena_get_ ## n, NULL, doc, NULL },
	SIMPLE_GETTER(status, "current status")
	SIMPLE_GETTER(name, "arena name")
	SIMPLE_GETTER(basename, "arena basename (without a number at the end)")
	SIMPLE_GETTER(cfg, "arena config file handle")
	SIMPLE_GETTER(specfreq, "the arena's spectator frequency")
	SIMPLE_GETTER(playing, "how many players are in ships in this arena")
	SIMPLE_GETTER(total, "how many players total are in this arena")
#undef SIMPLE_GETTER
	{NULL}
};

local PyTypeObject ArenaType =
{
	PyObject_HEAD_INIT(NULL)
	0,                         /*ob_size*/
	"asss.Arena",              /*tp_name*/
	sizeof(ArenaObject),       /*tp_basicsize*/
	0,                         /*tp_itemsize*/
	(destructor)Arena_dealloc, /*tp_dealloc*/
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
	"Arena object",            /* tp_doc */
	0,                         /* tp_traverse */
	0,                         /* tp_clear */
	0,                         /* tp_richcompare */
	0,                         /* tp_weaklistoffset */
	0,                         /* tp_iter */
	0,                         /* tp_iternext */
	0,                         /* tp_methods */
	0,                         /* tp_members */
	Arena_getseters,           /* tp_getset */
	0,                         /* tp_base */
	0,                         /* tp_dict */
	0,                         /* tp_descr_get */
	0,                         /* tp_descr_set */
	offsetof(ArenaObject, dict),  /* tp_dictoffset */
	0,                         /* tp_init */
	0,                         /* tp_alloc */
	0,                         /* tp_new */
};

/* player lists */

local PyObject *mthd_playerlist_append(PyObject *self, PyObject *args)
{
	Player *p;
	if (!PyArg_ParseTuple(args, "O&", cvt_p2c_player, &p)) return NULL;

	PyObject *obj = PyTuple_GetItem(args, 0);
	PyList_Append((PyObject *)self, obj);

	Py_RETURN_NONE;
}

local PyObject *mthd_playerlist_insert(PyObject *self, PyObject *args)
{
	Player *p;
	int index;
	if (!PyArg_ParseTuple(args, "iO&", &index, cvt_p2c_player, &p)) return NULL;

	PyObject *obj = PyTuple_GetItem(args, 1);
	PyList_Insert((PyObject *)self, index, obj);

	Py_RETURN_NONE;
}

local PyMethodDef player_list_methods[] =
{
	{"append", mthd_playerlist_append, METH_VARARGS,
		"Appends a player to the list"},
	{"insert", mthd_playerlist_insert, METH_VARARGS,
		"Inserts a player into to the list before an index"},
	{NULL, NULL}
};

local PyTypeObject PlayerListType =
{
	PyObject_HEAD_INIT(NULL)
	0,                         /*ob_size*/
	"asss.PlayerList",         /*tp_name*/
	sizeof(PlayerListObject),  /*tp_basicsize*/
	0,                         /*tp_itemsize*/
	0,                         /*tp_dealloc*/
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
	Py_TPFLAGS_DEFAULT |
	Py_TPFLAGS_BASETYPE,       /*tp_flags*/
	"Player list object",      /* tp_doc */
	0,                         /* tp_traverse */
	0,                         /* tp_clear */
	0,                         /* tp_richcompare */
	0,                         /* tp_weaklistoffset */
	0,                         /* tp_iter */
	0,                         /* tp_iternext */
	player_list_methods,       /* tp_methods */
	0,                         /* tp_members */
	0,                         /* tp_getset */
	0,                         /* tp_base */
	0,                         /* tp_dict */
	0,                         /* tp_descr_get */
	0,                         /* tp_descr_set */
	0,                         /* tp_dictoffset */
	0,                         /* tp_init */
	0,                         /* tp_alloc */
	0,                         /* tp_new */
};



/* associating asss objects with python objects */

typedef struct {
	PyObject_HEAD
	void *i;
} pyint_generic_interface_object;

typedef struct {
	INTERFACE_HEAD_DECL
#define PY_INT_MAGIC 0x13806503
	unsigned py_int_magic;
	void *real_interface;
	Arena *arena;
	PyObject *funcs;
} pyint_generic_interface;

local PyObject * call_gen_py_interface(const char *iid,
		const char *method, PyObject *args, Arena *arena)
/* steals ref to *args */
CPYCHECKER_STEALS_REFERENCE_TO_ARG(3);

local PyObject * call_gen_py_interface(const char *iid,
		const char *method, PyObject *args, Arena *arena)
{
	pyint_generic_interface *i;
	PyObject *ret = NULL;

	i = mm->GetInterface(iid, arena);
	if (i && i->py_int_magic == PY_INT_MAGIC)
	{
		if (i->funcs)
		{
			PyObject *func = PyObject_GetAttrString(i->funcs, (char*)method);
			if (func)
			{
				ret = PyObject_Call(func, args, NULL);
				Py_DECREF(func);
			}
		}
		else
			PyErr_SetString(PyExc_ValueError, "stale interface object");
	}
	else
	{
		PyErr_SetString(PyExc_ValueError, "interface invalid or not found");
	}

	/* do this in common code instead of repeating it for each stub. */
	Py_DECREF(args);
	/* the refcount on a python interface struct is meaningless, so
	 * don't bother releasing it. */
	return ret;
}

/* this is where most of the generated code gets inserted */
#include "py_types.inc"
#include "py_callbacks.inc"
#include "py_interfaces.inc"


local void py_newplayer(Player *p, int isnew)
{
	pdata *d = PPDATA(p, pdkey);
	if (isnew)
	{
		d->obj = PyObject_New(PlayerObject, &PlayerType);
		if (NULL == d->obj)
		{
			log_py_exception(L_ERROR, "PyObject_New failed in py_newplayer");
		}
		else
		{
			d->obj->p = p;
			d->obj->dict = PyDict_New();
		}
	}
	else
	{
		if (d->obj->ob_refcnt != 1)
			lm->Log(L_ERROR, "<pymod> there are %lu remaining references to a player object!",
					(unsigned long)d->obj->ob_refcnt);

		/* this stuff would usually be done in dealloc, but I want to
		 * make sure player objects for players who are gone are
		 * useless. specifically, all data assigned to misc. attributes
		 * of the player object should be deallocated and the object
		 * shouldn't be able to reference the asss player struct anymore
		 * (because it doesn't exist). */
		d->obj->p = NULL;
		Py_DECREF(d->obj->dict);
		d->obj->dict = NULL;

		/* this releases our reference */
		Py_DECREF(d->obj);
		d->obj = NULL;
	}
}


local void py_aaction(Arena *a, int action)
{
	adata *d = P_ARENA_DATA(a, adkey);

	if (action == AA_PRECREATE)
	{
		d->obj = PyObject_New(ArenaObject, &ArenaType);
		if (NULL == d->obj)
		{
			log_py_exception(L_ERROR, "PyObject_New failed in pa_aaction");
		}
		else
		{
			d->obj->a = a;
			d->obj->dict = PyDict_New();
		}
	}

	if (action == AA_POSTDESTROY && d->obj)
	{
		if (d->obj->ob_refcnt != 1)
			lm->Log(L_ERROR, "<pymod> there are %lu remaining references to an arena object!",
					(unsigned long)d->obj->ob_refcnt);

		/* see notes for py_newplayer as to why this is done here. */
		d->obj->a = NULL;
		Py_DECREF(d->obj->dict);
		d->obj->dict = NULL;

		/* this releases our reference */
		Py_DECREF(d->obj);
		d->obj = NULL;
	}
}



struct callback_ticket
{
	PyObject *func;
	Arena *arena;
	char cbid[1];
};

local void destroy_callback_ticket(void *v)
{
	struct callback_ticket *t = v;
	Py_DECREF(t->func);
	mm->UnregCallback(t->cbid, t->func, t->arena);
	afree(t);
}

local PyObject *mthd_reg_callback(PyObject *self, PyObject *args)
{
	char *rawcbid, pycbid[MAX_ID_LEN];
	PyObject *func;
	Arena *arena = ALLARENAS;
	struct callback_ticket *tkt;

	if (!PyArg_ParseTuple(args, "sO|O&", &rawcbid, &func, cvt_p2c_arena, &arena))
		return NULL;

	if (!PyCallable_Check(func))
	{
		PyErr_SetString(PyExc_TypeError, "func isn't callable");
		return NULL;
	}

	Py_INCREF(func);

	snprintf(pycbid, sizeof(pycbid), "%s%s", PYCBPREFIX, rawcbid);
	mm->RegCallback(pycbid, func, arena);

	tkt = amalloc(sizeof(*tkt) + strlen(pycbid));
	tkt->func = func;
	tkt->arena = arena;
	strcpy(tkt->cbid, pycbid);

	return PyCObject_FromVoidPtr(tkt, destroy_callback_ticket);
}


local PyObject *mthd_call_callback(PyObject *self, PyObject *args)
{
	char *rawcbid, pycbid[MAX_ID_LEN];
	PyObject *cbargs, *ret = NULL;
	Arena *arena = ALLARENAS;
	py_cb_caller func;

	if (!PyArg_ParseTuple(args, "sO|O&", &rawcbid, &cbargs, cvt_p2c_arena, &arena))
		return NULL;

	if (!PyTuple_Check(cbargs))
	{
		PyErr_SetString(PyExc_TypeError, "args isn't a tuple");
		return NULL;
	}

	snprintf(pycbid, sizeof(pycbid), "%s%s", PYCBPREFIX, rawcbid);
	func = HashGetOne(py_cb_callers, pycbid);
	if (func)
	{
		/* this is a callback defined in a C header file, so call the
		 * generated glue code. this might end up transferring control
		 * back into python. */
		ret = func(arena, cbargs);
	}
	else
	{
		/* this isn't defined in a C header file, so treat it as a
		 * python->python only callback, and manually call the
		 * registered handlers. */
		LinkedList cbs = LL_INITIALIZER;
		Link *l;
		mm->LookupCallback(pycbid, arena, &cbs);
		for (l = LLGetHead(&cbs); l; l = l->next)
		{
			PyObject * out = PyObject_Call(l->data, cbargs, NULL);
			if (!out)
				log_py_exception(L_ERROR, "python error calling py->py callback");
			else
			{
				if (out != Py_None)
					log_py_exception(L_ERROR, "callback didn't return None as expected");
				Py_DECREF(out);
			}
		}

		mm->FreeLookupResult(&cbs);

		ret = Py_BuildValue("");
	}

	return ret;
}


local PyObject *mthd_get_interface(PyObject *self, PyObject *args)
{
	char *iid;
	Arena *arena = ALLARENAS;
	pyint_generic_interface_object *o;
	PyTypeObject *typeo;

	if (!PyArg_ParseTuple(args, "s|O&", &iid, cvt_p2c_arena, &arena))
		return NULL;

	typeo = HashGetOne(pyint_ints, iid);
	if (typeo)
	{
		/* this is an interface defined in a C header file, so get the
		 * current C implementation and construct an interface object
		 * whose functions are generated glue code. */
		void *i = mm->GetInterface(iid, arena);
		if (i)
		{
			o = PyObject_New(pyint_generic_interface_object, typeo);

			if (o)
				o->i = i;

			return (PyObject*)o;
		}
		else
			return PyErr_Format(PyExc_Exception, "C interface %s wasn't found", iid);
	}
	else
	{
		/* this isn't defined in a C header file, so treat it as a
		 * python->python only interface, and return a reference to the
		 * implementation object. */
		char pyiid[MAX_ID_LEN];
		snprintf(pyiid, sizeof(pyiid), "%s%s", PYINTPREFIX, iid);
		pyint_generic_interface *i = mm->GetInterface(pyiid, arena);
		if (i && i->py_int_magic == PY_INT_MAGIC)
		{
			assert(i->real_interface == NULL);
			Py_INCREF(i->funcs);
			return i->funcs;
		}
		else
			return PyErr_Format(PyExc_Exception, "py->py interface %s wasn't found", iid);
	}
}


local void destroy_interface(void *v)
{
	pyint_generic_interface *i = v;
	int r;

	Py_DECREF(i->funcs);
	i->funcs = NULL;

	/* we haven't been properly maintaining this refcount, so zero it
	 * here so that the module manager won't complain. */
	i->head.global_refcount = 0;
	mm->UnregInterface(i, i->arena);

	if (i->real_interface)
	{
		if ((r = mm->UnregInterface(i->real_interface, i->arena)))
		{
			lm->Log(L_WARN, "<pymod> there are %d C remaining references"
					" to an interface implemented in python!", r);
			/* leak the interface struct. messy, but at least we have
			 * less chance of crashing this way. */
			return;
		}
	}

	afree(i->head.iid);
	afree(i);
}

local PyObject *mthd_reg_interface(PyObject *self, PyObject *args)
{
	char *rawiid, pyiid[MAX_ID_LEN];
	PyObject *obj, *iidobj;
	Arena *arena = ALLARENAS;
	pyint_generic_interface *newint;
	void *realint;

	if (!PyArg_ParseTuple(args, "O|O&", &obj, cvt_p2c_arena, &arena))
		return NULL;

	iidobj = PyObject_GetAttrString(obj, "iid");
	if (!iidobj || !(rawiid = PyString_AsString(iidobj)))
	{
		PyErr_SetString(PyExc_TypeError, "interface object missing 'iid'");
		Py_XDECREF(iidobj);
		return NULL;
	}

	snprintf(pyiid, sizeof(pyiid), "%s%s", PYINTPREFIX, rawiid);
	Py_DECREF(iidobj);
	realint = HashGetOne(pyint_impl_ints, pyiid);

	Py_INCREF(obj);

	newint = amalloc(sizeof(*newint));
	newint->head.magic = MODMAN_MAGIC;
	newint->head.iid = astrdup(pyiid);
	newint->head.name = "__unused__";
	newint->head.global_refcount = 0;
	newint->py_int_magic = PY_INT_MAGIC;
	newint->real_interface = realint;
	newint->arena = arena;
	newint->funcs = obj;

	/* this is the "real" one that other modules will make calls on */
	if (realint)
		mm->RegInterface(realint, arena);
	/* this is the dummy one registered with an iid that's only known
	 * internally to pymod. */
	mm->RegInterface(newint, arena);

	return PyCObject_FromVoidPtr(newint, destroy_interface);
}


/* commands */

local HashTable *pycmd_cmds;

local void init_py_commands(void)
{
	pycmd_cmds = HashAlloc();
}

local void deinit_py_commands(void)
{
	HashFree(pycmd_cmds);
}

local void pycmd_command(const char *tc, const char *params, Player *p, const Target *t)
{
	PyObject *args;
	LinkedList cmds = LL_INITIALIZER;
	Link *l;

	args = Py_BuildValue("ssO&O&",
			tc,
			params,
			cvt_c2p_player,
			p,
			cvt_c2p_target,
			t);
	if (args)
	{
		HashGetAppend(pycmd_cmds, tc, &cmds);
		for (l = LLGetHead(&cmds); l; l = l->next)
		{
			PyObject *ret = PyObject_Call(l->data, args, NULL);
			if (ret)
				Py_DECREF(ret);
			else
			{
				lm->Log(L_ERROR, "<pymod> error in command handler for '%s'", tc);
				log_py_exception(L_ERROR, NULL);
			}
		}
		Py_DECREF(args);
	}
}

struct pycmd_ticket
{
	PyObject *func, *htobj;
	Arena *arena;
	char cmd[1];
};

local void remove_command(void *v)
{
	struct pycmd_ticket *t = v;

	cmd->RemoveCommand(t->cmd, pycmd_command, t->arena);
	HashRemove(pycmd_cmds, t->cmd, t->func);
	Py_DECREF(t->func);
	Py_XDECREF(t->htobj);
	afree(t);
}

local PyObject *mthd_add_command(PyObject *self, PyObject *args)
{
	char *cmdname, *helptext = NULL;
	PyObject *func, *helptextobj;
	Arena *arena = ALLARENAS;
	struct pycmd_ticket *t;

	if (!PyArg_ParseTuple(args, "sO|O&", &cmdname, &func, cvt_p2c_arena, &arena))
		return NULL;

	if (!PyCallable_Check(func))
	{
		PyErr_SetString(PyExc_TypeError, "func isn't callable");
		return NULL;
	}

	Py_INCREF(func);

	t = amalloc(sizeof(*t) + strlen(cmdname));
	t->func = func;
	t->arena = arena;
	strcpy(t->cmd, cmdname);

	helptextobj = PyObject_GetAttrString(func, "__doc__");
	if (helptextobj && helptextobj != Py_None && PyString_Check(helptextobj))
	{
		t->htobj = helptextobj;
		helptext = PyString_AsString(helptextobj);
	}
	else
		Py_XDECREF(helptextobj);

	cmd->AddCommand(cmdname, pycmd_command, arena, helptext);
	HashAdd(pycmd_cmds, cmdname, func);

	return PyCObject_FromVoidPtr(t, remove_command);
}


/* timers */

local int pytmr_timer(void *v)
{
	PyObject *func = v, *ret;
	int goagain;

	ret = PyObject_CallFunction(func, NULL);
	if (!ret)
	{
		log_py_exception(L_ERROR, "error in python timer function");
		return FALSE;
	}

	goagain = ret == Py_None || PyObject_IsTrue(ret);
	Py_DECREF(ret);

	return goagain;
}

local void clear_timer(void *v)
{
	PyObject **funcptr = v;
	mainloop->ClearTimer(pytmr_timer, funcptr);
	Py_DECREF(*funcptr);
	afree(funcptr);
}

local PyObject *mthd_set_timer(PyObject *self, PyObject *args)
{
	PyObject *func, **funcptr;
	int initial, interval = -1;

	if (!PyArg_ParseTuple(args, "Oi|i", &func, &initial, &interval))
		return NULL;

	if (interval == -1)
		interval = initial;

	if (!PyCallable_Check(func))
	{
		PyErr_SetString(PyExc_TypeError, "func isn't callable");
		return NULL;
	}

	/* use an extra level of indirection for the key to ensure each
	 * timer can be destroyed indivudually even if multiple ones refer
	 * to the same python function. */
	funcptr = amalloc(sizeof(PyObject*));
	*funcptr = func;

	Py_INCREF(func);

	mainloop->SetTimer(pytmr_timer, initial, interval, func, funcptr);

	return PyCObject_FromVoidPtr(funcptr, clear_timer);
}


/* general persistent data */

local int persistent_data_common(PyObject *args,
		int *key, int *interval, int *scope, PyObject **funcs)
/* technically not always true. Callers should
 * if(!persistent_data_common(...)) return NULL; */
CPYCHECKER_SETS_EXCEPTION;

local int persistent_data_common(PyObject *args,
		int *key, int *interval, int *scope, PyObject **funcs)
{
	const char *attrs[] = { "get", "set", "clear", NULL };
	int i;
	PyObject *obj;

	if (!persist)
	{
		PyErr_SetString(PyExc_Exception, "the persist module isn't loaded");
		return FALSE;
	}

	if (!cPickle)
	{
		PyErr_SetString(PyExc_Exception, "cPickle isn't loaded");
		return FALSE;
	}

	if (!PyArg_ParseTuple(args, "O", funcs))
		return FALSE;

	for (i = 0; attrs[i]; i++)
	{
		PyObject *func = PyObject_GetAttrString(*funcs, (char*)attrs[i]);
		if (!func || !PyCallable_Check(func))
		{
			PyErr_SetString(PyExc_TypeError,
					"one of 'get', 'set', or 'clear' is missing or not callable");
			Py_XDECREF(func);
			return FALSE;
		}
		Py_DECREF(func);
	}

#define GET(name) \
	obj = PyObject_GetAttrString(*funcs, #name); \
	if (!obj || !PyInt_Check(obj)) \
	{ \
		PyErr_Format(PyExc_TypeError, "'%s' is missing or not integer", #name); \
		Py_XDECREF(obj); \
		return FALSE; \
	} \
	*name = PyInt_AsLong(obj); \
	Py_DECREF(obj)

	GET(key);
	GET(interval);
	GET(scope);

#undef GET

	if (*scope != PERSIST_ALLARENAS && *scope != PERSIST_GLOBAL)
	{
		PyErr_SetString(PyExc_ValueError, "scope must be PERSIST_ALLARENAS or PERSIST_GLOBAL");
		return FALSE;
	}

	if (*interval < 0 || *interval > MAX_INTERVAL)
	{
		PyErr_SetString(PyExc_ValueError, "interval out of range");
		return FALSE;
	}

	return TRUE;
}


/* player persistent data */

struct pypersist_ppd
{
	PlayerPersistentData ppd;
	PyObject *funcs;
};


local int get_player_data(Player *p, void *data, int len, void *v)
{
	struct pypersist_ppd *pyppd = v;
	PyObject *val, *pkl;
	const void *pkldata;
	Py_ssize_t pkllen;

	val = PyObject_CallMethod(pyppd->funcs, "get", "(O&)", cvt_c2p_player, p);
	if (!val)
	{
		log_py_exception(L_ERROR, "error in persistent data getter");
		return 0;
	}
	else if (val == Py_None)
	{
		Py_DECREF(val);
		return 0;
	}

	pkl = PyObject_CallMethod(cPickle, "dumps", "(Oi)", val, 1);
	Py_DECREF(val);
	if (!pkl)
	{
		log_py_exception(L_ERROR, "error pickling persistent data");
		return 0;
	}

	if (PyObject_AsReadBuffer(pkl, &pkldata, &pkllen))
	{
		Py_DECREF(pkl);
		lm->Log(L_ERROR, "<pymod> pickle result isn't buffer");
		return 0;
	}

	if (pkllen > len)
	{
		Py_DECREF(pkl);
		lm->Log(L_WARN, "<pymod> persistent data getter returned more "
				"than %lu bytes of data (%d allowed)",
				(unsigned long)pkllen, len);
		return 0;
	}

	memcpy(data, pkldata, pkllen);

	Py_DECREF(pkl);

	return pkllen;
}

local void set_player_data(Player *p, void *data, int len, void *v)
{
	struct pypersist_ppd *pyppd = v;
	PyObject *buf, *val, *ret;

	buf = PyString_FromStringAndSize(data, len);
	val = PyObject_CallMethod(cPickle, "loads", "(O)", buf);
	Py_XDECREF(buf);
	if (!val)
	{
		log_py_exception(L_ERROR, "can't unpickle persistent data");
		return;
	}

	ret = PyObject_CallMethod(pyppd->funcs, "set", "(O&O)", cvt_c2p_player, p, val);
	Py_DECREF(val);
	Py_XDECREF(ret);
	if (!ret)
		log_py_exception(L_ERROR, "error in persistent data setter");
}

local void clear_player_data(Player *p, void *v)
{
	struct pypersist_ppd *pyppd = v;
	PyObject *ret;

	ret = PyObject_CallMethod(pyppd->funcs, "clear", "(O&)", cvt_c2p_player, p);
	Py_XDECREF(ret);
	if (!ret)
		log_py_exception(L_ERROR, "error in persistent data clearer");
}


local void unreg_ppd(void *v)
{
	struct pypersist_ppd *pyppd = v;
	persist->UnregPlayerPD(&pyppd->ppd);
	Py_XDECREF(pyppd->funcs);
	afree(pyppd);
}

local PyObject * mthd_reg_ppd(PyObject *self, PyObject *args)
{
	struct pypersist_ppd *pyppd;
	int key, interval, scope;
	PyObject *funcs;

	if (!persistent_data_common(args, &key, &interval, &scope, &funcs))
		return NULL;

	Py_INCREF(funcs);

	pyppd = amalloc(sizeof(*pyppd));
	pyppd->ppd.key = key;
	pyppd->ppd.interval = interval;
	pyppd->ppd.scope = scope;
	pyppd->ppd.GetData = get_player_data;
	pyppd->ppd.SetData = set_player_data;
	pyppd->ppd.ClearData = clear_player_data;
	pyppd->ppd.clos = pyppd;
	pyppd->funcs = funcs;

	persist->RegPlayerPD(&pyppd->ppd);

	return PyCObject_FromVoidPtr(pyppd, unreg_ppd);
}


/* arena persistent data */

struct pypersist_apd
{
	ArenaPersistentData apd;
	PyObject *funcs;
};


local int get_arena_data(Arena *a, void *data, int len, void *v)
{
	struct pypersist_apd *pyapd = v;
	PyObject *val, *pkl;
	const void *pkldata;
	Py_ssize_t pkllen;

	val = PyObject_CallMethod(pyapd->funcs, "get", "(O&)", cvt_c2p_arena, a);
	if (!val)
	{
		log_py_exception(L_ERROR, "error in persistent data getter");
		return 0;
	}
	else if (val == Py_None)
	{
		Py_DECREF(val);
		return 0;
	}

	pkl = PyObject_CallMethod(cPickle, "dumps", "(Oi)", val, 1);
	Py_DECREF(val);
	if (!pkl)
	{
		log_py_exception(L_ERROR, "error pickling persistent data");
		return 0;
	}

	if (PyObject_AsReadBuffer(pkl, &pkldata, &pkllen))
	{
		Py_DECREF(pkl);
		lm->Log(L_ERROR, "<pymod> pickle result isn't buffer");
		return 0;
	}

	if (pkllen > len)
	{
		Py_DECREF(pkl);
		lm->Log(L_WARN, "<pymod> persistent data getter returned more "
				"than %lu bytes of data (%d allowed)",
				(unsigned long)pkllen, len);
		return 0;
	}

	memcpy(data, pkldata, pkllen);

	Py_DECREF(pkl);

	return pkllen;
}

local void set_arena_data(Arena *a, void *data, int len, void *v)
{
	struct pypersist_apd *pyapd = v;
	PyObject *buf, *val, *ret;

	buf = PyString_FromStringAndSize(data, len);
	val = PyObject_CallMethod(cPickle, "loads", "(O)", buf);
	Py_XDECREF(buf);
	if (!val)
	{
		log_py_exception(L_ERROR, "can't unpickle persistent data");
		return;
	}

	ret = PyObject_CallMethod(pyapd->funcs, "set", "(O&O)", cvt_c2p_arena, a, val);
	Py_DECREF(val);
	Py_XDECREF(ret);
	if (!ret)
		log_py_exception(L_ERROR, "error in persistent data setter");
}

local void clear_arena_data(Arena *a, void *v)
{
	struct pypersist_apd *pyapd = v;
	PyObject *ret;

	ret = PyObject_CallMethod(pyapd->funcs, "clear", "(O&)", cvt_c2p_arena, a);
	Py_XDECREF(ret);
	if (!ret)
		log_py_exception(L_ERROR, "error in persistent data clearer");
}


local void unreg_apd(void *v)
{
	struct pypersist_apd *pyapd = v;
	persist->UnregArenaPD(&pyapd->apd);
	Py_XDECREF(pyapd->funcs);
	afree(pyapd);
}

local PyObject * mthd_reg_apd(PyObject *self, PyObject *args)
{
	struct pypersist_apd *pyapd;
	PyObject *funcs;
	int key, interval, scope;

	if (!persistent_data_common(args, &key, &interval, &scope, &funcs))
		return NULL;

	Py_INCREF(funcs);

	pyapd = amalloc(sizeof(*pyapd));
	pyapd->apd.key = key;
	pyapd->apd.interval = interval;
	pyapd->apd.scope = scope;
	pyapd->apd.GetData = get_arena_data;
	pyapd->apd.SetData = set_arena_data;
	pyapd->apd.ClearData = clear_arena_data;
	pyapd->apd.clos = pyapd;
	pyapd->funcs = funcs;

	persist->RegArenaPD(&pyapd->apd);

	return PyCObject_FromVoidPtr(pyapd, unreg_apd);
}


local PyObject * mthd_for_each_player(PyObject *self, PyObject *args)
{
	PyObject *func;
	Link *link;
	Player *p;

	if (!PyArg_ParseTuple(args, "O", &func))
		return NULL;

	if (!PyCallable_Check(func))
	{
		PyErr_SetString(PyExc_TypeError, "func isn't callable");
		return NULL;
	}

	pd->Lock();
	FOR_EACH_PLAYER(p)
	{
		PyObject *ret = PyObject_CallFunction(func, "(O&)",
				cvt_c2p_player, p);
		Py_XDECREF(ret);
	}
	pd->Unlock();

	log_py_exception(L_ERROR, "error in a for_each_player callback");

	Py_INCREF(Py_None);
	return Py_None;
}


local PyObject * mthd_for_each_arena(PyObject *self, PyObject *args)
{
	PyObject *func;
	Link *link;
	Arena *a;

	if (!PyArg_ParseTuple(args, "O", &func))
		return NULL;

	if (!PyCallable_Check(func))
	{
		PyErr_SetString(PyExc_TypeError, "func isn't callable");
		return NULL;
	}

	aman->Lock();
	FOR_EACH_ARENA(a)
	{
		PyObject *ret = PyObject_CallFunction(func, "(O&)",
				cvt_c2p_arena, a);
		Py_XDECREF(ret);
	}
	aman->Unlock();

	log_py_exception(L_ERROR, "error in a for_each_arena callback");

	Py_INCREF(Py_None);
	return Py_None;
}


local PyObject * mthd_is_standard(PyObject *self, PyObject *args)
{
	Player *p;
	if (!PyArg_ParseTuple(args, "O&", cvt_p2c_player, &p)) return NULL;
	return PyInt_FromLong(IS_STANDARD(p));
}

local PyObject * mthd_is_chat(PyObject *self, PyObject *args)
{
	Player *p;
	if (!PyArg_ParseTuple(args, "O&", cvt_p2c_player, &p)) return NULL;
	return PyInt_FromLong(IS_CHAT(p));
}

local PyObject * mthd_is_human(PyObject *self, PyObject *args)
{
	Player *p;
	if (!PyArg_ParseTuple(args, "O&", cvt_p2c_player, &p)) return NULL;
	return PyInt_FromLong(IS_HUMAN(p));
}

local PyObject * mthd_current_ticks(PyObject *self, PyObject *args)
{
	return PyInt_FromLong(current_ticks());
}

local PyObject * mthd_current_millis(PyObject *self, PyObject *args)
{
	return PyInt_FromLong(current_millis());
}


local PyMethodDef asss_module_methods[] =
{
	{"reg_callback", mthd_reg_callback, METH_VARARGS,
		"registers a callback"},
	{"call_callback", mthd_call_callback, METH_VARARGS,
		"calls some callback functions"},
	{"get_interface", mthd_get_interface, METH_VARARGS,
		"gets an interface object"},
	{"reg_interface", mthd_reg_interface, METH_VARARGS,
		"registers an interface implemented in python"},
	{"add_command", mthd_add_command, METH_VARARGS,
		"registers a command implemented in python. the docstring of the "
		"command function will be used for command helptext."},
	{"reg_player_persistent", mthd_reg_ppd, METH_VARARGS,
		"registers a per-player persistent data handler"},
	{"reg_arena_persistent", mthd_reg_apd, METH_VARARGS,
		"registers a per-arena persistent data handler"},
	{"set_timer", mthd_set_timer, METH_VARARGS,
		"registers a timer"},
	{"for_each_player", mthd_for_each_player, METH_VARARGS,
		"runs a function for each player"},
	{"for_each_arena", mthd_for_each_arena, METH_VARARGS,
		"runs a function for each arena"},
	{"is_standard", mthd_is_standard, METH_VARARGS,
		"is this player using a regular game client?"},
	{"is_chat", mthd_is_chat, METH_VARARGS,
		"is this player using a chat-only client?"},
	{"is_human", mthd_is_human, METH_VARARGS,
		"is this player a human (as opposed to an internal fake player)?"},
	{"current_ticks", mthd_current_ticks, METH_VARARGS,
		"the current server time, in ticks"},
	{"current_millis", mthd_current_millis, METH_VARARGS,
		"the current server time, in millis"},
	{NULL}
};

local void init_asss_module(void)
{
	PyObject *m;

	if (PyType_Ready(&PlayerType) < 0)
		return;
	if (PyType_Ready(&ArenaType) < 0)
		return;
	PlayerListType.tp_base = &PyList_Type;
	if (PyType_Ready(&PlayerListType) < 0)
		return;

	if (ready_generated_types() < 0)
		return;

	m = Py_InitModule3("asss", asss_module_methods, "the asss interface module");
	if (m == NULL)
		return;

	Py_INCREF(&PlayerType);
	Py_INCREF(&ArenaType);
	Py_INCREF(&PlayerListType);
	PyModule_AddObject(m, "PlayerType", (PyObject*)&PlayerType);
	PyModule_AddObject(m, "ArenaType", (PyObject*)&ArenaType);
	PyModule_AddObject(m, "PlayerListType", (PyObject*)&PlayerListType);

	add_type_objects_to_module(m);

	/* handle constants */
#define STRING(x) PyModule_AddStringConstant(m, #x, x);
#define PYCALLBACK(x) PyModule_AddStringConstant(m, #x, x);
#define PYINTERFACE(x) PyModule_AddStringConstant(m, #x, x);
#define PYADVISER(x) PyModule_AddStringConstant(m, #x, x);
#define INT(x) PyModule_AddIntConstant(m, #x, x);
#define ONE(x) PyModule_AddIntConstant(m, #x, 1);
#include "py_constants.inc"
#undef STRING
#undef PYCALLBACK
#undef PYINTERFACE
#undef INT
#undef ONE
}



/* stuff relating to loading and unloading */

local int load_py_module(mod_args_t *args, const char *line)
{
	PyObject *mod;
	lm->Log(L_SYNC | L_INFO, "<pymod> loading python module '%s'", line);
	mod = PyImport_ImportModule((char*)line);
	if (mod)
	{
		args->privdata = mod;
		astrncpy(args->name, line, sizeof(args->name));
		args->info = NULL;
		mods_loaded++;
		return MM_OK;
	}
	else
	{
		lm->Log(L_SYNC | L_ERROR, "<pymod> error loading python module '%s'", line);
		log_py_exception(L_SYNC | L_ERROR, NULL);
		return MM_FAIL;
	}
}


local int unload_py_module(mod_args_t *args)
{
	PyObject *mdict = PyImport_GetModuleDict();
	PyObject *mod = args->privdata;
	char *mname = PyModule_GetName(mod);

	lm->Log(L_SYNC | L_INFO, "<pymod> unloading python module '%s'", args->name);

	if (mod->ob_refcnt != 2)
	{
		lm->Log(L_WARN, "<pymod> there are %lu remaining references to module %s",
				(unsigned long)mod->ob_refcnt, mname);
		return MM_FAIL;
	}

	/* the modules dictionary has a reference */
	PyDict_DelItemString(mdict, mname);
	/* and then get rid of our reference too */
	Py_DECREF(mod);

	mods_loaded--;

	return MM_OK;
}


local int call_maybe_with_arena(PyObject *mod, const char *funcname, Arena *a)
{
	PyObject *func = PyObject_GetAttrString(mod, (char*)funcname);
	if (func)
	{
		PyObject *ret = a ?
			PyObject_CallFunction(func, "(O&)", cvt_c2p_arena, a) :
			PyObject_CallFunction(func, NULL);
		Py_XDECREF(ret);
		Py_DECREF(func);
		if (ret)
			return MM_OK;
		else
		{
			lm->Log(L_ERROR, "<pymod> error in '%s'", funcname);
			log_py_exception(L_ERROR, NULL);
		}
	}
	else
		PyErr_Clear();
	return MM_FAIL;
}


local int pyloader(int action, mod_args_t *args, const char *line, Arena *arena)
{
	switch (action)
	{
		case MM_LOAD:
			return load_py_module(args, line);

		case MM_UNLOAD:
			return unload_py_module(args);

		case MM_ATTACH:
			return call_maybe_with_arena(args->privdata, "mm_attach", arena);

		case MM_DETACH:
			return call_maybe_with_arena(args->privdata, "mm_detach", arena);

		case MM_POSTLOAD:
			return call_maybe_with_arena(args->privdata, "mm_postload", NULL);

		case MM_PREUNLOAD:
			return call_maybe_with_arena(args->privdata, "mm_preunload", NULL);

		default:
			return MM_FAIL;
	}
}

EXPORT const char info_pymod[] = CORE_MOD_INFO("pymod");

EXPORT int MM_pymod(int action, Imodman *mm_, Arena *arena)
{
	Player *p;
	Link *link;
#ifdef CHECK_SIGS
	struct sigaction sa_before;
#endif
	if (action == MM_LOAD)
	{
		/* grab some modules */
		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		persist = mm->GetInterface(I_PERSIST, ALLARENAS);
		mainloop = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		if (!pd || !aman || !lm || !cfg || !cmd || !mainloop)
			return MM_FAIL;
		pdkey = pd->AllocatePlayerData(sizeof(pdata));
		adkey = aman->AllocateArenaData(sizeof(adata));
		if (pdkey < 0 || adkey < 0)
			return MM_FAIL;

		pthread_mutexattr_t attr;
		pthread_mutexattr_init(&attr);
		pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
		pthread_mutex_init(&pymtx, &attr);
		pthread_mutexattr_destroy(&attr);

#ifdef CHECK_SIGS
		sigaction(SIGINT, NULL, &sa_before);
#endif
		/* start up python */
		Py_Initialize();
#ifdef CHECK_SIGS
		/* if python changed this, reset it */
		sigaction(SIGINT, &sa_before, NULL);
#endif
		/* set up our search path */
		{
			char dir[1024], code[1024];
			const char *tmp = NULL;
			while (strsplit(CFG_PYTHON_IMPORT_PATH, ":", dir, sizeof(dir), &tmp))
			{
				if (snprintf(code, sizeof(code),
							"import sys, os\n"
							"sys.path.append(os.path.join(os.getcwd(), '%s'))\n",
							dir) > sizeof(code))
					continue;
				PyRun_SimpleString(code);
			}
		}
		/* add our module */
		init_asss_module();
		init_py_callbacks();
		init_py_interfaces();
		init_py_commands();
		init_log_py_code();
		/* extra init stuff */

		if (persist)
		{
			cPickle = PyImport_ImportModule("cPickle");
			if (!cPickle)
				log_py_exception(L_ERROR, "can't import cPickle");
		}

		pd->Lock();
		mm->RegCallback(CB_NEWPLAYER, py_newplayer, ALLARENAS);
		FOR_EACH_PLAYER(p)
			py_newplayer(p, TRUE);
		pd->Unlock();

		aman->Lock();
		mm->RegCallback(CB_ARENAACTION, py_aaction, ALLARENAS);
		FOR_EACH_ARENA(arena)
			py_aaction(arena, AA_PRECREATE);
		aman->Unlock();

		mm->RegModuleLoader("py", pyloader);

		mods_loaded = 0;

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		/* check that there are no more python modules loaded */
		if (mods_loaded)
			return MM_FAIL;

		mm->UnregModuleLoader("py", pyloader);

		pd->Lock();
		FOR_EACH_PLAYER(p)
			py_newplayer(p, FALSE);
		mm->UnregCallback(CB_NEWPLAYER, py_newplayer, ALLARENAS);
		pd->Unlock();

		aman->Lock();
		FOR_EACH_ARENA(arena)
			py_aaction(arena, AA_POSTDESTROY);
		mm->UnregCallback(CB_ARENAACTION, py_aaction, ALLARENAS);
		aman->Unlock();

		/* close down python */
		Py_XDECREF(log_code);
		Py_XDECREF(cPickle);
		mainloop->ClearTimer(pytmr_timer, NULL);
		deinit_py_commands();
		deinit_py_interfaces();
		deinit_py_callbacks();
		Py_Finalize();

		pthread_mutex_destroy(&pymtx);

		/* release our modules */
		pd->FreePlayerData(pdkey);
		aman->FreeArenaData(adkey);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(persist);
		mm->ReleaseInterface(mainloop);

		return MM_OK;
	}
	return MM_FAIL;
}

