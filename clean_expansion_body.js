    _expansion_body: $ => choice(
      // Basic variable reference: ${var}, ${var[index]}
      $._param_variable_ref,
      
      // Pattern operations: ${var##pattern} INCLUDING ${var[0]##pattern}
      seq(
        field('name', $._param_variable_ref),  // This includes subscript support
        field('operator', choice('#', alias($._immediate_double_hash, '##'), '%', '%%')),
        field('pattern', $._param_pattern)
      ),
      
      // Assignment operations: ${var=value}
      seq(
        field('name', $._param_variable_ref),
        field('operator', choice('=', ':=', '-', ':-', '+', ':+', '?', ':?')),
        field('value', $._param_assignment_value)
      ),
      
      // Substring operations: ${var:offset:length}
      seq(
        field('name', $._param_variable_ref),
        field('operator', token.immediate(':')),
        field('offset', $._param_numeric_expression),
        optional(seq(token.immediate(':'), field('length', $._param_numeric_expression)))
      ),
      
      // Pattern substitution: ${var/pattern/replacement}
      seq(
        field('name', $._param_variable_ref),
        field('operator', choice('/', '//', '/#', '/%')),
        field('pattern', $._param_pattern),
        optional(seq('/', field('replacement', $._param_replacement)))
      ),
      
      // Prefix operators: ${!var}, ${#var}
      seq(
        field('operator', choice(
          token.immediate('!'),  // ${!var} - indirect expansion
          token.immediate('#')   // ${#var} - length
        )),
        field('name', $._param_variable_ref)
      ),
      
      // Flags with variable: ${(L)var}
      seq(
        field('flags', $.expansion_flags),
        field('name', $._param_variable_ref)
      ),
    ),