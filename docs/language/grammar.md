# EBNF Grammar

```ebnf
(* ============================================ *)
(* Endo Language Grammar - EBNF Specification  *)
(* ============================================ *)

(* ---------- Top-level ---------- *)

program         = { statement } ;

(* ---------- Statements ---------- *)

statement       = let_statement
                | type_definition
                | for_statement
                | while_statement
                | match_statement
                | try_statement
                | return_statement
                | break_statement
                | continue_statement
                | export_statement
                | import_statement
                | expression_statement
                ;

let_statement   = "let" [ "export" | "private" ] [ "rec" ] [ "mut" ] [ "use" | "manual" ] let_binding { "and" let_binding }
                ;
let_binding     = pattern [ type_annotation ] "=" expression
                | identifier { pattern } [ type_annotation ] "=" expression
                ;

type_definition = "type" identifier [ type_params ] "=" type_body ;
type_params     = "<" type_var { "," type_var } ">" ;
type_var        = "'" lowercase_ident ;
lowercase_ident = lowercase_letter { lowercase_letter | digit | "_" } ;
type_body       = record_type | union_type | type ;

record_type     = "{" field_def { ";" field_def } [ ";" ] "}" ;
field_def       = identifier ":" type ;

union_type      = [ "|" ] union_case { "|" union_case } ;
union_case      = identifier [ "of" union_fields ] ;
union_fields    = union_field { "*" union_field } ;
union_field     = [ identifier ":" ] type ;

for_statement   = "for" pattern "in" expression [ ";" ] "do" block "end" ;

while_statement = "while" expression [ ";" ] "do" block "end" ;

match_statement = "match" expression "with" { match_arm } ;
match_arm       = "|" pattern [ "when" expression ] "->" block_or_expr ;

try_statement   = "try" block "with" { match_arm } [ "finally" block ] ;

return_statement   = "return" [ expression ] ;
break_statement    = "break" ;
continue_statement = "continue" ;
export_statement   = "export" identifier [ "=" expression ] ;
import_statement   = "import" string_literal [ "as" identifier ]
                   | "from" string_literal "import" import_list ;
import_list        = "(" identifier { "," identifier } ")" | "*" ;

expression_statement = expression ;

block           = { statement [ ";" ] } ;
block_or_expr   = block | expression ;

(* ---------- Types ---------- *)

type            = function_type ;
function_type   = tuple_type [ "->" function_type ] ;
tuple_type      = type_application { "," type_application } ;
type_application = type_atom [ "<" type { "," type } ">" ] ;
type_atom       = "int" | "float" | "str" | "bool" | "unit"
                | "list" | "option" | "result"
                | identifier
                | "(" type ")"
                ;

type_annotation = ":" type ;

(* ---------- Expressions ---------- *)

expression      = let_in_expression ;

let_in_expression = "let" [ "use" | "manual" ] pattern "=" expression "in" expression
                  | lambda_expression
                  ;

lambda_expression = "fun" { pattern } [ ":" base_type ] "->" expression
                  | if_expression
                  ;

if_expression   = "if" expression "then" expr_sequence [ "else" expr_sequence ]
                | match_expression
                ;

expr_sequence   = expression                                          (* single expression *)
                | expression NEWLINE { expression NEWLINE } expression (* multi-expression via offside rule *)
                ;

match_expression = "match" expression "with" { match_arm }
                 | pipeline_expression
                 ;

pipeline_expression = logical_or { "|>" logical_or } ;

logical_or      = logical_and { "||" logical_and } ;
logical_and     = comparison { "&&" comparison } ;
comparison      = range { comparison_op range } ;
range           = additive [ ".." additive [ ".." additive ] ] ;
additive        = multiplicative { ( "+" | "-" ) multiplicative } ;
multiplicative  = power { ( "*" | "/" | "%" ) power } ;
power           = unary [ "**" power ] ;
unary           = [ "!" | "-" ] postfix ;
postfix         = application { postfix_op } ;
postfix_op      = "?" | "." identifier | "[" expression "]" | "?." identifier | "?|" expression ;
application     = primary { primary } ;

primary         = literal
                | identifier
                | "_"
                | "(" expression ")"
                | "(" expression "," expression { "," expression } ")"
                | list_expression
                | record_expression
                | lazy_expression
                | seq_expression
                | command_substitution
                | variable_expansion
                ;

lazy_expression = "lazy" primary ;

seq_expression  = "seq" "{" { seq_yield } "}" ;
seq_yield       = "yield" [ "!" ] expression ;

literal         = integer_literal
                | float_literal
                | size_literal
                | timespan_literal
                | string_literal
                | "true" | "false"
                | "()"
                ;

size_literal    = ( integer_literal | float_literal ) size_suffix ;
size_suffix     = "B" | "KB" | "MB" | "GB" | "TB" ;

timespan_literal = ( integer_literal | float_literal ) timespan_suffix ;
timespan_suffix  = "ms" | "s" | "min" | "h" ;

list_expression = "[" [ list_elements ] "]" ;
list_elements   = expression { ";" expression } [ ";" ]
                | expression ".." expression [ ".." expression ]
                | "for" pattern "in" expression [ "when" expression ] "->" expression
                ;

record_expression = "{" [ record_base ] field_assign { ";" field_assign } [ ";" ] "}" ;
record_base     = expression "with" ;
field_assign    = identifier [ "=" expression ] ;

command_substitution = "$(" pipeline ")" | "`" { command_char } "`" ;
variable_expansion   = "$" identifier
                     | "${" expansion_body "}"
                     | "$((" arithmetic_expr "))"
                     ;
expansion_body  = identifier [ expansion_op ] ;
expansion_op    = ":-" word | ":+" word | "#" pattern_str | "%" pattern_str
                | "/" pattern_str "/" word ;

(* ---------- Patterns ---------- *)

pattern         = or_pattern ;
or_pattern      = as_pattern { "|" as_pattern } ;
as_pattern      = cons_pattern [ "as" identifier ] ;
cons_pattern    = primary_pattern { "::" primary_pattern } ;

primary_pattern = "_"
                | literal
                | identifier
                | identifier primary_pattern
                | "(" pattern ")"
                | "(" pattern "," pattern { "," pattern } ")"
                | "[" [ list_pattern_elements ] "]"
                | "{" [ record_pattern_fields ] "}"
                ;

list_pattern_elements = pattern { ";" pattern } [ ";" pattern "..." ] ;
record_pattern_fields = field_pattern { ";" field_pattern } [ ";" "_" ] ;
field_pattern   = identifier [ "=" pattern ] ;

(* ---------- Commands and Pipelines ---------- *)

pipeline        = command { "|" command } [ "&" ] ;
command         = simple_command { redirect } ;
simple_command  = word { word } ;

redirect        = ">" word
                | ">>" word
                | "<" word
                | "2>" word
                | "2>>" word
                | "2>&1"
                | "&>" word
                | "<<<" word
                | "<<" heredoc_delimiter
                ;

word            = bare_word | string_literal | variable_expansion | glob_pattern ;

heredoc_delimiter = identifier | "'" identifier "'" ;

(* ---------- Lexical Elements ---------- *)

identifier      = ( letter | "_" ) { letter | digit | "_" } ;
bare_word       = ( letter | digit | "_" | "-" | "." | "/" | "~" )+ ;
glob_pattern    = { glob_char | "[" char_class "]" } ;
glob_char       = "*" | "?" | any_char ;
char_class      = { char_range | single_char } ;
char_range      = single_char "-" single_char ;

integer_literal = [ "-" ] digits
                | "0x" hex_digits
                | "0o" octal_digits
                | "0b" binary_digits
                ;

float_literal   = [ "-" ] digits "." digits [ exponent ] ;
exponent        = ( "e" | "E" ) [ "+" | "-" ] digits ;

string_literal  = double_string | single_string ;
double_string   = '"' { string_char | escape_seq | interpolation } '"' ;
single_string   = "'" { any_char_except_quote } "'" ;
escape_seq      = "\\" ( "n" | "t" | "r" | "\\" | '"' | "$" ) ;
interpolation   = "$" identifier
                | "${" expression "}"
                | "$(" pipeline ")"
                | "$((" arithmetic_expr "))"
                ;

comparison_op   = "==" | "!=" | "<" | "<=" | ">" | ">=" ;

letter          = "a".."z" | "A".."Z" ;
digit           = "0".."9" ;
digits          = digit { digit } ;
hex_digits      = hex_digit { hex_digit } ;
hex_digit       = digit | "a".."f" | "A".."F" ;
octal_digits    = octal_digit { octal_digit } ;
octal_digit     = "0".."7" ;
binary_digits   = binary_digit { binary_digit } ;
binary_digit    = "0" | "1" ;

(* ---------- Comments ---------- *)

comment         = "#" { any_char } newline
                | "//" { any_char } newline
                | "(*" { any_char | comment } "*)"
                ;
```

