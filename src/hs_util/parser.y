%{
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "asss.h"
#include "formula.h"
#include "ast.h"

static ASTNode * create_binop(char op, ASTNode *left, ASTNode *right);
static ASTNode * create_unop(char op, ASTNode *left);
static ASTNode * create_const(double c);
static ASTNode * create_var(char *name);
static ASTNode * create_deref(ASTNode *var, char *name);
static ASTNode * create_func(char *name, LinkedList *arguments);
static ASTNode * create_logical(LogicalOperator op, ASTNode *left, ASTNode *right);
static ASTNode * create_trinary(ASTNode *logical, ASTNode *left, ASTNode *right);
static LinkedList * create_param_list(ASTNode *param);
static LinkedList * expand_param_list(LinkedList *list, ASTNode *param);
//static LinkedList * create_param_decl_list(char *param);
//static LinkedList * expand_param_decl_list(LinkedList *list, char *param);
static LinkedList * expand_assignment_list(LinkedList *list, AssignmentNode *param);
static LinkedList * create_assignment_list(AssignmentNode *param);
static ASTNode * create_for(char *i, ASTNode *var, ASTNode *minus, char *ret, LinkedList *body);
static AssignmentNode * create_assignment(char *left, ASTNode *right);
//static FunctionDecl * create_function_decl(char *name, LinkedList *params, ASTNode *body);

%}

%union {
	struct ASTNode *node;
	struct AssignmentNode *assign;
	struct FunctionDecl *func_decl;
	char * name;
	double val;
	struct LinkedList *list;
}

%token <val> NUMBER PI E
%token <name> NAME
%token <logical> T F
%token LT LTE EQ GTE GT NEQ AND OR
%token FOR
%type <node> stmt expr var logical function for
%type <list> params /*param_decl*/ assignment_list
%type <assign> assignment
/*%type <func_decl> function_decl*/

%pure_parser
%parse-param {struct ParseObject *parse_object}
%parse-param {yyscan_t *scanner}
%lex-param {yyscan_t *scanner}
%lex-param {struct ParseObject *parse_object}

%nonassoc '='
%right '?'
%right ':'
%left OR
%left AND
%left EQ NEQ
%left LT LTE GTE GT
%left '+' '-'
%left '*' '/'
%right '^'
%nonassoc UMINUS

%%

stmt:
	assignment_list	{ parse_object->assign_list = $1; }
	/*| function_decl	{ parse_object->func_decl = $1; }*/
	;

assignment:
	expr			{ $$ = create_assignment(astrdup("ans"), $1); }
	| logical		{ $$ = create_assignment(astrdup("ans"), create_trinary($1, create_const(1), create_const(0))); }
	| NAME '=' expr 	{ $$ = create_assignment($1, $3); }
	| NAME '=' logical	{ $$ = create_assignment($1, create_trinary($3, create_const(1), create_const(0))); }
	;

assignment_list:
	assignment		{ $$ = create_assignment_list($1); }
	| assignment_list ';' assignment	{ $$ = expand_assignment_list($1, $3); }
	| assignment_list ';'	{ $$ = $1; }
	;

logical:
	T			{ $$ = create_logical(LOGICAL_TRUE, NULL, NULL); }
	| F			{ $$ = create_logical(LOGICAL_FALSE, NULL, NULL); }
	| expr LT expr		{ $$ = create_logical(LOGICAL_LT, $1, $3); }
	| expr LTE expr		{ $$ = create_logical(LOGICAL_LTE, $1, $3); }
	| expr EQ expr		{ $$ = create_logical(LOGICAL_EQ, $1, $3); }
	| expr GTE expr		{ $$ = create_logical(LOGICAL_GTE, $1, $3); }
	| expr GT expr		{ $$ = create_logical(LOGICAL_GT, $1, $3); }
	| expr NEQ expr		{ $$ = create_logical(LOGICAL_NEQ, $1, $3); }
	| logical AND logical	{ $$ = create_logical(LOGICAL_AND, $1, $3); }
	| logical OR logical	{ $$ = create_logical(LOGICAL_OR, $1, $3); }
	| '(' logical ')'	{ $$ = $2; }
	;

