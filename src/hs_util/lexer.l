%{
#include <stdlib.h>
#include "parser.h"
#include "asss.h"

#define YY_DECL int yylex(YYSTYPE *yylval_param , yyscan_t yyscanner, void *parse_object)

void yyerror(void **ret, yyscan_t scanner, char *s);

%}

%option reentrant bison-bridge
%option noyywrap
%option nounput

DIGIT		[0-9]
LETTER		[a-zA-Z]

%%

"e"		return E;

"pi"		return PI;

"true"		return T;

"false"		return F;

"for"		return FOR;

"<"		return LT;

"<="		return LTE;

"=="		return EQ;

">="		return GTE;

">"		return GT;

"!="		return NEQ;

"&&"		return AND;

"||"		return OR;

${LETTER}({LETTER}|{DIGIT})* {
			yylval->name = astrdup(yytext+1);
			return NAME;
		}

{LETTER}({LETTER}|{DIGIT})* {
			yylval->name = astrdup(yytext);
			return NAME;
		}

{DIGIT}+("."{DIGIT}*)? |
"."{DIGIT}+	{
			yylval->val = atof(yytext);
			return NUMBER;
		}

[-+*/^(),?:={};.]	return *yytext;

[ \t\n]+	;

.		{
			yyerror(parse_object, yyscanner, "invalid character");
			return '!';
		}

%%

