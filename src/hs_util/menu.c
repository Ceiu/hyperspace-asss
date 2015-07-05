#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "asss.h"
#include "persist.h"
#include "menu.h"

typedef enum BlockType
{
	BLOCK_TYPE_TITLE = 0,
	BLOCK_TYPE_TABLE,
	BLOCK_TYPE_TEXT_AREA
} BlockType;

struct Menu
{
	LinkedList block_list;
	MenuColor default_color;
};

typedef struct Row
{
	int is_separator;
	int length;
	char **text;
	MenuColor color;
} Row;

struct Table
{
	int col_count;
	int var_col;
	int compact;

	char **col_names;
	ColAlignment *alignments;
	int *max_widths;

	LinkedList rows;
};

struct TextArea
{
	int min;
	int max;
	char *text;
	MenuColor color;
};

typedef struct MenuBlock
{
	BlockType type;
	int width;
	union {
		char *title;
		Table *table;
		TextArea *area;
	};

	// used only during iteration
	int min_width;
	int max_width;
} MenuBlock;

typedef struct Printer
{
	void (*callback)(const char *line, MenuColor color, void *clos);
	void *clos;
} Printer;

struct Formatter
{
	char *name;
	int (*get_min_width)(Table *table);
	void (*print_border)(MenuBlock *top, MenuBlock *bottom, MenuColor color, Printer *p);
	void (*print_line)(const char *line, int width, MenuColor color, Printer *p);
	void (*print_table)(Table *table, int width, MenuColor def, Printer *p);
};

typedef struct PData
{
	int formatter_index;
} PData;

// modules
local Imodman *mm;
local Ilogman *lm;
local Iconfig *cfg;
local Ichat *chat;
local Icmdman *cmd;
local Iplayerdata *pd;
local Ipersist *persist;

local int pdata_key;

local int hs_get_min_width(Table *table)
{
	int i;

	// 3 chars separate each column
	int width = (table->col_count - 1) * 3;

	for (i = 0; i < table->col_count; i++)
	{
		width += table->max_widths[i];
	}

	return width;
}

local void hs_print_border(MenuBlock *top, MenuBlock *bottom, MenuColor color, Printer *p)
{
	char buf[256];
	int len = 0;
	int i;

	if (top)
	{
		len = top->width;
	}

	if (bottom && bottom->width > len)
	{
		len = bottom->width;
	}

	buf[0] = '+';
	for (i = 1; i < len + 3; i++)
	{
		buf[i] = '-';
	}
	buf[i] = '+';
	buf[i+1] = '\0';

	if (top)
	{
		buf[top->width + 3] = '+';

		if (top->type == BLOCK_TYPE_TABLE)
		{
			int delta = top->width - hs_get_min_width(top->table);
			int index = 3;
			for (i = 0; i < top->table->col_count - 1; i++)
			{
				if (i == top->table->var_col)
					index += delta;
				buf[index + top->table->max_widths[i]] = '+';
				index += 3 + top->table->max_widths[i];
			}
		}
	}

	if (bottom)
	{
		buf[bottom->width + 3] = '+';

		if (bottom->type == BLOCK_TYPE_TABLE)
		{
			int delta = bottom->width - hs_get_min_width(bottom->table);
			int index = 3;
			for (i = 0; i < bottom->table->col_count - 1; i++)
			{
				if (i == bottom->table->var_col)
					index += delta;
				buf[index + bottom->table->max_widths[i]] = '+';
				index += 3 + bottom->table->max_widths[i];
			}
		}
	}

	p->callback(buf, color, p->clos);
}

local void hs_print_line(const char *line, int width, MenuColor color, Printer *p)
{
	char buf[256];
	snprintf(buf, sizeof(buf), "| %-*s |", width, line);

	p->callback(buf, color, p->clos);
}

