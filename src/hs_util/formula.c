#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <math.h>
#include <limits.h>

#include "asss.h"
#include "formula.h"
#include "jackpot.h"
#include "ast.h"

local FormulaVariable * evaluate_variable(ASTNode *node, HashTable *vars, char *error_buffer, int error_buffer_length, LinkedList *temp_vars);
local int evaluate_logical(ASTNode *node, HashTable *vars, char *error_buffer, int error_buffer_length, LinkedList *temp_vars);
local double evaluate_ast(ASTNode *node, HashTable *vars, char *error_buffer, int error_buffer_length, LinkedList *temp_vars);
local double evaluate_assignment(AssignmentNode *node, HashTable *vars, char *error_buffer, int error_buffer_length, LinkedList *temp_vars);

local Ilogman *lm;
local Iplayerdata *pd;
local Imodman *mm;

local HashTable *arena_callbacks;
local HashTable *freq_callbacks;
local HashTable *player_callbacks;

local FormulaVariable * arena_players_callback(Arena *arena)
{
	Link *link;
	Player *p;
	FormulaVariable *var = amalloc(sizeof(FormulaVariable));
	var->name = NULL;
	var->type = VAR_TYPE_PLAYER_LIST;
	LLInit(&var->list);

	// pd is already locked!
	FOR_EACH_PLAYER(p)
	{
		if (IS_HUMAN(p) && p->arena == arena && p->p_ship != SHIP_SPEC)
			LLAdd(&var->list, p);
	}

	return var;
}

local FormulaVariable * arena_specs_callback(Arena *arena)
{
	Link *link;
	Player *p;
	FormulaVariable *var = amalloc(sizeof(FormulaVariable));
	var->name = NULL;
	var->type = VAR_TYPE_PLAYER_LIST;
	LLInit(&var->list);

	// pd is already locked!
	FOR_EACH_PLAYER(p)
	{
		if (IS_HUMAN(p) && p->arena == arena && p->p_ship == SHIP_SPEC)
			LLAdd(&var->list, p);
	}

	return var;
}

local FormulaVariable * arena_everyone_callback(Arena *arena)
{
	Link *link;
	Player *p;
	FormulaVariable *var = amalloc(sizeof(FormulaVariable));
	var->name = NULL;
	var->type = VAR_TYPE_PLAYER_LIST;
	LLInit(&var->list);

	// pd is already locked!
	FOR_EACH_PLAYER(p)
	{
		if (IS_HUMAN(p) && p->arena == arena)
			LLAdd(&var->list, p);
	}

	return var;
}

local FormulaVariable * arena_freqs_callback(Arena *arena)
{
	Link *link, *freqlink;
	Player *p;
	FormulaVariable *var = amalloc(sizeof(FormulaVariable));
	var->name = NULL;
	var->type = VAR_TYPE_FREQ_LIST;
	LLInit(&var->list);

	// pd is already locked!
	FOR_EACH_PLAYER(p)
	{
		if (IS_HUMAN(p) && p->arena == arena)
		{
			int found = 0;
			for (freqlink = LLGetHead(&var->list); freqlink; freqlink = freqlink->next)
			{
				Freq *freq = freqlink->data;
				if (freq->freq == p->p_freq);
				{
					found = 1;
					break;
				}
			}
			if (!found)
			{
				Freq *freq = amalloc(sizeof(Freq));
				freq->arena = arena;
				freq->freq = p->p_freq;
				LLAdd(&var->list, freq);
			}
		}
	}

	return var;
}

local FormulaVariable * arena_jackpot_callback(Arena *arena)
{
	FormulaVariable *var = amalloc(sizeof(FormulaVariable));
	Ijackpot *jackpot;

	var->name = NULL;
	var->type = VAR_TYPE_DOUBLE;
	var->value = 0.0;

	jackpot = mm->GetInterface(I_JACKPOT, arena);
	if (jackpot)
	{
		var->value = (double)jackpot->GetJP(arena);
		mm->ReleaseInterface(jackpot);
	}

	return var;
}

