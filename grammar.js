/**
 * @file Zsh grammar for tree-sitter
 * @author Max Brunsfeld <maxbrunsfeld@gmail.com>
 * @author Amaan Qureshi <amaanq12@gmail.com>
 * @author George Harker <george@georgeharker.com>
 * @license MIT
 */

/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

const SPECIAL_CHARACTERS = [
  '\'', '"',
  '<', '>',
  '{', '}',
  '[', ']',
  '(', ')',
  '`', '$',
  '|', '&', ';',
  '\\', ':',
  '\\s'
  //' ', '\t', '\n'
];

// Glob qualifier body, see zsh_glob_qualifier. Kept deliberately simple
// (an exact per-qualifier regex made `generate` run for over 30 minutes):
// anything but blanks, ( ) | ; &, with quoted sections and [..] read whole.
// The first character isn't ':' so that (:h) (:t:r) stay zsh_glob_modifier.
const GLOB_QUALIFIER_BODY =
  /([^()\s|;&'"\[\]:]|'[^']*'|"[^"]*"|\[[^\]\n]*\])([^()\s|;&'"\[\]]|'[^']*'|"[^"]*"|\[[^\]\n]*\])*/;

const PREC = {
  UPDATE: 0,
  ASSIGN: 1,
  TERNARY: 2,
  LOGICAL_OR: 3,
  LOGICAL_AND: 4,
  BITWISE_OR: 5,
  BITWISE_XOR: 6,
  BITWISE_AND: 7,
  EQUALITY: 8,
  COMPARE: 9,
  TEST: 10,
  UNARY: 11,
  SHIFT: 12,
  ADD: 13,
  MULTIPLY: 14,
  EXPONENT: 15,
  NEGATE: 16,
  PREFIX: 17,
  POSTFIX: 18,
};

// Define operator precedence tables
const OPERATOR_TABLES = {
  // Full expression operators (used by binary_expression)
  FULL: [
    [choice('+=', '-=', '*=', '/=', '%=', '**=', '<<=', '>>=', '&=', '^=', '|='), PREC.UPDATE],
    [choice('=', '=~'), PREC.ASSIGN],
    ['||', PREC.LOGICAL_OR],
    ['&&', PREC.LOGICAL_AND],
    ['|', PREC.BITWISE_OR],
    ['^', PREC.BITWISE_XOR],
    ['&', PREC.BITWISE_AND],
    [choice('==', '!='), PREC.EQUALITY],
    [choice('<', '>', '<=', '>='), PREC.COMPARE],
    [choice('<<', '>>'), PREC.SHIFT],
    [choice('+', '-'), PREC.ADD],
    [choice('*', '/', '%'), PREC.MULTIPLY],
    ['**', PREC.EXPONENT],
  ],
  
  // Arithmetic-only operators (no test_operator, no =~)
  ARITHMETIC: [
    [choice('+=', '-=', '*=', '/=', '%=', '**=', '<<=', '>>=', '&=', '^=', '|='), PREC.UPDATE],
    [choice('=', '=~'), PREC.ASSIGN],
    ['||', PREC.LOGICAL_OR],
    ['&&', PREC.LOGICAL_AND],
    ['|', PREC.BITWISE_OR],
    ['^', PREC.BITWISE_XOR],
    ['&', PREC.BITWISE_AND],
    [choice('==', '!='), PREC.EQUALITY],
    [choice('<', '>', '<=', '>='), PREC.COMPARE],
    [choice('<<', '>>'), PREC.SHIFT],
    [choice('+', '-'), PREC.ADD],
    [choice('*', '/', '%'), PREC.MULTIPLY],
    ['**', PREC.EXPONENT],
  ],
  
  // C-style expressions (subset for C-style for loops)
  C_STYLE: [
    [choice('+=', '-=', '*=', '/=', '%=', '**=', '<<=', '>>=', '&=', '^=', '|='), PREC.UPDATE],
    [choice('||', '-o'), PREC.LOGICAL_OR],  // C-style allows -o
    [choice('&&', '-a'), PREC.LOGICAL_AND], // C-style allows -a
    ['|', PREC.BITWISE_OR],
    ['^', PREC.BITWISE_XOR],
    ['&', PREC.BITWISE_AND],
    [choice('==', '!='), PREC.EQUALITY],
    [choice('<', '>', '<=', '>='), PREC.COMPARE],
    [choice('<<', '>>'), PREC.SHIFT],
    [choice('+', '-'), PREC.ADD],
    [choice('*', '/', '%'), PREC.MULTIPLY],
    ['**', PREC.EXPONENT],
  ],
  
  // Regex matching operators - subset focused on pattern matching
  REGEX: [
    ['||', PREC.LOGICAL_OR], // Logical for compound patterns
    ['&&', PREC.LOGICAL_AND], // Logical for compound patterns  
    [choice('==', '!=', '=~'), PREC.EQUALITY], // Pattern matching
    [choice('+', '-'), PREC.ADD], // String/pattern concatenation
  ],
};

/**
 * Creates binary expression rules from operator table
 * @param {Array} operatorTable - Array of [operator, precedence] pairs
 * @param {string} leftField - Name of left operand field
 * @param {string} rightField - Name of right operand field
 * @param {Object} options - Additional options
 */
function createBinaryExpression(operatorTable, leftField, rightField, options = {}) {
  const { 
    specialCases = [], 
    rightAssociative = ['**'] 
  } = options;
  
  const tableRules = operatorTable.map(([operator, precedence]) => {
    const isRightAssoc = rightAssociative.some(op => 
      Array.isArray(operator) ? operator.includes(op) : operator === op
    );
    
    return prec[isRightAssoc ? 'right' : 'left'](precedence, seq(
      field('left', leftField),
      field('operator', operator),
      field('right', rightField),
    ));
  });
  
  return choice(...tableRules, ...specialCases);
}