local void hs_print_row(char **text, int row, Table *table, int delta, MenuColor color, Printer *p)
{
	int i;
	char buf[256];
	char *k = buf;

	*k++ = '|';

	for (i = 0; i < table->col_count; i++)
	{
		int len = table->max_widths[i];
		if (i == table->var_col)
			len += delta;

		*k++ = ' ';

		if (!row || table->alignments[i] == MENU_ALIGN_LEFT)
		{
			sprintf(k, "%-*s", len, text[i]);
		}
		else
		{
			sprintf(k, "%*s", len, text[i]);
		}

		k += len;
		*k++ = ' ';
		*k++ = '|';
	}

	*k = '\0';

	p->callback(buf, color, p->clos);
}

local void hs_print_table(Table *table, int width, MenuColor def, Printer *p)
{
	int delta = width - hs_get_min_width(table);
	Link *link;

	if (table->col_names)
	{
		// print header
		hs_print_row(table->col_names, FALSE, table, delta, def, p);

		// print border
		if (!table->compact)
		{
			char buf[256];
			int i;
			int index = 3;

			buf[0] = '+';
			for (i = 1; i < width + 3; i++)
			{
				buf[i] = '-';
			}
			buf[i] = '+';
			buf[i+1] = '\0';

			for (i = 0; i < table->col_count - 1; i++)
			{
				if (i == table->var_col)
					index += delta;
				buf[index + table->max_widths[i]] = '+';
				index += 3 + table->max_widths[i];
			}

			p->callback(buf, def, p->clos);
		}
	}

	// print rows
	for (link = LLGetHead(&table->rows); link; link = link->next)
	{
		Row *row = link->data;
		if (row->is_separator)
		{
			char buf[256];
			int i;
			int index = 3;

			buf[0] = '+';
			for (i = 1; i < width + 3; i++)
			{
				buf[i] = '-';
			}
			buf[i] = '+';
			buf[i+1] = '\0';

			for (i = 0; i < table->col_count - 1; i++)
			{
				if (i == table->var_col)
					index += delta;
				buf[index + table->max_widths[i]] = '+';
				index += 3 + table->max_widths[i];
			}

			p->callback(buf, def, p->clos);
		}
		else
		{
			hs_print_row(row->text, TRUE, table, delta, row->color, p);
		}
	}
}

local int simple_get_min_width(Table *table)
{
	int i;

	// 1 char separates each column
	int width = (table->col_count - 1);

	for (i = 0; i < table->col_count; i++)
	{
		width += table->max_widths[i];
	}

	return width;
}

local void simple_print_border(MenuBlock *top, MenuBlock *bottom, MenuColor color, Printer *p)
{
	if (top && bottom)
	{
		int i;
		char buf[256];
		int len;

		// max
		len = top->width > bottom->width ? top->width : bottom->width;

		for (i = 0; i < len; i++)
		{
			buf[i] = '-';
		}

		buf[i] = '\0';

		p->callback(buf, color, p->clos);
	}
}

local void simple_print_line(const char *line, int width, MenuColor color, Printer *p)
{
	char buf[256];
	snprintf(buf, sizeof(buf), "%s", line);

	p->callback(buf, color, p->clos);
}

local void simple_print_row(char **text, int row, Table *table, int delta, MenuColor color, Printer *p)
{
	int i;
	char buf[256];
	char *k = buf;

	for (i = 0; i < table->col_count; i++)
	{
		int len = table->max_widths[i];
		if (i == table->var_col)
			len += delta;

		if (!row || table->alignments[i] == MENU_ALIGN_LEFT)
		{
			sprintf(k, "%-*s", len, text[i]);
		}
		else
		{
			sprintf(k, "%*s", len, text[i]);
		}

		k += len;
		*k++ = '|';
	}

	// remove the trailing |
	k--;
	*k = '\0';

	p->callback(buf, color, p->clos);
}