local FormulaVariable * arena_size_callback(Arena *arena)
{
	Link *link;
	Player *p;
	FormulaVariable *var = amalloc(sizeof(FormulaVariable));
	int count = 0;
	var->name = NULL;
	var->type = VAR_TYPE_DOUBLE;

	// pd is already locked!
	FOR_EACH_PLAYER(p)
	{
		if (IS_HUMAN(p) && p->arena == arena)
			count++;
	}

	var->value = (double)count;

	return var;
}

local FormulaVariable * freq_players_callback(Arena *arena, int freq)
{
	Link *link;
	Player *p;
	FormulaVariable *var = amalloc(sizeof(FormulaVariable));
	var->name = NULL;
	var->type = VAR_TYPE_PLAYER_LIST;
	LLInit(&var->list);

	// pd is already locked!
	FOR_EACH_PLAYER(p)
	{
		if (IS_HUMAN(p) && p->arena == arena && p->p_freq == freq && p->p_ship != SHIP_SPEC)
			LLAdd(&var->list, p);
	}

	return var;
}

local FormulaVariable * freq_specs_callback(Arena *arena, int freq)
{
	Link *link;
	Player *p;
	FormulaVariable *var = amalloc(sizeof(FormulaVariable));
	var->name = NULL;
	var->type = VAR_TYPE_PLAYER_LIST;
	LLInit(&var->list);

	// pd is already locked!
	FOR_EACH_PLAYER(p)
	{
		if (IS_HUMAN(p) && p->arena == arena && p->p_freq == freq && p->p_ship == SHIP_SPEC)
			LLAdd(&var->list, p);
	}

	return var;
}

local FormulaVariable * freq_everyone_callback(Arena *arena, int freq)
{
	Link *link;
	Player *p;
	FormulaVariable *var = amalloc(sizeof(FormulaVariable));
	var->name = NULL;
	var->type = VAR_TYPE_PLAYER_LIST;
	LLInit(&var->list);

	// pd is already locked!
	FOR_EACH_PLAYER(p)
	{
		if (IS_HUMAN(p) && p->arena == arena && p->p_freq == freq)
			LLAdd(&var->list, p);
	}

	return var;
}

local FormulaVariable * freq_arena_callback(Arena *arena, int freq)
{
	FormulaVariable *var = amalloc(sizeof(FormulaVariable));
	var->name = NULL;
	var->type = VAR_TYPE_ARENA;
	var->arena = arena;

	return var;
}

local FormulaVariable * freq_size_callback(Arena *arena, int freq)
{
	Link *link;
	Player *p;
	FormulaVariable *var = amalloc(sizeof(FormulaVariable));
	int count = 0;
	var->name = NULL;
	var->type = VAR_TYPE_DOUBLE;

	// pd is already locked!
	FOR_EACH_PLAYER(p)
	{
		if (IS_HUMAN(p) && p->arena == arena && p->p_freq == freq)
			count++;
	}

	var->value = (double)count;

	return var;
}

local FormulaVariable * player_arena_callback(Player *p)
{
	FormulaVariable *var = amalloc(sizeof(FormulaVariable));
	var->name = NULL;
	var->type = VAR_TYPE_ARENA;
	var->arena = p->arena;

	return var;
}

local FormulaVariable * player_freq_callback(Player *p)
{
	FormulaVariable *var = amalloc(sizeof(FormulaVariable));
	var->name = NULL;
	var->type = VAR_TYPE_FREQ;
	var->freq.arena = p->arena;
	var->freq.freq = p->p_freq;

	return var;
}

local FormulaVariable * player_bounty_callback(Player *p)
{
	FormulaVariable *var = amalloc(sizeof(FormulaVariable));
	var->name = NULL;
	var->type = VAR_TYPE_DOUBLE;
	var->value = (double)p->position.bounty;

	return var;
}

