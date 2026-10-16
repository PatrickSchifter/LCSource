%{
// rcg10042001 Changed to specify Ecc directory...
#include "Ecc/StdH.h"
#include "Ecc/Main.h"
#include "Ecc/Parser.h"

#define YY_NEVER_INTERACTIVE 1

// Drop carriage returns while reading, as Windows text mode does, so CRLF
// sources scan the same everywhere (otherwise `.*` swallows "%}\r").
#define YY_INPUT(buf, result, max_size) {                  \
  int c = 0; size_t n = 0;                                  \
  while (n < (size_t)(max_size) && (c = getc(yyin)) != EOF) {\
    if (c == '\r') continue;                                \
    buf[n++] = (char)c;                                     \
    if (c == '\n') break;                                   \
  }                                                         \
  result = n;                                               \
}


int iLastLine = -1;

#define RETURN(symbolname)            \
  if (iLastLine==_iLinesCt || !_bTrackLineInformation){\
    yylval = yytext;                  \
  } else {                            \
    char strLine[1024];                \
    snprintf(strLine, sizeof(strLine), "\n#line %d \"%s\"\n", _iLinesCt, _strInputFileName);\
    yylval = SType(strLine)+yytext;  \
    iLastLine=_iLinesCt;             \
  }                                  \
  return symbolname;

// Accumulates %{ ... %} blocks; growing a std::string keeps this linear
// (re-copying the whole block per line needed gigabytes for Player.es).
#include <string>
static std::string _strCppBlock;
char *_strPreProc;

%}

%x COMMENT
%x CPPBLOCK
%x PREPROC

DIGIT		[0-9]
HEXDIGIT [0-9A-Fa-f]
IDENTIFIERFIRST [A-Za-z_]
IDENTIFIEROTHER [A-Za-z0-9_]
DOUBLEQUOTE	\"
NONEXP_FLT  ({DIGIT}+"."{DIGIT}*)
EXP_FLT (({DIGIT}+("."({DIGIT}*)?)?)("E"|"e")("+"|"-")?{DIGIT}+)

%%

 /* standard cpp-keywords */
"while"     {RETURN(k_while);}
"switch"    {RETURN(k_switch);}
"case"      {RETURN(k_case);}
"for"       {RETURN(k_for);}
"if"        {RETURN(k_if);}
"else"      {RETURN(k_else);}
"enum"      {RETURN(k_enum);}
"class"     {RETURN(k_class);}
"do"        {RETURN(k_do);}
"void"      {RETURN(k_void);}
"const"     {RETURN(k_const);}
"inline"    {RETURN(k_inline);}
"static"    {RETURN(k_static);}
"virtual"   {RETURN(k_virtual);}
"return"    {RETURN(k_return);}
"autowait"  {RETURN(k_autowait);}
"autocall"  {RETURN(k_autocall);}
"waitevent" {RETURN(k_waitevent);}

 /* aditional keywords */
"event"     {RETURN(k_event);}
"name"      {RETURN(k_name);}
"thumbnail" {RETURN(k_thumbnail);}
"features"  {RETURN(k_features);}
"uses"      {RETURN(k_uses);}
"export"    {RETURN(k_export);}

"texture"   {RETURN(k_texture);}
"sound"     {RETURN(k_sound);}
"model"     {RETURN(k_model);}
"skamodel"  {RETURN(k_skamodel);}
"editor"    {RETURN(k_editor);}

"properties"  {RETURN(k_properties);}
"components"  {RETURN(k_components);}
"functions"   {RETURN(k_functions);}
"procedures"  {RETURN(k_procedures);}

"wait"      {RETURN(k_wait     );}
"on"        {RETURN(k_on       );}
"otherwise" {RETURN(k_otherwise);}

"call"      {RETURN(k_call     );}
"jump"      {RETURN(k_jump     );}
"stop"      {RETURN(k_stop     );}
"resume"    {RETURN(k_resume   );}
"pass"      {RETURN(k_pass     );}

 /* special data types */