local void simple_print_table(Table *table, int width, MenuColor def, Printer *p)
{
	int delta = width - hs_get_min_width(table);
	Link *link;

	if (table->col_names)
	{
		// print header
		simple_print_row(table->col_names, FALSE, table, delta, def, p);

		// print border
		if (!table->compact)
		{
			char buf[256];
			int i;

			for (i = 0; i < width; i++)
			{
				buf[i] = '-';
			}
			buf[i] = '\0';

			p->callback(buf, def, p->clos);
		}
	}

	// print rows
	for (link = LLGetHead(&table->rows); link; link = link->next)
	{
		Row *row = link->data;
		if (row->is_separator)
		{
			char buf[256];
			int i;

			for (i = 0; i < width; i++)
			{
				buf[i] = '-';
			}
			buf[i] = '\0';

			p->callback(buf, def, p->clos);
		}
		else
		{
			simple_print_row(row->text, TRUE, table, delta, row->color, p);
		}
	}
}

local Formatter hs_formatter =
{
	"hs",
	hs_get_min_width,
	hs_print_border,
	hs_print_line,
	hs_print_table
};

local Formatter simple_formatter =
{
	"simple",
	simple_get_min_width,
	simple_print_border,
	simple_print_line,
	simple_print_table
};

local Formatter *all_formatters[] = {&hs_formatter, &simple_formatter};
local int formatter_count = sizeof(all_formatters)/sizeof(all_formatters[0]);

local int get_default_index()
{
	int i;
	int fallback = -1;
	/* cfghelp: Menu:DefaultFormatter, global, string, mod: menu
	 * The name of the menu formatter to use for new players.
	 * In the event of an error, the module will use the first
	 * menu formatter displayed with ?menuformat. */
	const char *name = cfg->GetStr(GLOBAL, "Menu", "DefaultFormatter");

	for (i = 0; i < formatter_count; i++)
	{
		if (all_formatters[i] != NULL)
		{
			if (name != NULL && strcasecmp(all_formatters[i]->name, name) == 0)
			{
				return i;
			}

			// grab the first non NULL formatter
			if (fallback == -1)
			{
				fallback = i;
			}
		}
	}

	return fallback;
}

local int get_formatter_index(const char *name)
{
	int i;
	if (!name || !*name)
	{
		return get_default_index();
	}
	else
	{
		for (i = 0; i < formatter_count; i++)
		{
			if (all_formatters[i] != NULL)
			{
				if (strcasecmp(all_formatters[i]->name, name) == 0)
				{
					return i;
				}
			}
		}
		return -1;
	}
}

local Formatter * GetFormatter(const char *name)
{
	int index = get_formatter_index(name);

	if (index != -1)
	{
		return all_formatters[index];
	}
	else
	{
		return NULL;
	}
}

local int GetPersistData(Player *p, void *data, int len, void *clos)
{
	PData *pdata = PPDATA(p, pdata_key);

	PData *persist_data = (PData*)data;

	persist_data->formatter_index = pdata->formatter_index;

	return sizeof(PData);
}

local void SetPersistData(Player *p, void *data, int len, void *clos)
{
	PData *pdata = PPDATA(p, pdata_key);

	PData *persist_data = (PData*)data;

	// do bounds checking and make sure it's not NULL
	if (0 <= persist_data->formatter_index
			&& persist_data->formatter_index < formatter_count
			&& all_formatters[persist_data->formatter_index])
	{
		pdata->formatter_index = persist_data->formatter_index;
	}
	else
	{
		pdata->formatter_index = get_default_index();
	}
}

local void ClearPersistData(Player *p, void *clos)
{
	PData *pdata = PPDATA(p, pdata_key);

	pdata->formatter_index = get_default_index();
}

local PlayerPersistentData my_persist_data =
{
	11505, INTERVAL_FOREVER, PERSIST_GLOBAL,
	GetPersistData, SetPersistData, ClearPersistData
};

local Menu * CreateMenu(const char *title, MenuColor default_color)
{
	Menu *menu = amalloc(sizeof(*menu));

	LLInit(&menu->block_list);
	menu->default_color = default_color;

	if (title && *title)
	{
		MenuBlock *block = amalloc(sizeof(*block));
		block->type = BLOCK_TYPE_TITLE;
		block->width = 0;
		block->title = astrdup(title);

		LLAdd(&menu->block_list, block);
	}

	return menu;
}