local double evaluate_function(const char *name, double *params, int param_len, char *error_buf, int buf_len)
{
	// TODO: add random functions

	if (param_len == 1)
	{
		double (*func)(double) = NULL;

		if (strcasecmp(name, "abs") == 0)
			func = fabs;
		else if (strcasecmp(name, "acos") == 0)
			func = acos;
		else if (strcasecmp(name, "acosh") == 0)
			func = acosh;
		else if (strcasecmp(name, "asin") == 0)
			func = asin;
		else if (strcasecmp(name, "asinh") == 0)
			func = asinh;
		else if (strcasecmp(name, "atan") == 0)
			func = atan;
		else if (strcasecmp(name, "atanh") == 0)
			func = atanh;
		else if (strcasecmp(name, "ceil") == 0)
			func = ceil;
		else if (strcasecmp(name, "cos") == 0)
			func = cos;
		else if (strcasecmp(name, "cosh") == 0)
			func = cosh;
		else if (strcasecmp(name, "exp") == 0)
			func = exp;
		else if (strcasecmp(name, "floor") == 0)
			func = floor;
		else if (strcasecmp(name, "log") == 0)
			func = log;
		else if (strcasecmp(name, "log10") == 0)
			func = log10;
		else if (strcasecmp(name, "log2") == 0)
			func = log2;
		else if (strcasecmp(name, "round") == 0)
			func = round;
		else if (strcasecmp(name, "sin") == 0)
			func = sin;
		else if (strcasecmp(name, "sinh") == 0)
			func = sinh;
		else if (strcasecmp(name, "sqrt") == 0)
			func = sqrt;
		else if (strcasecmp(name, "tan") == 0)
			func = tan;
		else if (strcasecmp(name, "tanh") == 0)
			func = tanh;
		else if (strcasecmp(name, "trunc") == 0)
			func = trunc;
		else
		{
			snprintf(error_buf, buf_len, "Unknown unary function '%s'", name);
			return 0.0;
		}
		return (func)(params[0]);
	}
	else if (param_len == 2)
	{
		double (*func)(double, double) = NULL;
		if (strcasecmp(name, "atan2") == 0)
			func = atan2;
		else if (strcasecmp(name, "hypot") == 0)
			func = hypot;
		else if (strcasecmp(name, "max") == 0)
			func = fmax;
		else if (strcasecmp(name, "min") == 0)
			func = fmin;
		else if (strcasecmp(name, "mod") == 0)
			func = fmod;
		else if (strcasecmp(name, "remainder") == 0)
			func = remainder;
		else
		{
			snprintf(error_buf, buf_len, "Unknown binary function '%s'", name);
			return 0.0;
		}
		return (func)(params[0], params[1]);
	}
	else
	{
		snprintf(error_buf, buf_len, "Too many parameters to function '%s'", name);
		return 0.0;
	}
}

local void free_ast(ASTNode *node)
{
	switch (node->type)
	{
		case NODE_TYPE_BINOP:
			free_ast(node->binop.left);
			free_ast(node->binop.right);
			break;
		case NODE_TYPE_UNOP:
			free_ast(node->unop.left);
			break;
		case NODE_TYPE_FUNCTION:
		{
			afree(node->func.name);
			Link *link;
			for (link = LLGetHead(node->func.arguments); link; link = link->next)
			{
				ASTNode *i = link->data;
				free_ast(i);
			}
			LLFree(node->func.arguments);
			break;
		}
		case NODE_TYPE_VAR:
			afree(node->var.name);
			break;
		case NODE_TYPE_DEREF:
			afree(node->deref.field);
			free_ast(node->deref.var);
			break;
		case NODE_TYPE_CONST:
			break;
		case NODE_TYPE_LOGICAL:
			if (node->logical.op != LOGICAL_TRUE && node->logical.op != LOGICAL_FALSE)
			{
				free_ast(node->logical.left);
				free_ast(node->logical.right);
			}
			break;
		case NODE_TYPE_TRINARY:
			free_ast(node->trinary.logical);
			free_ast(node->trinary.left);
			free_ast(node->trinary.right);
			break;
		case NODE_TYPE_FOR:
		{
			Link *link;
			afree(node->fornode.i);
			free_ast(node->fornode.var);
			if (node->fornode.minus)
				free_ast(node->fornode.minus);
			afree(node->fornode.ret);
			for (link = LLGetHead(node->fornode.body); link; link = link->next)
			{
				AssignmentNode *a = link->data;
				afree(a->name);
				free_ast(a->right);
				afree(a);
			}
			LLFree(node->fornode.body);
			break;
		}
		default:
			lm->Log(L_ERROR, "<formula> Unknown AST node type!");
			break;
	}

	afree(node);
}

