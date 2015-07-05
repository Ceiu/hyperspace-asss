#ifndef PARSE_H
#define PARSE_H

typedef struct ASTNode ASTNode;

typedef enum LogicalOperator {
	LOGICAL_LT,
	LOGICAL_LTE,
	LOGICAL_EQ,
	LOGICAL_GTE,
	LOGICAL_GT,
	LOGICAL_NEQ,
	LOGICAL_AND,
	LOGICAL_OR,
	LOGICAL_TRUE,
	LOGICAL_FALSE
} LogicalOperator;

typedef enum ASTNodeType {
	NODE_TYPE_BINOP,
	NODE_TYPE_UNOP,
	NODE_TYPE_FUNCTION,
	NODE_TYPE_VAR,
	NODE_TYPE_DEREF,
	NODE_TYPE_CONST,
	NODE_TYPE_LOGICAL,
	NODE_TYPE_TRINARY,
	NODE_TYPE_FOR,
} ASTNodeType;

typedef struct BinopNode {
	char op;
	ASTNode *left;
	ASTNode *right;
} BinopNode;

typedef struct UnopNode {
	char op;
	ASTNode *left;
} UnopNode;

typedef struct FunctionNode {
	char *name;
	LinkedList *arguments;
} FunctionNode;

typedef struct VarNode {
	char *name;
} VarNode;

typedef struct DerefNode {
	char *field;
	ASTNode *var;
} DerefNode;

typedef struct ConstNode {
	double val;
} ConstNode;

typedef struct LogicalNode {
	LogicalOperator op;
	ASTNode *left;
	ASTNode *right;
} LogicalNode;

typedef struct TrinaryNode {
	ASTNode *logical;
	ASTNode *left;
	ASTNode *right;
} TrinaryNode;

typedef struct ForNode {
	char *i;
	ASTNode *var;
	ASTNode *minus;
	char *ret;
	LinkedList *body;
} ForNode;

struct ASTNode {
	ASTNodeType type;
	union {
		UnopNode unop;
		BinopNode binop;
		FunctionNode func;
		VarNode var;
		DerefNode deref;
		ConstNode c;
		LogicalNode logical;
		TrinaryNode trinary;
		ForNode fornode;
	};
};

typedef struct AssignmentNode {
	char *name;
	ASTNode *right;
} AssignmentNode;

typedef struct FunctionDecl {
	char *name;
	LinkedList *params;
	ASTNode *body;
} FunctionDecl;

struct Formula {
	LinkedList *assign_list;
	FunctionDecl *func_decl;
};

/* Used to return the AST to the parse function,
 * along with an error message if necessary */
typedef struct ParseObject {
	LinkedList *assign_list;
	FunctionDecl *func_decl;
	int error;
	char *error_buffer;
	int error_buffer_len;
} ParseObject;

typedef void* yyscan_t;
typedef void* YY_BUFFER_STATE;

int yylex(void *yylval_param, yyscan_t scanner, ParseObject *parse_object);
int yylex_init(yyscan_t *scanner_p);
int yylex_destroy(yyscan_t scanner);
YY_BUFFER_STATE yy_scan_string (const char *str, yyscan_t scanner);
void yy_delete_buffer(YY_BUFFER_STATE b, yyscan_t scanner);
void yyerror(ParseObject *parse_object, yyscan_t scanner, char *message);


struct Formula * parse_formula(const char *string, char *error_buffer, int error_buffer_len);

#endif /* PARSE_H */
