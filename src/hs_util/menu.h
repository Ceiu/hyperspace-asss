#ifndef MENU_H
#define MENU_H

typedef enum MenuColor
{
	MENU_COLOR_GREEN = 0,
	MENU_COLOR_RED
} MenuColor;

typedef enum ColAlignment
{
	MENU_ALIGN_LEFT = 0,
	MENU_ALIGN_RIGHT
} ColAlignment;

typedef struct Menu Menu;
typedef struct Table Table;
typedef struct TextArea TextArea;
typedef struct Formatter Formatter;

#define I_MENU "menu-1"

typedef struct Imenu
{
	INTERFACE_HEAD_DECL

	/** Creates a menu object with the given title. The title will be
	 * shown at the top of the menu. If title is NULL or "", then no
	 * title will be displayed.
	 *
	 * Call FreeMenu when finished with the menu.
	 *
	 * @param title the title of the menu
	 * @param default_color the color to use for all lines in between
	 * @return a new Menu object
	 */
	Menu * (*CreateMenu)(const char *title, MenuColor default_color);

	/** Frees the memory associated with a menu, and all tables and text
	 * areas.
	 *
	 * @param menu the Menu to free
	 */
	void (*FreeMenu)(Menu *menu);

	/** Creates a table and adds it to the given menu.
	 *
	 * @param menu the target Menu
	 * @param column_names an array of char pointers, of length cols. If
	 * 		null, no header will be used.
	 * @param alignments an array of ColAlignments of length cols
	 * @param cols the number of columns in the table
	 * @param var_col the index of the column to expand with the table.
	 * 		specify -1 make the table fixed width.
	 * @param compact if the table should be displayed with a compact
	 *		format suitable for tables containing only one row.
	 * @return a new Table object
	 */
	Table * (*AddTable)(Menu *menu, char **column_names,
			ColAlignment *alignments, int cols, int var_col,
			int compact);

	/** Appends a row to the specified table.
	 *
	 * @param table the target Table
	 * @param color the color to use for the row
	 * @param row an array of char pointers. must be the same length as
	 * specifed to create the table.
	 */
	void (*AppendRow)(Table *table, MenuColor color, char **row);

	/** Appends a separator to the specified table.
	 *
	 * @param table the target Table
	 */
	void (*AppendSeparator)(Table *table);

	/** Creates a new text area and adds it to the given menu.
	 *
	 * @param menu the target Menu
	 * @param min_size the minimum width (in chars) of the text area
	 * @param max_size the maximum width of the text area. Specify a -1
	 *		to make the text area automatically determine its own
	 *		size from surrounding objects.
	 * @return a new TextArea object
	 */
	TextArea * (*AddTextArea)(Menu *menu, int min_size, int max_size);

	/** Sets the text for a text area
	 *
	 * @param area the target TextArea
	 * @param text the text to use (cannot be NULL)
	 * @param color the color to display the text in
	 */
	void (*SetText)(TextArea *area, const char *text, MenuColor color);

	/** Sends the formatted menu to a player
	 *
	 * @param p the destination player
	 * @param menu the Menu to send
	 */
	void (*SendMenu)(Menu *menu, Player *p);

	/** Formats a menu and calls the given callback for each line.
	 *
	 * @param menu the Menu to print
	 * @param formatter the Formatter to use. Use NULL for the default.
	 * @param callback the callback function to call with each line
	 * @param clos the closure argument to give to the callback
	 */
	void (*PrintMenu)(Menu *menu, Formatter *formatter,
			void (*callback)(const char *line, MenuColor color,
			void *clos), void *clos);

	/** Finds a formatter with the given name. "" or NULL will retrieve the
	 * default formatter
	 *
	 * @param name the name of the formatter to locate
	 * @return a Formatter object, or NULL
	 */
	Formatter * (*GetFormatter)(const char *name);
} Imenu;

#endif /* MENU_H */