local void FreeFormula(Formula *formula)
{
	if (formula->assign_list)
	{
		Link *link;
		for (link = LLGetHead(formula->assign_list); link; link = link->next)
		{
			AssignmentNode *node = link->data;
			afree(node->name);
			free_ast(node->right);
			afree(node);
		}

		LLFree(formula->assign_list);
	}

	if (formula->func_decl)
	{
		Link *link;
		afree(formula->func_decl->name);
		for (link = LLGetHead(formula->func_decl->params); link; link = link->next)
		{
			afree(link->data);
		}
		free_ast(formula->func_decl->body);
	}


	afree(formula);
}

local Formula * ParseFormula(const char *string, char *error_buffer, int error_buffer_length)
{
	Formula *formula;

	/* call the formula parser (in parse.y) */
	formula = parse_formula(string, error_buffer, error_buffer_length);

	return formula;
}

local FormulaVariable * evaluate_variable(ASTNode *node, HashTable *vars, char *error_buffer, int error_buffer_length, LinkedList *temp_vars)
{
	FormulaVariable *ret_val;
	if (node->type == NODE_TYPE_VAR)
	{
		FormulaVariable *var = HashGetOne(vars, node->var.name);
		if (var)
		{
			return var;
		}
		else
		{
			snprintf(error_buffer, error_buffer_length, "Unknown variable '%s'", node->var.name);
			return NULL;
		}
	}
	else if (node->type == NODE_TYPE_DEREF)
	{
		FormulaVariable *var = evaluate_variable(node->deref.var, vars, error_buffer, error_buffer_length, temp_vars);
		if (var)
		{
			switch (var->type)
			{
				case VAR_TYPE_ARENA:
				{
					ArenaPropertyCallback cb = HashGetOne(arena_callbacks, node->deref.field);
					if (cb)
					{
						ret_val = cb(var->arena);
						LLAdd(temp_vars, ret_val);
						return ret_val;
					}
					else
					{
						snprintf(error_buffer, error_buffer_length, "Cannot dereference arena variable with '%s'", node->deref.field);
						return NULL;
					}
				}
				case VAR_TYPE_FREQ:
				{
					FreqPropertyCallback cb = HashGetOne(freq_callbacks, node->deref.field);
					if (cb)
					{
						ret_val = cb(var->freq.arena, var->freq.freq);
						LLAdd(temp_vars, ret_val);
						return ret_val;
					}
					else
					{
						snprintf(error_buffer, error_buffer_length, "Cannot dereference freq variable with '%s'", node->deref.field);
						return NULL;
					}
				}
				case VAR_TYPE_PLAYER:
				{
					PlayerPropertyCallback cb = HashGetOne(player_callbacks, node->deref.field);
					if (cb)
					{
						ret_val = cb(var->p);
						LLAdd(temp_vars, ret_val);
						return ret_val;
					}
					else
					{
						snprintf(error_buffer, error_buffer_length, "Cannot dereference player variable with '%s'", node->deref.field);
						return NULL;
					}
				}
				case VAR_TYPE_FREQ_LIST:
				case VAR_TYPE_PLAYER_LIST:
					if (strcmp(node->deref.field, "length") == 0)
					{
						FormulaVariable *length = amalloc(sizeof(FormulaVariable));
						length->name = NULL; /* temporary only */
						length->type = VAR_TYPE_DOUBLE;
						length->value = (double)LLCount(&var->list);
						LLAdd(temp_vars, length);
						return length;
					}
					else
					{
						snprintf(error_buffer, error_buffer_length, "Cannot dereference list variable with '%s'", node->deref.field);
						return NULL;
					}
				case VAR_TYPE_DOUBLE:
					snprintf(error_buffer, error_buffer_length, "Cannot dereference numerical variable with '%s'", node->deref.field);
					return NULL;
				default:
					lm->Log(L_ERROR, "<formula> Unknown variable type!");
					return NULL;
			}
		}
		else
		{
			/* already printed error */
			return NULL;
		}
	}
	else
	{
		lm->Log(L_ERROR, "<formula> Asked to evaluate variable in bad context.");
		return NULL;
	}
}

