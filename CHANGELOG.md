# Changelog

## 2026-07-18

### Fixed

- Parse ANSI-C strings as parameter-substitution replacement values, including
  expressions such as `query=${query//\\t/$'\t'}`.
- Parse parameter-expansion flags before the length operator, as used by
  `${(m)#clean}`.
- Parse flagged extended glob patterns on the right side of conditional
  comparisons, including `[[ "$location" == (#b)(*):([0-9]##) ]]`.
- Preserve escaped pattern fragments concatenated after ANSI-C strings in
  parameter substitutions, including
  `${lines[i]//$'\e'\[[0-9;]#[a-zA-Z]/}`.

### Highlighting impact

These valid Zsh constructs previously produced Tree-sitter `ERROR` nodes.
One malformed subtree could cover a large remainder of a function, so
highlight queries received incorrect node types and boundaries. This made
otherwise unrelated tokens appear inconsistent: variables, comments,
braces, builtins, strings, operators, and separators could receive captures
intended for neighboring syntax.

The colorscheme and highlight queries were not the root cause. Correcting the
grammar and external scanner restores stable syntax trees, allowing existing
Neovim highlight captures and colorscheme links to apply normally.

### Verification

- Added four corpus regressions based on the affected `.zshrc` constructs.
- Passed all 147 corpus tests with Tree-sitter CLI 0.25.6.
- Parsed the complete affected `.zshrc` with zero `ERROR` or `MISSING`
  nodes.
- Built and loaded the generated parser in Neovim 0.12.2.

Neovim users must rebuild or reinstall the Zsh parser after updating because
an already loaded native parser library cannot be replaced within the running
process.
