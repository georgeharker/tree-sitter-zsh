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
  '\\', //':',
  ' ', '\t', '\n'
];

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

module.exports = grammar({
  name: 'zsh',

  conflicts: $ => [
    [$._expression, $.command_name],
    [$.command, $.variable_assignments],
    [$.redirected_statement, $.command],
    [$.pipeline],
    [$.repeat_statement, $.command],
    [$.binary_expression],
    [$._test_command_binary_expression, $.binary_expression],
  ],

  inline: $ => [
    $._statement,
    $._terminator,
    $._literal,
    $._terminated_statement,
    $._primary_expression,
    $._simple_variable_name,
    $._special_variable_name,
    $._c_word,
    $._statement_not_subshell,
    $._redirect,
    //$.expansion_style
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
    ']]',                              // TEST_COMMAND_END
    'esac',                            // ESAC
    $._zsh_extended_glob_flags,
    '"',
    '`',
    $.__error_recovery,
  ],

  extras: $ => [
    /\s/,
    /\\\r?\n/,  // FIXME
    /\\( |\t|\v|\f)/,
  ],

  supertypes: $ => [
    $._statement,
    $._expression,
    $._primary_expression,
  ],

  word: $ => $.word,

  rules: {
    program: $ => optional($._statements),

    _statements: $ => prec(1, seq(
      repeat($._terminator), // Allow leading terminators/newlines
      repeat(seq(
        $._statement,
        optional($.comment),
        $._terminator,
      )),
      $._statement,
      optional($.comment),
      optional($._terminator),
    )),

    // Zsh glob qualifiers: (.) (/) (*) etc.
    // FIXME: these qualifiers need extending to deal with
    //
    zsh_glob_qualifier: $ => token.immediate(seq(
      '(', optional('#q'),
      /[./*@=p%\-^rwxugoaLkamcFNDMsShHbBfFdcaAtImCYoOnPqUGzZ+\d]+/,
      ')',
    )),

    // Zsh glob modifiers: (:t) (:h) (:r) (:e) etc.  
    zsh_glob_modifier: $ => token.immediate(seq(
      '(',
      ':',
      /[threuoOnPAlUsSqQxXfFlW]/,
      optional(/[^)]*/),
      ')',
    )),

    _terminated_statement: $ => repeat1(seq(
      $._statement,
      $._terminator,
    )),

    // Statements

    _statement: $ => choice(
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
            $.heredoc_redirect,
          )),
        )),
      ),
      seq(
        field('body', choice($.if_statement, $.while_statement)),
        $.herestring_redirect,
      ),
      field('redirect', repeat1($._redirect)),
      $.herestring_redirect,
    ))),

    for_statement: $ => seq(
      'for',
      field('variable', $._simple_variable_name),
      optional(seq(
        'in',
        field('value', repeat1($._literal)),
      )),
      $._terminator,
      field('body', $.do_group),
    ),
    
    terse_for_statement: $ => seq(
      'for',
      field('variable', $._simple_variable_name),
      '(',
      $._literal,
      ')',
      $._terminator,
      field('body', $.do_group),
    ),

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
      $._c_terminator,
      field('condition', commaSep($._c_expression)),
      $._c_terminator,
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
    _c_binary_expression: $ => {
      const table = [
        [choice('+=', '-=', '*=', '/=', '%=', '**=', '<<=', '>>=', '&=', '^=', '|='), PREC.UPDATE],
        [choice('||', '-o'), PREC.LOGICAL_OR],
        [choice('&&', '-a'), PREC.LOGICAL_AND],
        ['|', PREC.BITWISE_OR],
        ['^', PREC.BITWISE_XOR],
        ['&', PREC.BITWISE_AND],
        [choice('==', '!='), PREC.EQUALITY],
        [choice('<', '>', '<=', '>='), PREC.COMPARE],
        [choice('<<', '>>'), PREC.SHIFT],
        [choice('+', '-'), PREC.ADD],
        [choice('*', '/', '%'), PREC.MULTIPLY],
        ['**', PREC.EXPONENT],
      ];

      return choice(...table.map(([operator, precedence]) => {
        // @ts-ignore
        return prec[operator === '**' ? 'right' : 'left'](precedence, seq(
          field('left', $._c_expression_not_assignment),
          // @ts-ignore
          field('operator', operator),
          field('right', $._c_expression_not_assignment),
        ));
      }));
    },
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
      field('condition', $._terminated_statement),
      field('body', $.do_group),
    ),

    repeat_statement: $ => prec.right(seq(
      'repeat',
      field('count', choice($.number, $.word, $._simple_variable_name, $.expansion)),
      choice(
        seq($._terminator, field('body', $.do_group)),
        $._statement,
      ),
    )),

    select_statement: $ => seq(
      'select',
      field('variable', $.variable_name),
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

    if_statement: $ => seq(
      'if',
      field('condition', $._terminated_statement),
      'then',
      optional($._terminated_statement),
      repeat($.elif_clause),
      optional($.else_clause),
      'fi',
    ),

    elif_clause: $ => seq(
      'elif',
      $._terminated_statement,
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
      optional($._statements),
      prec(1, choice(
        field('termination', ';;'),
        field('fallthrough', choice(';&', ';;&')),
      )),
    ),

    last_case_item: $ => seq(
      optional('('),
      field('value', choice($._literal, $._extglob_blob)),
      repeat(seq('|', field('value', choice($._literal, $._extglob_blob)))),
      ')',
      optional($._statements),
      optional(prec(1, ';;')),
    ),

    function_definition: $ => prec(4, prec.right(seq(
      choice(
        seq(
          'function',
          field('name', optional($.word)),
           optional(seq(
             '(', ')')),
        ),
        seq(
          field('name', optional($.word)),
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

    compound_statement: $ => prec.right(seq(
      //'{',
      alias($._brace_start, '{'),
      optional($._terminated_statement),
      token(prec(-1, '}')),
      optional($.always_clause),
    )),

    // Always clause that can attach to compound statements
    always_clause: $ => seq(
      'always',
      field('always', choice(
        $.compound_statement,
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
          choice(
            alias($._test_command_binary_expression, $.binary_expression),
            $._expression,
          ),
          ']]',
        ),
        seq(
          '((', 
          optional($._expression), 
          '))'
        ),
      )),
    ),
    
    _test_command_binary_expression: $ => prec(PREC.COMPARE,
      choice(
        // Regex matching operator
        prec(PREC.COMPARE, seq(
          field('left', $._expression),
          field('operator', '=~'),
          //field('right', alias($._regex_no_space, $.regex)),
          field('right', choice(
            alias($._regex_no_space, $.regex),
            $._expression
          )),
        )),
        // Pattern/string matching operators  
        prec(PREC.COMPARE - 1, seq(
          field('left', $._expression),
          field('operator', choice('=', '==', '!=', $.test_operator)),
          field('right', $._expression),
        )),
      ),
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
      repeat(choice(
        $.variable_assignment,
        field('redirect', $._redirect),
      )),
      field('name', $.command_name),
      choice(
        repeat(
          choice(
            field('argument', $._literal),
            field('argument', $._bare_dollar),
            field('argument', $.glob_pattern),
            field('argument', seq(
              choice('=~', '=='),
              choice($._literal, $.regex),
            ),
            field('redirect', $.herestring_redirect),
          )
        )),
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

    variable_assignments: $ => seq($.variable_assignment, repeat1($.variable_assignment)),

    subscript: $ => prec.left(seq(
      field('name', $.variable_name),
      '[',
      optional(field('flags', $.zsh_array_subscript_flags)),
      field('index', choice($._param_arithmetic_expression, $.array_star, $.array_at)),
      ']',
    )),

    // Arithmetic expressions for parameter subscripts (avoids subscript recursion)
    _param_arithmetic_expression: $ => prec(1, choice(
      $._param_arithmetic_literal,
      alias($._param_arithmetic_unary_expression, $.unary_expression),
      alias($._param_arithmetic_ternary_expression, $.ternary_expression),
      alias($._param_arithmetic_binary_expression, $.binary_expression),
      alias($._param_arithmetic_postfix_expression, $.postfix_expression),
      alias($._param_arithmetic_parenthesized_expression, $.parenthesized_expression),
      $.command_substitution,
    )),

    _param_arithmetic_literal: $ => prec(1, choice(
      $.number,
      // Note: no $.subscript to avoid recursion
      $.variable_ref,
      $.expansion,
      $.variable_name,
      $.string,
      $.word,  // Allow bare identifiers in parameter arithmetic context
    )),

    _param_arithmetic_binary_expression: $ => {
      const table = [
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
      ];

      return choice(...table.map(([operator, precedence]) => {
        // @ts-ignore
        return prec.left(precedence, seq(
          field('left', $._param_arithmetic_expression),
          // @ts-ignore
          field('operator', operator),
          field('right', $._param_arithmetic_expression),
        ));
      }));
    },

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

    // Zsh array subscript flags: (i) (I) (r) (R) etc.
    // FIXME: delimeter is forced as :, could be any
    zsh_array_subscript_flags: $ => seq(
      '(',
      choice(
        /[wpfrRiIkKe]+/,
        /s:[^:]+:/,
        seq('n:', $._arithmetic_expression,':'),
        seq('b:', $._arithmetic_expression,':'),
      ),
      ')',
    ),

    file_redirect: $ => prec.left(seq(
      field('descriptor', optional($.file_descriptor)),
      choice(
        seq(
          choice('<', '>', '>>', '&>', '&>>', '<&', '>&', '>|'),
          field('destination', repeat1($._literal)),
        ),
        seq(
          choice('<&-', '>&-'), // close file descriptor
          optional(field('destination', $._literal)),
        ),
      ),
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

    herestring_redirect: $ => prec.left(seq(
      field('descriptor', optional($.file_descriptor)),
      '<<<',
      $._literal,
    )),

    _redirect: $ => choice($.file_redirect, $.herestring_redirect),

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
    binary_expression: $ => {
      const table = [
        [choice('+=', '-=', '*=', '/=', '%=', '**=', '<<=', '>>=', '&=', '^=', '|='), PREC.UPDATE],
        [choice('=', '=~'), PREC.ASSIGN],
        ['||', PREC.LOGICAL_OR],
        ['&&', PREC.LOGICAL_AND],
        ['|', PREC.BITWISE_OR],
        ['^', PREC.BITWISE_XOR],
        ['&', PREC.BITWISE_AND],
        [choice('==', '!='), PREC.EQUALITY],
        [choice('<', '>', '<=', '>='), PREC.COMPARE],
        [$.test_operator, PREC.TEST],
        [choice('<<', '>>'), PREC.SHIFT],
        [choice('+', '-'), PREC.ADD],
        [choice('*', '/', '%'), PREC.MULTIPLY],
        ['**', PREC.EXPONENT],
      ];

      return choice(
        choice(...table.map(([operator, precedence]) => {
          // @ts-ignore
          return prec[operator === '**' ? 'right' : 'left'](precedence, seq(
            field('left', $._expression),
            // @ts-ignore
            field('operator', operator),
            field('right', $._expression),
          ));
        })),
        prec(PREC.ASSIGN, seq(
          field('left', $._expression),
          field('operator', '=~'),
          field('right', choice(alias($._regex_no_space, $.regex), $._expression)),
        )),
        prec(PREC.EQUALITY, seq(
          field('left', $._expression),
          field('operator', choice('==', '!=')),
          field('right', $._extglob_blob),
        )),
      );
    },

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

    // Literals

    _literal: $ => choice(
      $._primary_expression,
      $.concatenation,
      alias(prec(-2, repeat1($._special_character)), $.word),
    ),

    _primary_expression: $ => choice(
      $.qualified_expression,    // qualified globs should be first
      $.glob_pattern,
      $.word,
      alias($.test_operator, $.word),
      $.string,
      $.raw_string,
      $.translated_string,
      $.ansi_c_string,
      $.number,
      $._expansion_or_variable,
      $.command_substitution,
      $.process_substitution,
      $.arithmetic_expansion,
      $.brace_expression,
    ),
    
    // Unified rule for all dollar-based patterns to eliminate competition  
    _expansion_or_variable: $ => choice(
      $.expansion,      // Try ${...} patterns first
      $.variable_ref,   // Fall back to $var patterns
    ),

    arithmetic_expansion: $ => prec(4, choice(
      seq(
        choice(
          seq($._bare_dollar, '(('),
          '(('
        ),
        commaSep1($._arithmetic_expression),
        '))'
      ),
      seq(
        $._bare_dollar, 
        '[', 
        $._arithmetic_expression,
        ']'),
      )
    ),

    brace_expression: $ => seq(
      alias($._brace_expr_start, '{'),
      alias(token.immediate(/\d+/), $.number),
      token.immediate('..'),
      alias(token.immediate(/\d+/), $.number),
      token.immediate('}'),
    ),

    _glob_innards: $ => token(seq(
      repeat(/[^\s'"*?\[{~(<=]/),
      choice(
        /\*\*/,                                    // **
        /\*/,                                      // *
        /\?/,                                      // ?
        /\[[!^]?[^\]]*\]/,                        // [...]
        /<[0-9]+-[0-9]+>/,                        // <1-10>
        /\{[^}]*,[^}]*\}/,                        // {a,b}
        /~/,                                       // ~
        /\([^)|]+(\|[^)|]*)+\)/                   // (a|b|c)
      ),
      repeat(choice(
        /[^\s'"*?\[{~(<=]/,                         // regular chars
        /\*\*/,                                    // **
        /\*/,                                      // *
        /\?/,                                      // ?
        /\[[!^]?[^\]]*\]/,                        // [...]
        /<[0-9]+-[0-9]+>/,                        // <1-10>
        /\{[^}]+,[^}]*\}/,                        // {a,b}
        /\([^)|]+(\|[^)|]*)+\)/                   // (a|b|c)
      ))
    )),

    qualified_expression: $ => seq(
      field('expression', choice(
        $.expansion,              // ${...} - can produce arrays
        $.command_substitution,   // $(...) - can produce multiple lines  
        $.variable_ref,          // $array - if it's an array variable
        // Could also include arithmetic that produces arrays
      )),
      field('qualifier', $.zsh_glob_qualifier),  // (ND), (.), etc.
    ),

    // Unified glob pattern that handles **, *, ?, [] patterns with optional zsh qualifiers/modifiers
    glob_pattern: $ => prec.dynamic(-1, choice(
      // True glob patterns with wildcards
      seq(
        optional(field('flags', $.zsh_extended_glob_flags)),
        field('pattern', $._glob_innards),
        optional(choice(
          field('qualifier', $.zsh_glob_qualifier),
          field('modifier', $.zsh_glob_modifier),
          seq(
            field('qualifier', $.zsh_glob_qualifier),
            field('modifier', $.zsh_glob_modifier),
          ),
        )),
      ),
      // Words with qualifiers/modifiers (like /path/file(:h))
      prec(1, seq(
        optional(field('flags', $.zsh_extended_glob_flags)),
        field('pattern', $.word),
        choice(
          field('qualifier', $.zsh_glob_qualifier),
          field('modifier', $.zsh_glob_modifier),
          seq(
            field('qualifier', $.zsh_glob_qualifier),
            field('modifier', $.zsh_glob_modifier),
          ),
        ),
      )),
    )),
    


    // Zsh extended glob flags: (#i) (#q) (#a2) etc.
    zsh_extended_glob_flags: $ => $._zsh_extended_glob_flags,

    _arithmetic_expression: $ => prec(1, choice(
      $._arithmetic_literal,
      alias($._arithmetic_unary_expression, $.unary_expression),
      alias($._arithmetic_ternary_expression, $.ternary_expression),
      alias($._arithmetic_binary_expression, $.binary_expression),
      alias($._arithmetic_postfix_expression, $.postfix_expression),
      alias($._arithmetic_parenthesized_expression, $.parenthesized_expression),
      $.command_substitution,
    )),

    _arithmetic_literal: $ => prec(1, choice(
      $.number,
      $.subscript,
      $.variable_ref,
      $.expansion,
      $.variable_name,
      $.string,
      $.word,  // Allow bare identifiers in arithmetic context (e.g., 'start' in $((start)))
    )),

    _arithmetic_binary_expression: $ => {
      const table = [
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
      ];

      return choice(...table.map(([operator, precedence]) => {
        // @ts-ignore
        return prec.left(precedence, seq(
          field('left', $._arithmetic_expression),
          // @ts-ignore
          field('operator', operator),
          field('right', $._arithmetic_expression),
        ));
      }));
    },

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

    _arithmetic_parenthesized_expression: $ => seq(
      '(',
      $._arithmetic_expression,
      ')',
    ),


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

    _special_character: _ => token(prec(-1, choice('{', '}', '[', ']'))),

    string: $ => seq(
      '"',
      repeat(seq(
        choice(
          seq(optional($._bare_dollar), $.string_content),
          $.expansion,
          $.variable_ref,
          $.command_substitution,
          $.arithmetic_expansion,
        ),
        optional($._concat),
      )),
      optional($._raw_dollar),
      '"',
    ),

    string_content: _ => token(prec(-1, /([^"`$\\\r\n]|\\(.|\r?\n))+/)),

    translated_string: $ => prec(1, seq($._bare_dollar, $.string)),

    array: $ => seq(
      '(',
      repeat($._literal),
      ')',
    ),

    raw_string: _ => /'[^']*'/,

    ansi_c_string: _ => /\$'([^']|\\')*'/,

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
      $._bare_dollar,
      $._variable_ref,
    )),

    _variable_ref: $ => prec.right(40, seq(
      choice(
        $._special_variable_name,
        $._simple_variable_name,
        $.subscript,

        // alias('!', $.special_variable_name),
        // alias('#', $.special_variable_name),
      ),
      optional(field('modifier', $.expansion_modifier))
    )),

    dollar_variable: $ => prec(1, seq(
      $._bare_dollar,
      choice(
        $._simple_variable_name,
        $._special_variable_name,
      ),
    )),

    // Variable references within expansion contexts (similar to _variable_ref)
    _expansion_variable_ref: $ => seq(
      choice(
        $._simple_variable_name,
        $._special_variable_name,
      ),
      optional(seq(  // Postfix subscript operator (left-associating)
        '[',
        optional(field('flags', $.zsh_array_subscript_flags)),
        field('index', choice($._param_arithmetic_expression, $.array_star, $.array_at)),
        ']'
      ))
    ),

    string_expansion: $ => seq($._bare_dollar, $.string),

    // Visible rule for parameter expansion substring operations
    expansion_substring: $ => $._expansion_max_length,
    
    // Visible rule for parameter expansion defaults
    expansion_default: $ => seq(
      optional(field('name', choice($._expansion_variable_ref, $.expansion))),
      choice(
        seq(token.immediate('-'), field('default', $.word)),
        seq(token.immediate(':'), token.immediate('-'), field('default', $.word)),
        seq(token.immediate('+'), field('default', $.word)),
        seq(token.immediate(':'), token.immediate('+'), field('default', $.word)),
        seq(token.immediate('='), field('default', $.word)),
        seq(token.immediate(':'), token.immediate('='), field('default', $.word)),
        seq(token.immediate('='), field('default', $.word)),
        seq(token.immediate(':'), token.immediate(':'), token.immediate('='), field('default', $.word)),
        seq(token.immediate('?'), field('default', $.word)),
        seq(token.immediate(':'), token.immediate('?'), field('default', $.word)),
      )
    ),

    // Visible rule for parameter expansion patterns 
    expansion_pattern: $ => seq(
      field('name', choice($._expansion_variable_ref, $.expansion)),
      choice(
        seq($._hash_pattern, $._pattern_suffix_start, field('pattern', $._param_pattern)),
        seq($._double_hash_pattern, $._pattern_suffix_start, field('pattern', $._param_pattern)),
        seq(token.immediate('%'), $._pattern_suffix_start, field('pattern', $._param_pattern)),
        seq(token.immediate('%%'), $._pattern_suffix_start, field('pattern', $._param_pattern)),
        seq(token.immediate(':'), $._hash_pattern, $._pattern_suffix_start, field('pattern', $._param_pattern)),
        seq(token.immediate(':'), $._double_hash_pattern, $._pattern_suffix_start, field('pattern', $._param_pattern)),
        seq(token.immediate(':'), token.immediate('%'), $._pattern_suffix_start, field('pattern', $._param_pattern)),
        seq(token.immediate(':'), token.immediate('%%'), $._pattern_suffix_start, field('pattern', $._param_pattern)),
        seq(token.immediate('//'), 
            $._pattern_start, 
            field('pattern', $._param_pattern_no_slash),
            '/',
            field('replacement', $._param_replacement)
        ),
        seq(token.immediate('/'), 
            $._pattern_start, 
            field('pattern', $._param_pattern_no_slash),
            '/',
            field('replacement', $._param_replacement),
        ),
        seq(token.immediate(':'), token.immediate('/'), 
            $._pattern_start,
            field('pattern', $._param_pattern_no_slash),
            '/',
            field('replacement', $._param_replacement)),
      )
    ),

    // Visible rule for parameter expansion arrays
    expansion_array: $ => seq(
      field('name', choice($._expansion_variable_ref, $.expansion)),
      choice(
        seq(token.immediate(':'), token.immediate('|'), field('array', $._literal)),
        seq(token.immediate(':'), token.immediate('*'), field('array', $._literal)),
        seq(token.immediate(':'), token.immediate('^'), field('array', $._literal)),
        seq(token.immediate(':'), token.immediate('^'), token.immediate('^'), field('array', $._literal)),
      )
    ),

    expansion_modifier: $ => prec.right(4, seq(
      token.immediate(':'),
      choice(
        token(/[aAce]/),
        token(/h[0-9]*/),
        token(/[lpPqQr]/),
        seq(token.immediate('s'), token.immediate('/'), 
            $._pattern_start, 
            field('search', $._param_pattern_no_slash),
            token.immediate('/'), field('replace', $._param_replacement),
            optional(seq(
                token.immediate('/'), optional(
                  seq(token.immediate(':'), /[Gg]/)
               )
            )
          )
        ),
        token(/[&ux]/),
        token(/t[0-9]*/))
    )),
    
    // Maintain same order as zsh docs
    expansion_with_modifier: $ => seq(
      optional(field('name', choice($._expansion_variable_ref, $.expansion))),
      field('modifier', $.expansion_modifier)
    ),

    expansion_style: $ => choice(
      token.immediate('#'),  // ${# var} - length
      token.immediate('!'),  // ${! var} - indirect expansion
      // Order longer sequences first to resolve conflicts
      prec(2, token.immediate('^^')),  // ${^^ var} - RC_EXPAND_PARAM
      prec(1, token.immediate('^')),  // ${^ var} - RC_EXPAND_PARAM
      prec(2, token.immediate('==')),  // ${== var} - SH_WORD_SPLIT
      prec(1, token.immediate('=')),  // ${= var} - SH_WORD_SPLIT
      prec(2, token.immediate('~~')),  // ${~~ var} - GLOB_SUBST
      prec(1, token.immediate('~')),  // ${~ var} - GLOB_SUBST
    ),

    expansion: $ => seq(
      prec(2, seq(
        $._bare_dollar,
        alias($._brace_start, '{')
      )),
      choice(
        prec.right(10, seq(field('style', $.expansion_style), 
             field('flags', $.expansion_flags), $._expansion_body)),
        prec.right(10, seq(field('style', $.expansion_style), $._expansion_body)),
        prec.left(10, seq(field('flags', $.expansion_flags), $._expansion_body)),
        prec.left($._expansion_body),
      ),
      '}',
    ),

    // Zsh parameter expansion flags: ${(L)var}, ${(j:,:)array}, etc.
    expansion_flags: $ => token.immediate(/\([^)]+\)/),

    _expansion_body: $ => choice(
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
    ),

    // Base expressions (recursive)
    _expansion_pattern: $ => choice(
      $.regex,
      $.word,
      $.string,
      $.raw_string,
    ),
    
    // Replacement value for substitutions
    _expansion_replacement: $ => choice(
      $.word,
      $.string,
      $.raw_string,
      $.expansion,
      $.variable_ref,
      $.command_substitution,
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

    _expansion_max_length: $ => seq(
      field('name', choice($._expansion_variable_ref, $.expansion)),
      token.immediate(':'),
      field('offset', choice(
        $.variable_ref,
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
          $.variable_ref,
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
      $.variable_ref,
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

    _expansion_operator: $ => seq(
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
   
  _concatenation_in_expansion_regex_replacement: $ => prec(-2, seq(
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

    command_substitution: $ => prec(1, choice(
      seq($._bare_dollar, '(', $._statements, ')'),
      seq($._bare_dollar, '(', field('redirect', $.file_redirect), ')'),
      prec(1, seq('`', $._statements, '`')),
      //seq('$`', $._statements, '`'), // not legal zsh
    )),

    process_substitution: $ => seq(
      choice('<(', '>(',
        prec(-1, '=(')),  // Lower precedence to allow variable assignment to win
        $._statements,
      ')',
    ),

    _extglob_blob: $ => choice(
      $.extglob_pattern,
      seq(
        $.extglob_pattern,
        choice($.string, $.expansion, $.command_substitution),
        optional($.extglob_pattern),
      ),
    ),

    comment: $ => token(prec(-20, /#[^\r\n]*/)),

    _comment_word: _ => token(prec(-8, seq(
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

    _special_variable_name: $ => $.special_variable_name,

    word: $ => token(seq(
      choice(
        noneOf('#', ':', ...SPECIAL_CHARACTERS),
        seq('\\', noneOf('\\s')),
      ),
      repeat(choice(
        noneOf(...SPECIAL_CHARACTERS),
        seq('\\', noneOf('\\s')),
        '\\ ',
      )),
    )),

    _c_terminator: _ => choice(';', /\n/, '&'),
    _terminator: _ => choice(';', ';;', /\n/, '&'),
    

    // Parameter expansion specific rules - no globs allowed
    _param_variable_ref: $ => choice(
      $._simple_variable_name,
      $._special_variable_name,
      $.subscript,
      $.expansion,  // nested expansions
    ),

    // Parameter-safe expression system (excludes glob_pattern)
    _param_expression: $ => choice(
      $._param_literal,
      $._param_unary_expression,
      $._param_ternary_expression,
      $._param_binary_expression,
      $._param_postfix_expression,
      $._param_parenthesized_expression,
    ),

    _param_binary_expression: $ => {
      const table = [
        [choice('+=', '-=', '*=', '/=', '%=', '**=', '<<=', '>>=', '&=', '^=', '|='), PREC.UPDATE],
        ['||', PREC.LOGICAL_OR],
        ['&&', PREC.LOGICAL_AND],
        ['|', PREC.BITWISE_OR],
        ['^', PREC.BITWISE_XOR],
        ['&', PREC.BITWISE_AND],
        [choice('==', '!='), PREC.EQUALITY],
        [choice('<', '>', '<=', '>='), PREC.COMPARE],
        [$.test_operator, PREC.TEST],
        [choice('<<', '>>'), PREC.SHIFT],
        [choice('+', '-'), PREC.ADD],
        [choice('*', '/', '%'), PREC.MULTIPLY],
        ['**', PREC.EXPONENT],
      ];

      return choice(
        choice(...table.map(([operator, precedence]) => {
          // @ts-ignore
          return prec[operator === '**' ? 'right' : 'left'](precedence, seq(
            field('left', $._param_expression),
            // @ts-ignore
            field('operator', operator),
            field('right', $._param_expression),
          ));
        })),
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
      );
    },

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

      _param_concatenation: $ => prec(-1, seq(
        choice(
          $._param_primary_expression,
        ),
        repeat1(seq(
          $._concat,  // Same concat token
          choice(
            $._param_primary_expression,
          ),
        ))
      )),

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
        $.dollar_variable,        // $var - allows ${foo/$pattern/repl}
        $.regex,
        $.string,
        $.raw_string,

        $.glob_pattern,
        $.expansion,              // ${nested} - allows ${foo/${bar}/baz}
        $.command_substitution,   // $(cmd) - allows ${foo/$(pattern)/repl}
        $.arithmetic_expansion,   // $((expr)) - allows ${foo/$((n))/repl}
        
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
        alias($._expansion_word, $.word), // FIXME? YES / NO
        alias($._regex_no_slash, $.regex),
        $.string,
        $.raw_string,

        $.glob_pattern,
        $.expansion,              // ${nested} - allows ${foo/${bar}/baz}
        $.dollar_variable,        // $var - allows ${foo/$pattern/repl}
        $.command_substitution,   // $(cmd) - allows ${foo/$(pattern)/repl}
        $.arithmetic_expansion,   // $((expr)) - allows ${foo/$((n))/repl}
        
        // FIXME
        // seq($.string, alias($._regex_no_slash, $.regex)),
        // seq(alias($._regex_no_slash, $.regex), $.string),  // FIXME: expand
        alias($._concatenation_in_expansion_regex_no_slash, $.concatenation),
    )),

    _param_replacement: $ => choice(
      alias(prec(-2, repeat1($._special_character)), $._expansion_word),
      alias($._regex_no_slash, $.regex),
      alias($._expansion_word, $.word),
      $.string, 
      $.raw_string,
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