local int evaluate_logical(ASTNode *node, HashTable *vars, char *error_buffer, int error_buffer_length, LinkedList *temp_vars)
{
	if (node->type == NODE_TYPE_LOGICAL)
	{
		if (node->logical.op == LOGICAL_TRUE)
			return 1;
		else if (node->logical.op == LOGICAL_FALSE)
			return 0;
		else if (node->logical.op == LOGICAL_AND)
		{
			int left = evaluate_logical(node->logical.left, vars, error_buffer, error_buffer_length, temp_vars);
			int right = evaluate_logical(node->logical.right, vars, error_buffer, error_buffer_length, temp_vars);
			return (left && right);
		}
		else if (node->logical.op == LOGICAL_OR)
		{
			int left = evaluate_logical(node->logical.left, vars, error_buffer, error_buffer_length, temp_vars);
			int right = evaluate_logical(node->logical.right, vars, error_buffer, error_buffer_length, temp_vars);
			return (left || right);
		}
		else
		{
			double left = evaluate_ast(node->logical.left, vars, error_buffer, error_buffer_length, temp_vars);
			double right = evaluate_ast(node->logical.right, vars, error_buffer, error_buffer_length, temp_vars);
			switch (node->logical.op)
			{
				case LOGICAL_LT:
					return (left < right);
				case LOGICAL_LTE:
					return (left <= right);
				case LOGICAL_EQ:
					return (left == right);
				case LOGICAL_GTE:
					return (left >= right);
				case LOGICAL_GT:
					return (left > right);
				case LOGICAL_NEQ:
					return (left != right);
				default:
					lm->Log(L_ERROR, "<formula> Unknown logical operand '%d'", node->logical.op);
					return 0;
			}
		}
	}
	else
	{
		lm->Log(L_ERROR, "<formula> Asked to evaluate non-logical in logical context.");
		return 0;
	}
}