var:
	NAME			{ $$ = create_var($1); }
	| var '.' NAME		{ $$ = create_deref($1, $3); }
	;

params:
	expr			{ $$ = create_param_list($1); }
	| expr ',' params	{ $$ = expand_param_list($3, $1); }
	;

function:
	NAME '(' params ')'	{ $$ = create_func($1, $3); }
	;

/*param_decl:
	NAME			{ $$ = create_param_decl_list($1); }
	| NAME ',' param_decl	{ $$ = expand_param_decl_list($3, $1); }
	;

function_decl:
	NAME '(' param_decl ')'	'=' expr	{ $$ = create_function_decl($1, $3, $6); }
	;*/

for:
	FOR '(' NAME ':' var ',' NAME ')' '{' assignment_list '}'	{ $$ = create_for($3, $5, NULL, $7, $10); }
	| FOR '(' NAME ':' var '-' var ',' NAME ')' '{' assignment_list '}'	{ $$ = create_for($3, $5, $7, $9, $12); }
	;

expr:
	var			{ $$ = $1; }
	| function		{ $$ = $1; }
	| for			{ $$ = $1; }
	| NUMBER		{ $$ = create_const($1); }
	| PI			{ $$ = create_const(M_PI); }
	| E			{ $$ = create_const(M_E); }
	| logical '?' expr ':' expr	{ $$ = create_trinary($1, $3, $5); }
	| '-' expr %prec UMINUS	{ $$ = create_unop('-', $2); }
	| expr '+' expr		{ $$ = create_binop('+', $1, $3); }
	| expr '-' expr		{ $$ = create_binop('-', $1, $3); }
	| expr '*' expr		{ $$ = create_binop('*', $1, $3); }
	| expr '/' expr		{ $$ = create_binop('/', $1, $3); }
	| expr '^' expr 	{ $$ = create_binop('^', $1, $3); }
	| '(' expr ')'		{ $$ = $2; }
	;

%%

void yyerror(struct ParseObject *parse_object, yyscan_t scanner, char *s)
{
	/* keep the original error message, if one's occured */
	if (parse_object->error == 0)
	{
		parse_object->error = 1;
		snprintf(parse_object->error_buffer,
			parse_object->error_buffer_len, "%s", s);
	}
}

/* Returns a pointer to an ASTNode, or NULL. If NULL is returned, then
 * error_buffer will have been written to (no more than error_buffer_len bytes). */
struct Formula * parse_formula(const char *string, char *error_buffer, int error_buffer_len)
{
	YY_BUFFER_STATE string_buffer;
	yyscan_t scanner;

	/* setup parse_object */
	struct ParseObject parse_object;
	parse_object.assign_list = NULL;
	parse_object.func_decl = NULL;
	parse_object.error = 0;
	parse_object.error_buffer = error_buffer;
	parse_object.error_buffer_len = error_buffer_len;

	/* setup the lexer */
	yylex_init(&scanner);
	string_buffer = yy_scan_string(string, scanner);

	/* call the scanner */
	yyparse(&parse_object, scanner);

	/* free stuff */
	yy_delete_buffer(string_buffer, scanner);
	yylex_destroy(scanner);

	/* if there was an error, return NULL. Otherwise, return the AST. */
	if (parse_object.error)
		return NULL;
	else
	{
		struct Formula *f = amalloc(sizeof(*f));
		f->assign_list = parse_object.assign_list;
		f->func_decl = parse_object.func_decl;
		return f;
	}
}

static ASTNode * create_binop(char op, ASTNode *left, ASTNode *right)
{
	ASTNode *node = amalloc(sizeof(ASTNode));
	node->type = NODE_TYPE_BINOP;
	node->binop.op = op;
	node->binop.left = left;
	node->binop.right = right;

	return node;
}

static ASTNode * create_unop(char op, ASTNode *left)
{
	ASTNode *node = amalloc(sizeof(ASTNode));
	node->type = NODE_TYPE_UNOP;
	node->unop.op = op;
	node->unop.left = left;

	return node;
}