module.exports = grammar({
  name: 'zsh',

  conflicts: $ => [
    [$._condition],
    [$._expression, $.command_name],
    [$.command, $.variable_assignments],
    [$.redirected_statement, $.command],
    [$.pipeline],
    [$.repeat_statement, $.command],
    [$._expression, $._regex_match_literal], // Resolve regex expression conflict
    [$._regex_match_literal, $._expansion_or_variable], // Additional regex conflicts
    // (( $x$y )): arithmetic concatenation vs the test_command (( path
    [$._arithmetic_piece, $._expansion_or_variable],
  ],

  inline: $ => [
    $._statement,
    $._simple_statement,
    $._statement_or_comment,
    $._terminator,
    $._literal,
    $._terminated_statement,
    $._maybe_terminated_statement,
    $._primary_expression,
    $._simple_variable_name,
    $._special_variable_name,
    $._c_word,
    $._statement_not_subshell,
    $._redirect,
    $._word_or_word_with_colon
  ],

  externals: $ => [
    $.heredoc_start,
    $.simple_heredoc_body,
    $._heredoc_body_beginning,
    $.heredoc_content,
    $.heredoc_end,
    $.file_descriptor,
    $._empty_value,
    $._concat,
    $._concat_regex,
    $.variable_name, // Variable name followed by an operator like '=' or '+='
    $.simple_variable_name,
    $.special_variable_name,
    $.test_operator,
    $.regex,
    $._regex_no_slash,
    $._regex_no_space,
    $._expansion_word,
    $.extglob_pattern,
    $._raw_dollar,
    $._bare_dollar,
    $._peek_bare_dollar, // peek ahead for $ without consuming
    $._brace_start,
    $._brace_expr_start,
    $._immediate_double_hash,
    $._array_star_token,
    $._array_at_token,
    '}',                               // CLOSING_BRACE
    ']',                               // CLOSING_BRACKET
    ')',                               // CLOSING_PAREN
    '))',                              // CLOSING_DOUBLE_PAREN
    '<<',                              // HEREDOC_ARROW
    '<<-',                             // HEREDOC_ARROW_DASH
    '<<<',                             // HERESTRING
    $._hash_pattern,
    $._double_hash_pattern,
    $._enter_pattern,
    $._pattern_start,
    $._pattern_suffix_start,
    /\n/,                              // NEWLINE
    '(',                               // OPENING_PAREN
    '((',                              // DOUBLE_OPENING_PAREN
    '[',                               // OPENING_BRACKET  
    '[[',                              // TEST_COMMAND_START
    $.escaped_open_paren,              // ESCAPED_OPEN_PAREN: \(
    $.escaped_close_paren,             // ESCAPED_CLOSE_PAREN: \)
    ']]',                              // TEST_COMMAND_END
    'esac',                            // ESAC
    $._zsh_extended_glob_flags,
    '"',
    '\'',
    '`',
    $._subscript_flags,                // SUBSCRIPT_FLAGS: (w) (s:,:) (n:2:)
    $._subscript_pattern_flags,        // SUBSCRIPT_PATTERN_FLAGS: (i) (Ie) (r)
    $._subscript_pattern_word,         // SUBSCRIPT_PATTERN_WORD: (i)--  (i)pattern
    $._subscript_pattern_glob,         // SUBSCRIPT_PATTERN_GLOB: (I)foo_*  (r)[bf]*
    $._default_word,                   // DEFAULT_WORD: ${X:-(none)} ${X:-<unset>}
    $._test_pattern_word,              // TEST_PATTERN_WORD: [[ $x = -- ]]
    $._test_pattern_glob,              // TEST_PATTERN_GLOB: [[ $x = (a|b)* ]]
    $._arithmetic_word,                // ARITHMETIC_WORD: identifier inside (( ))
    $.arithmetic_char_code,            // ARITHMETIC_CHAR_CODE: ##a #name in (( ))
    $._literal_dollar,                 // LITERAL_DOLLAR: a '$' that starts no expansion: "%B$%b" echo $;
    $._subst_end,                      // SUBST_END: zero-width end of a modifier chain ($x:h:s/a/b/)
    $._function_name,                  // FUNCTION_NAME: zinit::setup() :helper(): a name read whole before ()
    $._modifier_other,                 // MODIFIER_OTHER: :fs#a#b# :F:2:s/a/b/ :s#a#b#: read whole by the scanner
    $._brace_else,                     // BRACE_ELSE: `} else` of a short if, one token
    $._brace_elif,                     // BRACE_ELIF: `} elif` of a short if, one token
    $.__error_recovery,
  ],

  extras: $ => [
    $.comment,
    /\s/,
    /\\\r?\n/,
    /\\( |\t|\v|\f)/,
  ],

  // (_statement, _statement_or_comment and _primary_expression are inlined,
  // so they can't be supertypes; tree-sitter 0.27 ignores them there.)
  supertypes: $ => [
    $._expression,
  ],

  word: $ => $._word,

  rules: {
    // A file of only blank lines / terminators is a valid (empty) program.
    program: $ => optional(choice($._statements, repeat1($._terminator))),

    _statements: $ => prec(1, seq(
      repeat($._terminator), // Allow leading terminators/newlines
      repeat(seq(
        $._statement_or_comment,
        optional($.comment),
        $._terminator,
      )),
      $._statement_or_comment,
      optional($.comment),
      optional($._terminator),
    )),

    // Zsh glob qualifiers: (.) (/) (*) etc.
    // FIXME: these qualifiers need extending to deal with
    //
    // zshexpn(1) "Glob Qualifiers": letters (with ^ / - toggles),
    // delimited arguments for e P u g f (any delimiter, paired ( [ { <,
    // or quotes), numeric u/g/f, +cmd, [n] / [n,m] subscripts, ','
    // alternatives, and trailing :modifiers (:t :r :s/x/y/ ...).
    zsh_glob_qualifier: _ => seq(
      '(',
      optional(token.immediate('#q')),
      token.immediate(GLOB_QUALIFIER_BODY),
      ')',
    ),

    // Zsh glob modifiers: (:t) (:h) (:r) (:e) etc.  
    zsh_glob_modifier: _ => seq(
      '(',
      token.immediate(':'),
      token.immediate(/[threuoOnPAlUsSqQxXfFlW]/),
      optional(token.immediate(/[^)]*/)),
      ')',
    ),

    _terminated_statement: $ => repeat1(seq(
      $._statement,
      $._terminator,
    )),
    
    _maybe_terminated_statement: $ => prec.right(
      choice(
        seq(
            repeat1(
                seq(
                    $._statement,
                    $._terminator,
                )
            ),
            optional($._simple_statement),
        ),
        $._simple_statement
      )),

    // Statements

    _statement: $ => choice(
      $._statement_not_subshell,
      $.subshell,
    ),

    _statement_or_comment: $ => choice(
      $._statement_not_subshell,
      $.subshell,
      $.comment,
    ),


    _statement_not_subshell: $ => choice(
      $.redirected_statement,
      $.variable_assignment,
      $.variable_assignments,
      $.command,
      $.declaration_command,
      $.unset_command,
      $.test_command,
      $.negated_command,
      $.for_statement,
      $.foreach_statement,
      $.terse_for_statement,
      $.c_style_for_statement,
      $.while_statement,
      $.repeat_statement,
      $.select_statement,
      $.if_statement,
      $.case_statement,
      $.pipeline,
      $.coprocess_statement,
      $.list,
      $.compound_statement,
      $.function_definition,
    ),
    
    _simple_statement: $ => choice(
      $.redirected_statement,
      $.variable_assignment,
      $.variable_assignments,
      $.command,
      $.declaration_command,
      $.unset_command,
      $.test_command,
      $.negated_command,
      $.for_statement,
      $.foreach_statement,
      $.terse_for_statement,
      $.c_style_for_statement,
      $.while_statement,
      $.repeat_statement,
      $.select_statement,
      $.if_statement,
      $.pipeline,
      // Also allowed before '}' without a terminator:
      // { a && [[ -f b ]] }, { { a } }, { ( a ) }, { case ... esac }
      $.list,
      $.compound_statement,
      $.subshell,
      $.case_statement,
    ),

    _statement_not_pipeline: $ => prec(1, choice(
      $.redirected_statement,
      $.variable_assignment,
      $.variable_assignments,
      $.command,
      $.declaration_command,
      $.unset_command,
      $.test_command,
      $.negated_command,
      $.for_statement,
      $.foreach_statement,
      $.terse_for_statement,
      $.c_style_for_statement,
      $.while_statement,
      $.repeat_statement,
      $.select_statement,
      $.if_statement,
      $.case_statement,
      $.list,
      $.compound_statement,
      $.function_definition,
      $.subshell,
    )),

    redirected_statement: $ => prec.dynamic(-1, prec.right(-1, choice(
      seq(
        field('body', $._statement),
        field('redirect', choice(
          repeat1(choice(
            $.file_redirect,
            alias($._named_fd_redirect, $.file_redirect),
            $.heredoc_redirect,
            $.herestring_redirect,  // moved here from standalone
          )),
        )),
      ),
      seq(
        field('body', choice($.if_statement, $.while_statement)),
        $.herestring_redirect,
      ),
      field('redirect', repeat1($._redirect)),
    ))),

    // zsh: for name ... [ in word ... ] term do list done
    // Several names (for k v in ...), digit names (for 1 in ...), no `in`
    // (for a; do -- loops over "$@"), and { } or SHORT_LOOPS bodies.
    for_statement: $ => seq(
      'for',
      $._for_names,
      optional(seq(
        'in',
        repeat(field('value', $._literal)),
      )),
      $._terminator,
      field('body', choice(
        $.do_group,
        $.compound_statement,
      )),
    ),

    // First name may be a normal variable name or digits; later names use
    // _for_name so the scanner can't take the `in` keyword as a name.
    _for_names: $ => seq(
      field('variable', choice(
        $._simple_variable_name,
        alias($._for_name, $.simple_variable_name),
      )),
      repeat(field('variable', alias($._for_name, $.simple_variable_name))),
    ),

    _for_name: _ => token(/[A-Za-z_][A-Za-z0-9_]*|[0-9]+/),
    
    // zsh: foreach name ... ( word ... ) list end
    foreach_statement: $ => seq(
      'foreach',
      repeat1(field('variable', $._simple_variable_name)),
      '(',
      repeat(field('value', $._literal)),
      ')',
      optional($._terminator),
      field('body', optional($._terminated_statement)),
      'end',
    ),

    terse_for_statement: $ => prec(2, seq(
      'for',
      $._for_names,
      '(',
      field('value', repeat1($._literal)),  // Allow multiple values
      ')',
      field('body', choice(
        $._statement,                      // Allow single statement (no do/done)
        seq($._terminator, $.do_group),    // Or traditional do/done
      )),
    )),

    c_style_for_statement: $ => seq(
      'for',
      '((',
      choice($._for_body),
      '))',
      optional(';'),
      field('body', choice(
        $.do_group,
        $.compound_statement,
      )),
    ),
    _for_body: $ => seq(
      field('initializer', commaSep($._c_expression)),
      $._terminator,
      field('condition', commaSep($._c_expression)),
      $._terminator,
      field('update', commaSep($._c_expression)),
    ),

    _c_expression: $ => choice(
      $._c_expression_not_assignment,
      alias($._c_variable_assignment, $.variable_assignment),
    ),
    _c_expression_not_assignment: $ => choice(
      $._c_word,
      $.variable_ref,
      $.expansion,
      $.number,
      $.string,
      alias($._c_unary_expression, $.unary_expression),
      alias($._c_binary_expression, $.binary_expression),
      alias($._c_postfix_expression, $.postfix_expression),
      alias($._c_parenthesized_expression, $.parenthesized_expression),
      $.command_substitution,
    ),

    _c_variable_assignment: $ => seq(
      field('name', alias($._c_word, $.variable_name)),
      '=',
      field('value', $._c_expression),
    ),
    _c_unary_expression: $ => prec(PREC.PREFIX, seq(
      field('operator', choice('++', '--')),
      $._c_expression_not_assignment,
    )),
    _c_binary_expression: $ => createBinaryExpression(
      OPERATOR_TABLES.C_STYLE,
      $._c_expression_not_assignment,
      $._c_expression_not_assignment
    ),
    _c_postfix_expression: $ => prec(PREC.POSTFIX, seq(
      $._c_expression_not_assignment,
      field('operator', choice('++', '--')),
    )),
    _c_parenthesized_expression: $ => seq(
      '(',
      commaSep1($._c_expression),
      ')',
    ),
    _c_word: $ => alias(/[a-zA-Z_][a-zA-Z0-9_]*/, $.word),

    while_statement: $ => seq(
      choice('while', 'until'),
      field('condition', $._condition),
      // zsh's short form: while (( i )) { ... }
      field('body', choice($.do_group, $.compound_statement)),
    ),

    repeat_statement: $ => prec.right(seq(
      'repeat',
      field('count', choice($.number, $.word, $._simple_variable_name, $.variable_ref, $.expansion)),
      choice(
        seq($._terminator, field('body', $.do_group)),
        $._statement,
      ),
    )),

    select_statement: $ => seq(
      'select',
      field('variable', $._simple_variable_name),
      optional(seq(
        'in',
        repeat1($._literal),
      )),
      $._terminator,
      field('body', $.do_group),
    ),

    do_group: $ => seq(
      'do',
      optional($._terminated_statement),
      'done',
    ),

    // zsh lets then / do follow a condition that closes itself without a
    // ';': if [[ -n $x ]] then, while (( i )) do, if { true } then,
    // if [[ a ]] && [[ b ]] then.
    _condition: $ => choice(
      $._terminated_statement,
      seq(
        optional($._terminated_statement),
        choice(
          $.test_command,
          $.compound_statement,
          $.list,
          // if (( 1 )) then: (( )) as a statement is a command whose name is
          // the arithmetic expansion. Only that form; a plain command would
          // swallow `then` as an argument (if print then; then ...).
          alias($._arithmetic_only_command, $.command),
        ),
      ),
    ),

    _arithmetic_only_command: $ => field('name',
      alias($._arithmetic_command_name, $.command_name)),
    _arithmetic_command_name: $ => $.arithmetic_expansion,

    if_statement: $ => choice(
      seq(
        'if',
        field('condition', $._condition),
        'then',
        optional($._terminated_statement),
        repeat($.elif_clause),
        optional($.else_clause),
        'fi',
      ),
      // zsh's short form: if [[ ... ]] { ... } elif (( ... )) { ... } else
      // { ... }. Only with a condition that closes itself ([[ ]], (( )) or
      // { }): after a plain command the '{' is just another argument.
      prec.right(seq(
        'if',
        field('condition', $._short_condition),
        field('body', $.compound_statement),
      )),
    ),

    _short_condition: $ => choice(
      $.test_command,
      $.compound_statement,
      alias($._arithmetic_only_command, $.command),  // if (( a )) { ... }
    ),
    _elif_clause_short: $ => seq('elif', $._short_condition, $.compound_statement),
    _else_clause_short: $ => seq('else', $.compound_statement),

    elif_clause: $ => seq(
      'elif',
      $._condition,
      'then',
      optional($._terminated_statement),
    ),

    else_clause: $ => seq(
      'else',
      optional($._terminated_statement),
    ),

    case_statement: $ => seq(
      'case',
      field('value', $._literal),
      optional($._terminator),
      optional($.comment),
      'in',
      optional($._terminator),
      optional(seq(
        repeat($.case_item),
        alias($.last_case_item, $.case_item),
      )),
      'esac',
    ),

    case_item: $ => seq(
      choice(
        seq(
          optional('('),
          field('value', choice($._literal, $._extglob_blob)),
          repeat(seq('|', field('value', choice($._literal, $._extglob_blob)))),
          ')',
        ),
      ),
      optional($._case_item_statements),
      prec(1, choice(
        // `b &;;`: a background command ending the item (the & is folded
        // into the termination token, longer than the `&;` terminator)
        field('termination', choice(';;', alias(/&[|!]?[ \t]*;;/, ';;'))),
        field('fallthrough', choice(
          alias(';&', $.case_fallthrough),
          alias(';;&', $.case_test_next),
          alias(';|', $.case_test_next),
          alias(/&[|!]?[ \t]*;&/, $.case_fallthrough),
          alias(/&[|!]?[ \t]*;\|/, $.case_test_next),
        )),
      )),
    ),

    last_case_item: $ => seq(
      optional('('),
      field('value', choice($._literal, $._extglob_blob)),
      repeat(seq('|', field('value', choice($._literal, $._extglob_blob)))),
      ')',
      optional($._case_item_statements),
      optional(prec(1, choice(
        // `b &;;`: a background command ending the item (the & is folded
        // into the termination token, longer than the `&;` terminator)
        field('termination', choice(';;', alias(/&[|!]?[ \t]*;;/, ';;'))),
        field('fallthrough', choice(
          alias(';&', $.case_fallthrough),
          alias(';;&', $.case_test_next),
          alias(';|', $.case_test_next),
          alias(/&[|!]?[ \t]*;&/, $.case_fallthrough),
          alias(/&[|!]?[ \t]*;\|/, $.case_test_next),
        )),
      ))),
    ),

    function_definition: $ => prec(4, prec.right(seq(
      choice(
        seq(
          'function',
          // Names may be built from expansions: function $w-by-keymap { }.
          // No name: an anonymous function, run where it's defined.
          repeat(field('name', choice(
            $._word_or_word_with_colon,
            $.variable_ref,
            $.expansion,
            alias($._function_expansion_name, $.concatenation),
          ))),
          optional(seq(
             '(', ')')),
        ),
        seq(
          // zinit::setup() :helper() a.b(): the scanner reads a name made of
          // several pieces whole when '()' follows it.
          field('name', optional(choice(
            $._word_or_word_with_colon,
            alias($._function_name, $.word),
          ))),
          '(', ')',
        ),
      ),
      field(
        'body',
        choice(
          $.compound_statement,
          $.subshell,
          $.test_command,
          $.if_statement,
        ),
      ),
      field('redirect', optional($._redirect)),
    ))),

    // function $w-by-keymap { }: a name that starts with an expansion.
    // Narrower than concatenation so `function f() { }` still sees its ().
    _function_expansion_name: $ => seq(
      choice($.variable_ref, $.expansion),
      repeat1(seq($._concat, choice($.word, $.variable_ref, $.expansion))),
    ),

    compound_statement_no_always: $ => prec(2, seq(
      alias($._brace_start, '{'),
      optional(
        $._maybe_terminated_statement,
        // $._terminated_statement,
      ),
      // External '}' (CLOSING_BRACE): the scanner pops CTX_COMPOUND here.
      // An internal token never reached the scanner, so every { } group
      // used to leave CTX_COMPOUND on the context stack.
      '}',
    )),

    compound_statement: $ => seq(
      alias($._brace_start, '{'),
      optional(
        $._maybe_terminated_statement,
        // $._terminated_statement,
      ),
      // External '}' (CLOSING_BRACE): the scanner pops CTX_COMPOUND here.
      // An internal token never reached the scanner, so every { } group
      // used to leave CTX_COMPOUND on the context stack.
      $._block_close,
      optional($.always_clause),
    ),

    // End of a { } block. A short if goes on from here with `} else` /
    // `} elif`, single scanner tokens (so no `else` keyword can follow a
    // block, which would make every statement's end ambiguous):
    //   if [[ a ]] { ... } elif (( b )) { ... } else { ... }
    _block_close: $ => choice(
      '}',
      seq(alias($._brace_else, '} else'), alias($._else_block, $.else_clause)),
      seq(alias($._brace_elif, '} elif'), alias($._elif_block, $.elif_clause)),
    ),
    _else_block: $ => seq(
      alias($._brace_start, '{'), optional($._maybe_terminated_statement), $._block_close),
    _elif_block: $ => seq(
      field('condition', $._short_condition),
      alias($._brace_start, '{'), optional($._maybe_terminated_statement), $._block_close),

    // Always clause that can attach to compound statements
    always_clause: $ => seq(
      'always',
      field('always', choice(
        $.compound_statement_no_always,
        $.command,
        $.pipeline
      ))
    ),

    // Zsh coprocess: coproc [name] command
    coprocess_statement: $ => prec(2, seq(
      'coproc',
      optional(field('name', $.word)),
      field('command', $.command)
    )),

    subshell: $ => seq(
      '(',
      $._statements,
      ')',
    ),

    pipeline: $ => prec.right(seq(
      $._statement_not_pipeline,
      repeat1(seq(
        choice('|', '|&'),
        $._statement_not_pipeline,
      )),
    )),

    list: $ => prec.left(-1, seq(
      $._statement,
      choice('&&', '||'),
      $._statement,
    )),

    // Commands

    negated_command: $ => seq(
      '!',
      choice(
        prec(2, $.command),
        prec(1, $.variable_assignment),
        $.test_command,
        $.subshell,
      ),
    ),

    test_command: $ => prec(2, seq(
      choice(
        seq(
          '[', 
          optional(choice($._expression, $.redirected_statement)), 
          ']'
        ),
        seq(
          '[[',
          $._test_condition,
          ']]',
        ),
        seq(
          '((', 
          optional($._expression), 
          '))'
        ),
      )),
    ),
    
    // [[ ... ]] conditions (zshmisc "CONDITIONAL EXPRESSIONS"). Operands
    // are words, not arithmetic: || binds loosest, then &&, then !, then
    // ( ) groups, unary tests and comparisons. The right of = == != is a
    // pattern; =~ takes a regex.
    _test_condition: $ => choice(
      alias($._test_or, $.binary_expression),
      alias($._test_and, $.binary_expression),
      alias($._test_not, $.unary_expression),
      alias($._test_group, $.parenthesized_expression),
      alias($._test_unary, $.unary_expression),
      alias($._test_command_binary_expression, $.binary_expression),
      $._literal,
    ),

    _test_or: $ => prec.left(1, seq(
      field('left', $._test_condition),
      field('operator', '||'),
      field('right', $._test_condition),
    )),

    _test_and: $ => prec.left(2, seq(
      field('left', $._test_condition),
      field('operator', '&&'),
      field('right', $._test_condition),
    )),

    _test_not: $ => prec.right(3, seq(
      field('operator', '!'),
      $._test_condition,
    )),

    _test_group: $ => seq('(', $._test_condition, ')'),

    _test_unary: $ => choice(
      prec(4, seq(
        field('operator', $.test_operator),
        $._literal,
      )),
      // Completion conditions with a count: [[ -prefix 1 (f|ht)tp:// ]]
      // (zshcompwid "Completion Condition Codes"); the operand is a pattern.
      prec(4, seq(
        field('operator', $.test_operator),
        field('count', $.number),
        choice($._literal, $._test_pattern),
      )),
    ),

    _test_command_binary_expression: $ => prec(5, choice(
      seq(
        field('left', $._literal),
        field('operator', '=~'),
        field('right', $._regex_match_expression),
      ),
      // When the right starts with pattern text the scanner lexes it as
      // pattern pieces ([[ $x = (a|b)* ]], [[ $1 = -- ]]); when it starts
      // with $x, "str" or $(cmd) it's an ordinary literal.
      seq(
        field('left', $._literal),
        field('operator', choice('=', '==', '!=')),
        field('right', choice($._literal, $._test_pattern)),
      ),
      seq(
        field('left', $._literal),
        field('operator', choice($.test_operator, '<', '>')),
        field('right', $._literal),
      ),
    )),

    // Right side of [[ x = pattern ]]: starts with a pattern piece, then
    // any mix of pieces, expansions and strings with no blanks between.
    _test_pattern: $ => choice(
      $._test_pattern_piece,
      alias($._test_pattern_concatenation, $.concatenation),
      alias($._test_glob_flagged, $.glob_pattern),
      alias($._test_glob_qualified, $.glob_pattern),
    ),

    // [[ $n != (#i)*.TMP ]] / [[ $x = (#b)(*) ]]: same shape as an ordinary
    // glob_pattern with flags (the piece itself stays anonymous).
    _test_glob_flagged: $ => seq(
      field('flags', $.zsh_extended_glob_flags),
      choice(
        $._test_pattern_word,
        $._test_pattern_glob,
        alias($._test_pattern_concatenation, $.concatenation),
      ),
      optional(field('qualifier', $.zsh_glob_qualifier)),
    ),

    // [[ foo.txt == *.txt(#qN) ]]
    _test_glob_qualified: $ => seq(
      choice($._test_pattern_word, $._test_pattern_glob),
      field('qualifier', $.zsh_glob_qualifier),
    ),

    // Either starts with a pattern piece, or with strings/expansions and
    // reaches a piece later ([[ $w != ":"(\\|)"!"* ]]). Glob flags may
    // appear mid-pattern ([[ $x = a(#i)b ]]).
    _test_pattern_concatenation: $ => prec.left(choice(
      seq(
        $._test_pattern_piece,
        repeat1($._test_pattern_element),
      ),
      seq(
        repeat1($._test_pattern_nonpiece),
        choice($._test_pattern_piece, $.zsh_extended_glob_flags),
        repeat($._test_pattern_element),
      ),
    )),

    _test_pattern_element: $ => choice(
      $._test_pattern_piece,
      $.zsh_extended_glob_flags,
      $._test_pattern_nonpiece,
    ),

    _test_pattern_nonpiece: $ => choice(
      $.variable_ref,
      $.expansion,
      $.command_substitution,
      $.string,
      $.raw_string,
      $.ansi_c_string,
    ),

    _test_pattern_piece: $ => choice(
      alias($._test_pattern_word, $.word),
      alias($._test_pattern_glob, $.glob_pattern),
    ),

    declaration_command: $ => prec.left(seq(
      choice('declare', 'typeset', 'export', 'readonly', 'local', 'integer', 'float'),
      repeat(
        choice(
          $.variable_assignment,
          field('argument', $._literal),
          field('redirect', $._redirect),
        )
      ),
    )),

    unset_command: $ => prec.left(seq(
      choice('unset', 'unsetenv'),
      repeat(
        choice(
          $._literal,
          $._simple_variable_name,
        )
      ),
    )),

    command: $ => prec.left(seq(
      prec(4, repeat(choice(
        $.variable_assignment,
        field('redirect', $._redirect),
      ))),
      field('name', $.command_name),
      choice(
        prec(4, repeat(
          choice(
            field('argument', $._literal),
//            prec(-1, field('argument', $._bare_dollar)),
            field('argument', $.glob_pattern),
            field('argument', seq(
              choice('=~', '=='),
              choice($._literal, $.regex),
            ),
            field('redirect', $.herestring_redirect),
          )
        ))),
        $.subshell,
      ),
    )),

    command_name: $ => $._literal,

    variable_assignment: $ => seq(
      field('name', choice(
        $.variable_name,
        $.subscript,
      )),
      choice(
        '=',
        '+=',
      ),
      field('value', choice(
        $._literal,
        $.array,
        $._empty_value,
        alias($._comment_word, $.word),
      )),
    ),

    // `a=x b=y` is ambiguous with the assignment prefixes of a command
    // (conflict [command, variable_assignments]). If the next line starts a
    // command, the command branch can skip the newline as whitespace (extras)
    // and take that line's first word as its name; both branches then parse
    // without errors. Prefer the assignments: a newline always ends them,
    // while on one line (or after a \-continuation) the assignments branch
    // fails and the command branch still wins.
    variable_assignments: $ => prec.dynamic(1, seq($.variable_assignment, repeat1($.variable_assignment))),

    subscript: $ => prec.left(seq(
      field('name', $.variable_name),
      '[',
      $._subscript_body_with_string,
      ']',
    )),

    // Arithmetic expressions for parameter subscripts (avoids subscript recursion)
    _param_arithmetic_expression: $ => prec(1, choice(
      $._param_arithmetic_literal,
      $.concatenation,
      alias($._param_arithmetic_unary_expression, $.unary_expression),
      alias($._param_arithmetic_ternary_expression, $.ternary_expression),
      alias($._param_arithmetic_binary_expression, $.binary_expression),
      alias($._param_arithmetic_postfix_expression, $.postfix_expression),
      alias($._param_arithmetic_parenthesized_expression, $.parenthesized_expression),
      $.command_substitution,
      $.process_substitution,
      $.arithmetic_expansion,
      $.brace_expression,

    )),

    _param_arithmetic_literal: $ => prec(1, choice(
      $.number,
      // Nested subscript inside a subscript: ${suf[a[1]+1]}
      alias($._param_subscript, $.subscript),
      $.variable_ref,
      $.expansion,
      $.variable_name,
      $.string,
      $.glob_pattern,             // Allow glob patterns as subscript indices: var[(I)foo_*]
      $._word_or_word_with_colon  // Allow bare identifiers in parameter arithmetic context
    )),

    _param_subscript: $ => prec.left(seq(
      field('name', $.variable_name),
      '[',
      $._subscript_body,
      ']',
    )),

    _param_arithmetic_binary_expression: $ => createBinaryExpression(
      OPERATOR_TABLES.ARITHMETIC,
      $._param_arithmetic_expression,
      $._param_arithmetic_expression
    ),

    _param_arithmetic_ternary_expression: $ => prec.left(PREC.TERNARY, seq(
      field('condition', $._param_arithmetic_expression),
      '?',
      field('consequence', $._param_arithmetic_expression),
      ':',
      field('alternative', $._param_arithmetic_expression),
    )),

    _param_arithmetic_unary_expression: $ => choice(
      prec(PREC.PREFIX, seq(
        field('operator', tokenLiterals(1, '++', '--')),
        $._param_arithmetic_expression,
      )),
      prec(PREC.UNARY, seq(
        field('operator', tokenLiterals(1, '-', '+', '~')),
        $._param_arithmetic_expression,
      )),
      prec.right(PREC.UNARY, seq(
        field('operator', '!'),
        $._param_arithmetic_expression,
      )),
    ),

    _param_arithmetic_postfix_expression: $ => prec(PREC.POSTFIX, seq(
      $._param_arithmetic_expression,
      field('operator', choice('++', '--')),
    )),

    _param_arithmetic_parenthesized_expression: $ => seq(
      '(',
      $._param_arithmetic_expression,
      ')',
    ),
    // Array expansion operators: [*] and [@]
    array_star: $ => $._array_star_token,
    array_at: $ => $._array_at_token,

    // Zsh array subscript flags: (i) (I) (r) (R) (ws:,:) (n:2:i) etc.
    //
    // The flags decide how the index is read. The external scanner lexes the
    // flags (it owns '(') and picks the token, so no backtracking is needed:
    //  - pattern flags (any of r R i I k K) take a *pattern* index:
    //    $words[(i)--], ${a[(r)*@*]}, $a[(I)(foo|bar)]
    //  - other flags (w p f e s n b) keep the arithmetic index:
    //    ${x[(ws:,:)2]}
    // Arguments (s n b j) take any delimiter; ( [ { < pair with their closer.
    _subscript_body: $ => choice(
      seq(
        optional(field('flags', alias($._subscript_flags, $.zsh_array_subscript_flags))),
        field('index', choice($._param_arithmetic_expression, $.array_star, $.array_at)),
      ),
      seq(
        field('flags', alias($._subscript_pattern_flags, $.zsh_array_subscript_flags)),
        // ${r[(r)]}: the pattern may be empty
        optional(field('index', $._subscript_pattern)),
      ),
    ),

    _subscript_body_with_string: $ => choice(
      seq(
        optional(field('flags', alias($._subscript_flags, $.zsh_array_subscript_flags))),
        // pc['key']=value: a quoted key ('...' as well as "...")
        field('index', choice($._param_arithmetic_expression, $.array_star, $.array_at, $.string, $.raw_string)),
      ),
      seq(
        field('flags', alias($._subscript_pattern_flags, $.zsh_array_subscript_flags)),
        // ${r[(r)]}: the pattern may be empty
        optional(field('index', $._subscript_pattern)),
      ),
    ),

    // Pattern index: adjacent pieces, no whitespace between them.
    _subscript_pattern: $ => choice(
      $._subscript_pattern_piece,
      alias($._subscript_pattern_concatenation, $.concatenation),
    ),

    _subscript_pattern_concatenation: $ => prec.left(seq(
      $._subscript_pattern_piece,
      repeat1($._subscript_pattern_piece),
    )),

    _subscript_pattern_piece: $ => choice(
      alias($._subscript_pattern_word, $.word),
      alias($._subscript_pattern_glob, $.glob_pattern),
      $.variable_ref,
      $.expansion,
      $.command_substitution,
      $.string,
      $.raw_string,
    ),

    // _subscript_pattern_word / _subscript_pattern_glob are external: the
    // scanner reads the piece and calls it a word or a glob_pattern, and it
    // refuses them during error recovery (as broad grammar tokens they were
    // lexed there and made recovery much worse).


    file_redirect: $ => prec.left(seq(
      field('descriptor', optional($.file_descriptor)),
      choice(
        seq(
          // zsh adds <> (read/write) and the clobber forms >! >>| >>! &>| &>!
          choice('<', '>', '>>', '&>', '&>>', '<&', '>&', '>|',
                 '<>', '>!', '>>|', '>>!', '&>|', '&>!', '&>>|', '&>>!'),
          field('destination', repeat1($._literal)),
        ),
        seq(
          choice('<&-', '>&-'), // close file descriptor
          optional(field('destination', $._literal)),
        ),
      ),
    )),

    // exec {fd}<>"$fifo": a named descriptor. One token with the operator
    // so a {word} elsewhere (git @{upstream}, print {a}) isn't taken as a
    // descriptor; the operator is part of the descriptor's text.
    _named_fd_redirect: $ => prec.left(seq(
      field('descriptor', alias($._named_fd_with_operator, $.file_descriptor)),
      optional(field('destination', repeat1($._literal))),
    )),
    _named_fd_with_operator: _ => token(seq(
      /\{[A-Za-z_][A-Za-z0-9_]*\}/,
      choice('<>', '>>|', '>>!', '>>', '>|', '>!', '<&-', '>&-', '<&', '>&',
             '<', '>'),
    )),

    heredoc_redirect: $ => seq(
      field('descriptor', optional($.file_descriptor)),
      choice('<<', '<<-'),
      $.heredoc_start,
      optional(choice(
        alias($._heredoc_pipeline, $.pipeline),
        seq(
          field('redirect', repeat1($._redirect)),
          optional($._heredoc_expression),
        ),
        $._heredoc_expression,
        $._heredoc_command,
      )),
      /\n/,
      choice($._heredoc_body, $._simple_heredoc_body),
    ),

    _heredoc_pipeline: $ => seq(
      choice('|', '|&'),
      $._statement,
    ),

    _heredoc_expression: $ => seq(
      field('operator', choice('||', '&&')),
      field('right', $._statement),
    ),

    _heredoc_command: $ => repeat1(field('argument', $._literal)),

    _heredoc_body: $ => seq(
      $.heredoc_body,
      $.heredoc_end,
    ),

    heredoc_body: $ => seq(
      $._heredoc_body_beginning,
      repeat(choice(
        $.expansion,
        $.variable_ref,
        $.command_substitution,
        $.heredoc_content,
      )),
    ),

    _simple_heredoc_body: $ => seq(
      alias($.simple_heredoc_body, $.heredoc_body),
      $.heredoc_end,
    ),

    herestring_redirect: $ => prec.left(1, seq(
      optional(field('descriptor', $.file_descriptor)),
      '<<<',
      $._literal,
    )),

    _redirect: $ => choice($.file_redirect, alias($._named_fd_redirect, $.file_redirect), $.herestring_redirect),

    // Expressions

    _expression: $ => choice(
      $._literal,
      $.unary_expression,
      $.ternary_expression,
      $.binary_expression,
      $.postfix_expression,
      $.parenthesized_expression,
    ),

    // https://tldp.org/LDP/abs/html/opprecedence.html
    binary_expression: $ => createBinaryExpression(
      [...OPERATOR_TABLES.FULL, [$.test_operator, PREC.TEST]], // Add test_operator to full table
      $._expression,
      $._expression,
      {
        specialCases: [
          // Regex matching with new regex expression type
          prec(PREC.ASSIGN, seq(
            field('left', $._expression),
            field('operator', '=~'),
            field('right', $._regex_match_expression), // NEW: dedicated regex expression
          )),
          // Glob pattern matching (unchanged)
          prec(PREC.EQUALITY, seq(
            field('left', $._expression),
            field('operator', choice('==', '!=')),
            field('right', $._extglob_blob),
          )),
        ]
      }
    ),

    ternary_expression: $ => prec.left(PREC.TERNARY, seq(
      field('condition', $._expression),
      '?',
      field('consequence', $._expression),
      ':',
      field('alternative', $._expression),
    )),

    unary_expression: $ => choice(
      prec(PREC.PREFIX, seq(
        field('operator', tokenLiterals(1, '++', '--')),
        $._expression,
      )),
      prec(PREC.UNARY, seq(
        field('operator', tokenLiterals(1, '-', '+', '~')),
        $._expression,
      )),
      prec.right(PREC.UNARY, seq(
        field('operator', '!'),
        $._expression,
      )),
      prec.right(PREC.TEST, seq(
        field('operator', $.test_operator),
        $._expression,
      )),
    ),

    postfix_expression: $ => prec(PREC.POSTFIX, seq(
      $._expression,
      field('operator', choice('++', '--')),
    )),

    parenthesized_expression: $ => seq(
      '(',
      $._expression,
      ')',
    ),

    // SIMPLIFIED: Regex matching expression system (for use after =~ operator)
    // Structure: simple choice of literals and concatenation
    // Content: full complexity within expansions and other components
    // Let the scanner handle regex tokenization (including (.*)$ patterns)
    _regex_match_expression: $ => choice(
      $._regex_match_literal,
      alias($._regex_match_concatenation, $.concatenation),
    ),

    // Regex literals - let scanner handle regex patterns, grammar handles expansions
    _regex_match_literal: $ => choice(
      alias($._regex_no_space, $.regex), // Use no-space regex to avoid consuming ]] terminator
      $.word,                     // Simple words
      $.string,                   // "quoted strings" with full internal complexity
      $.raw_string,               // 'raw strings'
      $.ansi_c_string,            // $'\t' style literals
      $.expansion,                // ${foo:-default} with full parameter expansion complexity
      $.variable_ref,             // $var with full variable reference complexity
      $.command_substitution,     // $(cmd) with full command complexity
      $.arithmetic_expansion,     // $((expr)) with full arithmetic complexity
    ),

    // Regex concatenation - simple structure, complex components
    _regex_match_concatenation: $ => prec(-1, seq(
      $._regex_match_literal,
      repeat1(seq(
        choice($._concat_regex, alias(/`\s*`/, '``')),
        $._regex_match_literal,
      )),
    )),

    // Literals

    _literal: $ => choice(
      $._primary_expression,
      $.concatenation,
      alias(prec(-2, repeat1($._special_character)), $.word),
    ),

    _primary_expression: $ => choice(
      $.qualified_expression,    // qualified globs should be first
      $._expansion_or_variable,
      $.glob_pattern,
      $._word_or_word_with_colon,
      alias($.test_operator, $.word),
      alias($.escaped_open_paren, $.word),
      alias($.escaped_close_paren, $.word),
      $.string,
      $.raw_string,
      $.translated_string,
      $.ansi_c_string,
      $.number,
      $.command_substitution,
      $.process_substitution,
      $.arithmetic_expansion,
      $.brace_expression,
      // A '$' that can't start an expansion is literal (echo $; "%B$%b").
      alias($._literal_dollar, $.word),
    ),
    
    _limited_primary_expression: $ => choice(
      $.expansion,      // Try ${...} patterns first
      $.variable_ref,   // Fall back to $var patterns
      $.string,
      $.raw_string,
      $.translated_string,
      $.ansi_c_string,
      $.number,
      $.command_substitution,
      $.process_substitution,
      $.arithmetic_expansion,
    ),
    
    // Unified rule for all dollar-based patterns to eliminate competition  
    _expansion_or_variable: $ => choice(
      $.expansion,      // Try ${...} patterns first
      $.variable_ref,   // Fall back to $var patterns
    ),

    _dollar_arithmetic_expansion: $ => prec(4, choice(
      seq(
        alias(seq($._bare_dollar, '(('), "$(("),
        optional($.arithmetic_base),
        commaSep1($._arithmetic_expression),
        '))'
      ),
      seq(
        alias(seq($._bare_dollar, '['), "$["),
        $._arithmetic_expression,
        ']'),
    )),

    arithmetic_expansion: $ => prec(4, choice(
      seq(
        choice(
          alias(seq($._bare_dollar, '(('), "$(("),
          '(('
        ),
        optional($.arithmetic_base),
        commaSep1($._arithmetic_expression),
        '))'
      ),
      seq(
        alias(seq($._bare_dollar, '['), "$["),
        $._arithmetic_expression,
        ']'),
      )
    ),

    // {1..5} {1..10..2} {01..03} {-2..2}. The closing '}' is the external
    // CLOSING_BRACE, so the scanner pops the CTX_BRACE_EXPANSION it pushed
    // at '{' (an internal '}' never reached it: every range leaked it).
    brace_expression: $ => seq(
      alias($._brace_expr_start, '{'),
      alias(token.immediate(/-?\d+/), $.number),
      token.immediate('..'),
      alias(token.immediate(/-?\d+/), $.number),
      optional(seq(token.immediate('..'), alias(token.immediate(/-?\d+/), $.number))),
      '}',
    ),

    // A robust glob pattern must contain at least one glob metacharacter
    // This prevents tilde expansion like ~/foo from being parsed as glob
    // Groups may have empty alternatives: *.info(|.gz|.bz2), x(|.*)
    _glob_innards: _ => token(choice(
      // Pattern starting with glob metacharacter
      seq(
        choice(
          /\*\*/,                                   // **
          /\*/,                                     // *
          /\?/,                                     // ?
          /\[[!^]?[^\]]*\]/,                        // [...]
          /<[0-9]*-[0-9]*>/,                        // <1-10> <-> <20-> <-5>
          /\{[^}]*,[^}]*\}/,                        // {a,b}
          /\([^)|]*(\|[^)|]*)+\)/                   // (a|b|c)
        ),
        repeat(choice(
          /[^\s'"*?\[{()}<=\];]/,                 // regular chars excluding delimiters and backslash (allow ) for filenames)
          /\\./,                                   // escaped sequences
          /\*\*/,                                   // **
          /\*/,                                     // *
          /\?/,                                     // ?
          /\[[!^]?[^\]]*\]/,                        // [...]
          /<[0-9]*-[0-9]*>/,                        // <1-10> <-> <20-> <-5>
          /\{[^}]+,[^}]*\}/,                        // {a,b}
          /~/,                                      // ~ (allowed after initial char)
          /\([^)|]*(\|[^)|]*)+\)/                   // (a|b|c)
        ))
      ),
      // Pattern with regular chars containing at least one glob metacharacter
      seq(
        repeat1(choice(
          /[^\s'"*?\[{()}<=\];#]/,                // regular chars excluding delimiters, backslash, and # (# at start is a comment)
          /\\./                                    // escaped sequences
        )),
        choice(
          /\*\*/,                                   // **
          /\*/,                                     // *
          /\?/,                                     // ?
          /\[[!^]?[^\]]*\]/,                        // [...]
          /<[0-9]*-[0-9]*>/,                        // <1-10> <-> <20-> <-5>
          /\{[^}]*,[^}]*\}/,                        // {a,b}
          /\([^)|]*(\|[^)|]*)+\)/                   // (a|b|c)
        ),
        repeat(choice(
          /[^\s'"*?\[{()}<=\];]/,                 // regular chars excluding delimiters and backslash (allow ) for filenames)
          /\\./,                                   // escaped sequences
          /\*\*/,                                   // **
          /\*/,                                     // *
          /\?/,                                     // ?
          /\[[!^]?[^\]]*\]/,                        // [...]
          /<[0-9]*-[0-9]*>/,                        // <1-10> <-> <20-> <-5>
          /\{[^}]+,[^}]*\}/,                        // {a,b}
          /~/,                                      // ~ (allowed after initial char)
          /\([^)|]*(\|[^)|]*)+\)/                   // (a|b|c)
        ))
      )
    )),

    qualified_expression: $ => prec(2, seq(
      field('expression', choice(
        $.expansion,              // ${...} - can produce arrays
        $.command_substitution,   // $(...) - can produce multiple lines  
        $.variable_ref,          // $array - if it's an array variable
        // Could also include arithmetic that produces arrays
      )),
      field('qualifier', $.zsh_glob_qualifier),  // (ND), (.), etc.
    )),

    // Unified glob pattern that handles **, *, ?, [] patterns with optional zsh qualifiers/modifiers
    glob_pattern: $ => prec.dynamic(-1, prec.right(choice(
      // True glob patterns with wildcards
      seq(
        optional(field('flags', $.zsh_extended_glob_flags)),
        field('pattern', $._glob_innards),
        // Flags may also appear mid-pattern: *(#i)foo, (#b)a(#B)b*
        repeat(seq(
          field('flags', $.zsh_extended_glob_flags),
          field('pattern', choice($._glob_innards, $.word)),
        )),
        optional(choice(
          prec.right(field('qualifier', $.zsh_glob_qualifier)),
          prec.right(field('modifier', $.zsh_glob_modifier)),
          seq(
            prec.right(field('qualifier', $.zsh_glob_qualifier)),
            prec.right(field('modifier', $.zsh_glob_modifier)),
          ),
        )),
      ),
      // Flags before an expansion: (#i)$PWD, (#b)${~pat}
      seq(
        field('flags', $.zsh_extended_glob_flags),
        field('pattern', choice($.variable_ref, $.expansion)),
      ),
      // Words with qualifiers/modifiers (like /path/file(:h))
      prec(1, seq(
        optional(field('flags', $.zsh_extended_glob_flags)),
        field('pattern', $.word),
        choice(
          prec.right(field('qualifier', $.zsh_glob_qualifier)),
          prec.right(field('modifier', $.zsh_glob_modifier)),
          seq(
            prec.right(field('qualifier', $.zsh_glob_qualifier)),
            prec.right(field('modifier', $.zsh_glob_modifier)),
          ),
        ),
      )),
    ))),
    


    // Zsh extended glob flags: (#i) (#q) (#a2) etc.
    zsh_extended_glob_flags: $ => $._zsh_extended_glob_flags,

    _arithmetic_expression: $ => prec(1, choice(
      $._arithmetic_literal,
      alias($._arithmetic_unary_expression, $.unary_expression),
      alias($._arithmetic_ternary_expression, $.ternary_expression),
      alias($._arithmetic_binary_expression, $.binary_expression),
      alias($._arithmetic_postfix_expression, $.postfix_expression),
      alias($._arithmetic_parenthesized_expression, $.parenthesized_expression),
      $.arithmetic_call,
      $.command_substitution,
    )),

    _arithmetic_literal: $ => prec(1, choice(
      // Identifiers and numbers lexed for arithmetic (the scanner emits
      // _arithmetic_word in CTX_ARITHMETIC), so i++ a-b a,b split into
      // operands and operators instead of one shell word.
      alias($._arithmetic_concatenation, $.concatenation),
      alias($._arithmetic_word, $.word),
      alias($._arithmetic_subscript, $.subscript),
      alias($._arithmetic_number, $.number),
      $.arithmetic_char_code,   // ##a (code of a), #name (code of $name's 1st char)
      $.arithmetic_expansion,   // $[1+2] / $(( )) nested
      $.number,
      $.subscript,
      $.variable_ref,
      $.expansion,
      $.variable_name,
      $.string,
      $._word_or_word_with_colon  // Allow bare identifiers in arithmetic context (e.g., 'start' in $((start)))
    )),

    _arithmetic_binary_expression: $ => createBinaryExpression(
      OPERATOR_TABLES.ARITHMETIC,
      $._arithmetic_expression,
      $._arithmetic_expression
    ),

    _arithmetic_ternary_expression: $ => prec.left(PREC.TERNARY, seq(
      field('condition', $._arithmetic_expression),
      '?',
      field('consequence', $._arithmetic_expression),
      ':',
      field('alternative', $._arithmetic_expression),
    )),

    _arithmetic_unary_expression: $ => choice(
      prec(PREC.PREFIX, seq(
        field('operator', tokenLiterals(1, '++', '--')),
        $._arithmetic_expression,
      )),
      prec(PREC.UNARY, seq(
        field('operator', tokenLiterals(1, '-', '+', '~')),
        $._arithmetic_expression,
      )),
      prec.right(PREC.UNARY, seq(
        field('operator', '!'),
        $._arithmetic_expression,
      )),
    ),

    _arithmetic_postfix_expression: $ => prec(PREC.POSTFIX, seq(
      $._arithmetic_expression,
      field('operator', choice('++', '--')),
    )),

    // (( (H=h, C=c) )): the comma operator inside parentheses
    _arithmetic_parenthesized_expression: $ => seq(
      '(',
      commaSep1($._arithmetic_expression),
      ')',
    ),

    // Function call in arithmetic context: sqrt(x), sin(a), abs(n), etc.
    // Adjacent pieces form one operand: $x$y, x$i (the name x1), 1$x
    _arithmetic_concatenation: $ => prec.left(seq(
      $._arithmetic_piece,
      repeat1(seq($._concat, $._arithmetic_piece)),
    )),
    _arithmetic_piece: $ => choice(
      $.arithmetic_char_code,   // ##$chr
      alias($._arithmetic_word, $.word),
      alias($._arithmetic_number, $.number),
      $.variable_ref,
      $.expansion,
      $.command_substitution,
      $.arithmetic_expansion,
      $.string,
    ),

    // x[1] / x[(i)a] inside (( )): same shape as subscript
    _arithmetic_subscript: $ => prec.left(seq(
      field('name', alias($._arithmetic_word, $.variable_name)),
      '[',
      $._subscript_body_with_string,
      ']',
    )),

    // 0x1F 1.5 .5 1e3 16#ff 2#1010 (zshmisc "ARITHMETIC EVALUATION")
    _arithmetic_number: _ => token(prec(1,
      // (a lone '.' only matters before an expansion: .${frac} is a float)
      /0[xX][0-9A-Fa-f]+|[0-9]+#[0-9A-Za-z@_]+|([0-9]+\.?[0-9]*|\.[0-9]*)([eE][-+]?[0-9]+)?/)),

    // $(( [##8] x )) / $(( [#16_4] x )): output base
    arithmetic_base: _ => token(/\[##?[0-9]+(_[0-9]*)?\]/),

    arithmetic_call: $ => prec(2, seq(
      field('name', choice($.word, alias($._arithmetic_word, $.word))),
      token.immediate('('),
      optional(seq(
        field('argument', $._arithmetic_expression),
        repeat(seq(',', field('argument', $._arithmetic_expression))),
      )),
      ')',
    )),


    concatenation: $ => prec(-1, 
      seq(
      choice(
        $._primary_expression,
        alias($._special_character, $.word),
      ),
      repeat1(seq(
        choice($._concat, alias(/`\s*`/, '``')),
        choice(
          $._primary_expression,
          alias($._special_character, $.word),
          alias($._comment_word, $.word),
          // Use PEEK to check for $ without consuming it for concatenation
          $._peek_bare_dollar,
        ),
      )),
      // Use PEEK for trailing $ in concatenation
      optional(seq($._concat, $._peek_bare_dollar)),
    )),

    _special_character: _ => token(prec(-1,
      choice(token.immediate('{'), 
             token.immediate('}'), 
             token.immediate('['),
             token.immediate(']'),
             token.immediate(':')))),

    string: $ => seq(
      '"',
      repeat(seq(
        choice(
          $.string_content,
          alias($._literal_dollar, $.string_content),
          // A literal '$' before text ("a$ b", "$:"). The text can't start
          // like a parameter or a style (^ = ~ +), so "$^jobs" "$=x" "$~p"
          // go through variable_ref instead of becoming '$' + text.
          seq($._bare_dollar, alias($._string_content_after_dollar, $.string_content)),
          $.expansion,
          $.variable_ref,
          $.command_substitution,
          // Only $(( )) / $[ ] expand in a string; a bare (( is text.
          alias($._dollar_arithmetic_expansion, $.arithmetic_expansion),
        ),
        optional($._concat),
      )),
      optional($._raw_dollar),
      '"',
    ),

    // Default lexical precedence: at prec -1 whitespace-skipping beat a
    // string_content starting with a blank right after an expansion
    // ("$R " lost the blank and the closing quote bypassed the scanner,
    // leaking CTX_STRING onto the context stack).
    // A newline inside "..." is part of the string. (Excluding it left
    // newlines to the extras: they were dropped from the tree, and before
    // a closing quote the internal lexer produced the '"' itself, so the
    // scanner never popped CTX_STRING.)
    _string_content_after_dollar: _ => token(
      /([^"`$\\^=~+A-Za-z0-9_]|\\(.|\r?\n))([^"`$\\]|\\(.|\r?\n))*/),

    string_content: _ => token(/([^"`$\\]|\\(.|\r?\n))+/),

    translated_string: $ => prec(1, seq($._bare_dollar, $.string)),

    array: $ => seq(
      '(',
      repeat($._literal),
      ')',
    ),

    raw_string: _ => seq("'", /[^']*/, "'"),

    ansi_c_string: $ => prec(0,
      seq(
        alias($._bare_dollar, "$"), '\'', /([^']|\\')*/, '\''
      )
    ),

    number: $ => choice(
      /-?(0x)?[0-9]+(#[0-9A-Za-z@_]+)?/,
      // the base can be an expansion or command substitution
      seq(/-?(0x)?[0-9]+#/, choice($.expansion, $.command_substitution)),
    ),

    // Note: this is different than variable_name
    // in part it allows subscript, 
    // but more importably, variable_name is used with
    // assignment like semantics
    variable_ref: $ => prec.right(1, seq(
      alias($._bare_dollar, "$"),
      $._variable_ref,
    )),

    _variable_ref: $ => prec.right(40, seq(
      seq(
          optional($.expansion_style),
          choice($._special_variable_name, $._simple_variable_name)
      ),
      optional(seq(
        '[',
        $._subscript_body,
        ']'
      )),
      optional(field('modifier', $.expansion_modifier))
    )),

    // Optional style: ${a:#$~p} / ${pages%$~suf} (glob-subst of $p)
    dollar_variable: $ => prec.right(1, seq(
      alias($._bare_dollar, "$"),
      optional($.expansion_style),
      choice(
        $._simple_variable_name,
        $._special_variable_name,
      ),
      // ${x//$y[1]/}: right after a parameter '[' is a subscript, as in zsh
      optional(seq('[', $._subscript_body, ']')),
    )),

    // Variable references within expansion contexts (similar to _variable_ref)
    _expansion_variable_ref: $ => seq(
      choice(
        $._simple_variable_name,
        $._special_variable_name,
        $._raw_command_expansion,
        $._limited_primary_expression
      ),
      // Postfix subscripts; inside braces they chain: ${a[1][2,3]} is
      // characters 2-3 of element 1.
      repeat(seq(
        '[',
        $._subscript_body,
        ']'
      ))
    ),

    string_expansion: $ => seq($._bare_dollar, $.string),

    // Visible rule for parameter expansion substring operations
    expansion_substring: $ => $._expansion_max_length,
    
    // Inside ${x:-...} the value is one word up to the matching '}':
    // ( ) < > | ; & are literal there, not shell syntax. _default_word
    // (external) covers text starting with one of those, e.g.
    // ${X:-(none)} "${X:-<unset>}" "${x:+ (${y}ms)}"; everything else
    // keeps the normal literal rules.
    expansion_default_list: $ => repeat1(choice(
      $._literal,
      alias($._default_word, $.word),
    )),

    _expansion_default_value: $ => choice(
      $.expansion_default_list,
      $._empty_value
    ),

    // Visible rule for parameter expansion defaults
    expansion_default: $ => prec(5, seq(
      optional(field('name', $._expansion_variable_ref)),
      choice(
        seq(token.immediate('-'), field('default', $._expansion_default_value)),
        // Single multichar immediate token so the lexer commits to `:-`
        // before `$.number`'s `-?` can claim the `-`. Same idea for the
        // other colon-prefixed operators below. Without this, inputs like
        // ${var:-N} or ${5:-0.2} mis-lex as substring with negative offset.
        seq(token.immediate(':-'), field('default', $._expansion_default_value)),
        seq(token.immediate('+'), field('default', $._expansion_default_value)),
        seq(token.immediate(':+'), field('default', $._expansion_default_value)),
        seq(token.immediate('='), field('default', $._expansion_default_value)),
        seq(token.immediate(':='), field('default', $._expansion_default_value)),
        seq(token.immediate('='), field('default', $._expansion_default_value)),
        seq(token.immediate('::='), field('default', $._expansion_default_value)),
        seq(token.immediate('?'), field('default', $._expansion_default_value)),
        seq(token.immediate(':?'), field('default', $._expansion_default_value)),
      )
    )),

    // Visible rule for parameter expansion patterns 
    // Suffix/prefix patterns may be empty: ${(@)x:#} drops empty
    // elements, ${x#} is just $x. The scanner won't emit
    // PATTERN_SUFFIX_START before '}', so the start+pattern pair is optional.
    expansion_pattern: $ => seq(
      field('name', $._expansion_variable_ref),
      choice(
        seq($._hash_pattern, optional(seq($._pattern_suffix_start, field('pattern', $._param_pattern)))),
        seq($._double_hash_pattern, optional(seq($._pattern_suffix_start, field('pattern', $._param_pattern)))),
        seq(token.immediate('%'), optional(seq($._pattern_suffix_start, field('pattern', $._param_pattern)))),
        seq(token.immediate('%%'), optional(seq($._pattern_suffix_start, field('pattern', $._param_pattern)))),
        seq(token.immediate(':'), $._hash_pattern, optional(seq($._pattern_suffix_start, field('pattern', $._param_pattern)))),
        seq(token.immediate(':'), $._double_hash_pattern, optional(seq($._pattern_suffix_start, field('pattern', $._param_pattern)))),
        seq(token.immediate(':'), token.immediate('%'), optional(seq($._pattern_suffix_start, field('pattern', $._param_pattern)))),
        seq(token.immediate(':'), token.immediate('%%'), optional(seq($._pattern_suffix_start, field('pattern', $._param_pattern)))),
        seq(token.immediate('//'), 
            $._pattern_start, 
            field('pattern', $._param_pattern_no_slash),
            optional(
              seq(
                token.immediate('/'),
                optional(repeat1(choice(field('replacement', $._param_replacement), alias(token.immediate('/'), $.word))))
              )
            )
        ),
        seq(token.immediate('/'), 
            $._pattern_start, 
            field('pattern', $._param_pattern_no_slash),
            optional(
              seq(
                token.immediate('/'),
                optional(repeat1(choice(field('replacement', $._param_replacement), alias(token.immediate('/'), $.word)))),
              )
            ),
        ),
        seq(token.immediate(':'), token.immediate('/'), 
            $._pattern_start,
            field('pattern', $._param_pattern_no_slash),
            optional(
              seq(
                token.immediate('/'),
                optional(repeat1(choice(field('replacement', $._param_replacement), alias(token.immediate('/'), $.word)))),
              )
            ),
        )
      )
    ),

    // Visible rule for parameter expansion arrays
    expansion_array: $ => seq(
      field('name', $._expansion_variable_ref),
      choice(
        seq(token.immediate(':'), token.immediate('|'), field('array', $._literal)),
        seq(token.immediate(':'), token.immediate('*'), field('array', $._literal)),
        seq(token.immediate(':'), token.immediate('^'), field('array', $._literal)),
        seq(token.immediate(':'), token.immediate('^'), token.immediate('^'), field('array', $._literal)),
      )
    ),

    // Each modifier is lexed together with its ':' (:h, :t2, :P, :s/,
    // :gs/), so a ':' not followed by a modifier is just a ':' ("$A:$B").
    // The chain always ends with the scanner's zero-width _subst_end, so
    // whatever follows $x:h:s/a/b in the surrounding context can't reach
    // inside it: otherwise every context gets its own copy of the modifier
    // (and :s pattern/replacement) states, which was over a third of the
    // parse table.
    expansion_modifier: $ => prec.right(4, seq(repeat1(choice(
      token.immediate(/:[aAce]/),
      token.immediate(/:h[0-9]*/),
      token.immediate(/:[lpPqQr]/),
      // :s/pat/rep/ and :gs/pat/rep/ (the closing '/' may be left off).
      seq(
        token.immediate(/:g?s\//),
        $._pattern_start,
        field('search', $._param_pattern_no_slash),
        token.immediate('/'), optional(field('replace', $._param_replacement)),
        optional(seq(token.immediate('/'), optional(token.immediate(':G')))),
      ),
      token.immediate(/:[&ux]/),
      token.immediate(/:t[0-9]*/),
      // :f / :F:n: (repeat the next modifier) and :s with a delimiter other
      // than '/' (:s#a#b#): one scanner token, as the delimiter is chosen
      // in the text.
      $._modifier_other,
    )), $._subst_end)),

    // Maintain same order as zsh docs
    expansion_with_modifier: $ => seq(
      optional(field('name', $._expansion_variable_ref)),
      field('modifier', $.expansion_modifier)
    ),

    expansion_style: _ => choice(
      token.immediate('#'),  // ${# var} - length
      token.immediate('!'),  // ${! var} - indirect expansion
      token.immediate('+'),  // ${+ var} - exists
      // Order longer sequences first to resolve conflicts
      prec(2, token.immediate('^^')),  // ${^^ var} - RC_EXPAND_PARAM
      prec(1, token.immediate('^')),  // ${^ var} - RC_EXPAND_PARAM
      prec(2, token.immediate('==')),  // ${== var} - SH_WORD_SPLIT
      prec(1, token.immediate('=')),  // ${= var} - SH_WORD_SPLIT
      prec(2, token.immediate('~~')),  // ${~~ var} - GLOB_SUBST
      prec(1, token.immediate('~')),  // ${~ var} - GLOB_SUBST
      // Combinations: ${^~x} ${=~x} ${~^x} ...
      prec(3, token.immediate(/[\^=~][\^=~]+/)),
    ),

    expansion: $ => seq(
      prec(2, alias(seq($._bare_dollar, $._brace_start), "${")),
      choice(
        prec.right(10, seq(field('style', $.expansion_style), 
             field('flags', $.expansion_flags), $._expansion_body)),
        prec.right(10, seq(field('style', $.expansion_style), $._expansion_body)),
        prec.left(10, seq(field('flags', $.expansion_flags), $._expansion_body)),
        // Style after the flags, as zsh also accepts: ${(s: :)^x}, ${(j:,:)=x}
        prec.right(11, seq(field('flags', $.expansion_flags),
             field('style', $.expansion_style), $._expansion_body)),
        // ${(q)} ${(l:11:: :)}: flags with no parameter (an empty value)
        prec.left(9, field('flags', $.expansion_flags)),
        prec.left($._expansion_body),
      ),
      '}',
    ),

    // Zsh parameter expansion flags: ${(L)var}, ${(j:,:)array}, etc.
    // Flag arguments may contain one level of parentheses:
    // ${(r.(c==1) ? 28 : 32.):-x}
    // ... and two levels for arithmetic in an argument: ${(pl:$((w-8))::-:)}
    expansion_flags: _ => token.immediate(/\(([^()]|\(([^()]|\([^()]*\))*\))+\)/),

    _expansion_body: $ => prec(1, choice(
     // HIGHEST PRIORITY: Colon operations (moved to top)
     $.expansion_with_modifier,
    
     $.expansion_substring,
     
     $.expansion_pattern,
     $.expansion_default,
     $.expansion_array,
     
     // Basic variable reference (fallback for ${var})
     field('name', choice($._expansion_variable_ref,
       $.expansion, $.string, $.raw_string,
       $.command_substitution)),
    )),

    // Base expressions (recursive)
    _expansion_pattern: $ => choice(
      $.regex,
      $.word,
      $.string,
      $.raw_string,
      $._limited_primary_expression,
    ),
    
    // Replacement value for substitutions
    _expansion_replacement: $ => choice(
      $.word,
      $.string,
      $.raw_string,
      $.expansion,
      $.variable_ref,
      $.command_substitution,
      $._limited_primary_expression,
    ),
    
    // Values for assignment operations
    _expansion_value: $ => choice(
      $.word,
      $.string,
      $.raw_string,
      $.array,
      $.expansion,
      $.variable_ref, 
      $.command_substitution,
    ),
    
    // Numeric expressions for substring operations
    _expansion_number: $ => choice(
      $.number,
      $.expansion,
      $.variable_ref,
      $.arithmetic_expansion,
      $.command_substitution,   // $(echo 5) - allows ${var:$(command)}
    ),
    _expansion_expression: $ => prec(1, seq(
      field('operator', immediateLiterals('=', ':=', '-', ':-', '+', ':+', '?', ':?')),
      optional(seq(
        choice(
          alias($._concatenation_in_expansion, $.concatenation),
          $.command_substitution,
          $.word,
          $.expansion,
          $.variable_ref,
          $.array,
          $.string,
          $.raw_string,
          $.ansi_c_string,
          alias($._expansion_word, $.word),
        ),
      )),
    )),

    // variable_ref without trailing expansion_modifier, so that `:offset:length`
    // separators are not greedily consumed by the variable itself.
    _expansion_max_length_variable_ref: $ => prec.left(1, seq(
      alias($._bare_dollar, "$"),
      seq(
        optional($.expansion_style),
        choice($._special_variable_name, $._simple_variable_name)
      ),
      optional(seq(
        '[',
        $._subscript_body,
        ']'
      )),
    )),

    _expansion_max_length: $ => seq(
      field('name', $._expansion_variable_ref),
      token.immediate(':'),
      field('offset', choice(
        alias($._expansion_max_length_variable_ref, $.variable_ref),
        $.number,
        $.arithmetic_expansion,
        $.expansion,
        $.parenthesized_expression,
        $.command_substitution,
        alias($._expansion_max_length_binary_expression, $.binary_expression),
        /\n/,
      )),
      optional(seq(
        token.immediate(':'),
        optional(field('length', choice(
          alias($._expansion_max_length_variable_ref, $.variable_ref),
          $.number,
          $.arithmetic_expansion,
          $.expansion,
          $.parenthesized_expression,
          $.command_substitution,
          alias($._expansion_max_length_binary_expression, $.binary_expression),
          /\n/,
        ))),
      )),
    ),

    _expansion_max_length_expression: $ => choice(
      alias($._expansion_max_length_variable_ref, $.variable_ref),
      $.number,
      $.expansion,
      alias($._expansion_max_length_binary_expression, $.binary_expression),
    ),
    _expansion_max_length_binary_expression: $ => {
      const table = [
        [choice('+', '-'), PREC.ADD],
        [choice('*', '/', '%'), PREC.MULTIPLY],
      ];

      return choice(...table.map(([operator, precedence]) => {
        // @ts-ignore
        return prec.left(precedence, seq(
          $._expansion_max_length_expression,
          // @ts-ignore
          field('operator', operator),
          $._expansion_max_length_expression,
        ));
      }));
    },

    _expansion_operator: _ => seq(
      field('operator', token.immediate('@')),
      field('operator', immediateLiterals('U', 'u', 'L', 'Q', 'E', 'P', 'A', 'K', 'a', 'k')),
    ),

    _concatenation_in_expansion: $ => prec(-2, seq(
      choice(
        $.word,
        $.variable_name,
        $.variable_ref,
        $.expansion,
        $.string,
        $.raw_string,
        $.ansi_c_string,
        $.command_substitution,
        alias($._expansion_word, $.word),
        $.array,
        $.process_substitution,
      ),
      repeat1(seq(
        choice($._concat, alias(/`\s*`/, '``')),
        choice(
          $.word,
          $.variable_name,
          $.variable_ref,
          $.expansion,
          $.string,
          $.raw_string,
          $.ansi_c_string,
          $.command_substitution,
          alias($._expansion_word, $.word),
          $.array,
          $.process_substitution,
        ),
      )),
    )),
   
   _concatenation_in_expansion_regex: $ => prec(-2, seq(
      choice(
        $.regex,
        $.dollar_variable,
        $.expansion,
        $.string,
        $.raw_string,
        $.ansi_c_string,
        $.command_substitution,
        alias($._expansion_word, $.word),  // FIXME
      ),
      repeat1(seq(
        choice($._concat, alias(/`\s*`/, '``')),
        choice(
          $.regex,
          $.dollar_variable,
          $.expansion,
          $.string,
          $.raw_string,
          $.ansi_c_string,
          $.command_substitution,
          alias($._expansion_word, $.word),  // FIXME
        ),
      )),
    )),
 
    _concatenation_in_expansion_regex_no_slash: $ => prec(-2, seq(
      choice(
        alias($._regex_no_slash, $.regex),
        token.immediate(/\s+/),
        $.dollar_variable,
        $.expansion,
        $.string,
        $.raw_string,
        $.ansi_c_string,
        $.command_substitution,
        alias($._expansion_word, $.word),
      ),
      repeat1(seq(
        choice($._concat, alias(/`\s*`/, '``')),
        choice(
          alias($._regex_no_slash, $.regex),
          token.immediate(/\s+/),
          $.dollar_variable,
          $.expansion,
          $.string,
          $.raw_string,
          $.ansi_c_string,
          $.command_substitution,
          alias($._expansion_word, $.word),
        ),
      )),
    )),
   
  _concatenation_in_expansion_regex_replacement: $ => prec(-2, seq(
      choice(
        alias($._regex_no_slash, $.regex),
        token.immediate(/\s+/),
        $.dollar_variable,
        $.expansion,
        $.string,
        $.raw_string,
        $.ansi_c_string,
        $.command_substitution,
        alias($._expansion_word, $.word),
      ),
      repeat1(seq(
        choice($._concat, alias(/`\s*`/, '``')),
        choice(
          alias($._regex_no_slash, $.regex),
          $.dollar_variable,
          $.expansion,
          $.string,
          $.raw_string,
          $.ansi_c_string,
          $.command_substitution,
          alias($._expansion_word, $.word),
        ),
      )),
    )),

    _raw_command_expansion: $ => seq(
      alias(seq($._bare_dollar, '('), "$("), $._statements, ')'),

    command_substitution: $ => prec(1, choice(
      seq(alias(seq($._bare_dollar, '('), "$("), $._statements, ')'),
      seq(alias(seq($._bare_dollar, '('), "$("), field('redirect', $.file_redirect), ')'),
      prec(1, seq('`', $._statements, '`')),
      //seq('$`', $._statements, '`'), // not legal zsh
    )),

    process_substitution: $ => seq(
      choice('<(', '>(',
        prec(-1, '=(')),  // Lower precedence to allow variable assignment to win
        $._statements,
      ')',
    ),

    // Pattern pieces alternating with strings / expansions:
    // (#bi)${~dspat}((19|20)[0-9][0-9])[-/]${~monthpat}[-/](<1-31>)*)
    _extglob_blob: $ => choice(
      $.extglob_pattern,
      seq(
        $.extglob_pattern,
        repeat1(seq(
          $._extglob_nonpiece,
          optional($.extglob_pattern),
        )),
      ),
      // Starting with a string or expansion: (":"(\\|)"!"*),
      // ("tar (GNU tar) "(#b)([0-9.-]##)*)
      seq(
        $._extglob_nonpiece,
        repeat1(seq(
          $.extglob_pattern,
          optional($._extglob_nonpiece),
        )),
      ),
    ),

    _extglob_nonpiece: $ => choice(
      $.string, $.raw_string, $.expansion, $.variable_ref, $.command_substitution,
    ),

    comment: _ => token(prec(-20, /#[^\r\n]*/)),

    // token.immediate: a `#`-word only binds as a value when directly after
    // `=` (var=#foo); otherwise a comment on the next line was taken as the
    // value of an empty `var=` (PR #41).
    _comment_word: _ => token.immediate(prec(-8, seq(
      choice(
        noneOf(...SPECIAL_CHARACTERS),
        seq('\\', noneOf('\\s')),
      ),
      repeat(choice(
        noneOf(...SPECIAL_CHARACTERS),
        seq('\\', noneOf('\\s')),
        '\\ ',
      )),
    ))),

    _simple_variable_name: $ => $.simple_variable_name,

    _special_variable_name: $ => choice($.special_variable_name, $._raw_dollar),

    // A balanced {...} with no blank, separator, ',' or '.' inside is a
    // literal part of a word in zsh ({}, {b}, x@{upstream}, a{b}c), e.g.
    // `find . -exec ls {} +` in a brace group, where the '}' must not close
    // the group. (With ',' or '..' it's a brace expansion, scanned
    // separately; at a command start '{' opens a group, and the scanner
    // emits that first.)
    _word: _ => token(seq(
      choice(
        noneOf('#', ':', ...SPECIAL_CHARACTERS),
        seq('\\', noneOf('\\s')),
        seq('{', repeat(noneOf(',', '.', ...SPECIAL_CHARACTERS)), '}'),
      ),
      repeat(choice(
        noneOf(...SPECIAL_CHARACTERS),
        seq('\\', noneOf('\\s')),
        '\\ ',
        seq('{', repeat(noneOf(',', '.', ...SPECIAL_CHARACTERS)), '}'),
      )),
    )),

    word: $ => $._word,

    _word_with_colon: $ => prec.right(1, seq(
      $._word,
      token.immediate(':'),
      repeat1(choice(
        token.immediate(':'),
        $._word
      )),
    )),

    _word_or_word_with_colon: $ => choice(
      $.word,
      alias($._word_with_colon, $.word)),

    _terminator: _ => choice(';', /\n/, alias(/&([ \t]*;)?/, '&'), alias(/&!([ \t]*;)?/, '&!'), alias(/&\|([ \t]*;)?/, '&|')),

    // Terminator for use inside case item bodies (between statements).
    // Does NOT include ;; ;& ;| so they are not consumed as inter-statement
    // separators and remain available as the explicit case item termination field.
    _case_body_terminator: _ => choice(';', /\n/, alias(/&([ \t]*;)?/, '&'), alias(/&!([ \t]*;)?/, '&!'), alias(/&\|([ \t]*;)?/, '&|')),

    // Like _statements but uses _case_body_terminator between statements so
    // that ;; ;& ;| on a new line end the body rather than being consumed.
    // No leading repeat of terminators — whitespace/newlines before the first
    // statement are handled by extras, and we must NOT consume a leading \n
    // that is followed by ;; (which should close the case arm, not start body).
    _case_item_statements: $ => prec(1, seq(
      repeat(seq(
        $._statement_or_comment,
        optional($.comment),
        $._case_body_terminator,
      )),
      $._statement_or_comment,
      optional($.comment),
      optional($._case_body_terminator),
    )),
    
    // Parameter-safe expression system (excludes glob_pattern)
    _param_expression: $ => choice(
      $._param_literal,
      $._param_unary_expression,
      $._param_ternary_expression,
      $._param_binary_expression,
      $._param_postfix_expression,
      $._param_parenthesized_expression,
    ),

    _param_binary_expression: $ => createBinaryExpression(
      [...OPERATOR_TABLES.ARITHMETIC, [$.test_operator, PREC.TEST]], // Add test_operator to arithmetic table
      $._param_expression,
      $._param_expression,
      {
        specialCases: [
          prec.right(PREC.ASSIGN, seq(
            field('left', $._param_expression),
            field('operator', '='),
            field('right', $._param_expression),
          )),
          prec.right(PREC.ASSIGN, seq(
            field('left', $._param_expression),
            field('operator', '=~'),
            field('right', choice(alias($._regex_no_space, $.regex), $._param_expression)),
          )),
        ]
      }
    ),

    _param_ternary_expression: $ => prec.left(PREC.TERNARY, seq(
      field('condition', $._param_expression),
      '?',
      field('consequence', $._param_expression),
      ':',
      field('alternative', $._param_expression),
    )),

    _param_unary_expression: $ => choice(
      prec(PREC.PREFIX, seq(
        field('operator', tokenLiterals(1, '++', '--')),
        $._param_expression,
      )),
      prec(PREC.UNARY, seq(
        field('operator', tokenLiterals(1, '-', '+', '~')),
        $._param_expression,
      )),
      prec.right(PREC.UNARY, seq(
        field('operator', '!'),
        $._param_expression,
      )),
      prec.right(PREC.TEST, seq(
        field('operator', $.test_operator),
        $._param_expression,
      )),
    ),

    _param_postfix_expression: $ => prec(PREC.POSTFIX, seq(
      $._param_expression,
      field('operator', choice('++', '--')),
    )),

    _param_parenthesized_expression: $ => seq(
      '(',
      $._param_expression,
      ')',
    ),
    _param_assignment_value: $ => choice(
      $._param_literal,
      $._param_array,


      $.command_substitution,
    ),

    _param_literal: $ => choice(
      $.word,
      $.number,
      $.expansion,
      $.variable_ref,

      $.string,
      $.raw_string,
      $.ansi_c_string,
      $.translated_string,
      // Explicitly exclude glob_pattern and test_operator
    ),

    _param_array: $ => seq(
      "(",
      repeat($._param_literal),
      ")",
    ),

    // Parameter-specific primary expressions
    _param_primary_expression: $ => choice(
      alias($._expansion_word, $.word),
      $.string,
      $.raw_string,
      $.expansion,
      $.variable_ref,
      $.command_substitution,
      $.arithmetic_expansion,
      // Note: NOT $.glob_pattern here to avoid the ambiguities
    ),

  _param_pattern: $ => seq(
      optional(choice(token.immediate('%'),
                      token.immediate('#'))),
      choice(
        alias($._literal_dollar, $.word),   // ${x//$} ${x%$}: a literal '$'
        $.dollar_variable,        // $var - allows ${foo/$pattern/repl}
        $.regex,
        $.string,
        $.raw_string,
        alias(token.immediate(/\s+/), $.word),   // bare whitespace pattern e.g. ${VAR# }

        $.glob_pattern,
        $.expansion,              // ${nested} - allows ${foo/${bar}/baz}
        $.command_substitution,   // $(cmd) - allows ${foo/$(pattern)/repl}
        $.arithmetic_expansion,   // $((expr)) - allows ${foo/$((n))/repl}
        $.ansi_c_string,          // $'...' - allows ${foo%%$'\n'}

        // FIXME
        // seq($.string, $.regex),
        // seq($.regex, $.string),  // FIXME: expand
        alias($._expansion_word, $.word),
        alias($._concatenation_in_expansion_regex, $.concatenation),
    )),

    _param_pattern_no_slash: $ => seq(
      optional(choice(token.immediate('%'),
                      token.immediate('#'))),
      choice(
        alias($._literal_dollar, $.word),   // ${x//$} ${x%$}: a literal '$'
        alias($._regex_no_slash, $.regex),
        alias(token.immediate(/\s+/), $.word),
        alias($._expansion_word, $.word), // FIXME? YES / NO
        $.string,
        $.raw_string,

        $.glob_pattern,
        $.expansion,              // ${nested} - allows ${foo/${bar}/baz}
        $.dollar_variable,        // $var - allows ${foo/$pattern/repl}
        $.command_substitution,   // $(cmd) - allows ${foo/$(pattern)/repl}
        $.arithmetic_expansion,   // $((expr)) - allows ${foo/$((n))/repl}
        $.ansi_c_string,          // $'...' - allows ${foo%%$'\n'}

        // FIXME
        // seq($.string, alias($._regex_no_slash, $.regex)),
        // seq(alias($._regex_no_slash, $.regex), $.string),  // FIXME: expand
        alias($._concatenation_in_expansion_regex_no_slash, $.concatenation),
    )),

    _param_replacement: $ => choice(
      alias(token.immediate(/\s+/), $.word),   // ${u:gs/+/ /}: a blank replacement
      alias($._default_word, $.word),   // ${a//x/(* }: text starting with ( ) < > | ; &
      alias($._literal_dollar, $.word),   // ${x//a/$}: a literal '$'
      alias(prec(-2, repeat1($._special_character)), $.word),
      alias($._regex_no_slash, $.regex),
      alias($._expansion_word, $.word),
      $.string, 
      $.raw_string,
      $.ansi_c_string,          // ${x//$'\n'/$'\t'}
      $.expansion,              // ${nested} - allows ${foo/bar/${baz}}
      $.variable_ref,           // $var - allows ${foo/bar/$repl/}
      $.command_substitution,   // $(cmd) - allows ${foo/bar/$(repl)}
      $.arithmetic_expansion,   // $((expr)) - allows ${foo/a/$((n))}
      alias($._concatenation_in_expansion_regex_replacement, $.concatenation),
      $.array,
    ),
  },
});

/**
 * Returns a regular expression that matches any character except the ones
 * provided.
 *
 * @param  {...string} characters
 *
 * @returns {RegExp}
 */
function noneOf(...characters) {
  // const negatedString = characters.map(c => c == '\\' ? '\\\\' : c).join('');
  const negatedString = characters.map(c => {
    if (c === '\\') return '\\\\';
    if (c === '[') return '\\[';  // Escape [ in character class
    if (c === ']') return '\\]';  // Escape ] in character class
    if (c === '^') return '\\^';  // Escape ^ if it's first
    if (c === '-') return '\\-';  // Escape - to avoid ranges
    return c;
  }).join('');
  return new RegExp('[^' + negatedString + ']');
}

/**
 * Creates a rule to optionally match one or more of the rules separated by a comma
 *
 * @param {RuleOrLiteral} rule
 *
 * @returns {ChoiceRule}
 */
function commaSep(rule) {
  return optional(commaSep1(rule));
}

/**
 * Creates a rule to match one or more of the rules separated by a comma
 *
 * @param {RuleOrLiteral} rule
 *
 * @returns {SeqRule}
 */
function commaSep1(rule) {
  return seq(rule, repeat(seq(',', rule)));
}

/**
 *
 * Turns a list of rules into a choice of immediate rule
 *
 * @param {(RegExp | string)[]} literals
 *
 * @returns {ChoiceRule}
 */
function immediateLiterals(...literals) {
  return choice(...literals.map(l => token.immediate(l)));
}

/**
 *
 * Turns a list of rules into a choice of aliased token rules
 *
 * @param {number} precedence
 *
 * @param {(RegExp | string)[]} literals
 *
 * @returns {ChoiceRule}
 */
function tokenLiterals(precedence, ...literals) {
  return choice(...literals.map(l => token(prec(precedence, l))));
}