local double evaluate_ast(ASTNode *node, HashTable *vars, char *error_buffer, int error_buffer_length, LinkedList *temp_vars)
{
	double left, right;
	switch (node->type)
	{
		case NODE_TYPE_BINOP:
			left = evaluate_ast(node->binop.left, vars, error_buffer, error_buffer_length, temp_vars);
			right = evaluate_ast(node->binop.right, vars, error_buffer, error_buffer_length, temp_vars);
			switch (node->binop.op)
			{
				case '+':
					return left + right;
				case '-':
					return left - right;
				case '*':
					return left * right;
				case '/':
					return left / right;
				case '^':
					return pow(left, right);
				default:
					lm->Log(L_ERROR, "<formula> Unknown binary operand '%c'", node->binop.op);
					return 0.0;
			}
		case NODE_TYPE_UNOP:
		{
			left = evaluate_ast(node->unop.left, vars, error_buffer, error_buffer_length, temp_vars);
			if (node->unop.op == '-')
				return -left;

			lm->Log(L_ERROR, "<formula> Unknown unary operand '%c'", node->unop.op);
			return 0.0;
		}
		case NODE_TYPE_FUNCTION:
		{
			int param_len = LLCount(node->func.arguments);
			double *params = amalloc(sizeof(double)*param_len);
			Link *link;
			int i = 0;
			double result;
			for (link = LLGetHead(node->func.arguments); link; link = link->next)
			{
				params[i] = evaluate_ast(link->data, vars, error_buffer, error_buffer_length, temp_vars);
				i++;
			}
			result = evaluate_function(node->func.name, params, param_len, error_buffer, error_buffer_length);
			afree(params);
			return result;
		}
		case NODE_TYPE_VAR:
		case NODE_TYPE_DEREF:
		{
			FormulaVariable *var = evaluate_variable(node, vars, error_buffer, error_buffer_length, temp_vars);
			if (var)
			{
				if (var->type == VAR_TYPE_DOUBLE)
				{
					return var->value;
				}
				else
				{
					snprintf(error_buffer, error_buffer_length, "Variable type is not numeric! Use the '.' operator.");
					return 0.0;
				}
			}
			else
			{
				/* error already printed by evaluate_variable */
				return 0.0;
			}
		}
		case NODE_TYPE_CONST:
			return node->c.val;
		case NODE_TYPE_LOGICAL:
			lm->Log(L_ERROR, "<formula> Asked to evaluate a logical in double context!");
			return 0.0;
		case NODE_TYPE_TRINARY:
		{
			int logical = evaluate_logical(node->trinary.logical, vars, error_buffer, error_buffer_length, temp_vars);
			if (logical)
			{
				return evaluate_ast(node->trinary.left, vars, error_buffer, error_buffer_length, temp_vars);
			}
			else
			{
				return evaluate_ast(node->trinary.right, vars, error_buffer, error_buffer_length, temp_vars);
			}
		}
		case NODE_TYPE_FOR:
		{
			if (node->fornode.var->type == NODE_TYPE_VAR || node->fornode.var->type == NODE_TYPE_DEREF)
			{
				FormulaVariable *var = evaluate_variable(node->fornode.var, vars, error_buffer, error_buffer_length, temp_vars);
				FormulaVariable *minus = NULL;

				if (var)
				{
					if (node->fornode.minus)
					{
						if (node->fornode.minus->type == NODE_TYPE_VAR || node->fornode.minus->type == NODE_TYPE_DEREF)
						{
							minus = evaluate_variable(node->fornode.minus, vars, error_buffer, error_buffer_length, temp_vars);
							if (!minus)
							{
								return 0.0;
							}
						}
						else
						{
							lm->Log(L_ERROR, "<formula> Bad AST node type in fornode.minus!");
							return 0.0;
						}
					}

					// check for list type for var
					if (var->type == VAR_TYPE_FREQ_LIST)
					{
						Link *link;
						int minus_freq = -1;
						FormulaVariable i;

						// check minus's type
						if (minus)
						{
							if (minus->type == VAR_TYPE_FREQ)
							{
								minus_freq = minus->freq.freq;
							}
							else
							{
								snprintf(error_buffer, error_buffer_length, "When iterating a freq list, the excluded var must be a freq!");
								return 0.0;
							}
						}

						i.name = NULL;
						i.type = VAR_TYPE_FREQ;
						HashAddFront(vars, node->fornode.i, &i);

						for (link = LLGetHead(&var->list); link; link = link->next)
						{
							Freq *freq = link->data;
							if (minus_freq != freq->freq)
							{
								Link *assign_link;
								i.freq.freq = freq->freq;
								i.freq.arena = freq->arena;
								for (assign_link = LLGetHead(node->fornode.body); assign_link; assign_link = assign_link->next)
								{
									AssignmentNode *assign = assign_link->data;
									evaluate_assignment(assign, vars, error_buffer, error_buffer_length, temp_vars);
								}
							}
						}

						HashRemove(vars, node->fornode.i, &i);
					}
					else if (var->type == VAR_TYPE_PLAYER_LIST)
					{
						Link *link;
						Player *minus_p = NULL;
						FormulaVariable i;

						// check minus's type
						if (minus)
						{
							if (minus->type == VAR_TYPE_PLAYER)
							{
								minus_p = minus->p;
							}
							else
							{
								snprintf(error_buffer, error_buffer_length, "When iterating a player list, the excluded var must be a player!");
								return 0.0;
							}
						}

						i.name = NULL;
						i.type = VAR_TYPE_PLAYER;
						HashAddFront(vars, node->fornode.i, &i);

						for (link = LLGetHead(&var->list); link; link = link->next)
						{
							Player *p = link->data;
							if (p != minus_p)
							{
								Link *assign_link;
								i.p = p;
								for (assign_link = LLGetHead(node->fornode.body); assign_link; assign_link = assign_link->next)
								{
									AssignmentNode *assign = assign_link->data;
									evaluate_assignment(assign, vars, error_buffer, error_buffer_length, temp_vars);
								}
							}
						}

						HashRemove(vars, node->fornode.i, &i);
					}
					else
					{
						snprintf(error_buffer, error_buffer_length, "For loops must be given a player list or a freq list!");
						return 0.0;
					}

					FormulaVariable *ret = HashGetOne(vars, node->fornode.ret);
					if (ret)
					{
						if (ret->type == VAR_TYPE_DOUBLE)
						{
							return ret->value;
						}
						else
						{
							snprintf(error_buffer, error_buffer_length, "Bad for loop return type");
							return 0.0;
						}
					}
					else
					{
						snprintf(error_buffer, error_buffer_length, "Unknown variable '%s'", node->fornode.ret);
						return 0.0;
					}
				}
				else
				{
					/* error already printed by evaluate_variable */
					return 0.0;
				}
			}
			else
			{
				lm->Log(L_ERROR, "<formula> Bad AST node type in fornode.var!");
				return 0.0;
			}
		}
		default:
			lm->Log(L_ERROR, "<formula> Unknown AST node type!");
			return 0.0;
	}
}