static ASTNode * create_const(double c)
{
	ASTNode *node = amalloc(sizeof(ASTNode));
	node->type = NODE_TYPE_CONST;
	node->c.val = c;

	return node;
}

static ASTNode * create_var(char *name)
{
	ASTNode *node = amalloc(sizeof(ASTNode));
	node->type = NODE_TYPE_VAR;
	node->var.name = name;

	return node;
}

static ASTNode * create_deref(ASTNode *var, char *name)
{
	ASTNode *node = amalloc(sizeof(ASTNode));
	node->type = NODE_TYPE_DEREF;
	node->deref.field = ToLowerStr(name);
	node->deref.var = var;

	return node;
}

/*static ASTNode * create_unary_func(char *name, ASTNode *left)
{
	ASTNode *node = amalloc(sizeof(ASTNode));
	node->type = NODE_TYPE_UNFUNC;
	node->unfunc.name = name;
	node->unfunc.left = left;
	node->unfunc.func = NULL;

	return node;
}

static ASTNode * create_binary_func(char *name, ASTNode *left, ASTNode *right)
{
	ASTNode *node = amalloc(sizeof(ASTNode));
	node->type = NODE_TYPE_BINFUNC;
	node->binfunc.name = name;
	node->binfunc.left = left;
	node->binfunc.right = right;
	node->binfunc.func = NULL;

	return node;
}*/

static ASTNode * create_func(char *name, LinkedList *arguments)
{
	ASTNode *node = amalloc(sizeof(ASTNode));
	node->type = NODE_TYPE_FUNCTION;
	node->func.name = name;
	node->func.arguments = arguments;
	return node;
}

static ASTNode * create_logical(LogicalOperator op, ASTNode *left, ASTNode *right)
{
	ASTNode *node = amalloc(sizeof(ASTNode));
	node->type = NODE_TYPE_LOGICAL;
	node->logical.op = op;
	node->logical.left = left;
	node->logical.right = right;
	return node;
}

static ASTNode * create_trinary(ASTNode *logical, ASTNode *left, ASTNode *right)
{
	ASTNode *node = amalloc(sizeof(ASTNode));
	node->type = NODE_TYPE_TRINARY;
	node->trinary.logical = logical;
	node->trinary.left = left;
	node->trinary.right = right;
	return node;
}

static LinkedList * expand_param_list(LinkedList *list, ASTNode *param)
{
	LLAdd(list, param);
	return list;
}

static LinkedList * create_param_list(ASTNode *param)
{
	LinkedList *list = LLAlloc();
	expand_param_list(list, param);
	return list;
}

/*static LinkedList * expand_param_decl_list(LinkedList *list, char *param)
{
	LLAdd(list, param);
	return list;
}*/

/*static LinkedList * create_param_decl_list(char *param)
{
	LinkedList *list = LLAlloc();
	expand_param_decl_list(list, param);
	return list;
}*/

static LinkedList * expand_assignment_list(LinkedList *list, AssignmentNode *param)
{
	LLAdd(list, param);
	return list;
}

static LinkedList * create_assignment_list(AssignmentNode *param)
{
	LinkedList *list = LLAlloc();
	expand_assignment_list(list, param);
	return list;
}

static ASTNode * create_for(char *i, ASTNode *var, ASTNode *minus, char *ret, LinkedList *body)
{
	ASTNode *node = amalloc(sizeof(ASTNode));
	node->type = NODE_TYPE_FOR;
	node->fornode.i = i;
	node->fornode.var = var;
	node->fornode.minus = minus;
	node->fornode.ret = ret;
	node->fornode.body = body;
	return node;
}

static AssignmentNode * create_assignment(char *left, ASTNode *right)
{
	AssignmentNode *node = amalloc(sizeof(AssignmentNode));
	node->name = strndup(left, MAX_VAR_NAME_LENGTH - 1);
	afree(left);
	node->right = right;
	return node;
}

/*static FunctionDecl * create_function_decl(char *name, LinkedList *params, ASTNode *body)
{
	FunctionDecl *func_decl = amalloc(sizeof(FunctionDecl));
	func_decl->name = name;
	func_decl->params = params;
	func_decl->body = body;
	return func_decl;
}*/