local void free_row_cb(const void *ptr)
{
	int i;
	const Row *row = ptr;

	if (!row->is_separator)
	{
		for (i = 0; i < row->length; i++)
		{
			afree(row->text[i]);
		}

		afree(row->text);
	}

	afree(row);
}

local void FreeMenu(Menu *menu)
{
	int i;
	Link *link;

	for(link = LLGetHead(&menu->block_list); link; link = link->next)
	{
		MenuBlock *block = link->data;

		switch (block->type)
		{
			case BLOCK_TYPE_TITLE:
				afree(block->title);
				break;
			case BLOCK_TYPE_TABLE:
				for (i = 0; i < block->table->col_count; i++)
				{
					afree(block->table->col_names[i]);
				}
				LLEnum(&block->table->rows, free_row_cb);
				LLEmpty(&block->table->rows);
				afree(block->table->col_names);
				afree(block->table->alignments);
				afree(block->table->max_widths);
				afree(block->table);
				break;
			case BLOCK_TYPE_TEXT_AREA:
				if (block->area->text != NULL)
				{
					afree(block->area->text);
				}
				afree(block->area);
				break;
		}

		afree(block);
	}

	afree(menu);
}

local Table * AddTable(Menu *menu, char **column_names, ColAlignment *alignments, int cols, int var_col, int compact)
{
	int i;
	Table *table = amalloc(sizeof(*table));
	MenuBlock *block = amalloc(sizeof(*block));

	table->col_count = cols;
	table->var_col = var_col;
	table->compact = compact;
	LLInit(&table->rows);

	table->alignments = amalloc(cols * sizeof(ColAlignment));
	table->max_widths = amalloc(cols * sizeof(int));

	if (column_names != NULL)
	{
		table->col_names = amalloc(cols * sizeof(char *));
	}
	else
	{
		table->col_names = NULL;
	}

	for (i = 0; i < cols; i++)
	{
		if (column_names)
		{
			table->col_names[i] = astrdup(column_names[i]);
			table->max_widths[i] = strlen(column_names[i]);
		}
		else
		{
			table->max_widths[i] = 0;
		}

		table->alignments[i] = alignments[i];
	}

	block->type = BLOCK_TYPE_TABLE;
	block->width = 0;
	block->table = table;

	LLAdd(&menu->block_list, block);

	return table;
}

local void AppendRow(Table *table, MenuColor color, char **row_text)
{
	int i;
	Row *row = amalloc(sizeof(*row));
	row->is_separator = FALSE;
	row->length = table->col_count;
	row->color = color;
	row->text = amalloc(row->length * sizeof(char *));

	for (i = 0; i < row->length; i++)
	{
		int len = strlen(row_text[i]);
		row->text[i] = astrdup(row_text[i]);
		if (len > table->max_widths[i])
		{
			table->max_widths[i] = len;
		}
	}

	LLAdd(&table->rows, row);
}

local void AppendSeparator(Table *table)
{
	Row *row = amalloc(sizeof(*row));
	row->is_separator = TRUE;
	LLAdd(&table->rows, row);
}

local TextArea * AddTextArea(Menu *menu, int min_size, int max_size)
{
	TextArea *area = amalloc(sizeof(*area));
	MenuBlock *block = amalloc(sizeof(*block));

	area->min = min_size;
	area->max = max_size;
	area->text = NULL;

	block->type = BLOCK_TYPE_TEXT_AREA;
	block->width = 0;
	block->area = area;

	LLAdd(&menu->block_list, block);

	return area;
}

local void SetText(TextArea *area, const char *text, MenuColor color)
{
	int len = strlen(text);
	if (area->text != NULL)
	{
		afree(area->text);
	}

	area->text = astrdup(text);
	area->color = color;

	if (area->max < len)
	{
		len = area->max;
	}

	if (area->min < len)
	{
		area->min = len;
	}
}