local double evaluate_assignment(AssignmentNode *node, HashTable *vars, char *error_buffer, int error_buffer_length, LinkedList *temp_vars)
{
	double value = evaluate_ast(node->right, vars, error_buffer, error_buffer_length, temp_vars);

	FormulaVariable *var = HashGetOne(vars, node->name);
	if (var)
	{
		if (var->type == VAR_TYPE_FREQ_LIST)
		{
			LLEnum(&var->list, afree);
			LLEmpty(&var->list);
		}
		else if (var->type == VAR_TYPE_PLAYER_LIST)
		{
			LLEmpty(&var->list);
		}
	}
	else
	{
		var = amalloc(sizeof(FormulaVariable));
		var->name = astrdup(node->name);
		HashAdd(vars, var->name, var);
		LLAdd(temp_vars, var);
	}

	var->type = VAR_TYPE_DOUBLE;
	var->value = value;

	return value;
}

local double EvaluateFormula(Formula *formula, HashTable *vars, char *ret_var_name, char *error_buffer, int error_buffer_length)
{
	if (formula->assign_list)
	{
		Link *link;
		LinkedList temp_vars;
		double value = 0.0;

		LLInit(&temp_vars);

		pd->Lock();
		for (link = LLGetHead(formula->assign_list); link; link = link->next)
		{
			AssignmentNode *assign = link->data;
			value = evaluate_assignment(assign, vars, error_buffer, error_buffer_length, &temp_vars);
			if (ret_var_name)
			{
				snprintf(ret_var_name, MAX_VAR_NAME_LENGTH, "%s", assign->name);
			}
		}
		pd->Unlock();

		// delete temporaries and delete created vars if ret_var_name == NULL
		for (link = LLGetHead(&temp_vars); link; link = link->next)
		{
			FormulaVariable *var = link->data;
			if (ret_var_name == NULL && var->name != NULL)
			{
				// remove it from the hash table
				HashRemove(vars, var->name, var);
			}
			if (ret_var_name == NULL || var->name == NULL)
			{
				if (var->type == VAR_TYPE_FREQ_LIST)
				{
					LLEnum(&var->list, afree);
					LLEmpty(&var->list);
				}
				else if (var->type == VAR_TYPE_PLAYER_LIST)
				{
					LLEmpty(&var->list);
				}
				if (var->name)
				{
					afree(var->name);
				}
				afree(var);
			}
		}
		LLEmpty(&temp_vars);

		return value;
	}
	else
	{
		snprintf(error_buffer, error_buffer_length, "Functions not yet supported!");
		return 0.0;
	}

}

