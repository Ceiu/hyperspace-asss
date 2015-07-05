#ifndef __FORMULA_H
#define __FORMULA_H

#define MAX_VAR_NAME_LENGTH 16

typedef enum VariableType
{
	VAR_TYPE_DOUBLE,
	VAR_TYPE_ARENA,
	VAR_TYPE_FREQ,
	VAR_TYPE_FREQ_LIST,
	VAR_TYPE_PLAYER,
	VAR_TYPE_PLAYER_LIST,
} VariableType;

typedef struct Freq
{
	Arena *arena;
	int freq;
} Freq;

typedef struct FormulaVariable
{
	char *name;
	VariableType type;
	union {
		double value;
		Arena *arena;
		Freq freq;
		Player *p;
		LinkedList list;
	};
} FormulaVariable;

typedef FormulaVariable * (*ArenaPropertyCallback)(Arena *arena);
typedef FormulaVariable * (*FreqPropertyCallback)(Arena *arena, int freq);
typedef FormulaVariable * (*PlayerPropertyCallback)(Player *p);

typedef struct Formula Formula;

#define I_FORMULA "formula-3"

typedef struct Iformula
{
	INTERFACE_HEAD_DECL

	/** Parses a formula from a given string. Don't forget to free it with
	 * FreeFormula when you're done.
	 *
	 * @param string the string to parse
	 * @param error_buffer a buffer to write any potential errors into
	 * @param error_buffer_length the length of error_buffer
	 * @return a new Formula object
	 */
	Formula * (*ParseFormula)(const char *string, char *error_buffer,
			int error_buffer_length);

	/** Frees the memory associated with a Formula object.
	 *
	 * @param f the Formula to free
	 */
	void (*FreeFormula)(Formula *f);

	/** Evaluates a formula object using the given variables. The HashMap
	 * of variables should contain only doubles.
	 *
	 * @param f the formula to evaluate
	 * @param variables a HashTable containing FormulaVariables
	 * @param error_buffer a buffer to write any potential errors into
	 * @param error_buffer_length the length of error_buffer
	 * @return the result of the evaluated formula
	 */
	double (*EvaluateFormula)(Formula *f, HashTable *variables,
			char *ret_var_name, char *error_buffer, 
			int error_buffer_length);

	/** Evaluates a formula and returns an int. If the double is too big,
	 * too small, or NaN, then then default_val will be returned instead.
	 * The result will be *rounded* to the nearest integer.
	 *
	 * The HashMap of variables should contain only doubles.
	 *
	 * @param f the formula to evaluate
	 * @param variables a HashTable containing FormulaVariables
	 * @param error_buffer a buffer to write any potential errors into
	 * @param error_buffer_length the length of error_buffer
	 * @param default_val the value to return when the result cannot be
	 * 		converted to an int
	 * @return the result of the evaulated formula, or default_val
	 */
	int (*EvaluateFormulaInt)(Formula *f, HashTable *variables,
			char *ret_var_name, char *error_buffer, 
			int error_buffer_length, int default_val);

	/** Registers a property on an arena that can be accessed in formulas.
	 *
	 * @param property the name of the property to register
	 * @param cb the function to call to compute the property. The
	 *		returned FormulaVariable will be freed independently.
	 */
	void (*RegArenaProperty)(const char *property, ArenaPropertyCallback cb);

	/** Unregisters a property registered with RegArenaProperty.
	 *
	 * @param property the name of the property to unregister
	 * @param cb the calllback used to register the property
	 */
	void (*UnregArenaProperty)(const char *property, ArenaPropertyCallback cb);

	/** Registers a property on a freq that can be accessed in formulas.
	 *
	 * @param property the name of the property to register
	 * @param cb the function to call to compute the property. The
	 *		returned FormulaVariable will be freed independently.
	 */
	void (*RegFreqProperty)(const char *property, FreqPropertyCallback cb);

	/** Unregisters a property registered with RegFreqProperty.
	 *
	 * @param property the name of the property to unregister
	 * @param cb the calllback used to register the property
	 */
	void (*UnregFreqProperty)(const char *property, FreqPropertyCallback cb);

	/** Registers a property on a player that can be accessed in formulas.
	 *
	 * @param property the name of the property to register
	 * @param cb the function to call to compute the property. The
	 *		returned FormulaVariable will be freed independently.
	 */
	void (*RegPlayerProperty)(const char *property, PlayerPropertyCallback cb);

	/** Unregisters a property registered with RegPlayerProperty.
	 *
	 * @param property the name of the property to unregister
	 * @param cb the calllback used to register the property
	 */
	void (*UnregPlayerProperty)(const char *property, PlayerPropertyCallback cb);
} Iformula;

#endif