local void set_width_bounds(Menu *menu, Formatter *formatter)
{
	Link *link;

	for (link = LLGetHead(&menu->block_list); link; link = link->next)
	{
		MenuBlock *block = link->data;

		switch (block->type)
		{
			case BLOCK_TYPE_TITLE:
				block->min_width = block->max_width = strlen(block->title);
				break;
			case BLOCK_TYPE_TABLE:
				block->min_width = formatter->get_min_width(block->table);
				if (block->table->var_col == -1)
				{
					block->max_width = block->min_width;
				}
				else
				{
					block->max_width = -1;
				}
				break;
			case BLOCK_TYPE_TEXT_AREA:
				block->max_width = block->area->max;
				block->min_width = block->area->min;
				break;
		}

		// a starting point
		block->width = block->min_width;
	}
}

local void set_widths(Menu *menu, Formatter *formatter)
{
	int changes_made = 1;
	Link *link;
	MenuBlock *prev;

	// set min and max width, and a starting point for width
	set_width_bounds(menu, formatter);

	while (changes_made)
	{
		changes_made = 0;
		prev = NULL;
		for (link = LLGetHead(&menu->block_list); link; link = link->next)
		{
			MenuBlock *block = link->data;

			if (prev)
			{
				if (block->width < prev->width)
				{
					// try to expand block
					if (block->max_width == -1 || prev->width <= block->max_width)
					{
						block->width = prev->width;
						changes_made = 1;
					}
				}
				else if (prev->width < block->width)
				{
					// try to expand prev
					if (prev->max_width == -1 || block->width <= prev->max_width)
					{
						prev->width = block->width;
						changes_made = 1;
					}
				}
			}

			prev = block;
		}
	}
}

/* used to pass data between do_text_area and do_wrapped_text. */
struct WrapClos
{
	Formatter *formatter;
	Printer *printer;
	MenuColor color;
	int width;
};

local void do_wrapped_text(const char *line, void *clos)
{
	struct WrapClos *my_clos = clos;

	my_clos->formatter->print_line(line, my_clos->width, my_clos->color, my_clos->printer);
}

local void do_text_area(TextArea *area, int width, Formatter *formatter, Printer *printer)
{
	char buf[1024];
	const char *temp = NULL;

	struct WrapClos my_clos;
	my_clos.formatter = formatter;
	my_clos.printer = printer;
	my_clos.color = area->color;
	my_clos.width = width;

	while (strsplit(area->text, "\n", buf, sizeof(buf), &temp))
	{
		wrap_text(buf, width + 1, ' ', do_wrapped_text, &my_clos);

		// ignore the first newline (at the end of the line)
		if (*temp == '\n')
			temp++;

		// but display all the rest
		while (*temp == '\n')
		{
			do_wrapped_text("", &my_clos);
			temp++;
		}
	}
}

local void do_block(MenuBlock *prev, MenuBlock *block, MenuColor color, Formatter *formatter, Printer *printer)
{
	if (block)
	{
		// print border between prev and block
		formatter->print_border(prev, block, color, printer);

		// print block
		switch (block->type)
		{
			case BLOCK_TYPE_TITLE:
				formatter->print_line(block->title, block->width, color, printer);
				break;
			case BLOCK_TYPE_TABLE:
				formatter->print_table(block->table, block->width, color, printer);
				break;
			case BLOCK_TYPE_TEXT_AREA:
				do_text_area(block->area, block->width, formatter, printer);
				break;
		}
	}
	else
	{
		formatter->print_border(prev, NULL, color, printer);
	}
}

local void PrintMenu(Menu *menu, Formatter *formatter, void (*callback)(const char *line, MenuColor color, void *clos), void *clos)
{
	Link *link;
	MenuBlock *prev = NULL;

	Printer printer;
	printer.callback = callback;
	printer.clos = clos;

	if (!formatter)
	{
		callback("invalid formatter", MENU_COLOR_RED, clos);
		return;
	}

	set_widths(menu, formatter);

	for (link = LLGetHead(&menu->block_list); link; link = link->next)
	{
		MenuBlock *block = link->data;

		do_block(prev, block, menu->default_color, formatter, &printer);

		prev = block;
	}

	do_block(prev, NULL, menu->default_color, formatter, &printer);
}