local int EvaluateFormulaInt(Formula * formula, HashTable *vars, char *ret_var_name, char *error_buffer, int error_buffer_length, int default_val)
{
	double result = EvaluateFormula(formula, vars, ret_var_name, error_buffer, error_buffer_length);

	if (fpclassify(result) == FP_NORMAL)
	{
		result = round(result);
		if (INT_MIN <= result && result <= INT_MAX)
		{
			return (int)result;
		}
	}

	return default_val;
}

void RegArenaProperty(const char *property, ArenaPropertyCallback cb)
{
	HashAdd(arena_callbacks, property, cb);
}

void UnregArenaProperty(const char *property, ArenaPropertyCallback cb)
{
	HashRemove(arena_callbacks, property, cb);
}

void RegFreqProperty(const char *property, FreqPropertyCallback cb)
{
	HashAdd(freq_callbacks, property, cb);
}

void UnregFreqProperty(const char *property, FreqPropertyCallback cb)
{
	HashRemove(arena_callbacks, property, cb);
}

void RegPlayerProperty(const char *property, PlayerPropertyCallback cb)
{
	HashAdd(player_callbacks, property, cb);
}

void UnregPlayerProperty(const char *property, PlayerPropertyCallback cb)
{
	HashRemove(arena_callbacks, property, cb);
}

local Iformula interface =
{
	INTERFACE_HEAD_INIT(I_FORMULA, "formula")
	ParseFormula, FreeFormula, EvaluateFormula, EvaluateFormulaInt,
	RegArenaProperty, UnregArenaProperty,
	RegFreqProperty, UnregFreqProperty,
	RegPlayerProperty, UnregPlayerProperty,
};

EXPORT const char info_formula[] = "v2.0 Dr Brain <drbrain@gmail.com>";

EXPORT int MM_formula(int action, Imodman *_mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;

		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);

		if (!lm || !pd)
			return MM_FAIL;

		arena_callbacks = HashAlloc();
		freq_callbacks = HashAlloc();
		player_callbacks = HashAlloc();

		RegArenaProperty("players", arena_players_callback);
		RegArenaProperty("specs", arena_specs_callback);
		RegArenaProperty("everyone", arena_everyone_callback);
		RegArenaProperty("freqs", arena_freqs_callback);
		RegArenaProperty("jackpot", arena_jackpot_callback);
		RegArenaProperty("size", arena_size_callback);
		RegFreqProperty("players", freq_players_callback);
		RegFreqProperty("specs", freq_specs_callback);
		RegFreqProperty("everyone", freq_everyone_callback);
		RegFreqProperty("arena", freq_arena_callback);
		RegFreqProperty("size", freq_size_callback);
		RegPlayerProperty("arena", player_arena_callback);
		RegPlayerProperty("freq", player_freq_callback);
		RegPlayerProperty("bounty", player_bounty_callback);

		mm->RegInterface(&interface, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&interface, ALLARENAS))
			return MM_FAIL;

		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(pd);

		UnregArenaProperty("players", arena_players_callback);
		UnregArenaProperty("specs", arena_specs_callback);
		UnregArenaProperty("everyone", arena_everyone_callback);
		UnregArenaProperty("freqs", arena_freqs_callback);
		UnregArenaProperty("jackpot", arena_jackpot_callback);
		UnregArenaProperty("size", arena_size_callback);
		UnregFreqProperty("players", freq_players_callback);
		UnregFreqProperty("specs", freq_specs_callback);
		UnregFreqProperty("everyone", freq_everyone_callback);
		UnregFreqProperty("arena", freq_arena_callback);
		UnregFreqProperty("size", freq_size_callback);
		UnregPlayerProperty("arena", player_arena_callback);
		UnregPlayerProperty("freq", player_freq_callback);
		UnregPlayerProperty("bounty", player_bounty_callback);

		HashFree(arena_callbacks);
		HashFree(freq_callbacks);
		HashFree(player_callbacks);

		return MM_OK;
	}
	return MM_FAIL;
}