---

## Appendix: Quick Reference Card

```
+----------------------------------------------------------------------+
|                     ENDO LANGUAGE QUICK REFERENCE                     |
+----------------------------------------------------------------------+
| BINDINGS                                                             |
|   let x = 42                    Immutable binding                    |
|   let mut x = 42                Mutable binding                      |
|   let export X = 42             Bind and export as env var           |
|   x <- x + 1                    Mutation                             |
|   let (a, b) = tuple            Destructuring                        |
+----------------------------------------------------------------------+
| FUNCTIONS                                                            |
|   let f x y = x + y             Named function (curried)             |
|   let f = fun x -> x * 2        Lambda                               |
|   _.field, _ + 1                Placeholder lambda sugar              |
|   let add5 = add 5              Partial application                  |
|   f >> g                        Forward composition                  |
+----------------------------------------------------------------------+
| TYPES                                                                |
|   int, float, str, bool, unit   Primitives                           |
|   list<T>, option<T>            Generics                             |
|   result<T, E>                  Error handling                       |
|   { name: str; age: int }       Record                               |
|   | Case1 | Case2 of T          Union                                |
+----------------------------------------------------------------------+
| LISTS                                                                |
|   [1; 2; 3]                     List literal                         |
|   [1..10]                       Range                                |
|   head :: tail                  Cons                                 |
|   list1 @ list2                 Concatenate                          |
|   [for x in xs -> x * 2]        Comprehension                        |
+----------------------------------------------------------------------+
| PATTERN MATCHING                                                     |
|   match x with                                                       |
|   | pattern -> result           Basic arm                            |
|   | p when guard -> result      With guard                           |
|   | p1 | p2 -> result           Or pattern                           |
|   | p as name -> result         As pattern                           |
+----------------------------------------------------------------------+
| PIPELINES                                                            |
|   x |> f |> g                   Function pipeline                    |
|   cmd1 | cmd2                   Shell pipeline                       |
|   cmd | lines |> map f          Mixed                                |
+----------------------------------------------------------------------+
| CONTROL FLOW                                                         |
|   if cond then a [else b]       If expression (else optional)         |
|   for x in xs do block end      For loop                             |
|   while cond do block end       While loop                           |
+----------------------------------------------------------------------+
| ERROR HANDLING                                                       |
|   let x = cmd?                  Propagate error                      |
|   Ok value, Error e             Result constructors                  |
|   Some x, None                  Option constructors                  |
|   try block with | e -> ...     Try-with                             |
+----------------------------------------------------------------------+
| LAZY EVALUATION                                                      |
|   lazy expr                     Deferred computation                 |
|   force lazyVal                 Evaluate and cache result            |
|   lazyVal |> force              Pipeline force                       |
+----------------------------------------------------------------------+
| HTTP                                                                 |
|   fetch url                       Download to file -> result<str,str>|
|   fetch url headers               Download with custom headers       |
+----------------------------------------------------------------------+
| SHELL FEATURES                                                       |
|   $VAR, ${VAR}                  Variable expansion                   |
|   ${VAR:-default}               Default value                        |
|   $(command)                    Command substitution                 |
|   > >> < 2>&1                   Redirections                         |
|   <<EOF ... EOF                 Here document                        |
|   <(cmd) >(cmd)                 Process substitution                 |
+----------------------------------------------------------------------+
```

---

*This specification is a living document. As endo evolves, this document will be updated to reflect new features and refinements.*

---
**See also:** [Implementation Notes](implementation-notes.md) | [Lexical Elements](lexical-elements.md) | [Philosophy & Goals](index.md)