local void send_menu_cb(const char *line, MenuColor color, void *clos)
{
	Player *p = clos;
	Link plink = {NULL, p};
	LinkedList lst = { &plink, &plink };
	char message_type = MSG_ARENA;

	switch (color)
	{
		case MENU_COLOR_GREEN:
			message_type = MSG_ARENA;
			break;
		case MENU_COLOR_RED:
			message_type = MSG_SYSOPWARNING;
			break;
	}

	chat->SendAnyMessage(&lst, message_type, 0, NULL, "%s", line);
}

local void SendMenu(Menu *menu, Player *p)
{
	PData *pdata = PPDATA(p, pdata_key);
	PrintMenu(menu, all_formatters[pdata->formatter_index], send_menu_cb, p);
}

local Imenu interface =
{
	INTERFACE_HEAD_INIT(I_MENU, "menu")
	CreateMenu, FreeMenu, AddTable, AppendRow,
	AppendSeparator, AddTextArea, SetText, SendMenu,
	PrintMenu, GetFormatter
};

local helptext_t menuformat_help =
"Targets: none\n"
"Args: none or <formatter>\n"
"With no arguments, this command displays the available menu formatters.\n"
"Otherwise, this changes your formatter to the one specified by the\n"
"command's parameters.\n";

local void Cmenuformat(const char *cmd, const char *params, Player *p, const Target *target)
{
	if (params == NULL || *params == '\0')
	{
		int i;
		int default_index = get_default_index();
		Menu *m = CreateMenu("Menu Formatters", MENU_COLOR_GREEN);

		char *header[] = {"Formatter", "Default"};
		ColAlignment align[] = {MENU_ALIGN_LEFT, MENU_ALIGN_RIGHT};
		Table *table = AddTable(m, header, align, 2, -1, 0);

		char *row[2];
		char buf[64];

		row[0] = buf;

		for (i = 0; i < formatter_count; i++)
		{
			if (all_formatters[i] != NULL)
			{
				if (i == default_index)
				{
					row[1] = "*";
				}
				else
				{
					row[1] = "";
				}

				snprintf(buf, sizeof(buf), "%s", all_formatters[i]->name);

				AppendRow(table, MENU_COLOR_GREEN, row);
			}
		}

		SendMenu(m, p);
		FreeMenu(m);
	}
	else
	{
		int index = get_formatter_index(params);

		if (index == -1)
		{
			chat->SendMessage(p, "No formatter by that name!");
		}
		else
		{
			PData *pdata = PPDATA(p, pdata_key);
			pdata->formatter_index = index;
			chat->SendMessage(p, "Set formatter to %s", all_formatters[index]->name);
		}
	}
}

EXPORT const char info_menu[] = "v1.0 Dr Brain <drbrain@gmail.com>";

EXPORT int MM_menu(int action, Imodman *_mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;

		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		persist = mm->GetInterface(I_PERSIST, ALLARENAS);

		if (!lm || !cfg || !chat || !cmd || !pd || !persist)
		{
			mm->ReleaseInterface(lm);
			mm->ReleaseInterface(cfg);
			mm->ReleaseInterface(chat);
			mm->ReleaseInterface(cmd);
			mm->ReleaseInterface(pd);
			mm->ReleaseInterface(persist);

			return MM_FAIL;
		}

		pdata_key = pd->AllocatePlayerData(sizeof(PData));
		if (pdata_key == -1) return MM_FAIL;

		persist->RegPlayerPD(&my_persist_data);

		mm->RegInterface(&interface, ALLARENAS);

		cmd->AddCommand("menuformat", Cmenuformat, ALLARENAS, menuformat_help);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&interface, ALLARENAS))
		{
			return MM_FAIL;
		}

		cmd->RemoveCommand("menuformat", Cmenuformat, ALLARENAS);

		persist->UnregPlayerPD(&my_persist_data);

		pd->FreePlayerData(pdata_key);

		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(persist);

		return MM_OK;
	}
	return MM_FAIL;
}