"CTString"          {RETURN(k_CTString);}
"CTStringTrans"     {RETURN(k_CTStringTrans);}
"CTFileName"        {RETURN(k_CTFileName);}
"CTFileNameNoDep"   {RETURN(k_CTFileNameNoDep);}
"BOOL"              {RETURN(k_BOOL);}
"COLOR"             {RETURN(k_COLOR);}
"FLOAT"             {RETURN(k_FLOAT);}
"INDEX"             {RETURN(k_INDEX);}
"RANGE"             {RETURN(k_RANGE);}
"CEntityPointer"    {RETURN(k_CEntityPointer);}
"CModelObject"      {RETURN(k_CModelObject);}
"CModelInstance"    {RETURN(k_CModelInstance);}
"CAnimObject"       {RETURN(k_CAnimObject);}
"CSoundObject"      {RETURN(k_CSoundObject);}
"CPlacement3D"      {RETURN(k_CPlacement3D);}
"FLOATaabbox3D"     {RETURN(k_FLOATaabbox3D);}
"FLOATmatrix3D"     {RETURN(k_FLOATmatrix3D);}
"FLOATquat3D"       {RETURN(k_FLOATquat3D);}
"ANGLE"             {RETURN(k_ANGLE);};
"ANIMATION"         {RETURN(k_ANIMATION);};
"ILLUMINATIONTYPE"  {RETURN(k_ILLUMINATIONTYPE);};
"FLOAT3D"           {RETURN(k_FLOAT3D);};
"ANGLE3D"           {RETURN(k_ANGLE3D);};
"FLOATplane3D"      {RETURN(k_FLOATplane3D);};
"flags"             {RETURN(k_FLAGS);};
"zoneflags"			{RETURN(k_ZONEFLAGS);};
"zoneflags_ex"		{RETURN(k_ZONEFLAGS_EX);};

 /* single character operators and punctuations */
";"|"("|")"|"{"|"}"|"="|"+"|"-"|"<"|">"|"!"|"|"|"&"|"*"|"/"|"%"|"^"|"["|"]"|":"|","|"."|"?"|"~" {
  RETURN(yytext[0]);}

 /* constants */
{DIGIT}+                  { RETURN(c_int); }
"0x"{HEXDIGIT}+           { RETURN(c_int)};
{NONEXP_FLT}("f"|"F")?    { RETURN(c_int); }
{EXP_FLT}("f"|"F")?       { RETURN(c_int); }
(\"[^\n"]+\")|(\"\")      { RETURN(c_string); }
(\'[^\n']+\')|(\'\')      { RETURN(c_char); }

 /* identifier */
{IDENTIFIERFIRST}{IDENTIFIEROTHER}* {RETURN(identifier);}

 /* specially bracketed cpp blocks */
"%{" {
  char strLine[1024];
  snprintf(strLine, sizeof(strLine), "#line %d \"%s\"\n", _iLinesCt, _strInputFileName );
  _strCppBlock = strLine;
  BEGIN(CPPBLOCK);
}
<CPPBLOCK>"%}" {
  BEGIN(INITIAL);
  yylval = SType(_strCppBlock.c_str());
  return cppblock;
}
<CPPBLOCK>\n	{
  _iLinesCt++;
  _strCppBlock += yytext;
 }
<CPPBLOCK>.*   {
  _strCppBlock += yytext;
}

 /* special handling for preprocessor directives */
"#" {
  char strLine[1024];
  snprintf(strLine, sizeof(strLine), "#line %d \"%s\"\n#", _iLinesCt, _strInputFileName );
  _strPreProc = strdup(strLine);
  BEGIN(PREPROC);
}
<PREPROC>\n {
  BEGIN(INITIAL);
  _iLinesCt++;
  _strPreProc = (SType(_strPreProc)+yytext).strString;
  fprintf(_fDeclaration, "%s", _strPreProc);
  fprintf(_fTables, "%s", _strPreProc);
  fprintf(_fImplementation, "%s", _strPreProc);
}
<PREPROC>.*   {
  _strPreProc = (SType(_strPreProc)+yytext).strString;
}

 /* eat up comments */
"/*"          { BEGIN(COMMENT); }
<COMMENT>"*/" { BEGIN(INITIAL); }
<COMMENT>.    {}
"//"[^\n]*\n { _iLinesCt++; }

 /* eat up whitespace */
[ \t\r]+	 {
}
 /* eat up linefeeds and count lines in all conditions */
<*>\n	{
  _iLinesCt++;
 }
 /* for all unrecognized characters */
. {
  // report an error
  char strError[1024];
  snprintf(strError, sizeof(strError), "Unrecognized character '%c' (ASCII 0x%02x)\n", 
	  yytext[0], yytext[0] );
  yyerror(strError);
}

%%

