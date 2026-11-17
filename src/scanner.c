#include "tree_sitter/array.h"
#include "tree_sitter/parser.h"

#include <assert.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wctype.h>

#define DEBUG 0

enum TokenType {
    HEREDOC_START,
    SIMPLE_HEREDOC_BODY,
    HEREDOC_BODY_BEGINNING,
    HEREDOC_CONTENT,
    HEREDOC_END,
    FILE_DESCRIPTOR,
    EMPTY_VALUE,
    CONCAT,
    CONCAT_REGEX,
    VARIABLE_NAME,
    SIMPLE_VARIABLE_NAME,
    SPECIAL_VARIABLE_NAME,
    TEST_OPERATOR,
    REGEX,
    REGEX_NO_SLASH,
    REGEX_NO_SPACE,
    EXPANSION_WORD,
    EXTGLOB_PATTERN,
    RAW_DOLLAR,       // Consumes spaces, only if $ alone for strings / commands
    BARE_DOLLAR,      // Consumes spaces
    PEEK_BARE_DOLLAR, // Just determines if immediate $ is present
    BRACE_START,
    BRACE_EXPR_START,
    IMMEDIATE_DOUBLE_HASH,
    ARRAY_STAR_TOKEN,
    ARRAY_AT_TOKEN,
    CLOSING_BRACE,
    CLOSING_BRACKET,
    CLOSING_PAREN,
    CLOSING_DOUBLE_PAREN,
    HEREDOC_ARROW,
    HEREDOC_ARROW_DASH,
    HERESTRING,
    HASH_PATTERN,         // #pattern
    DOUBLE_HASH_PATTERN,  // ##pattern
    ENTER_PATTERN,        // implicit / etc
    PATTERN_START,        // After pattern operators, before pattern content
    PATTERN_SUFFIX_START, // After # operators, before pattern content
    NEWLINE,
    OPENING_PAREN,
    DOUBLE_OPENING_PAREN,
    OPENING_BRACKET,
    TEST_COMMAND_START,  // [[
    ESCAPED_OPEN_PAREN,  // \(
    ESCAPED_CLOSE_PAREN, // \)
    TEST_COMMAND_END,    // ]]
    ESAC,
    ZSH_EXTENDED_GLOB_FLAGS,
    DOUBLE_QUOTE,
    SINGLE_QUOTE,
    BACKTICK,
    SUBSCRIPT_FLAGS,         // (w) (s:,:) (n:2:) - arithmetic index follows
    SUBSCRIPT_PATTERN_FLAGS, // (i) (Ie) (r) ... - pattern index follows
    SUBSCRIPT_PATTERN_WORD,  // plain piece of a pattern index: -- pattern
    SUBSCRIPT_PATTERN_GLOB,  // glob piece of a pattern index: foo_* [bf]*
    DEFAULT_WORD,            // ${X:-(none)} ${X:-<unset>}: literal ( < > ...
    TEST_PATTERN_WORD,       // [[ $x = -- ]]: plain pattern piece
    TEST_PATTERN_GLOB,       // [[ $x = (a|b)* ]]: glob pattern piece
    ARITHMETIC_WORD,         // identifier inside (( )) / $(( ))
    ARITHMETIC_CHAR_CODE,    // ##a / #name inside (( ))
    LITERAL_DOLLAR,          // a '$' that starts no expansion: "%B$%b" echo $;
    SUBST_END,               // zero-width end of $x:s/a/b with no closing '/'
    FUNCTION_NAME,  // zinit::setup() :helper(): a name read whole before ()
    MODIFIER_OTHER, // :fs#a#b# :F:2:s/a/b/ :s#a#b#: a modifier read whole
    BRACE_ELSE,     // `} else` of a short if, one token
    BRACE_ELIF,     // `} elif` of a short if, one token
    ERROR_RECOVERY,
};

#if DEBUG
const char *TokenNames[] = {
    "HEREDOC_START",
    "SIMPLE_HEREDOC_BODY",
    "HEREDOC_BODY_BEGINNING",
    "HEREDOC_CONTENT",
    "HEREDOC_END",
    "FILE_DESCRIPTOR",
    "EMPTY_VALUE",
    "CONCAT",
    "CONCAT_REGEX",
    "VARIABLE_NAME",
    "SIMPLE_VARIABLE_NAME",
    "SPECIAL_VARIABLE_NAME",
    "TEST_OPERATOR",
    "REGEX",
    "REGEX_NO_SLASH",
    "REGEX_NO_SPACE",
    "EXPANSION_WORD",
    "EXTGLOB_PATTERN",
    "RAW_DOLLAR",
    "BARE_DOLLAR",
    "PEEK_BARE_DOLLAR",
    "BRACE_START",
    "BRACE_EXPR_START",
    "IMMEDIATE_DOUBLE_HASH",
    "ARRAY_STAR_TOKEN",
    "ARRAY_AT_TOKEN",
    "CLOSING_BRACE",
    "CLOSING_BRACKET",
    "CLOSING_PAREN",
    "CLOSING_DOUBLE_PAREN",
    "HEREDOC_ARROW",
    "HEREDOC_ARROW_DASH",
    "HERESTRING",
    "HASH_PATTERN",        // #pattern
    "DOUBLE_HASH_PATTERN", // ##pattern
    "ENTER_PATTERN",
    "PATTERN_START",
    "PATTERN_SUFFIX_START",
    "NEWLINE",
    "OPENING_PAREN",
    "DOUBLE_OPENING_PAREN",
    "OPENING_BRACKET",
    "TEST_COMMAND_START",
    "ESCAPED_OPEN_PAREN",
    "ESCAPED_CLOSE_PAREN",
    "TEST_COMMAND_END",
    "ESAC",
    "ZSH_EXTENDED_GLOB_FLAGS",
    "DOUBLE_QUOTE",
    "SINGLE_QUOTE",
    "BACKTICK",
    "SUBSCRIPT_FLAGS",
    "SUBSCRIPT_PATTERN_FLAGS",
    "SUBSCRIPT_PATTERN_WORD",
    "SUBSCRIPT_PATTERN_GLOB",
    "DEFAULT_WORD",
    "TEST_PATTERN_WORD",
    "TEST_PATTERN_GLOB",
    "ARITHMETIC_WORD",
    "ARITHMETIC_CHAR_CODE",
    "LITERAL_DOLLAR",
    "SUBST_END",
    "FUNCTION_NAME",
    "MODIFIER_OTHER",
    "BRACE_ELSE",
    "BRACE_ELIF",
    "ERROR_RECOVERY",
};
#endif

typedef Array(char) String;

// Context types for nested expansion tracking
typedef enum {
    CTX_NONE = 0,
    CTX_PARAMETER = 1,       // ${...}
    CTX_ARITHMETIC = 2,      // $((...))
    CTX_COMMAND = 3,         // $(...)
    CTX_TEST = 4,            // [[ ... ]]
    CTX_BRACE_EXPANSION = 5, // {a..b} and {a..b..c}
    CTX_PARAMETER_PATTERN_SUFFIX =
        6, // ${var%pattern} and ${var#pattern} - suffix/prefix removal
    CTX_PARAMETER_PATTERN_SUBSTITUTE =
        7,                   // ${var/pattern/replacement} - substitution
    CTX_STRING = 8,          // "..." string context
    CTX_COMPOUND = 9,        // "{ x; y; z; }"
    CTX_BACKTICK = 10,       // `a b c`
    CTX_RAW_STRING = 11,     // '...' string context
    CTX_SUBSCRIPT = 12,      // a[...] subscript (not an arithmetic expression)
    CTX_MODIFIER_SUBST = 13, // $x:s/a/b/ outside ${...}, until SUBST_END
} context_type_t;

const char *ContextNames[] = {
    "CTX_NONE",
    "CTX_PARAMETER",                    // ${...}
    "CTX_ARITHMETIC",                   // $((...))
    "CTX_COMMAND",                      // $(...)
    "CTX_TEST",                         // [[ ... ]]
    "CTX_BRACE_EXPANSION",              // {a..b} and {a..b..c}
    "CTX_PARAMETER_PATTERN_SUFFIX",     // ${var%pattern} and ${var#pattern} -
                                        // suffix/prefix removal
    "CTX_PARAMETER_PATTERN_SUBSTITUTE", // ${var/pattern/replacement} -
                                        // substitution
    "CTX_STRING",                       // "..." string context
    "CTX_COMPOUND",                     // "{ x; y; z; }"
    "CTX_BACKTICK",                     // `a b c`
    "CTX_RAW_STRING",                   // '...' string context
    "CTX_SUBSCRIPT",
    "CTX_MODIFIER_SUBST",
};

typedef struct {
    bool is_raw;
    bool started;
    bool allows_indent;
    String delimiter;
    String current_leading_word;
} Heredoc;

#define heredoc_new()                                                          \
    {                                                                          \
        .is_raw = false,                                                       \
        .started = false,                                                      \
        .allows_indent = false,                                                \
        .delimiter = array_new(),                                              \
        .current_leading_word = array_new(),                                   \
    };

typedef struct {
    uint8_t last_glob_paren_depth;
    uint8_t test_pattern_depth; // open '(' groups in a [[ x = pattern ]]
    bool after_case_opener;     // just emitted a case item's optional '('
    bool ext_was_in_double_quote;
    bool ext_saw_outside_quote;
    Array(context_type_t) context_stack; // Proper context stack
    bool just_returned_variable_name; // Track if we just returned VARIABLE_NAME
    uint16_t variable_name_col;     // ... and the column where that token ended
    bool just_returned_bare_dollar; // Track if we just returned BARE_DOLLAR
    bool just_exited_string;        // Track if we just exited a string context
    bool just_newline;              // Track if we just handled newline
    bool saw_pipe_in_case; // Track if | was seen in current case arm pattern
    Array(Heredoc) heredocs;
} Scanner;

static inline void advance(TSLexer *lexer) { lexer->advance(lexer, false); }

// Context management functions using proper stack
static inline context_type_t get_current_context(Scanner *scanner) {
    if (scanner->context_stack.size == 0) {
        return CTX_NONE;
    }
    return *array_back(&scanner->context_stack);
}

static inline bool in_parameter_expansion(Scanner *scanner) {
    context_type_t ctx = get_current_context(scanner);
    return ctx == CTX_PARAMETER || ctx == CTX_PARAMETER_PATTERN_SUFFIX ||
           ctx == CTX_PARAMETER_PATTERN_SUBSTITUTE;
}

// Helper to determine if we should stop at pattern operators
static inline bool should_stop_at_pattern_operators(Scanner *scanner) {
    context_type_t ctx = get_current_context(scanner);
    return ctx == CTX_PARAMETER || ctx == CTX_PARAMETER_PATTERN_SUFFIX ||
           ctx == CTX_PARAMETER_PATTERN_SUBSTITUTE;
}

static inline bool should_stop_at_pattern_slash(Scanner *scanner) {
    context_type_t ctx = get_current_context(scanner);
    return ctx == CTX_PARAMETER_PATTERN_SUBSTITUTE;
}

// Helper to check if we're in parameter expansion context (for tokenization
// decisions)
static inline bool in_parameter_expansion_context(Scanner *scanner) {
    return in_parameter_expansion(scanner);
}

// Helper to check if we should break on '/' in EXPANSION_WORD
static inline bool should_break_on_slash(Scanner *scanner) {
    context_type_t ctx = get_current_context(scanner);
    return ctx == CTX_PARAMETER_PATTERN_SUBSTITUTE;
}
static inline void enter_context(Scanner *scanner, context_type_t context) {
#if DEBUG
    fprintf(stderr, "DEBUG: Entering context %s\n", ContextNames[context]);
    for (int i = 0; i < scanner->context_stack.size; ++i) {
        fprintf(stderr, "   DEBUG: context_stack %d= %s\n", i,
                ContextNames[*array_get(&scanner->context_stack, i)]);
    }
#endif
    array_push(&scanner->context_stack, context);
}

#ifdef ZSH_SCANNER_AUDIT
// Audit build only (-DZSH_SCANNER_AUDIT): counts context-stack mismatches
// (see tree_sitter_zsh_audit_report). Used to find leaks; never compiled
// into normal builds.
#define AUDIT_NCTX 16
static unsigned audit_mismatch[AUDIT_NCTX][AUDIT_NCTX]; // [found][wanted]
static unsigned audit_underflow[AUDIT_NCTX];            // exit on empty stack
#endif

static inline void exit_context(Scanner *scanner,
                                context_type_t expected_context) {
#ifdef ZSH_SCANNER_AUDIT
    if (scanner->context_stack.size == 0 && expected_context < AUDIT_NCTX) {
        audit_underflow[expected_context]++;
    }
#endif
    if (scanner->context_stack.size > 0) {
        context_type_t current = *array_back(&scanner->context_stack);
        // Verify we're exiting the expected context (for debugging)
        if (current == expected_context) {
#if DEBUG
            fprintf(stderr, "DEBUG: Exiting matching context %s\n",
                    ContextNames[current]);
#endif
            array_pop(&scanner->context_stack);
        } else {
#if DEBUG
            fprintf(stderr,
                    "DEBUG: Exiting mismatching context %s, wanted %s\n",
                    ContextNames[current], ContextNames[expected_context]);
#endif
            // Gracefully handle mismatched contexts by popping anyway
#ifdef ZSH_SCANNER_AUDIT
            if (current < AUDIT_NCTX && expected_context < AUDIT_NCTX) {
                audit_mismatch[current][expected_context]++;
            }
#endif
            array_pop(&scanner->context_stack);
        }
#if DEBUG
        for (int i = 0; i < scanner->context_stack.size; ++i) {
            fprintf(stderr, "   DEBUG: context_stack %d= %s\n", i,
                    ContextNames[*array_get(&scanner->context_stack, i)]);
        }
#endif
    }
}

// Pop contexts down to and including the innermost `ctx`. If `ctx` isn't on
// the stack, pop just the top (same as exit_context's mismatch handling).
static inline void exit_context_through(Scanner *scanner, context_type_t ctx) {
    bool present = false;
    for (uint32_t i = 0; i < scanner->context_stack.size; i++) {
        if (*array_get(&scanner->context_stack, i) == ctx) {
            present = true;
        }
    }
    if (!present) {
        exit_context(scanner, ctx);
        return;
    }
    while (scanner->context_stack.size > 0) {
        context_type_t top = *array_back(&scanner->context_stack);
        array_pop(&scanner->context_stack);
        if (top == ctx) {
            break;
        }
#ifdef ZSH_SCANNER_AUDIT
        if (top < AUDIT_NCTX && ctx < AUDIT_NCTX) {
            audit_mismatch[top][ctx]++;
        }
#endif
    }
}

// Helper functions for checking contexts
static inline bool in_expansion_context(Scanner *scanner) {
    context_type_t ctx = get_current_context(scanner);
    return ctx == CTX_PARAMETER || ctx == CTX_ARITHMETIC ||
           ctx == CTX_SUBSCRIPT || ctx == CTX_COMMAND;
}

// Skip blanks, and newlines too where a newline is just whitespace (inside an
// array or other list where NEWLINE isn't valid). Openers/closers that start
// a line, as in a multi-line array, otherwise reach only the internal lexer
// (the newline is an extra) and their context push/pop is lost.
static inline void skip_ws_maybe_nl(TSLexer *lexer, const bool *valid_symbols) {
    for (;;) {
        int32_t c = lexer->lookahead;
        if (c == ' ' || c == '\t' || c == '\r') {
            lexer->advance(lexer, true);
        } else if (c == '\n' && !valid_symbols[NEWLINE]) {
            lexer->advance(lexer, true);
        } else {
            break;
        }
    }
}

static inline bool in_pattern_context(Scanner *scanner) {
    context_type_t ctx = get_current_context(scanner);
    return ctx == CTX_PARAMETER_PATTERN_SUFFIX ||
           ctx == CTX_PARAMETER_PATTERN_SUBSTITUTE || ctx == CTX_MODIFIER_SUBST;
}

static inline bool in_test_command(Scanner *scanner) {
    context_type_t ctx = get_current_context(scanner);
    return ctx == CTX_TEST;
}

static inline void skip(TSLexer *lexer) { lexer->advance(lexer, true); }

static inline void skip_ws(TSLexer *lexer) {
    while (iswspace(lexer->lookahead) && lexer->lookahead != '\n' &&
           !lexer->eof(lexer)) {
#if DEBUG
        fprintf(stderr, "WARNING skip_ws skipping space");
#endif
        skip(lexer);
    }
}
static inline void skip_wsnl(TSLexer *lexer) {
    while (iswspace(lexer->lookahead) && !lexer->eof(lexer)) {
#if DEBUG
        fprintf(stderr, "WARNING skip_wsnl skipping space");
#endif
        skip(lexer);
    }
}

// Like skip_wsnl but also skips backslash-newline continuations (\ at end of
// line)
static inline void skip_wsnl_continuation(TSLexer *lexer) {
    for (;;) {
        while (iswspace(lexer->lookahead) && !lexer->eof(lexer)) {
            skip(lexer);
        }
        if (lexer->lookahead == '\\') {
            // Peek: consume \ then check if next char is \n
            skip(lexer);
            if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
                // backslash-newline continuation: skip the newline too
                skip(lexer);
            } else {
                // Not a continuation — we consumed \ but it wasn't \<newline>
                // This shouldn't happen in well-formed zsh but stop here
                break;
            }
        } else {
            break;
        }
    }
}

static inline bool in_error_recovery(const bool *valid_symbols) {
    return valid_symbols[ERROR_RECOVERY];
}

static inline bool is_plain_modifier_letter(int32_t c) {
    return c == 'a' || c == 'A' || c == 'c' || c == 'e' || c == 'h' ||
           c == 'l' || c == 'p' || c == 'P' || c == 'q' || c == 'Q' ||
           c == 'r' || c == 't' || c == 'u' || c == 'x' || c == '&';
}

// Read text up to an unescaped delimiter d (consumed), stopping at the end
// of the word. True if d was found.
static bool scan_to_delimiter(TSLexer *lexer, int32_t d) {
    while (!lexer->eof(lexer) && lexer->lookahead != d &&
           lexer->lookahead != '\n' && lexer->lookahead != '}') {
        if (lexer->lookahead == '\\') {
            lexer->advance(lexer, false);
            if (lexer->eof(lexer)) {
                return false;
            }
        }
        lexer->advance(lexer, false);
    }
    if (lexer->lookahead != d) {
        return false;
    }
    lexer->advance(lexer, false);
    return true;
}

// The rest of a modifier the grammar's tokens can't express, the lexer
// just past its ':' (the caller marked the end before the ':'):
//   :f<modifier>  :F<d>n<d><modifier>  (repeat the next modifier)
//   :s<d>pat<d>rep[<d>] / :gs<d>... with a delimiter other than '/'
// After :f / :F the modifier may be any of these, :s/../../ included. On a
// match the whole modifier is marked as one MODIFIER_OTHER token. On no
// match the caller goes on, with the lexer possibly moved.
static bool scan_modifier_other(TSLexer *lexer) {
    bool repeat = false;
    if (lexer->lookahead == 'f') {
        lexer->advance(lexer, false);
        repeat = true;
    } else if (lexer->lookahead == 'F') {
        lexer->advance(lexer, false);
        int32_t d = lexer->lookahead;
        if (lexer->eof(lexer) || iswalnum(d) || iswspace(d)) {
            return false;
        }
        lexer->advance(lexer, false);
        if (!scan_to_delimiter(lexer, d)) {
            return false;
        }
        repeat = true;
    }
    if (repeat && is_plain_modifier_letter(lexer->lookahead)) {
        lexer->advance(lexer, false);
        while (iswdigit(lexer->lookahead)) {
            lexer->advance(lexer, false);
        }
        lexer->mark_end(lexer);
        lexer->result_symbol = MODIFIER_OTHER;
        return true;
    }
    if (lexer->lookahead == 'g') {
        lexer->advance(lexer, false);
    }
    if (lexer->lookahead != 's') {
        return false;
    }
    lexer->advance(lexer, false);
    int32_t d = lexer->lookahead;
    if (lexer->eof(lexer) || iswalnum(d) || iswspace(d) || d == '}' ||
        (d == '/' && !repeat)) {
        return false; // :s/../../ alone is the grammar's own token
    }
    lexer->advance(lexer, false);
    if (!scan_to_delimiter(lexer, d)) {
        return false;
    }
    // The replacement runs to the next delimiter, or the end of the word.
    while (!lexer->eof(lexer) && lexer->lookahead != d &&
           !iswspace(lexer->lookahead) && lexer->lookahead != '}' &&
           lexer->lookahead != '"') {
        if (lexer->lookahead == '\\') {
            lexer->advance(lexer, false);
        }
        lexer->advance(lexer, false);
    }
    if (lexer->lookahead == d) {
        lexer->advance(lexer, false);
    }
    lexer->mark_end(lexer);
    lexer->result_symbol = MODIFIER_OTHER;
    return true;
}

// The rest of a function name with ':' in it (zinit::setup, :helper,
// _whois:whois.nic.ad.jp), when '()' follows. Called where the
// VARIABLE_NAME handler would decline anyway, so a 'no' costs nothing.
static bool scan_function_name(TSLexer *lexer) {
    for (;;) {
        int32_t c = lexer->lookahead;
        if (lexer->eof(lexer) || iswspace(c) || c == '(' || c == ')' ||
            c == '<' || c == '>' || c == ';' || c == '&' || c == '|' ||
            c == '\'' || c == '"' || c == '`' || c == '$' || c == '\\' ||
            c == '{' || c == '}' || c == '[' || c == ']') {
            break;
        }
        advance(lexer);
    }
    lexer->mark_end(lexer);
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
        advance(lexer);
    }
    if (lexer->lookahead != '(') {
        return false;
    }
    advance(lexer);
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
        advance(lexer);
    }
    if (lexer->lookahead != ')') {
        return false;
    }
    lexer->result_symbol = FUNCTION_NAME;
    return true;
}

// Note that a name token ends here (the lexer is at its end). The flag
// keeps $x[1] / $x:h / ${x#p} apart, and only holds while the next scan
// starts right where the name ended: tokens the internal lexer produces in
// between can't clear it (`zformat ... $h s:"$x"`), so the column tells.
static inline void note_variable_name(Scanner *scanner, TSLexer *lexer) {
    uint32_t col = lexer->get_column(lexer);
    scanner->just_returned_variable_name = true;
    scanner->variable_name_col = col > 0xffff ? 0xffff : (uint16_t)col;
}

static inline void reset_string(String *string) {
    if (string->size > 0) {
        memset(string->contents, 0, string->size);
        array_clear(string);
    }
}

static inline void reset_heredoc(Heredoc *heredoc) {
    heredoc->is_raw = false;
    heredoc->started = false;
    heredoc->allows_indent = false;
    reset_string(&heredoc->delimiter);
}

static inline void reset(Scanner *scanner) {
#if DEBUG
    fprintf(stderr, "DEBUG: Reset called - heredocs.size before=%u %u\n",
            scanner->heredocs.size, scanner->context_stack.size);
#endif
    scanner->last_glob_paren_depth = 0;
    scanner->test_pattern_depth = 0;
    scanner->after_case_opener = false;
    scanner->ext_was_in_double_quote = false;
    scanner->ext_saw_outside_quote = false;
    scanner->context_stack.size = 0; // Clear context stack
    scanner->just_returned_variable_name = false;
    scanner->variable_name_col = 0;
    scanner->just_returned_bare_dollar = false;
    scanner->just_exited_string = false;
    scanner->just_newline = false;
    scanner->saw_pipe_in_case = false;
    // The entries are dropped below (size = 0), so free their strings: a
    // later heredoc_new() overwrites the slot and would leak them (found by
    // the fuzzer: `<<` then a control character).
    for (uint32_t i = 0; i < scanner->heredocs.size; i++) {
        Heredoc *heredoc = array_get(&scanner->heredocs, i);
        array_delete(&heredoc->current_leading_word);
        array_delete(&heredoc->delimiter);
    }
    // Unlike context_stack.size above, heredocs.size previously survived a
    // reset untouched. heredocs.size only ever grows during scanning (no
    // code path pops a finished heredoc), so once it crosses a
    // serialization limit above, every future serialize() call would keep
    // bailing out — even for unrelated, small context_stack state — for
    // the rest of the parse. Dropping it back to 0 here makes a reset
    // actually restore the scanner to a serializable state.
    scanner->heredocs.size = 0;
#if DEBUG
    fprintf(stderr, "DEBUG: Reset done - heredocs.size after=%u %u\n",
            scanner->heredocs.size, scanner->context_stack.size);
#endif
}

static unsigned serialize(Scanner *scanner, char *buffer) {
    uint32_t size = 0;

    // context_stack.size and heredocs.size are each encoded as a single
    // byte below. If either count exceeds what a byte can hold, the header
    // would silently wrap (mod 256) while the loops further down still
    // serialize the full, untruncated number of entries. That desyncs the
    // header counts from the actual payload length, which trips the
    // `assert(size == length)` in deserialize() once the mismatched buffer
    // is restored. Bail out to a full reset instead of producing a
    // corrupt buffer, consistent with the capacity checks below.
    if (scanner->context_stack.size > UINT8_MAX ||
        scanner->heredocs.size > UINT8_MAX) {
        return 0;
    }

    buffer[size++] = (char)scanner->last_glob_paren_depth;
    buffer[size++] = (char)scanner->ext_was_in_double_quote;
    buffer[size++] = (char)scanner->ext_saw_outside_quote;
    buffer[size++] = (char)scanner->context_stack.size;
    buffer[size++] = (char)scanner->heredocs.size;
    buffer[size++] = (char)scanner->just_returned_variable_name;
    buffer[size++] = (char)scanner->just_returned_bare_dollar;
    buffer[size++] = (char)scanner->just_exited_string;
    buffer[size++] = (char)scanner->just_newline;
    buffer[size++] = (char)scanner->test_pattern_depth;
    buffer[size++] = (char)scanner->after_case_opener;
    buffer[size++] = (char)scanner->saw_pipe_in_case;
    buffer[size++] = (char)(scanner->variable_name_col & 0xff);
    buffer[size++] = (char)(scanner->variable_name_col >> 8);

    // Serialize context stack
    for (uint32_t i = 0; i < scanner->context_stack.size; i++) {
        if (size >= TREE_SITTER_SERIALIZATION_BUFFER_SIZE) {
            return 0;
        }
        context_type_t *ctx = array_get(&scanner->context_stack, i);
        buffer[size++] = (char)*ctx;
    }

    for (uint32_t i = 0; i < scanner->heredocs.size; i++) {
        Heredoc *heredoc = array_get(&scanner->heredocs, i);
        if (size + 3 + sizeof(uint32_t) + heredoc->delimiter.size >=
            TREE_SITTER_SERIALIZATION_BUFFER_SIZE) {
            return 0;
        }

        buffer[size++] = (char)heredoc->is_raw;
        buffer[size++] = (char)heredoc->started;
        buffer[size++] = (char)heredoc->allows_indent;

        memcpy(&buffer[size], &heredoc->delimiter.size, sizeof(uint32_t));
        size += sizeof(uint32_t);
        if (heredoc->delimiter.size > 0) {
            memcpy(&buffer[size], heredoc->delimiter.contents,
                   heredoc->delimiter.size);
            size += heredoc->delimiter.size;
        }
    }
    return size;
}

static void deserialize(Scanner *scanner, const char *buffer, unsigned length) {
#if DEBUG
    fprintf(stderr,
            "DEBUG: Deserialize called - length=%u, before heredocs.size =%u "
            "ctx_stack=%u\n",
            length, scanner->heredocs.size, scanner->context_stack.size);
#endif
    if (length == 0) {
        reset(scanner);
    } else {
        uint32_t size = 0;
        scanner->last_glob_paren_depth = buffer[size++];
        scanner->ext_was_in_double_quote = buffer[size++];
        scanner->ext_saw_outside_quote = buffer[size++];
        uint32_t context_stack_size = (unsigned char)buffer[size++];
        uint32_t heredoc_count = (unsigned char)buffer[size++];
#if DEBUG
        fprintf(stderr,
                "DEBUG: Deserialize - heredoc_count=%u context_stack_size=%u\n",
                heredoc_count, context_stack_size);
#endif
        scanner->just_returned_variable_name = buffer[size++];
        scanner->just_returned_bare_dollar = buffer[size++];
        scanner->just_exited_string = buffer[size++];
        scanner->just_newline = buffer[size++];
        scanner->test_pattern_depth = (uint8_t)buffer[size++];
        scanner->after_case_opener = buffer[size++];
        scanner->saw_pipe_in_case = buffer[size++];
        scanner->variable_name_col = (uint8_t)buffer[size++];
        scanner->variable_name_col |= (uint16_t)((uint8_t)buffer[size++] << 8);

        // Deserialize context stack
        scanner->context_stack.size = 0;
        for (uint32_t i = 0; i < context_stack_size; i++) {
            if (size >= length)
                break;
            context_type_t ctx = (context_type_t)buffer[size++];
            array_push(&scanner->context_stack, ctx);
        }

        for (uint32_t i = 0; i < heredoc_count; i++) {
            Heredoc *heredoc = NULL;
            if (i < scanner->heredocs.size) {
                heredoc = array_get(&scanner->heredocs, i);
            } else {
                Heredoc new_heredoc = heredoc_new();
                array_push(&scanner->heredocs, new_heredoc);
                heredoc = array_back(&scanner->heredocs);
            }

            heredoc->is_raw = buffer[size++];
            heredoc->started = buffer[size++];
            heredoc->allows_indent = buffer[size++];

            memcpy(&heredoc->delimiter.size, &buffer[size], sizeof(uint32_t));
            size += sizeof(uint32_t);
            array_reserve(&heredoc->delimiter, heredoc->delimiter.size);

            if (heredoc->delimiter.size > 0) {
                memcpy(heredoc->delimiter.contents, &buffer[size],
                       heredoc->delimiter.size);
                size += heredoc->delimiter.size;
            }
        }
        // Drop heredocs beyond the serialized count. They come from a
        // later point in the text (another branch, or the previous parse
        // when re-parsing incrementally) and aren't part of this state.
        for (uint32_t i = heredoc_count; i < scanner->heredocs.size; i++) {
            Heredoc *heredoc = array_get(&scanner->heredocs, i);
            array_delete(&heredoc->current_leading_word);
            array_delete(&heredoc->delimiter);
        }
        if (scanner->heredocs.size > heredoc_count) {
            scanner->heredocs.size = heredoc_count;
        }
        assert(size == length);
    }
#if DEBUG
    fprintf(stderr, "DEBUG: Deserialize done - heredocs.size after=%u %u\n",
            scanner->heredocs.size, scanner->context_stack.size);
#endif
}

/**
 * Consume a "word" in POSIX parlance, and returns it unquoted.
 *
 * This is an approximate implementation that doesn't deal with any
 * POSIX-mandated substitution, and assumes the default value for
 * IFS.
 */
static bool advance_word(TSLexer *lexer, String *unquoted_word) {
    bool empty = true;

    int32_t quote = 0;
    if (lexer->lookahead == '\'' || lexer->lookahead == '"') {
        quote = lexer->lookahead;
        advance(lexer);
    }

    while (lexer->lookahead &&
           !(quote ? lexer->lookahead == quote || lexer->lookahead == '\r' ||
                         lexer->lookahead == '\n'
                   : iswspace(lexer->lookahead))) {
        if (lexer->lookahead == '\\') {
            advance(lexer);
            if (!lexer->lookahead) {
                return false;
            }
        }
        empty = false;
        array_push(unquoted_word, lexer->lookahead);
        advance(lexer);
    }
    array_push(unquoted_word, '\0');

    if (quote && lexer->lookahead == quote) {
        advance(lexer);
    }

    return !empty;
}

static inline bool scan_raw_dollar(TSLexer *lexer, const bool *valid_symbols) {
    skip_ws(lexer);

    if (lexer->lookahead == '$') {
        advance(lexer);
        lexer->result_symbol = RAW_DOLLAR;
        lexer->mark_end(lexer);
        return iswspace(lexer->lookahead) || lexer->eof(lexer) ||
               lexer->lookahead == '\"';
    }

    return false;
}

static bool scan_heredoc_start(Heredoc *heredoc, TSLexer *lexer) {
    while (iswspace(lexer->lookahead)) {
        skip(lexer);
    }

    lexer->result_symbol = HEREDOC_START;
    heredoc->is_raw = lexer->lookahead == '\'' || lexer->lookahead == '"' ||
                      lexer->lookahead == '\\';

    bool found_delimiter = advance_word(lexer, &heredoc->delimiter);
    if (!found_delimiter) {
        reset_string(&heredoc->delimiter);
        return false;
    }
    return found_delimiter;
}

static bool scan_heredoc_end_identifier(Heredoc *heredoc, TSLexer *lexer) {
    reset_string(&heredoc->current_leading_word);
    // Scan the first 'n' characters on this line, to see if they match the
    // heredoc delimiter
    int32_t size = 0;
    if (heredoc->delimiter.size > 0) {
        while (lexer->lookahead != '\0' && lexer->lookahead != '\n' &&
               (int32_t)*array_get(&heredoc->delimiter, size) ==
                   lexer->lookahead &&
               heredoc->current_leading_word.size < heredoc->delimiter.size) {
            array_push(&heredoc->current_leading_word, lexer->lookahead);
            advance(lexer);
            size++;
        }
    }
    array_push(&heredoc->current_leading_word, '\0');
    return heredoc->delimiter.size == 0
               ? false
               : strcmp(heredoc->current_leading_word.contents,
                        heredoc->delimiter.contents) == 0;
}

static bool scan_heredoc_content(Scanner *scanner, TSLexer *lexer,
                                 enum TokenType middle_type,
                                 enum TokenType end_type) {
    bool did_advance = false;
    Heredoc *heredoc = array_back(&scanner->heredocs);

    for (;;) {
        switch (lexer->lookahead) {
        case '\0': {
            if (lexer->eof(lexer) && did_advance) {
                reset_heredoc(heredoc);
                lexer->result_symbol = end_type;
                return true;
            }
            return false;
        }

        case '\\': {
            did_advance = true;
            advance(lexer);
            advance(lexer);
            break;
        }

        case '$': {
            if (heredoc->is_raw) {
                did_advance = true;
                advance(lexer);
                break;
            }
            if (did_advance) {
                lexer->mark_end(lexer);
                lexer->result_symbol = middle_type;
                heredoc->started = true;
                advance(lexer);
                if (iswalpha(lexer->lookahead) || lexer->lookahead == '{' ||
                    lexer->lookahead == '(') {
                    return true;
                }
                break;
            }
            if (middle_type == HEREDOC_BODY_BEGINNING &&
                lexer->get_column(lexer) == 0) {
                lexer->mark_end(lexer);
                lexer->result_symbol = middle_type;
                heredoc->started = true;
                return true;
            }
            return false;
        }

        case '\n': {
            if (!did_advance) {
                skip(lexer);
            } else {
                advance(lexer);
            }
            did_advance = true;
            if (heredoc->allows_indent) {
                while (iswspace(lexer->lookahead)) {
                    advance(lexer);
                }
            }
            lexer->result_symbol = heredoc->started ? middle_type : end_type;
            lexer->mark_end(lexer);
            if (scan_heredoc_end_identifier(heredoc, lexer)) {
                if (lexer->result_symbol == HEREDOC_END) {
                    array_delete(&heredoc->current_leading_word);
                    array_delete(&heredoc->delimiter);
                    array_pop(&scanner->heredocs);
                }
                return true;
            }
            break;
        }

        default: {
            if (lexer->get_column(lexer) == 0) {
                // an alternative is to check the starting column of the
                // heredoc body and track that statefully
                while (iswspace(lexer->lookahead)) {
                    if (did_advance) {
                        advance(lexer);
                    } else {
                        skip(lexer);
                    }
                }
                if (end_type != SIMPLE_HEREDOC_BODY) {
                    lexer->result_symbol = middle_type;
                    if (scan_heredoc_end_identifier(heredoc, lexer)) {
                        return true;
                    }
                }
                if (end_type == SIMPLE_HEREDOC_BODY) {
                    lexer->result_symbol = end_type;
                    lexer->mark_end(lexer);
                    if (scan_heredoc_end_identifier(heredoc, lexer)) {
                        return true;
                    }
                }
            }
            did_advance = true;
            advance(lexer);
            break;
        }
        }
    }
}

// Subscript flags: $a[(i)pat] ${x[(ws:,:)2]} $a[(n:2:i)pat]
// zshparam(1) "Subscript Flags". Called with the '(' already consumed and
// marked, lookahead on the first flag char. Pattern flags (r R i I k K) mean
// the index is a pattern, so they get their own token. On failure the caller
// still has the plain '(' marked, so nothing needs to backtrack.
static inline bool is_subscript_flag_start(int32_t c) {
    return c && strchr("wpferRiIkKsnbj", (int)c) != NULL;
}

static bool scan_subscript_flags(TSLexer *lexer, const bool *valid_symbols) {
    bool is_pattern = false;
    int count = 0;
    for (;;) {
        int32_t c = lexer->lookahead;
        if (c == ')') {
            if (count == 0) {
                return false;
            }
            break;
        }
        if (c == 'w' || c == 'p' || c == 'f' || c == 'e') {
            advance(lexer);
        } else if (c == 'r' || c == 'R' || c == 'i' || c == 'I' || c == 'k' ||
                   c == 'K') {
            is_pattern = true;
            advance(lexer);
        } else if (c == 's' || c == 'n' || c == 'b' || c == 'j') {
            // Argument with any delimiter; ( [ { < pair with their closers.
            advance(lexer);
            int32_t open = lexer->lookahead;
            int32_t close = open == '('   ? ')'
                            : open == '[' ? ']'
                            : open == '{' ? '}'
                            : open == '<' ? '>'
                                          : open;
            if (open == 0 || open == ')' || iswspace(open) ||
                lexer->eof(lexer)) {
                return false;
            }
            advance(lexer);
            while (lexer->lookahead != close && !lexer->eof(lexer) &&
                   lexer->lookahead != '\n') {
                advance(lexer);
            }
            if (lexer->lookahead != close) {
                return false;
            }
            advance(lexer);
        } else {
            return false;
        }
        count++;
    }
    enum TokenType sym = is_pattern ? SUBSCRIPT_PATTERN_FLAGS : SUBSCRIPT_FLAGS;
    if (!valid_symbols[sym]) {
        return false;
    }
    advance(lexer); // ')'
    lexer->mark_end(lexer);
    lexer->result_symbol = sym;
    return true;
}

// Read a bracket class [abc] [^)] []x] [!]x] [[:space:]] [^[:blank:]\#]
// with the lexer on the opening '['. POSIX classes [:name:], [=c=] and
// [.c.] inside it don't close it. Returns false (lexer advanced) if it
// doesn't close on this line; callers then end their token at the last
// mark_end, before the '['.
static bool scan_bracket_class(TSLexer *lexer) {
    advance(lexer); // '['
    if (lexer->lookahead == '^' || lexer->lookahead == '!') {
        advance(lexer);
    }
    if (lexer->lookahead == ']') {
        advance(lexer); // leading ']' is literal
    }
    for (;;) {
        int32_t c = lexer->lookahead;
        if (lexer->eof(lexer) || c == '\n' || c == 0) {
            return false;
        }
        if (c == ']') {
            advance(lexer);
            return true;
        }
        if (c == '\\') {
            advance(lexer);
            if (lexer->eof(lexer) || lexer->lookahead == '\n') {
                return false;
            }
            advance(lexer);
            continue;
        }
        if (c == '[') {
            advance(lexer);
            int32_t kind = lexer->lookahead;
            if (kind == ':' || kind == '=' || kind == '.') {
                advance(lexer);
                // up to the matching ":]" / "=]" / ".]"
                for (;;) {
                    if (lexer->eof(lexer) || lexer->lookahead == '\n' ||
                        lexer->lookahead == 0) {
                        return false;
                    }
                    if (lexer->lookahead == kind) {
                        advance(lexer);
                        if (lexer->lookahead == ']') {
                            advance(lexer);
                            break;
                        }
                        continue;
                    }
                    advance(lexer);
                }
            }
            continue; // a lone '[' inside a class is literal
        }
        advance(lexer);
    }
}

// Case item starting with '(': zsh's optional opening paren, or a group at
// the start of the pattern? (free|net)bsd*) and (a|b):*) are groups,
// (a) and (a|b) y are openers. Read to the matching ')' and look at the
// next character: blank / ; / newline / EOF means opener; anything else
// (pattern text, ')' or '|') means the '(' begins a group and the whole
// value is one extglob_pattern. With the lexer on '('. Returns true with
// result_symbol set, or false if it isn't a group (caller emits the
// opener: the '(' is already marked).
static bool scan_case_pattern_rest(TSLexer *lexer, uint32_t depth);

static bool scan_case_group_pattern(TSLexer *lexer, bool always_group) {
    advance(lexer); // '('
    lexer->mark_end(lexer);
    uint32_t depth = 1;
    while (depth > 0) {
        int32_t c = lexer->lookahead;
        if (lexer->eof(lexer) || c == 0 || c == '\n') {
            return false;
        }
        if (c == '\\') {
            advance(lexer);
            if (lexer->eof(lexer) || lexer->lookahead == '\n') {
                return false;
            }
            advance(lexer);
        } else if (c == '[') {
            if (!scan_bracket_class(lexer)) {
                return false;
            }
        } else {
            if (c == '(') {
                depth++;
            } else if (c == ')') {
                depth--;
            }
            advance(lexer);
        }
    }
    int32_t n = lexer->lookahead;
    if (!always_group &&
        (lexer->eof(lexer) || n == 0 || iswspace(n) || n == ';')) {
        return false; // "(pattern)" then blank: the opener form
    }
    return scan_case_pattern_rest(lexer, depth);
}

// Rest of a case pattern from the lexer's position, at group depth `depth`:
// up to the item's ')' or a '|' separating alternatives (both at depth 0),
// a blank, or $ " ' `. Bracket classes are read whole (quotes and $ inside
// them are literal: [" "$TAB]). Emits EXTGLOB_PATTERN.
static bool scan_case_pattern_rest(TSLexer *lexer, uint32_t depth) {
    for (;;) {
        lexer->mark_end(lexer);
        int32_t c = lexer->lookahead;
        if (lexer->eof(lexer) || c == 0 || iswspace(c) || c == ';' ||
            c == '$' || c == '"' || c == '\'' || c == '`') {
            break;
        }
        if (depth == 0 && (c == ')' || c == '|')) {
            break;
        }
        if (c == '\\') {
            advance(lexer);
            if (lexer->eof(lexer) || lexer->lookahead == '\n') {
                break;
            }
            advance(lexer);
        } else if (c == '[') {
            if (!scan_bracket_class(lexer)) {
                break;
            }
        } else {
            if (c == '(') {
                depth++;
            } else if (c == ')') {
                depth--;
            }
            advance(lexer);
        }
    }
    lexer->result_symbol = EXTGLOB_PATTERN;
    return true;
}

// One piece of a pattern subscript index, after pattern flags:
// $words[(i)--] ${a[(r)*@*]} ${tmp[(i)\[Zend Modules\]]}. Stops before
// $ " ' ` (those are their own nodes), a newline, or the closing ']'.
// A '[' starts a bracket class only if it closes on the same line;
// otherwise the piece ends before it (mark_end, so no backtracking).
// Plain text is a word, anything with glob characters a glob_pattern.
static inline bool is_subscript_word_char(int32_t c) {
    return iswalnum(c) || (c && strchr("_.,:+=@%/-", (int)c) != NULL);
}

static bool scan_subscript_pattern(TSLexer *lexer, const bool *valid_symbols,
                                   bool *moved) {
    bool any = false;
    *moved = false;
    bool glob = false;
    for (;;) {
        int32_t c = lexer->lookahead;
        if (lexer->eof(lexer) || c == 0 || c == '\n' || c == ']' || c == '$' ||
            c == '"' || c == '\'' || c == '`') {
            break;
        }
        *moved = true;
        if (c == '\\') {
            advance(lexer);
            if (lexer->eof(lexer) || lexer->lookahead == '\n') {
                break;
            }
            advance(lexer);
        } else if (c == '[') {
            if (!scan_bracket_class(lexer)) {
                break; // unclosed: token ends at the last mark
            }
            glob = true;
        } else {
            if (!is_subscript_word_char(c)) {
                glob = true;
            }
            advance(lexer);
        }
        any = true;
        lexer->mark_end(lexer);
    }
    if (!any) {
        return false;
    }
    enum TokenType sym = glob ? SUBSCRIPT_PATTERN_GLOB : SUBSCRIPT_PATTERN_WORD;
    if (!valid_symbols[sym]) {
        // Only one kind is ever valid alone in practice; prefer what fits.
        sym = valid_symbols[SUBSCRIPT_PATTERN_GLOB] ? SUBSCRIPT_PATTERN_GLOB
                                                    : SUBSCRIPT_PATTERN_WORD;
        if (!valid_symbols[sym]) {
            return false;
        }
    }
    lexer->result_symbol = sym;
    return true;
}

// Text in a ${x:-...} / ${x:+...} / ${x:=...} / ${x:?...} value that
// starts with a character which outside ${} would be shell syntax:
// ( ) < > | ; &. In the value they're literal, as in ${X:-(none)} and
// "${X:-<unset>}". Reads up to $ " ' ` whitespace or an unmatched '}'.
// Only starts on one of those characters, so ordinary words keep their
// usual tokens.
static inline bool is_default_word_start(int32_t c) {
    return c == '(' || c == ')' || c == '<' || c == '>' || c == '|' ||
           c == ';' || c == '&';
}

static bool scan_default_word(TSLexer *lexer) {
    if (!is_default_word_start(lexer->lookahead)) {
        return false;
    }
    uint32_t depth = 0;
    for (;;) {
        int32_t c = lexer->lookahead;
        if (lexer->eof(lexer) || c == 0 || c == '$' || c == '"' || c == '\'' ||
            c == '`' || iswspace(c)) {
            break;
        }
        if (c == '{') {
            depth++;
        } else if (c == '}') {
            if (depth == 0) {
                break;
            }
            depth--;
        }
        if (c == '\\') {
            advance(lexer);
            if (lexer->eof(lexer) || lexer->lookahead == '\n') {
                break;
            }
        }
        advance(lexer);
        lexer->mark_end(lexer);
    }
    lexer->result_symbol = DEFAULT_WORD;
    return true;
}

// One piece of the pattern on the right of = == != inside [[ ]]:
// [[ $x = (a|b)* ]] [[ $1 = -- ]] [[ $f = *.(c|h) ]] [[ $n = <1-5> ]].
// Stops at a blank, ']' (end of [[ ]]), ')' with no open '(' in this
// piece, && / || / ; / &, a newline, or $ " ' ` (their own nodes).
// Bracket classes are read whole when they close on the line. Plain text
// is a word, anything with glob characters a glob_pattern.
static bool scan_test_pattern(Scanner *scanner, TSLexer *lexer,
                              const bool *valid_symbols, bool *moved,
                              uint32_t opened, bool allow_close_start) {
    // opened == 1: the caller already consumed (and marked) a '('.
    // Groups opened in earlier pieces are carried in test_pattern_depth,
    // so @(*${x}.tar.xz|*.asc) closes in a later piece.
    uint32_t depth = scanner->test_pattern_depth + opened;
    bool any = opened > 0;
    bool glob = opened > 0;
    *moved = opened > 0;
    for (;;) {
        int32_t c = lexer->lookahead;
        if (lexer->eof(lexer) || c == 0 || iswspace(c) || c == ']' ||
            c == '"' || c == '\'' || c == '`' || c == ';') {
            break;
        }
        if (c == '$') {
            // $x ${x} $(x) $'x' $"x" start their own nodes: end the piece
            // before the '$' (last mark). A '$' that can't start one, as
            // in (#b)[^$]#($)*, is a literal pattern character.
            *moved = true;
            advance(lexer);
            int32_t n = lexer->lookahead;
            if (iswalnum(n) || n == '_' || n == '{' || n == '(' || n == '\'' ||
                n == '"' || n == '#' || n == '*' || n == '@' || n == '?' ||
                n == '$' || n == '!' || n == '-' || n == '+' || n == '~' ||
                n == '^' || n == '=') {
                break;
            }
            glob = true;
            any = true;
            lexer->mark_end(lexer);
            continue;
        }
        if (c == ')' && depth == 0) {
            // After an expansion inside a group, (#b)(${~x})*: the ')'
            // closes the pattern group when pattern text follows directly.
            // Followed by a blank ] ) & | it closes a [[ ( ... ) ]] group
            // instead: return false and let the internal lexer take it.
            if (!any && allow_close_start) {
                *moved = true;
                advance(lexer);
                int32_t n = lexer->lookahead;
                if (!lexer->eof(lexer) && n != 0 && !iswspace(n) && n != ']' &&
                    n != ')' && n != '&' && n != '|' && n != ';') {
                    glob = true;
                    any = true;
                    lexer->mark_end(lexer);
                    continue;
                }
            }
            break;
        }
        if (c == '&') {
            break;
        }
        if (c == '|' && depth == 0) {
            // a|b alternation at top level is fine, but || ends the operand
            *moved = true;
            advance(lexer);
            if (lexer->lookahead == '|') {
                break; // token ends at the last mark, before the first '|'
            }
            glob = true;
            any = true;
            lexer->mark_end(lexer);
            continue;
        }
        *moved = true;
        if (c == '\\') {
            advance(lexer);
            if (lexer->eof(lexer) || lexer->lookahead == '\n') {
                break;
            }
            advance(lexer);
        } else if (c == '[') {
            if (!scan_bracket_class(lexer)) {
                break; // unclosed: token ends at the last mark
            }
            glob = true;
        } else if (c == '(') {
            // "(#" starts glob flags (#i) or a qualifier (#qN): end the
            // piece before it (last mark) so they get their own nodes.
            advance(lexer);
            if (lexer->lookahead == '#') {
                break;
            }
            depth++;
            glob = true;
        } else {
            if (c == ')') {
                depth--;
            }
            if (!is_subscript_word_char(c)) {
                glob = true;
            }
            advance(lexer);
        }
        any = true;
        lexer->mark_end(lexer);
    }
    if (!any) {
        return false;
    }
    enum TokenType sym = glob ? TEST_PATTERN_GLOB : TEST_PATTERN_WORD;
    if (!valid_symbols[sym]) {
        return false;
    }
    scanner->test_pattern_depth =
        depth > UINT8_MAX ? UINT8_MAX : (uint8_t)depth;
    lexer->result_symbol = sym;
    return true;
}

static bool scan(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols) {
#if DEBUG
    fprintf(stderr, "SCANNER: invoked lookahead='%c'\n", lexer->lookahead);
    for (int i = 0; i <= ERROR_RECOVERY; i++) {
        if (valid_symbols[i]) {
            fprintf(stderr, "SCANNER: valid symbol: %s\n", TokenNames[i]);
        }
    }
#endif

    // Was the scanner entered on a blank (before any handler skipped it)?
    bool scan_started_on_blank = iswspace(lexer->lookahead);
    // Set when a handler skipped blanks and fell through without a token.
    bool skipped_blank_before = false;

    // Clear flag at start and capture its previous value
    // (See note_variable_name. Not set for assignment names: id=x:$h.)
    bool was_just_variable_name =
        scanner->just_returned_variable_name &&
        lexer->get_column(lexer) == scanner->variable_name_col;
    scanner->just_returned_variable_name = false;

    bool was_just_bare_dollar = scanner->just_returned_bare_dollar;
    scanner->just_returned_bare_dollar = false;

    // Clear string exit flag at start and capture its previous value
    bool was_just_exited_string = scanner->just_exited_string;
    scanner->just_exited_string = false;

    // FIXME: newline handling and exited string handling should go
    bool was_just_newline = scanner->just_newline;
    scanner->just_newline = false;

    // Only meaningful for the token right after a case item's '('
    bool was_after_case_opener = scanner->after_case_opener;
    scanner->after_case_opener = false;

    // Empty assignment value (`var=` followed by whitespace, newline or a
    // terminator). Must run before handlers that skip whitespace and
    // newlines (e.g. BARE_DOLLAR hunting for a `$`), which would consume the
    // newline after `=` and take a word on the next line as the value. In
    // zsh any whitespace after `=` means the value is empty. (PR #41)
    // Not inside ${...}: there "${x:+ (y)}" may have a blank before the
    // value, which DEFAULT_WORD handles; the later EMPTY_VALUE check covers
    // genuinely empty defaults.
    if (valid_symbols[EMPTY_VALUE] && !in_error_recovery(valid_symbols) &&
        !in_parameter_expansion(scanner) &&
        (iswspace(lexer->lookahead) || lexer->eof(lexer) ||
         lexer->lookahead == ';' || lexer->lookahead == '&' ||
         lexer->lookahead == '}')) {
        lexer->mark_end(lexer);
        lexer->result_symbol = EMPTY_VALUE;
        return true;
    }

    // Identifier inside arithmetic: [A-Za-z_][A-Za-z0-9_]*. Lexed here so
    // the shell word token (which would take `a,` `i++` `a-b` whole) never
    // sees it.
    if (valid_symbols[ARITHMETIC_WORD] && !in_error_recovery(valid_symbols) &&
        get_current_context(scanner) == CTX_ARITHMETIC) {
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t' ||
               lexer->lookahead == '\n' || lexer->lookahead == '\r') {
            skip(lexer);
        }
        if (iswalpha(lexer->lookahead) || lexer->lookahead == '_') {
            while (iswalnum(lexer->lookahead) || lexer->lookahead == '_') {
                advance(lexer);
            }
            lexer->mark_end(lexer);
            lexer->result_symbol = ARITHMETIC_WORD;
            return true;
        }
        // ##a is the character code of a (##\n of a newline), #name the
        // code of the first character of $name. Inside arithmetic '#' isn't
        // a comment, so this has to come before the comment extra.
        if (lexer->lookahead == '#' && valid_symbols[ARITHMETIC_CHAR_CODE]) {
            advance(lexer);
            if (lexer->lookahead == '#') {
                advance(lexer);
                // ##$chr: the code of $chr's value. Emit just "##"; the
                // expansion follows as the rest of the same operand.
                if (lexer->lookahead == '$') {
                    lexer->mark_end(lexer);
                    lexer->result_symbol = ARITHMETIC_CHAR_CODE;
                    return true;
                }
                if (lexer->lookahead == '\\') {
                    advance(lexer);
                }
                if (!lexer->eof(lexer) && lexer->lookahead != '\n') {
                    advance(lexer);
                    lexer->mark_end(lexer);
                    lexer->result_symbol = ARITHMETIC_CHAR_CODE;
                    return true;
                }
                return false;
            }
            if (iswalpha(lexer->lookahead) || lexer->lookahead == '_') {
                while (iswalnum(lexer->lookahead) || lexer->lookahead == '_') {
                    advance(lexer);
                }
                lexer->mark_end(lexer);
                lexer->result_symbol = ARITHMETIC_CHAR_CODE;
                return true;
            }
            // A lone '#' is $#, the argument count: (( # > 0 ))
            if (valid_symbols[ARITHMETIC_WORD]) {
                lexer->mark_end(lexer);
                lexer->result_symbol = ARITHMETIC_WORD;
                return true;
            }
            return false; // lexer moved: only skips the handlers below here
        }
    }

    // Only at the start of the operand: both piece kinds are valid there.
    // Operands starting with $ " ' ` fall through to the normal handlers.
    if (valid_symbols[TEST_PATTERN_WORD] && valid_symbols[TEST_PATTERN_GLOB] &&
        !in_error_recovery(valid_symbols)) {
        // A piece directly after a string/expansion has no blank before
        // it; at the start of the operand there's one after the operator.
        // A blank after a string/expansion ends the operand ([[ $x = $y ]]),
        // so only skip blanks when CONCAT (mid-word) isn't expected.
        // A blank always separates operands (a concatenation can't
        // contain one), even where CONCAT is valid, e.g. after the count
        // in [[ -prefix 1 (f|ht)tp:// ]].
        bool adjacent = lexer->lookahead != ' ' && lexer->lookahead != '\t';
        if (!adjacent) {
            scanner->test_pattern_depth = 0; // new operand
            while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
                skip(lexer);
            }
            // Handlers below must still know there was a blank here:
            // CONCAT in particular may not join across it ([[ $x = $y ]]).
            skipped_blank_before = true;
        }
        int32_t c = lexer->lookahead;
        // Mid-pattern (adjacent to the previous piece) a piece may start
        // with ')' or '|': ((#s)|*:)${x}. A following '|' makes it '||',
        // which scan_test_pattern hands back to the internal lexer.
        if (c != '$' && c != '"' && c != '\'' && c != '`' && c != ']' &&
            ((c != ')' && c != '|') || adjacent) && c != '&' && !iswspace(c) &&
            c != 0) {
            bool moved = false;
            uint32_t depth = 0;
            if (c == '(') {
                advance(lexer);
                lexer->mark_end(lexer);
                if (lexer->lookahead == '#') {
                    advance(lexer);
                    if (lexer->lookahead == 'q') {
                        // (#qN) qualifier: the grammar's '(' '#q' tokens
                        // handle it; the internal lexer restarts at '('.
                        return false;
                    }
                    // (#i) (#b) (#a2) (#s) ... extended glob flags
                    bool any_flag = false;
                    while (iswalnum(lexer->lookahead) ||
                           lexer->lookahead == '.' || lexer->lookahead == ',') {
                        advance(lexer);
                        any_flag = true;
                    }
                    if (any_flag && lexer->lookahead == ')' &&
                        valid_symbols[ZSH_EXTENDED_GLOB_FLAGS]) {
                        advance(lexer);
                        lexer->mark_end(lexer);
                        lexer->result_symbol = ZSH_EXTENDED_GLOB_FLAGS;
                        return true;
                    }
                    return false;
                }
                depth = 1;
            }
            if (scan_test_pattern(scanner, lexer, valid_symbols, &moved, depth,
                                  adjacent)) {
                return true;
            }
            if (moved) {
                return false;
            }
        }
    }

    if (valid_symbols[DEFAULT_WORD] && !in_error_recovery(valid_symbols) &&
        in_parameter_expansion(scanner)) {
        // "${x:+ (${y}ms)}": the value may start with blanks. Only skip
        // them when CONCAT isn't wanted, so adjacency checks still see
        // the real next character; other handlers skip blanks themselves.
        if (!valid_symbols[CONCAT] && !valid_symbols[CONCAT_REGEX]) {
            while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
                skip(lexer);
            }
        }
        if (is_default_word_start(lexer->lookahead)) {
            return scan_default_word(lexer);
        }
        // Inside "..." zsh doesn't pair braces in a value, so a '{' there
        // is one literal character: "${${Q:+{}:-x}" has the value '{' and
        // the '}' closes. (Unquoted they pair, ${Q:+{}x} is '{}x', which
        // the word token's {...} pieces cover.)
        if (lexer->lookahead == '{') {
            for (uint32_t i = scanner->context_stack.size; i > 0; i--) {
                if (*array_get(&scanner->context_stack, i - 1) == CTX_STRING) {
                    advance(lexer);
                    lexer->mark_end(lexer);
                    lexer->result_symbol = DEFAULT_WORD;
                    return true;
                }
            }
        }
    }

    if ((valid_symbols[SUBSCRIPT_PATTERN_WORD] ||
         valid_symbols[SUBSCRIPT_PATTERN_GLOB]) &&
        !in_error_recovery(valid_symbols)) {
        bool moved = false;
        if (scan_subscript_pattern(lexer, valid_symbols, &moved)) {
            return true;
        }
        if (moved) {
            return false; // lexer advanced; can't hand off to other handlers
        }
    }

    // Inside arithmetic only a piece can follow a piece directly ($x$y,
    // x$i, $(cmd)$x); an operator (a++ a-b x+=1) or bracket never joins.
    // (Subscripts run in CTX_SUBSCRIPT, so a[foo::bar] isn't affected.)
    bool arith_no_concat =
        get_current_context(scanner) == CTX_ARITHMETIC &&
        !(lexer->lookahead == '$' || lexer->lookahead == '`' ||
          lexer->lookahead == '"' || lexer->lookahead == '\'' ||
          iswalnum(lexer->lookahead) || lexer->lookahead == '_');
    if ((valid_symbols[CONCAT] || valid_symbols[CONCAT_REGEX]) &&
        !in_error_recovery(valid_symbols) && !skipped_blank_before &&
        !arith_no_concat) {
        context_type_t ctx = get_current_context(scanner);
#if DEBUG
        fprintf(stderr,
                "SCANNER: CONCAT handler lookeahead=%c "
                "was_just_exited_string=%d was_just_newline=%d\n",
                lexer->lookahead, was_just_exited_string, was_just_newline);
#endif

        // After $x a ':' is a modifier only if a modifier letter follows
        // (modifiers are lexed with their ':', :h :t2 :s/ ...); otherwise
        // it's a literal ':' joined to the word: $0::$cmd, $a:$b.
        if (lexer->lookahead == ':' && was_just_variable_name &&
            valid_symbols[CONCAT]) {
            lexer->mark_end(lexer); // zero-width; the advance only peeks
            advance(lexer);
            int32_t m = lexer->lookahead;
            if (valid_symbols[MODIFIER_OTHER] && scan_modifier_other(lexer)) {
                return true;
            }
            if (m == 'a' || m == 'A' || m == 'c' || m == 'e' || m == 'h' ||
                m == 'l' || m == 'p' || m == 'P' || m == 'q' || m == 'Q' ||
                m == 'r' || m == 's' || m == 'g' || m == 't' || m == 'u' ||
                m == 'x' || m == '&') {
                return false; // a modifier: the internal lexer takes it
            }
            lexer->result_symbol = CONCAT;
            return true;
        }

        if (!(lexer->lookahead == 0 || iswspace(lexer->lookahead) ||
              lexer->lookahead == '>' || lexer->lookahead == '<' ||
              (lexer->lookahead == ')' &&
               (valid_symbols[CLOSING_PAREN] ||
                valid_symbols[CLOSING_DOUBLE_PAREN])) ||
              (lexer->lookahead == '(' && !valid_symbols[CONCAT_REGEX]) ||
              lexer->lookahead == ';' || lexer->lookahead == '&' ||
              lexer->lookahead == '|' || lexer->lookahead == '{' ||
              // prevent concat over newline after string ends
              (was_just_exited_string && lexer->lookahead == '\n') ||
              (lexer->lookahead == '"' && ctx == CTX_STRING) ||
              // (No "just after a NEWLINE" check: CONCAT can't be valid at
              // a statement start, and the flag goes stale when the
              // internal lexer produces the next tokens, as in
              // `# c<newline>h t:$x`, where it blocked the concat.)
              // Prevent recursion on / pattern
              (lexer->lookahead == '/' &&
               (ctx == CTX_PARAMETER_PATTERN_SUBSTITUTE ||
                ctx == CTX_MODIFIER_SUBST)) ||
              (lexer->lookahead == '}' &&
               (in_parameter_expansion(scanner) ||
                get_current_context(scanner) == CTX_COMPOUND)) ||
              // Split subscript out
              (lexer->lookahead == ']' && valid_symbols[CLOSING_BRACKET]) ||
              // In a pattern ${x//$'\e'[0-9]#m/} a '[' after a string
              // is a bracket class, not a subscript ($y[1] is still caught
              // by was_just_variable_name).
              (lexer->lookahead == '[' &&
               ((in_parameter_expansion(scanner) &&
                 !in_pattern_context(scanner)) ||
                was_just_variable_name)) || // Suppress CONCAT after $var when [
              (lexer->lookahead == ':' &&
               was_just_variable_name) || // Suppress CONCAT after $var when :
              (lexer->lookahead == '`' && ctx == CTX_BACKTICK))) {
            // follows
#if DEBUG
            fprintf(stderr, "SCANNER: CONCAT\n");
#endif

            TSSymbol concat_type =
                valid_symbols[CONCAT_REGEX] ? CONCAT_REGEX : CONCAT;
            // So for a`b`, we want to return a concat. We check if the
            // 2nd backtick has whitespace after it, and if it does we
            // return concat.
            if (lexer->lookahead == '`' && ctx != CTX_BACKTICK) {
                lexer->mark_end(lexer);
                advance(lexer);
                bool was_escape = false;
                while ((lexer->lookahead != '`' || was_escape) &&
                       !lexer->eof(lexer)) {
                    advance(lexer);
                    was_escape = false;
                    if (lexer->lookahead == '\\') {
                        was_escape = true;
                    }
                }
                if (lexer->eof(lexer)) {
                    return false;
                }
                // A backtick right after a word piece always continues the
                // word ($p`ls`, "a"`b`c). The end was marked before the
                // backtick, so the concat stays zero-width. (Falling through
                // to the generic path below re-marked the end after the
                // whole `...`, so the concat swallowed it, and with it the
                // newline after: `p="$p"`ls`` then `done` on the next line
                // became one command.)
                lexer->result_symbol = concat_type;
                return true;
            }
            // strings w/ expansions that contains escaped quotes or
            // backslashes need this to return a concat
            if (lexer->lookahead == '\\') {
                lexer->mark_end(lexer);
                advance(lexer);
                // (\` and \$ too: "${b}\`x\`" is an escaped backtick, not
                // the start of a command substitution.)
                if (lexer->lookahead == '"' || lexer->lookahead == '\'' ||
                    lexer->lookahead == '\\' || lexer->lookahead == '`' ||
                    lexer->lookahead == '$') {
                    lexer->result_symbol = concat_type;
                    return true;
                }
                // \( and \) are escaped parens (e.g. find ... \( ... \)).
                // Return false here so tree-sitter rewinds to the mark_end
                // position (before \), letting the internal grammar lexer
                // match \( as a word token via seq('\\', noneOf('\\s')).
                if (lexer->lookahead == '(' || lexer->lookahead == ')') {
                    return false;
                }
                if (lexer->eof(lexer)) {
                    return false;
                }
                // A line continuation right after a word: zsh drops the
                // \<newline>, so if the next line starts with more of a
                // word ('a'\<newline>'b' is the word 'a''b') this is a
                // concatenation. Otherwise (blank, operator,
                // closer) it only separates: decline, so the continuation
                // is skipped as whitespace instead of falling through to
                // the NEWLINE handler, which would end the command.
                // Declining can't be done by returning false: the internal
                // lexer would skip the continuation but can't produce the
                // scanner-only tokens ('$' ...) that may follow. Instead
                // step over it here and carry on as after any blank.
                if (lexer->lookahead == '\r') {
                    advance(lexer);
                }
                if (lexer->lookahead == '\n') {
                    int32_t c;
                    advance(lexer);
                    c = lexer->lookahead;
                    if (!lexer->eof(lexer) && !iswspace(c) && c != ';' &&
                        c != '&' && c != '|' && c != '<' && c != '>' &&
                        c != '(' && c != ')' && c != '}' && c != ']' &&
                        c != '\\') {
                        // The concat covers the continuation, so the next
                        // scan starts at the word's next piece (a '$' there
                        // is a scanner-only token the internal lexer can't
                        // produce after skipping the continuation itself).
                        lexer->mark_end(lexer);
                        lexer->result_symbol = concat_type;
                        return true;
                    }
                    // Skipping the blanks moves the token start past the
                    // '\', the newline and themselves.
                    while (lexer->lookahead == ' ' ||
                           lexer->lookahead == '\t') {
                        skip(lexer);
                    }
                    lexer->mark_end(lexer);
                    skipped_blank_before = true;
                    scan_started_on_blank = true;
                    goto after_concat;
                }
                // In a pattern an escape after a string continues the
                // pattern: ${out//$'\e'\[[0-9;]#m/}. (Elsewhere this path
                // still falls through as before.)
                if (in_pattern_context(scanner)) {
                    lexer->result_symbol = concat_type;
                    return true;
                }
            } else {
                lexer->mark_end(lexer);
                lexer->result_symbol = concat_type;
                return true;
            }
        } else {
#if DEBUG
            fprintf(stderr, "SCANNER: not a CONCAT\n");
#endif
        }
    }
after_concat:

    // End of a modifier chain ($x:h:t, $f:s/a/b/; zero-width, always
    // emitted, so it can also pop CTX_MODIFIER_SUBST). Not before another
    // modifier (':' + a modifier letter, or ':G' after s///) or a
    // substitution's '/'. Where a replacement may start or continue
    // (REGEX_NO_SLASH valid) only a blank or a word-ending character ends
    // it; anywhere else (after :h) any other character does: $x:hfoo.
    if (valid_symbols[SUBST_END] && !in_error_recovery(valid_symbols)) {
        int32_t c = lexer->lookahead;
        bool end;
        lexer->mark_end(lexer);
        if (c == ':') {
            advance(lexer); // peek: the token stays zero-width
            int32_t m = lexer->lookahead;
            if (valid_symbols[MODIFIER_OTHER] && scan_modifier_other(lexer)) {
                return true;
            }
            end = !(m == 'a' || m == 'A' || m == 'c' || m == 'e' || m == 'h' ||
                    m == 'l' || m == 'p' || m == 'P' || m == 'q' || m == 'Q' ||
                    m == 'r' || m == 's' || m == 'g' || m == 't' || m == 'u' ||
                    m == 'x' || m == '&' || m == 'G');
            if (end && valid_symbols[REGEX_NO_SLASH]) {
                end = false; // a ':' starting the replacement: s/x/:/
            }
        } else if (c == '/') {
            end = false;
        } else if (valid_symbols[REGEX_NO_SLASH]) {
            end = lexer->eof(lexer) || iswspace(c) || c == '}' || c == ')' ||
                  c == '"' || c == '`' || c == ';' || c == '|' || c == '&' ||
                  c == ']';
        } else {
            end = true;
        }
        if (end) {
            if (get_current_context(scanner) == CTX_MODIFIER_SUBST) {
                exit_context(scanner, CTX_MODIFIER_SUBST);
            }
            lexer->result_symbol = SUBST_END;
            return true;
        }
        if (c == ':') {
            return false; // the lexer moved: let it lex the ':'
        }
    }

    // Handle string context tracking
    // Quotes. Whether a quote closes is read from the valid symbols, not the
    // context stack: inside "..." a single quote is literal (SINGLE_QUOTE is
    // never valid there), inside '...' DOUBLE_QUOTE is never valid, while at
    // an opening position both are, except directly after '$' ($'...',
    // $"..."). An opening quote the scanner never saw (after a \-newline,
    // lexed by the internal lexer) then simply has no context to pop, which
    // keeps the stack balanced instead of inverting every later string.
    if (valid_symbols[DOUBLE_QUOTE]) {
        // (No '$' exception here: right after '$' SINGLE_QUOTE is valid, so
        // $"..." already opens. The bare-dollar flag can also be stale when
        // the internal lexer produced the token in between, as in "$/".)
        bool closing = !valid_symbols[SINGLE_QUOTE];
        if (!closing) {
            skip_ws_maybe_nl(lexer, valid_symbols);
        }
        if (lexer->lookahead == '"') {
            if (closing) {
                if (get_current_context(scanner) == CTX_STRING) {
                    exit_context(scanner, CTX_STRING);
                }
                was_just_exited_string = scanner->just_exited_string = true;
#if DEBUG
                fprintf(stderr, "SCANNER: Exiting string context\n");
#endif
            } else {
                enter_context(scanner, CTX_STRING);
#if DEBUG
                fprintf(stderr, "SCANNER: Entering string context\n");
#endif
            }
            advance(lexer);
            lexer->mark_end(lexer);
            lexer->result_symbol = DOUBLE_QUOTE;
            return true;
        }
    }

    if (valid_symbols[SINGLE_QUOTE]) {
        bool closing = !valid_symbols[DOUBLE_QUOTE] && !was_just_bare_dollar;
        if (!closing) {
            skip_ws_maybe_nl(lexer, valid_symbols);
        }
        if (lexer->lookahead == '\'') {
            if (closing) {
                if (get_current_context(scanner) == CTX_RAW_STRING) {
                    exit_context(scanner, CTX_RAW_STRING);
                }
                was_just_exited_string = scanner->just_exited_string = true;
#if DEBUG
                fprintf(stderr, "SCANNER: Exiting raw string context\n");
#endif
            } else {
                enter_context(scanner, CTX_RAW_STRING);
#if DEBUG
                fprintf(stderr, "SCANNER: Entering raw string context\n");
#endif
            }
            advance(lexer);
            lexer->mark_end(lexer);
            lexer->result_symbol = SINGLE_QUOTE;
            return true;
        }
    }

    // Handle string context tracking
    if (valid_symbols[BACKTICK]) {
        if (get_current_context(scanner) != CTX_BACKTICK) {
            skip_ws_maybe_nl(lexer, valid_symbols);

            if (lexer->lookahead == '`') {
                // Entering a string context
                enter_context(scanner, CTX_BACKTICK);
#if DEBUG
                fprintf(stderr, "SCANNER: Entering backtick context\n");
#endif
                advance(lexer);
                lexer->mark_end(lexer);
                lexer->result_symbol = BACKTICK;
                return true;
            }
        } else if (lexer->lookahead == '`') {
            // Exiting a string context
            exit_context(scanner, CTX_BACKTICK);

#if DEBUG
            fprintf(stderr, "SCANNER: Exiting backtick context\n");
#endif

            advance(lexer);
            lexer->mark_end(lexer);
            lexer->result_symbol = BACKTICK;
            return true;
        }
    }

#if DEBUG
    fprintf(stderr,
            "DEBUG: scan() start - was_just_bare_dollar=%s, lookahead='%c'\n",
            was_just_bare_dollar ? "true" : "false", lexer->lookahead);
#endif

    // Resolve and absorb newlines when requested
    if (valid_symbols[NEWLINE] && !in_error_recovery(valid_symbols)) {
#if DEBUG
        fprintf(stderr, "SCANNER: NEWLINE handler, lookahead='%c'\n",
                lexer->lookahead);
#endif
        skip_ws(lexer);
        if (lexer->lookahead == '\n') {
            while (iswspace(lexer->lookahead)) {
                skip(lexer);
            }
            was_just_newline = scanner->just_newline = true;
            lexer->mark_end(lexer);
            lexer->result_symbol = NEWLINE;
            return true;
        } else if (lexer->lookahead == '\\') {
            lexer->mark_end(lexer);
            advance(lexer);
            if (lexer->lookahead == '(' || lexer->lookahead == ')') {
                // \( and \) are escaped parens used as word characters
                // (e.g. find ... \( ... \)). Emit as distinct tokens so
                // the grammar can treat them as word tokens.
                bool is_open = lexer->lookahead == '(';
                advance(lexer);
                lexer->mark_end(lexer);
                lexer->result_symbol =
                    is_open ? ESCAPED_OPEN_PAREN : ESCAPED_CLOSE_PAREN;
                return true;
            } else if (lexer->lookahead == '\n') {
                // Line continuation: skip the backslash and newline
                skip(lexer);
                skip_ws(lexer);
            } else {
                // we consumed things we should not have
                lexer->mark_end(lexer);
            }
        }
    }

    // Dedicated context-aware brace handler - handles closing braces for
    // different contexts
    // ${arr[@]:# } has a whitespace pattern. Patterns may also be empty
    // (${x:#}), so '}' is valid here too; don't let this handler skip the
    // blank and close the expansion before the pattern handler sees it.
    bool pattern_may_start =
        (valid_symbols[PATTERN_SUFFIX_START] || valid_symbols[PATTERN_START]) &&
        get_current_context(scanner) == CTX_PARAMETER &&
        (lexer->lookahead == ' ' || lexer->lookahead == '\t');
    if (valid_symbols[CLOSING_BRACE] && !in_error_recovery(valid_symbols) &&
        !pattern_may_start) {
        context_type_t active = get_current_context(scanner);

        skip_wsnl(lexer);
        if (lexer->lookahead == '}') {
#if DEBUG
            fprintf(stderr,
                    "SCANNER: Detected } closing brace, active "
                    "context=%d\n",
                    active);
#endif
            if (active == CTX_PARAMETER ||
                active == CTX_PARAMETER_PATTERN_SUFFIX ||
                active == CTX_PARAMETER_PATTERN_SUBSTITUTE) {
#if DEBUG
                fprintf(stderr,
                        "SCANNER: Exiting parameter expansion context on }\n");
#endif
                exit_context(scanner, active);
                lexer->result_symbol = CLOSING_BRACE;
                advance(lexer);
                lexer->mark_end(lexer);
                return true;
            } else if (active == CTX_BRACE_EXPANSION) {
#if DEBUG
                fprintf(stderr,
                        "SCANNER: Exiting brace expression context on }\n");
#endif
                exit_context(scanner, active);
                lexer->result_symbol = CLOSING_BRACE;
                advance(lexer);
                lexer->mark_end(lexer);
                return true;
            } else if (active == CTX_COMPOUND) {
#if DEBUG
                fprintf(stderr,
                        "SCANNER: Exiting compound expression context on }\n");
#endif
                exit_context(scanner, active);
                lexer->result_symbol = CLOSING_BRACE;
                advance(lexer);
                lexer->mark_end(lexer);
                // A short if going on: `} else` / `} elif` on the same line
                // are one token each (the next '{' opens its own block).
                // On a later line an `else` belongs to an enclosing
                // if ... then ... else.
                if (valid_symbols[BRACE_ELSE] || valid_symbols[BRACE_ELIF]) {
                    while (lexer->lookahead == ' ' ||
                           lexer->lookahead == '\t') {
                        advance(lexer);
                    }
                    if (lexer->lookahead == 'e') {
                        advance(lexer);
                        if (lexer->lookahead == 'l') {
                            advance(lexer);
                            bool is_else = false, is_elif = false;
                            if (lexer->lookahead == 's') {
                                advance(lexer);
                                if (lexer->lookahead == 'e') {
                                    advance(lexer);
                                    is_else = valid_symbols[BRACE_ELSE];
                                }
                            } else if (lexer->lookahead == 'i') {
                                advance(lexer);
                                if (lexer->lookahead == 'f') {
                                    advance(lexer);
                                    is_elif = valid_symbols[BRACE_ELIF];
                                }
                            }
                            int32_t c = lexer->lookahead;
                            bool word_end = lexer->eof(lexer) || iswspace(c) ||
                                            c == '{' || c == '[' || c == '(';
                            if ((is_else || is_elif) && word_end) {
                                lexer->mark_end(lexer);
                                lexer->result_symbol =
                                    is_else ? BRACE_ELSE : BRACE_ELIF;
                            }
                        }
                    }
                }
                return true;
            }

            // Note: CTX_BRACE_EXPANSION closing braces are handled by grammar
            // as token.immediate('}')
        }
    }

    // if (valid_symbols[RAW_DOLLAR] && !in_error_recovery(valid_symbols) &&
    //     scan_raw_dollar(lexer, valid_symbols)) {
    //     return true;
    // }

    // Handle BARE_DOLLAR for parameter expansion: $ followed by {
    if ((valid_symbols[BARE_DOLLAR] || valid_symbols[RAW_DOLLAR] ||
         valid_symbols[LITERAL_DOLLAR]) &&
        !in_error_recovery(valid_symbols)) {
#if DEBUG
        fprintf(stderr,
                "SCANNER: Entering BARE_DOLLAR handler, lookahead='%c'\n",
                lexer->lookahead);
#endif

        // Only skip whitespace if we're starting with whitespace
        // This preserves whitespace significance for concatenation
#if DEBUG
        fprintf(stderr,
                "SCANNER: BARE_DOLLAR whitespace check: "
                "valid_symbols[CONCAT]=%d, lookahead='%c'\n",
                valid_symbols[CONCAT], lexer->lookahead);
#endif
        if (iswspace(lexer->lookahead)) {
            if (!valid_symbols[CONCAT]) {
#if DEBUG
                fprintf(stderr, "SCANNER: BARE_DOLLAR skipping whitespace\n");
#endif
                skip_wsnl(lexer);
            } else if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
                // Skip newlines even in CONCAT context so that expansions on
                // separate lines inside array literals are recognised.
                // Spaces and tabs are intentionally NOT skipped here because
                // they are meaningful for word concatenation.
                skip_wsnl(lexer);
            }
        }
        if (!valid_symbols[CONCAT] && !valid_symbols[CONCAT_REGEX] &&
            get_current_context(scanner) == CTX_TEST && !valid_symbols[REGEX] &&
            !valid_symbols[REGEX_NO_SLASH] && !valid_symbols[REGEX_NO_SPACE] &&
            !valid_symbols[TEST_PATTERN_WORD] &&
            !valid_symbols[TEST_PATTERN_GLOB]) {
            // Also step over line continuations ([[ $a || \<newline>
            // $b ]]): otherwise the internal lexer skips them and then
            // can't produce the '$'. Only in [[ ]], and not where a regex
            // or pattern may start: those (and handlers elsewhere) need to
            // see a leading '\' themselves. A '\' that doesn't start one is
            // consumed by now: finish an escaped paren here, as the
            // NEWLINE handler does, else leave it to the internal lexer.
            while (lexer->lookahead == '\\') {
                skip(lexer);
                if (lexer->lookahead == '\r') {
                    skip(lexer);
                }
                if (lexer->lookahead == '\n') {
                    skip(lexer);
                    skip_wsnl(lexer);
                    continue;
                }
                if ((lexer->lookahead == '(' &&
                     valid_symbols[ESCAPED_OPEN_PAREN]) ||
                    (lexer->lookahead == ')' &&
                     valid_symbols[ESCAPED_CLOSE_PAREN])) {
                    bool is_open = lexer->lookahead == '(';
                    advance(lexer);
                    lexer->mark_end(lexer);
                    lexer->result_symbol =
                        is_open ? ESCAPED_OPEN_PAREN : ESCAPED_CLOSE_PAREN;
                    return true;
                }
                return false;
            }
        }

        if (lexer->lookahead == '$') {
#if DEBUG
            fprintf(stderr, "SCANNER: Found $ character\n");
#endif
            advance(lexer);
            // Special case for ${$} which must be handled with care
            // The / case is for parameter expansion patterns like
            // ${var/pattern/replacement} but should only apply inside parameter
            // expansion context In string context, ?, # and - are special
            // variable names ($?, $#, $-) not bare dollar signs, so RAW_DOLLAR
            // must not be emitted for them there.
            bool in_string = get_current_context(scanner) == CTX_STRING;
            // A '$' that can't start an expansion is a literal character
            // (zsh prints it as is): echo $; "%B$%b" "a$ b" $/ $%. Before
            // the RAW_DOLLAR check, which would take a '$' before a blank
            // in a string, where it's only valid before the closing quote.
            // ('$"' is still RAW_DOLLAR / $"...".)
            int32_t n = lexer->lookahead;
            // (A case pattern's ($) stays an extglob_pattern, below.)
            bool case_pattern_dollar = n == ')' &&
                                       valid_symbols[EXTGLOB_PATTERN] &&
                                       was_after_case_opener;
            if (valid_symbols[LITERAL_DOLLAR] && !case_pattern_dollar &&
                (lexer->eof(lexer) || iswspace(n) || n == ';' || n == '&' ||
                 n == '|' || n == ')' || n == ']' || n == '}' || n == '%' ||
                 n == '/' || n == ',' || n == '.' || n == ':' || n == '<' ||
                 n == '>' || n == '`' || n == '\\')) {
                lexer->mark_end(lexer);
                lexer->result_symbol = LITERAL_DOLLAR;
                was_just_bare_dollar = scanner->just_returned_bare_dollar =
                    false;
                return true;
            }
            if (valid_symbols[RAW_DOLLAR] &&
                (lexer->lookahead == ':' ||
                 (lexer->lookahead == '?' && !in_string) ||
                 lexer->lookahead == '%' ||
                 (lexer->lookahead == '#' && !in_string) ||
                 (lexer->lookahead == '/' && in_parameter_expansion(scanner)) ||
                 lexer->lookahead == '+' ||
                 (lexer->lookahead == '-' && !in_string) ||
                 lexer->lookahead == '}' || lexer->lookahead == '"' ||
                 iswspace(lexer->lookahead) || lexer->eof(lexer))) {
                lexer->mark_end(lexer);
                lexer->result_symbol = RAW_DOLLAR;
                return true;
            }
            // Case pattern ($) / a|$): a '$' with nothing after it is a
            // literal pattern character.
            if (lexer->lookahead == ')' && valid_symbols[EXTGLOB_PATTERN] &&
                was_after_case_opener) {
                lexer->mark_end(lexer);
                lexer->result_symbol = EXTGLOB_PATTERN;
                return true;
            }
            if (lexer->lookahead != '\"') {
                lexer->mark_end(lexer);
                lexer->result_symbol = BARE_DOLLAR;
                was_just_bare_dollar = scanner->just_returned_bare_dollar =
                    true;
                return true;
            }
#if DEBUG
            fprintf(stderr, "SCANNER: Not ${...} pattern, returning false\n");
#endif
        }
#if DEBUG
        fprintf(stderr, "SCANNER: No $ character found, continuing\n");
#endif
    }

    // Must be after BARE_DOLLAR to avoid conflict
    // Handle PEEK_BARE_DOLLAR for concatenation: check if next non-whitespace
    // token is $ without consuming
    if (valid_symbols[PEEK_BARE_DOLLAR] && !in_error_recovery(valid_symbols)) {
#if DEBUG
        fprintf(stderr,
                "SCANNER: Entering PEEK_BARE_DOLLAR handler, lookahead='%c'\n",
                lexer->lookahead);
#endif

        if (lexer->lookahead == '$') {
#if DEBUG
            fprintf(stderr, "SCANNER: PEEK found $ character, returning "
                            "PEEK_BARE_DOLLAR\n");
#endif
            lexer->result_symbol = PEEK_BARE_DOLLAR;
            return true;
        }

#if DEBUG
        fprintf(stderr, "SCANNER: PEEK did not find $ character\n");
#endif
    }

    // Handle BRACE_START - if we're in parameter expansion context, this is
    // part of ${
    if (valid_symbols[BRACE_START] && !in_error_recovery(valid_symbols)) {
        if (lexer->lookahead == '{') {
            if (was_just_bare_dollar) {
                advance(lexer);
                was_just_bare_dollar = scanner->just_returned_bare_dollar =
                    false; // Reset flag
                lexer->result_symbol = BRACE_START;
                lexer->mark_end(lexer);
                // This is ${...} - increment expansion depth
                enter_context(scanner, CTX_PARAMETER);
                return true;
            }
        }
        // If not after $, we may need to consume spaces, newlines, and
        // backslash-newline continuations
        // (e.g. function body brace on its own line after names)
        skip_wsnl_continuation(lexer);
        if (lexer->lookahead == '{') {
            advance(lexer);
            lexer->result_symbol = BRACE_START;
            lexer->mark_end(lexer);
            // This is ${...} - increment expansion depth
            enter_context(scanner, CTX_COMPOUND);
            return true;
        }
    }

    // Alternative after a case-item '|' that is a group:
    // erase|(luks(AddKey|Erase)|isLuks)). saw_pipe_in_case is set by the
    // extglob handler when it stops a value at a top-level '|'. There the
    // '(' can't be the optional opener, so it always starts a group.
    // Same right after the case item's own '(': ((#b)x(*)) has the opener
    // and then a group.
    // A value starting with a bracket class there is read whole as well:
    // ([[:alnum:]]#[" "$TAB]#(\?|)=*) has quotes and $ inside the class.
    // Also directly after another piece of a case pattern (no blank), e.g.
    // after the expansion in (#bi)${~dspat}((19|20)..)[-/]...: in case
    // patterns ( is always a group (zshexpn: qualifiers only exist in
    // filename generation), so this must not become an arithmetic "((".
    bool case_pattern_continues =
        valid_symbols[EXTGLOB_PATTERN] && !valid_symbols[ESAC] &&
        !scan_started_on_blank &&
        get_current_context(scanner) != CTX_ARITHMETIC &&
        get_current_context(scanner) != CTX_TEST;
    if (valid_symbols[EXTGLOB_PATTERN] &&
        (scanner->saw_pipe_in_case || was_after_case_opener) &&
        !valid_symbols[OPENING_PAREN] && !in_error_recovery(valid_symbols) &&
        lexer->lookahead == '[') {
        scanner->saw_pipe_in_case = false;
        lexer->mark_end(lexer);
        if (!scan_bracket_class(lexer)) {
            return false; // unclosed: lexer moved, let the internal lexer try
        }
        return scan_case_pattern_rest(lexer, 0);
    }
    if (valid_symbols[EXTGLOB_PATTERN] &&
        (scanner->saw_pipe_in_case || was_after_case_opener ||
         case_pattern_continues) &&
        (!valid_symbols[OPENING_PAREN] || case_pattern_continues) &&
        !in_error_recovery(valid_symbols) && lexer->lookahead == '(') {
        scanner->saw_pipe_in_case = false;
        if (scan_case_group_pattern(lexer, true)) {
            return true;
        }
        return false; // unclosed: lexer moved, let the internal lexer try
    }

    // Case item that starts with a group: (free|net)bsd*) (a|b):*)
    // An item start is recognised by ESAC being valid, or by the scan
    // starting on a blank (an item always follows `in ` or a newline). ESAC
    // alone isn't enough: after a reused case_item (incremental parsing) the
    // same position is lexed in a state where ESAC isn't valid, and the
    // token must not depend on that.
    if (valid_symbols[EXTGLOB_PATTERN] &&
        (valid_symbols[ESAC] || scan_started_on_blank) &&
        valid_symbols[OPENING_PAREN] && !in_error_recovery(valid_symbols)) {
        skip_wsnl(lexer);
        if (lexer->lookahead == '(') {
            if (scan_case_group_pattern(lexer, false)) {
                return true;
            }
            // Opener: same token and context push as the OPENING_PAREN path
            was_just_bare_dollar = scanner->just_returned_bare_dollar = false;
            enter_context(scanner, CTX_COMMAND);
            scanner->after_case_opener = true;
            lexer->result_symbol = OPENING_PAREN;
            return true;
        }
    }

    // Handle OPENING_PAREN after BARE_DOLLAR
    // Inside "..." a '(' is only syntax right after '$' ($( $(( ); otherwise
    // it's string content: "((${x})|)" must not become an arithmetic "((".
    if ((valid_symbols[OPENING_PAREN] || valid_symbols[DOUBLE_OPENING_PAREN] ||
         valid_symbols[ZSH_EXTENDED_GLOB_FLAGS]) &&
        !in_error_recovery(valid_symbols) &&
        (was_just_bare_dollar || get_current_context(scanner) != CTX_STRING)) {
        // Blank before the '(' means it starts a word ((#q)foo: flags);
        // directly after pattern text it can be a qualifier (*(#qN)).
        // Earlier handlers may already have skipped the blank, so use the
        // lookahead captured on entry to scan().
        bool paren_after_blank = scan_started_on_blank;
        skip_ws(lexer);
        // If a regex is allowed, only proceed with analysis if a bare_dollar
        // preceedes
        if (lexer->lookahead == '(' &&
            (valid_symbols[OPENING_PAREN] &&
             (was_just_bare_dollar || !valid_symbols[REGEX_NO_SPACE]))) {
            advance(lexer);
            lexer->mark_end(lexer);

            // $a[(i)pat]: try subscript flags. If they don't match, the
            // marked token is still the single '(' and the OPENING_PAREN
            // handling below applies unchanged.
            if (!was_just_bare_dollar &&
                (valid_symbols[SUBSCRIPT_FLAGS] ||
                 valid_symbols[SUBSCRIPT_PATTERN_FLAGS]) &&
                is_subscript_flag_start(lexer->lookahead)) {
                if (scan_subscript_flags(lexer, valid_symbols)) {
                    return true;
                }
                was_just_bare_dollar = scanner->just_returned_bare_dollar =
                    false;
                enter_context(scanner, CTX_COMMAND);
                lexer->result_symbol = OPENING_PAREN;
                return true;
            }

            if (was_just_bare_dollar) {
#if DEBUG
                fprintf(stderr, "SCANNER: Detected OPENING_PAREN after "
                                "BARE_DOLLAR\n");
#endif
                if (lexer->lookahead == '(' &&
                    valid_symbols[DOUBLE_OPENING_PAREN]) {
                    advance(lexer);
                    lexer->mark_end(lexer);
                    // This is $((...)) - increment arithmetic depth
                    was_just_bare_dollar = scanner->just_returned_bare_dollar =
                        false; // Reset flag
                    enter_context(scanner, CTX_ARITHMETIC);
                    lexer->result_symbol = DOUBLE_OPENING_PAREN;
                    return true;
                } else if (valid_symbols[OPENING_PAREN]) {
                    // This is $(...) - increment command_depth
                    was_just_bare_dollar = scanner->just_returned_bare_dollar =
                        false; // Reset flag
                    enter_context(scanner, CTX_COMMAND);
                    lexer->result_symbol = OPENING_PAREN;
                    return true;
                }
            } else if (lexer->lookahead == '(' &&
                       valid_symbols[DOUBLE_OPENING_PAREN] &&
                       // [[ a || (( -n $x ) && b ) ]]: inside [[ ]] "((" is
                       // two nested groups, not arithmetic. Same inside
                       // arithmetic: (( x + ((a == 1) ? b : c) )).
                       get_current_context(scanner) != CTX_TEST &&
                       get_current_context(scanner) != CTX_ARITHMETIC) {
                advance(lexer);
                lexer->mark_end(lexer);
                // This is ((...)) - increment arithmetic depth
                enter_context(scanner, CTX_ARITHMETIC);
                lexer->result_symbol = DOUBLE_OPENING_PAREN;
                return true;
            } else if ((valid_symbols[OPENING_PAREN] ||
                        valid_symbols[ZSH_EXTENDED_GLOB_FLAGS]) &&
                       !valid_symbols[REGEX_NO_SPACE]) {
                // Handle ZSH_EXTENDED_GLOB_FLAGS - (#flags) patterns
                if (lexer->lookahead == '#' &&
                    valid_symbols[ZSH_EXTENDED_GLOB_FLAGS]) {
                    advance(lexer);
                    // *(#qN): a glob qualifier, not glob flags. The token is
                    // the '(' already marked; zsh_glob_qualifier takes the
                    // rest ('#q' body ')').
                    if (lexer->lookahead == 'q' && !paren_after_blank &&
                        valid_symbols[OPENING_PAREN]) {
                        was_just_bare_dollar =
                            scanner->just_returned_bare_dollar = false;
                        enter_context(scanner, CTX_COMMAND);
                        lexer->result_symbol = OPENING_PAREN;
                        return true;
                    }

                    // Check for valid flag characters
                    bool found_flags = false;
                    while (
                        lexer->lookahead &&
                        (iswalnum(lexer->lookahead) ||
                         lexer->lookahead == '.' || lexer->lookahead == 'i' ||
                         lexer->lookahead == 'q' || lexer->lookahead == 'b' ||
                         lexer->lookahead == 'm' || lexer->lookahead == 'n' ||
                         lexer->lookahead == 's' || lexer->lookahead == 'B' ||
                         lexer->lookahead == 'I' || lexer->lookahead == 'N' ||
                         lexer->lookahead == 'U' || lexer->lookahead == 'X' ||
                         lexer->lookahead == 'c' || lexer->lookahead == 'e' ||
                         lexer->lookahead == 'l' || lexer->lookahead == 'f' ||
                         lexer->lookahead == 'a' || lexer->lookahead == 'C' ||
                         lexer->lookahead == 'o')) {
                        found_flags = true;
                        advance(lexer);
                    }

                    if (found_flags && lexer->lookahead == ')') {
                        advance(lexer);
                        lexer->mark_end(lexer);
                        lexer->result_symbol = ZSH_EXTENDED_GLOB_FLAGS;
                        return true;
                    }

                    // If we get here, it's not a valid glob flags pattern
                    // Reset and let other tokens handle it
                    return false;
                }
                if (valid_symbols[OPENING_PAREN]) {
#if DEBUG
                    fprintf(stderr, "SCANNER: Detected OPENING_PAREN NOT AFTER "
                                    "BARE_DOLLAR\n");
#endif
                    was_just_bare_dollar = scanner->just_returned_bare_dollar =
                        false; // Reset flag
                    lexer->mark_end(lexer);
                    // Inside arithmetic a bare '(' is a grouping paren:
                    // stay in arithmetic ((( (a + b) * c ))).
                    enter_context(scanner,
                                  get_current_context(scanner) == CTX_ARITHMETIC
                                      ? CTX_ARITHMETIC
                                      : CTX_COMMAND);
                    lexer->result_symbol = OPENING_PAREN;
                    return true;
                }
            }
        }
    }

    // In a string "$0 [ -q ]" a '[' after a blank is string content, not a
    // subscript. Earlier handlers may already have skipped the blank, so use
    // the lookahead captured on entry.
    // (TEST_COMMAND_START is never valid inside a real string; requiring
    // that keeps a stale CTX_STRING on the stack from blocking `if [[`.)
    bool bracket_after_blank_in_string =
        scan_started_on_blank && get_current_context(scanner) == CTX_STRING &&
        !valid_symbols[TEST_COMMAND_START];
    if ((valid_symbols[OPENING_BRACKET] || valid_symbols[TEST_COMMAND_START]) &&
        !in_error_recovery(valid_symbols) && !bracket_after_blank_in_string) {
#if DEBUG
        fprintf(stderr,
                "DEBUG: CHECKING TEST_COMMAND_START=%d OPENING_BRACKET=%d "
                "lookahead=%c\n",
                valid_symbols[TEST_COMMAND_START],
                valid_symbols[OPENING_BRACKET], lexer->lookahead);
#endif
        skip_wsnl(lexer);
#if DEBUG
        fprintf(stderr,
                "DEBUG: CHECKING TEST_COMMAND_START=%d OPENING_BRACKET=%d "
                "lookahead=%c\n",
                valid_symbols[TEST_COMMAND_START],
                valid_symbols[OPENING_BRACKET], lexer->lookahead);
#endif
        if (lexer->lookahead == '[') {
            advance(lexer);

            if (lexer->lookahead == '[' && valid_symbols[TEST_COMMAND_START]) {
                advance(lexer);
                was_just_bare_dollar = scanner->just_returned_bare_dollar =
                    false; // Reset flag
                lexer->result_symbol = TEST_COMMAND_START;
                lexer->mark_end(lexer);

#if DEBUG
                fprintf(stderr, "DEBUG: Detected TEST_COMMAND_START [[\n");
#endif
                // Enter test command context
                enter_context(scanner, CTX_TEST);
                return true;
            } else if (was_just_bare_dollar && valid_symbols[OPENING_BRACKET]) {
                // This is $[
                was_just_bare_dollar = scanner->just_returned_bare_dollar =
                    false; // Reset flag
                lexer->result_symbol = OPENING_BRACKET;
                lexer->mark_end(lexer);

#if DEBUG
                fprintf(stderr, "DEBUG: Detected OPENING_BRACKET $[\n");
#endif
                enter_context(scanner, CTX_ARITHMETIC);
                return true;
            } else if (valid_symbols[OPENING_BRACKET] &&
                       !valid_symbols[REGEX_NO_SPACE]) {
                // This is single [
                was_just_bare_dollar = scanner->just_returned_bare_dollar =
                    false; // Reset flag
                lexer->result_symbol = OPENING_BRACKET;
                lexer->mark_end(lexer);

#if DEBUG
                fprintf(stderr, "DEBUG: Detected OPENING_BRACKET [\n");
#endif
                // Its own context: subscripts aren't arithmetic expressions
                // (a[foo::bar], a[${x}:${y}]); $[ ... ] stays CTX_ARITHMETIC.
                enter_context(scanner, CTX_SUBSCRIPT);
                return true;
            }
        }
    }

    // Handle TEST_COMMAND_END ]]
    if ((valid_symbols[TEST_COMMAND_END] || valid_symbols[CLOSING_BRACKET]) &&
        !in_error_recovery(valid_symbols)) {
        skip_ws(lexer);
        if (lexer->lookahead == ']') {
            advance(lexer);
            if (lexer->lookahead == ']' && valid_symbols[TEST_COMMAND_END]) {
                advance(lexer);
                lexer->result_symbol = TEST_COMMAND_END;
                lexer->mark_end(lexer);

#if DEBUG
                fprintf(stderr, "DEBUG: Detected TEST_COMMAND_END ]]\n");
#endif
                // Exit test command context. Pop down to (and including)
                // the innermost CTX_TEST: anything above it was leaked
                // inside the [[ ]] (e.g. a string whose opening quote came
                // after a \-newline and bypassed the scanner), and leaving
                // CTX_TEST behind confuses later context checks.
                exit_context_through(scanner, CTX_TEST);
                return true;
            } else if (valid_symbols[CLOSING_BRACKET]) {
                // This is single ]
                was_just_bare_dollar = scanner->just_returned_bare_dollar =
                    false; // Reset flag
                lexer->result_symbol = CLOSING_BRACKET;
                lexer->mark_end(lexer);

#if DEBUG
                fprintf(stderr, "DEBUG: Detected CLOSING_BRACKET ]\n");
#endif
                exit_context(scanner,
                             get_current_context(scanner) == CTX_SUBSCRIPT
                                 ? CTX_SUBSCRIPT
                                 : CTX_ARITHMETIC);
                return true;
            }
            // If only one ], don't consume it - let normal parsing handle it
            return false;
        }
    }

    if ((valid_symbols[CLOSING_PAREN] || valid_symbols[CLOSING_DOUBLE_PAREN]) &&
        !in_error_recovery(valid_symbols)) {
        skip_ws_maybe_nl(lexer, valid_symbols);
        // A line continuation before the ')' (a=( x \<newline> )): step
        // over it, or the internal lexer skips it as an extra and produces
        // the ')' itself, leaving the context pushed at '(' open. Having
        // consumed the '\', finish any escaped paren here too (as the
        // NEWLINE handler does); any other escape is a word character for
        // the internal lexer, so decline.
        while (lexer->lookahead == '\\') {
            skip(lexer);
            if (lexer->lookahead == '\r') {
                skip(lexer);
            }
            if (lexer->lookahead == '\n') {
                skip(lexer);
                skip_ws_maybe_nl(lexer, valid_symbols);
                continue;
            }
            if ((lexer->lookahead == '(' &&
                 valid_symbols[ESCAPED_OPEN_PAREN]) ||
                (lexer->lookahead == ')' &&
                 valid_symbols[ESCAPED_CLOSE_PAREN])) {
                bool is_open = lexer->lookahead == '(';
                advance(lexer);
                lexer->mark_end(lexer);
                lexer->result_symbol =
                    is_open ? ESCAPED_OPEN_PAREN : ESCAPED_CLOSE_PAREN;
                return true;
            }
            return false;
        }
        if (lexer->lookahead == ')') {
            advance(lexer);

            if (lexer->lookahead == ')' &&
                valid_symbols[CLOSING_DOUBLE_PAREN]) {
                advance(lexer);
                lexer->result_symbol = CLOSING_DOUBLE_PAREN;
                lexer->mark_end(lexer);

                // Exit test command context
                exit_context(scanner, CTX_ARITHMETIC);
                return true;
            } else if (valid_symbols[CLOSING_PAREN]) {
                // This is single )
                lexer->result_symbol = CLOSING_PAREN;
                lexer->mark_end(lexer);

                // Exit relevant context

                if (get_current_context(scanner) == CTX_COMMAND) {
                    exit_context(scanner, CTX_COMMAND);
                } else if (get_current_context(scanner) == CTX_ARITHMETIC) {
                    exit_context(scanner, CTX_ARITHMETIC);
                }
                return true;
            }
            // If only one ], don't consume it - let normal parsing handle it
            return false;
        }
    }

    // Handle PATTERN_START - emitted after pattern operators in parameter
    // expansions
    if (valid_symbols[PATTERN_START] && !in_error_recovery(valid_symbols)) {
        // Chained substitutions ${x:s/a/b/:s/c/d/}: the first one left us in
        // the substitute context already; emit again and stay there.
        if (get_current_context(scanner) == CTX_PARAMETER_PATTERN_SUBSTITUTE &&
            lexer->lookahead != '}') {
            lexer->result_symbol = PATTERN_START;
            lexer->mark_end(lexer);
            return true;
        }
        if (get_current_context(scanner) == CTX_PARAMETER &&
            lexer->lookahead !=
                '}') { // Don't emit if expansion is about to end

            // Determine pattern context based on what type of pattern we're
            // entering
#if DEBUG
            fprintf(stderr,
                    "DEBUG: PATTERN_START emitting for substitution, "
                    "lookahead='%c'\n",
                    lexer->lookahead);
#endif
            exit_context(scanner, CTX_PARAMETER);
            enter_context(scanner, CTX_PARAMETER_PATTERN_SUBSTITUTE);
#if DEBUG
            fprintf(stderr, "DEBUG: Context after transition: %d\n",
                    get_current_context(scanner));
#endif
            lexer->result_symbol = PATTERN_START;
            lexer->mark_end(lexer);
            return true;
        }
        // $f:s/a/b/ outside ${...}: its own context, popped by the
        // SUBST_END that always ends the modifier.
        if (!in_parameter_expansion(scanner) && lexer->lookahead != '/' &&
            !iswspace(lexer->lookahead) && !lexer->eof(lexer)) {
            // One context per modifier chain ($w:gs/a/b/:gs/c/d/): the
            // chain's single SUBST_END pops it.
            if (get_current_context(scanner) != CTX_MODIFIER_SUBST) {
                enter_context(scanner, CTX_MODIFIER_SUBST);
            }
            lexer->result_symbol = PATTERN_START;
            lexer->mark_end(lexer);
            return true;
        }
    }

    // Handle PATTERN_SUFFIX_START - emitted after pattern operators in
    // parameter expansions
    if (valid_symbols[PATTERN_SUFFIX_START] &&
        !in_error_recovery(valid_symbols)) {
        if (get_current_context(scanner) == CTX_PARAMETER &&
            lexer->lookahead !=
                '}') { // Don't emit if expansion is about to end

            // Determine pattern context based on what type of pattern we're
            // entering % # patterns are suffix/prefix removal
#if DEBUG
            fprintf(stderr,
                    "DEBUG: PATTERN_START emitting for suffix/prefix, "
                    "lookahead='%c'\n",
                    lexer->lookahead);
#endif

            exit_context(scanner, CTX_PARAMETER);
            enter_context(scanner, CTX_PARAMETER_PATTERN_SUFFIX);
#if DEBUG
            fprintf(stderr, "DEBUG: Context after transition: %d\n",
                    get_current_context(scanner));
#endif
            lexer->result_symbol = PATTERN_SUFFIX_START;
            lexer->mark_end(lexer);
            return true;
        }
    }

    // Handle hash operations in parameter expansion context
    if (in_parameter_expansion(scanner) && lexer->lookahead == '#' &&
        (valid_symbols[HASH_PATTERN] || valid_symbols[DOUBLE_HASH_PATTERN]) &&
        !in_error_recovery(valid_symbols)) {
#if DEBUG
        fprintf(stderr, "SCANNER: Hash operation detected\n");
#endif
        advance(lexer); // consume first #

        if (lexer->lookahead == '#') {
            // Double hash: ##pattern
            if (valid_symbols[DOUBLE_HASH_PATTERN]) {
#if DEBUG
                fprintf(stderr, "SCANNER: Returning DOUBLE_HASH_PATTERN\n");
#endif
                advance(lexer); // consume second #
                lexer->result_symbol = DOUBLE_HASH_PATTERN;
                lexer->mark_end(lexer);
                return true;
            }
        } else {
            // Single hash: #pattern
            if (valid_symbols[HASH_PATTERN]) {
#if DEBUG
                fprintf(stderr, "SCANNER: Returning HASH_PATTERN\n");
#endif
                lexer->result_symbol = HASH_PATTERN;
                lexer->mark_end(lexer);
                return true;
            }
        }
#if DEBUG
        fprintf(stderr, "SCANNER: Hash operation not matched\n");
#endif
        return false;
    }

    // Array operators: ${var[*]} and ${var[@]}
    if ((valid_symbols[ARRAY_STAR_TOKEN] || valid_symbols[ARRAY_AT_TOKEN]) &&
        !in_error_recovery(valid_symbols)) {
        if (lexer->lookahead == '*' && valid_symbols[ARRAY_STAR_TOKEN] &&
            !valid_symbols[REGEX] && !valid_symbols[REGEX_NO_SLASH] &&
            !valid_symbols[REGEX_NO_SPACE]) {
            lexer->result_symbol = ARRAY_STAR_TOKEN;
            advance(lexer);
            lexer->mark_end(lexer);
            return true;
        }
        if (lexer->lookahead == '@' && valid_symbols[ARRAY_AT_TOKEN]) {
            lexer->result_symbol = ARRAY_AT_TOKEN;
            advance(lexer);
            lexer->mark_end(lexer);
            return true;
        }
    }

    if (valid_symbols[EMPTY_VALUE]) {
        if (iswspace(lexer->lookahead) || lexer->eof(lexer) ||
            lexer->lookahead == ';' || lexer->lookahead == '&' ||
            lexer->lookahead == '}') {
            lexer->mark_end(lexer);
            lexer->result_symbol = EMPTY_VALUE;
            return true;
        }
    }

    if ((valid_symbols[HEREDOC_BODY_BEGINNING] ||
         valid_symbols[SIMPLE_HEREDOC_BODY]) &&
        scanner->heredocs.size > 0 &&
        !array_back(&scanner->heredocs)->started &&
        !in_error_recovery(valid_symbols)) {
        return scan_heredoc_content(scanner, lexer, HEREDOC_BODY_BEGINNING,
                                    SIMPLE_HEREDOC_BODY);
    }

    if (valid_symbols[HEREDOC_END] && scanner->heredocs.size > 0) {
        Heredoc *heredoc = array_back(&scanner->heredocs);
        if (scan_heredoc_end_identifier(heredoc, lexer)) {
            array_delete(&heredoc->current_leading_word);
            array_delete(&heredoc->delimiter);
            array_pop(&scanner->heredocs);
            lexer->result_symbol = HEREDOC_END;
            return true;
        }
    }

    if (valid_symbols[HEREDOC_CONTENT] && scanner->heredocs.size > 0 &&
        array_back(&scanner->heredocs)->started &&
        !in_error_recovery(valid_symbols)) {
        return scan_heredoc_content(scanner, lexer, HEREDOC_CONTENT,
                                    HEREDOC_END);
    }

    if (valid_symbols[HEREDOC_START] && !in_error_recovery(valid_symbols) &&
        scanner->heredocs.size > 0) {
#if DEBUG
        fprintf(stderr,
                "DEBUG: HEREDOC_START check - heredocs.size=%u, "
                "in_error_recovery=%s\n",
                scanner->heredocs.size,
                in_error_recovery(valid_symbols) ? "true" : "false");
#endif
        return scan_heredoc_start(array_back(&scanner->heredocs), lexer);
    }

    if (valid_symbols[TEST_OPERATOR] && !valid_symbols[EXPANSION_WORD]) {
        skip_ws(lexer);
        if (lexer->lookahead == '\\') {
            if (valid_symbols[EXTGLOB_PATTERN]) {
                goto extglob_pattern;
            }
            if (valid_symbols[REGEX_NO_SPACE]) {
                goto regex;
            }
            skip(lexer);

            if (lexer->eof(lexer)) {
                return false;
            }

            if (lexer->lookahead == '\r') {
                skip(lexer);
                if (lexer->lookahead == '\n') {
                    skip(lexer);
                }
            } else if (lexer->lookahead == '\n') {
                skip(lexer);
            } else {
                return false;
            }

            while (iswspace(lexer->lookahead)) {
                skip(lexer);
            }
        }

        if (lexer->lookahead == '\n' && !valid_symbols[NEWLINE]) {
            skip(lexer);

            while (iswspace(lexer->lookahead)) {
                skip(lexer);
            }
        }

        if (lexer->lookahead == '-') {
            advance(lexer);

            bool advanced_once = false;
            while (iswalpha(lexer->lookahead)) {
                advanced_once = true;
                advance(lexer);
            }

            if (iswspace(lexer->lookahead) && advanced_once) {
                lexer->mark_end(lexer);
                advance(lexer);
                context_type_t ctx = get_current_context(scanner);
                if (lexer->lookahead == '}' &&
                    (ctx == CTX_PARAMETER ||
                     ctx == CTX_PARAMETER_PATTERN_SUFFIX ||
                     ctx == CTX_PARAMETER_PATTERN_SUBSTITUTE)) {
                    if (valid_symbols[EXPANSION_WORD]) {
                        lexer->mark_end(lexer);
                        lexer->result_symbol = EXPANSION_WORD;
                        return true;
                    }
                    return false;
                }
                lexer->result_symbol = TEST_OPERATOR;
                return true;
            }
            if (iswspace(lexer->lookahead) && valid_symbols[EXTGLOB_PATTERN]) {
                lexer->result_symbol = EXTGLOB_PATTERN;
                return true;
            }
        }

        if (valid_symbols[RAW_DOLLAR] && !in_error_recovery(valid_symbols) &&
            scan_raw_dollar(lexer, valid_symbols)) {
            return true;
        }
    }

    if (valid_symbols[SIMPLE_VARIABLE_NAME] &&
        !in_error_recovery(valid_symbols)) {
        bool in_param_expand = in_parameter_expansion_context(scanner);

#if DEBUG
        fprintf(stderr, "SCANNER: trying SIMPLE_VARIABLE_NAME\n");
#endif

        context_type_t cur_ctx = get_current_context(scanner);
        skip_ws(lexer);
        if (iswalpha(lexer->lookahead) || lexer->lookahead == '_' ||
            (lexer->lookahead == ':' &&
             (cur_ctx == CTX_ARITHMETIC || cur_ctx == CTX_SUBSCRIPT))) {
            int consumed = 0;
            while (iswalnum(lexer->lookahead) || lexer->lookahead == '_' ||
                   (lexer->lookahead == ':' &&
                    (cur_ctx == CTX_ARITHMETIC || cur_ctx == CTX_SUBSCRIPT))) {
                advance(lexer);
                consumed++;
            }

            if (consumed > 0) {
                lexer->mark_end(lexer);
                was_just_bare_dollar = scanner->just_returned_bare_dollar =
                    false;
                note_variable_name(scanner, lexer);
                lexer->result_symbol = SIMPLE_VARIABLE_NAME;
#if DEBUG
                fprintf(stderr, "SCANNER: SIMPLE_VARIABLE_NAME found\n");
#endif
                return true;
            }
        }
    }

    if (valid_symbols[SPECIAL_VARIABLE_NAME] &&
        !in_error_recovery(valid_symbols)) {
        // '*', '@', '?', '!', '#', '-', '$', '0', '_'
        skip_ws(lexer);
        bool in_param_expand = in_parameter_expansion_context(scanner);
#if DEBUG
        if (in_param_expand) {
            if (lexer->lookahead == '!' || lexer->lookahead == '#')
                fprintf(stderr, "SCANNER: skipping flag chars as part of "
                                "SPECIAL_VARIABLE_NAME\n");
        }
#endif
        // ${#} is the argument count and ${##} the length of $#: inside
        // ${...} a '#' directly before '}' is the special parameter. Any
        // other '#' there is the length operator (${#x}, first # of ${##}),
        // left to the grammar's expansion_style.
        if (lexer->lookahead == '#' && in_param_expand) {
            advance(lexer);
            if (lexer->lookahead == '}') {
                lexer->mark_end(lexer);
                was_just_bare_dollar = scanner->just_returned_bare_dollar =
                    false;
                note_variable_name(scanner, lexer);
                lexer->result_symbol = SPECIAL_VARIABLE_NAME;
                return true;
            }
            return false; // lexer moved: only skips handlers for this spot
        }
        if (lexer->lookahead == '*' || lexer->lookahead == '@' ||
            lexer->lookahead == '?' || lexer->lookahead == '-' ||
            (lexer->lookahead == '!' && !in_param_expand) ||
            (lexer->lookahead == '#' && !in_param_expand) ||
            lexer->lookahead == '$' || lexer->lookahead == '_' ||
            iswdigit(lexer->lookahead)) {
            bool was_hash = lexer->lookahead == '#';
            advance(lexer);
            // $#name is the length of name ($#f, $#array), not $# followed
            // by a word: let the grammar's expansion_style '#' take it, as
            // for $+name. (Returning false after the advance only skips the
            // handlers below for this position.)
            // Same for $#* and $#@ (the number of positional parameters).
            if (was_hash && was_just_bare_dollar &&
                (iswalpha(lexer->lookahead) || lexer->lookahead == '_' ||
                 lexer->lookahead == '*' || lexer->lookahead == '@')) {
                return false;
            }
            // Consume additional digits for multi-digit positional params
            // (${10}, ${10:-}, etc.)
            while (iswdigit(lexer->lookahead)) {
                advance(lexer);
            }
            lexer->mark_end(lexer);
            was_just_bare_dollar = scanner->just_returned_bare_dollar = false;
            note_variable_name(scanner, lexer);
            lexer->result_symbol = SPECIAL_VARIABLE_NAME;
            return true;
        }
    }

    if ((valid_symbols[VARIABLE_NAME] || valid_symbols[FILE_DESCRIPTOR] ||
         valid_symbols[HEREDOC_ARROW]) &&
        !valid_symbols[REGEX_NO_SLASH] && !in_error_recovery(valid_symbols)) {
        for (;;) {
            if ((lexer->lookahead == ' ' || lexer->lookahead == '\t' ||
                 lexer->lookahead == '\r' ||
                 (lexer->lookahead == '\n' && !valid_symbols[NEWLINE])) &&
                !valid_symbols[EXPANSION_WORD] && !valid_symbols[CONCAT]) {
#if DEBUG
                fprintf(stderr, "SCANNER: VARIABLE_NAME skipped ws\n");
#endif
                // Only skip whitespace if CONCAT is not valid
                skip(lexer);
            } else if (lexer->lookahead == '\\') {
                skip(lexer);

                if (lexer->eof(lexer)) {
                    lexer->mark_end(lexer);
                    was_just_bare_dollar = scanner->just_returned_bare_dollar =
                        false; // Reset flag
#if DEBUG
                    fprintf(stderr, "SCANNER: VARIABLE_NAME after \\\n");
#endif
                    lexer->result_symbol = VARIABLE_NAME;
                    if (in_parameter_expansion(scanner)) {
                        note_variable_name(scanner, lexer);
                    }
                    return true;
                }

                if (lexer->lookahead == '\r') {
                    skip(lexer);
                }
                if (lexer->lookahead == '\n') {
                    skip(lexer);
                } else {
                    if (lexer->lookahead == '\\' &&
                        valid_symbols[EXPANSION_WORD]) {
                        goto expansion_word;
                    }
                    return false;
                }
            } else {
                break;
            }
        }

        context_type_t ctx = get_current_context(scanner);
        // Allow a single digit (e.g. '0', '1') as a VARIABLE_NAME when
        // immediately followed by '=' or '[' so that assignments like
        // 0=${(%):-%N} are parsed correctly.  Only do this when VARIABLE_NAME
        // is actually valid (not for FILE_DESCRIPTOR or HEREDOC_ARROW paths).
        if (valid_symbols[VARIABLE_NAME] && !valid_symbols[EXPANSION_WORD] &&
            iswdigit(lexer->lookahead)) {
            uint32_t digit = lexer->lookahead;
            lexer->mark_end(lexer);
            advance(lexer);
            if (lexer->lookahead == '=' || lexer->lookahead == '[') {
                lexer->mark_end(lexer);
#if DEBUG
                fprintf(stderr, "SCANNER: VARIABLE_NAME single-digit %c=\n",
                        (char)digit);
#endif
                (void)digit;
                lexer->result_symbol = VARIABLE_NAME;
                was_just_bare_dollar = scanner->just_returned_bare_dollar =
                    false;
                if (in_parameter_expansion(scanner)) {
                    note_variable_name(scanner, lexer);
                }
                return true;
            }
            // Digit not followed by = or [ — fall through to let the normal
            // VARIABLE_NAME loop or FILE_DESCRIPTOR handler deal with it.
            // Reset so the rejection block below can work as usual.
        }

        // no '*', '@', '?', '-', '$', '0', '_', '#'
        if (!valid_symbols[EXPANSION_WORD] &&
            (lexer->lookahead == '*' || lexer->lookahead == '@' ||
             lexer->lookahead == '?' || lexer->lookahead == '-' ||
             lexer->lookahead == '0' || lexer->lookahead == '_' ||
             lexer->lookahead == '#' || lexer->lookahead == '$')) {
            lexer->mark_end(lexer);
            advance(lexer);
            if (lexer->lookahead == '=' || lexer->lookahead == '[' ||
                (lexer->lookahead == ':' &&
                 (ctx == CTX_ARITHMETIC || ctx == CTX_SUBSCRIPT)) ||
                lexer->lookahead == '-' || lexer->lookahead == '%' ||
                lexer->lookahead == '/') {
                return false;
            }
            if (valid_symbols[EXTGLOB_PATTERN] && iswspace(lexer->lookahead)) {
                lexer->mark_end(lexer);
                lexer->result_symbol = EXTGLOB_PATTERN;
                return true;
            }
        }

        if ((valid_symbols[HEREDOC_ARROW] ||
             valid_symbols[HEREDOC_ARROW_DASH] || valid_symbols[HERESTRING]) &&
            lexer->lookahead == '<') {
            advance(lexer);
            if (lexer->lookahead == '<') {
                advance(lexer);
                if (lexer->lookahead == '-') {
                    advance(lexer);
                    Heredoc heredoc = heredoc_new();
                    heredoc.allows_indent = true;
                    array_push(&scanner->heredocs, heredoc);
#if DEBUG
                    fprintf(stderr,
                            "DEBUG: HEREDOC_ARROW_DASH - added heredoc, size "
                            "now=%u\n",
                            scanner->heredocs.size);
#endif
                    lexer->result_symbol = HEREDOC_ARROW_DASH;
                } else if (lexer->lookahead == '<') {
                    advance(lexer);
#if DEBUG
                    fprintf(stderr, "DEBUG: HERESTRING");
#endif
                    lexer->result_symbol = HERESTRING;
                    return true;
                } else if (lexer->lookahead == '=') {
                    return false;
                } else {
                    Heredoc heredoc = heredoc_new();
                    array_push(&scanner->heredocs, heredoc);
#if DEBUG
                    fprintf(
                        stderr,
                        "DEBUG: HEREDOC_ARROW - added heredoc, size now=%u\n",
                        scanner->heredocs.size);
#endif
                    lexer->result_symbol = HEREDOC_ARROW;
                }
                return true;
            }
            return false;
        }

        bool is_number = true;
        if (iswdigit(lexer->lookahead)) {
            advance(lexer);
        } else if (iswalpha(lexer->lookahead) || lexer->lookahead == '_' ||
                   (lexer->lookahead == ':' &&
                    (ctx == CTX_ARITHMETIC || ctx == CTX_SUBSCRIPT))) {
            is_number = false;
            advance(lexer);
        } else {
            if (lexer->lookahead == '{') {
                goto brace_start;
            }
            if (lexer->lookahead == ':' && valid_symbols[FUNCTION_NAME]) {
                return scan_function_name(lexer);
            }
            if (valid_symbols[EXPANSION_WORD]) {
                goto expansion_word;
            }
            if (valid_symbols[EXTGLOB_PATTERN]) {
                goto extglob_pattern;
            }
            return false;
        }

        for (;;) {
            if (iswdigit(lexer->lookahead)) {
                advance(lexer);
            } else if (iswalpha(lexer->lookahead) || lexer->lookahead == '_' ||
                       (lexer->lookahead == ':' &&
                        (ctx == CTX_ARITHMETIC || ctx == CTX_SUBSCRIPT))) {
                is_number = false;
                advance(lexer);
            } else {
                break;
            }
        }

        if (is_number && valid_symbols[FILE_DESCRIPTOR] &&
            (lexer->lookahead == '>' || lexer->lookahead == '<')) {
            lexer->mark_end(lexer);
            lexer->result_symbol = FILE_DESCRIPTOR;
            return true;
        }

        if (valid_symbols[FUNCTION_NAME] && lexer->lookahead == ':') {
            return scan_function_name(lexer);
        }

        if (valid_symbols[VARIABLE_NAME]) {
            if (lexer->lookahead == '+') {
                lexer->mark_end(lexer);
                advance(lexer);
                context_type_t ctx = get_current_context(scanner);
                if (lexer->lookahead == '=' || lexer->lookahead == ':' ||
                    (ctx == CTX_PARAMETER ||
                     ctx == CTX_PARAMETER_PATTERN_SUFFIX ||
                     ctx == CTX_PARAMETER_PATTERN_SUBSTITUTE)) {
#if DEBUG
                    fprintf(stderr,
                            "SCANNER: VARIABLE_NAME after + operator\n");
#endif
                    lexer->result_symbol = VARIABLE_NAME;
                    was_just_bare_dollar = scanner->just_returned_bare_dollar =
                        false;
                    if (in_parameter_expansion(scanner)) {
                        note_variable_name(scanner, lexer);
                    }
                    return true;
                }
                return false;
            }
            if (lexer->lookahead == '/') {
                return false;
            }
            context_type_t ctx = get_current_context(scanner);
            if (lexer->lookahead == '=' || lexer->lookahead == '[' ||
                lexer->lookahead == '%' ||
                (lexer->lookahead == '#' && !is_number) ||
                lexer->lookahead == '@' ||
                (lexer->lookahead == '-' &&
                 (ctx == CTX_PARAMETER || ctx == CTX_PARAMETER_PATTERN_SUFFIX ||
                  ctx == CTX_PARAMETER_PATTERN_SUBSTITUTE))) {
                lexer->mark_end(lexer);
#if DEBUG
                fprintf(stderr, "SCANNER: VARIABLE_NAME after =\n");
#endif
                lexer->result_symbol = VARIABLE_NAME;
                was_just_bare_dollar = scanner->just_returned_bare_dollar =
                    false;
                if (in_parameter_expansion(scanner)) {
                    note_variable_name(scanner, lexer);
                }
                return true;
            }

            if (lexer->lookahead == '?') {
                lexer->mark_end(lexer);
                advance(lexer);
                lexer->result_symbol = VARIABLE_NAME;
                was_just_bare_dollar = scanner->just_returned_bare_dollar =
                    false;
#if DEBUG
                fprintf(stderr, "SCANNER: VARIABLE_NAME after ?\n");
#endif
                if (in_parameter_expansion(scanner)) {
                    note_variable_name(scanner, lexer);
                }
                return iswalpha(lexer->lookahead);
            }
        }

#if DEBUG
        fprintf(stderr, "DEBUG: expansion word not valud returning false\n");
#endif
        return false;
    }

    if (valid_symbols[BARE_DOLLAR] && !in_error_recovery(valid_symbols) &&
        scan_raw_dollar(lexer, valid_symbols)) {
        return true;
    }

regex:
    if ((valid_symbols[REGEX] || valid_symbols[REGEX_NO_SLASH] ||
         valid_symbols[REGEX_NO_SPACE]) &&
        !in_error_recovery(valid_symbols)) {
        if (valid_symbols[REGEX] || valid_symbols[REGEX_NO_SPACE]) {
            while (iswspace(lexer->lookahead)) {
                skip(lexer);
            }
        }

        if ((lexer->lookahead != '"' && lexer->lookahead != '\'') ||
            ((lexer->lookahead == '$' || lexer->lookahead == '\'') &&
             valid_symbols[REGEX_NO_SLASH]) ||
            (lexer->lookahead == '\'' && valid_symbols[REGEX_NO_SPACE])) {
            typedef struct {
                bool done;
                bool advanced_once;
                bool found_non_alnumdollarunderdash;
                bool last_was_escape;
                bool in_single_quote;
                uint32_t paren_depth;
                uint32_t bracket_depth;
                uint32_t brace_depth;
            } State;

            if (lexer->lookahead == '$' && valid_symbols[REGEX_NO_SLASH]) {
                lexer->mark_end(lexer);
                advance(lexer);
                if (lexer->lookahead == '(') {
                    return false;
                }
            }

#if DEBUG
            fprintf(stderr, "DEBUG: regex scan start\n");
#endif

            lexer->mark_end(lexer);

            // Inside ${var/pat/repl}, ${var#pat} and ${var%pat} zsh ends the
            // pattern only at an unescaped '/' (substitution) or an
            // unbalanced '}'. A bare ')' or ']' at depth 0 is a literal, as
            // in ${x//[^)]} or ${x//]/_}, so it must not end the token there.
            // Elsewhere (e.g. [[ x =~ (re) ]]) they still terminate.
            bool literal_close = in_pattern_context(scanner);

            // $f:s/a/b outside ${...} is part of one word: a blank or a
            // character that ends a word ends the pattern / replacement.
            bool in_word_subst =
                valid_symbols[REGEX_NO_SLASH] &&
                get_current_context(scanner) == CTX_MODIFIER_SUBST;

            State state = {false, false, false, false, false, 0, 0, 0};
            while (!state.done) {
                if (in_word_subst && !state.in_single_quote &&
                    (lexer->eof(lexer) || iswspace(lexer->lookahead) ||
                     lexer->lookahead == '"' || lexer->lookahead == '`' ||
                     lexer->lookahead == ';' || lexer->lookahead == '|' ||
                     lexer->lookahead == '&' ||
                     (lexer->lookahead == ')' && state.paren_depth == 0))) {
                    lexer->result_symbol = REGEX_NO_SLASH;
                    return state.advanced_once;
                }
                switch (lexer->lookahead) {
                case '\\':
                    state.last_was_escape = true;
                    break;
                case '\0':
                    return false;
                case '(':
                    state.paren_depth++;
                    state.last_was_escape = false;
                    break;
                case '[':
                    state.bracket_depth++;
                    state.last_was_escape = false;
                    break;
                case '{':
                    if (!state.last_was_escape) {
                        state.brace_depth++;
                    }
                    state.last_was_escape = false;
                    break;
                case ')':
                    if (state.paren_depth > 0) {
                        state.paren_depth--;
                    } else if (!literal_close) {
                        state.done = true;
                    }
                    state.last_was_escape = false;
                    break;
                case ']':
                    if (state.bracket_depth > 0) {
                        state.bracket_depth--;
                    } else if (!literal_close) {
                        state.done = true;
                    }
                    state.last_was_escape = false;
                    break;
                case '}':
                    if (state.brace_depth == 0) {
                        state.done = true;
                    }
                    state.brace_depth--;
                    state.last_was_escape = false;
                    break;
                case '\'':
                    // An escaped quote outside quotes is a literal character:
                    // ${w%\'}. (Inside $'\t' the flag can still be set from
                    // the \t, so only outside a quoted string.)
                    if (state.last_was_escape && !state.in_single_quote) {
                        state.last_was_escape = false;
                        break;
                    }
                    // Enter or exit a single-quoted string.
                    if (state.in_single_quote) {
                        state.in_single_quote = false;
#if DEBUG
                        fprintf(stderr, "DEBUG: regex scan exit raw string\n");
#endif

                        // Track entering parameter expansion context
                        exit_context(scanner, CTX_RAW_STRING);
                    } else {
                        state.in_single_quote = true;
#if DEBUG
                        fprintf(stderr,
                                "DEBUG: regex scan entering raw string\n");
#endif

                        // Track entering parameter expansion context
                        enter_context(scanner, CTX_RAW_STRING);
                    }
                    advance(lexer);
                    lexer->mark_end(lexer);
                    state.advanced_once = true;
                    state.last_was_escape = false;
                    continue;
                default:
                    state.last_was_escape = false;
                    break;
                }

                if (!state.done) {
                    if (valid_symbols[REGEX]) {
                        bool was_space = !state.in_single_quote &&
                                         iswspace(lexer->lookahead);
                        advance(lexer);
                        state.advanced_once = true;
                        if (!was_space || state.paren_depth > 0) {
                            lexer->mark_end(lexer);
                        }
                    } else if (valid_symbols[REGEX_NO_SLASH]) {
                        if (lexer->lookahead == '/') {
                            lexer->mark_end(lexer);
                            lexer->result_symbol = REGEX_NO_SLASH;
                            return state.advanced_once;
                        }
                        if (lexer->lookahead == '\\') {
                            advance(lexer);
                            state.advanced_once = true;
                            if (!lexer->eof(lexer) && lexer->lookahead != '[') {
                                advance(lexer);
                                lexer->mark_end(lexer);
                            }
                        } else {
                            bool was_space = !state.in_single_quote &&
                                             iswspace(lexer->lookahead);
                            advance(lexer);
                            state.advanced_once = true;
                            if (!was_space) {
                                lexer->mark_end(lexer);
                            }
                        }
                    } else if (valid_symbols[REGEX_NO_SPACE]) {
                        if (lexer->lookahead == '\\') {
                            state.found_non_alnumdollarunderdash = true;
                            advance(lexer);
                            if (!lexer->eof(lexer)) {
                                advance(lexer);
                            }
                        } else if (lexer->lookahead == '$') {
                            lexer->mark_end(lexer);
                            advance(lexer);
                            // do not parse a command
                            // substitution
                            if (lexer->lookahead == '(') {
                                return false;
                            }
                            // end $ always means regex, e.g.
                            // 99999999$
                            if (iswspace(lexer->lookahead)) {
                                lexer->result_symbol = REGEX_NO_SPACE;
                                lexer->mark_end(lexer);
                                return true;
                            }
                        } else {
                            bool was_space = !state.in_single_quote &&
                                             iswspace(lexer->lookahead);
                            if (was_space && state.paren_depth == 0) {
                                lexer->mark_end(lexer);
                                lexer->result_symbol = REGEX_NO_SPACE;
                                return state.found_non_alnumdollarunderdash;
                            }
                            if (!iswalnum(lexer->lookahead) &&
                                lexer->lookahead != '$' &&
                                lexer->lookahead != '-' &&
                                lexer->lookahead != '_') {
                                state.found_non_alnumdollarunderdash = true;
                            }
                            advance(lexer);
                        }
                    }
                }
            }

            lexer->result_symbol =
                valid_symbols[REGEX_NO_SLASH]   ? REGEX_NO_SLASH
                : valid_symbols[REGEX_NO_SPACE] ? REGEX_NO_SPACE
                                                : REGEX;
            if (valid_symbols[REGEX] && !state.advanced_once) {

#if DEBUG
                fprintf(stderr, "DEBUG: regex not valid returning false\n");
#endif
                return false;
            }
            if (valid_symbols[REGEX_NO_SPACE] && !state.advanced_once) {

#if DEBUG
                fprintf(stderr, "DEBUG: regex not valid returning false\n");
#endif
                return false;
            }
            if (valid_symbols[REGEX_NO_SLASH] && !state.advanced_once) {
                // Never emit a zero-width token: the parser makes no
                // progress and loops forever growing the tree.

#if DEBUG
                fprintf(stderr, "DEBUG: regex not valid returning false\n");
#endif
                return false;
            }

#if DEBUG
            fprintf(stderr, "DEBUG: regex scan returning regex\n");
#endif

            return true;
        }
    }

extglob_pattern:
    if (valid_symbols[EXTGLOB_PATTERN] && !in_error_recovery(valid_symbols) &&
        !valid_symbols[REGEX] && !valid_symbols[REGEX_NO_SLASH] &&
        !valid_symbols[REGEX_NO_SPACE] &&
        !in_parameter_expansion_context(
            scanner) // Don't generate EXTGLOB_PATTERN inside ${...}
    ) {
        // first skip ws, then check for ? * + @ !
        while (iswspace(lexer->lookahead)) {
            skip(lexer);
        }

        if (lexer->lookahead == '?' || lexer->lookahead == '*' ||
            lexer->lookahead == '+' || lexer->lookahead == '@' ||
            lexer->lookahead == '!' || lexer->lookahead == '-' ||
            lexer->lookahead == ')' || lexer->lookahead == '\\' ||
            lexer->lookahead == '.' || lexer->lookahead == '[' ||
            (iswalpha(lexer->lookahead))) {
            if (lexer->lookahead == '\\') {
                advance(lexer);
                if ((iswspace(lexer->lookahead) || lexer->lookahead == '"') &&
                    lexer->lookahead != '\r' && lexer->lookahead != '\n') {
                    advance(lexer);
                } else {
                    return false;
                }
            }

            if (lexer->lookahead == ')' &&
                scanner->last_glob_paren_depth == 0) {
                lexer->mark_end(lexer);
                advance(lexer);

                if (iswspace(lexer->lookahead)) {
                    return false;
                }
            }

            lexer->mark_end(lexer);
            bool was_non_alpha = !iswalpha(lexer->lookahead);
            if (lexer->lookahead != '[') {
                // no esac
                if (lexer->lookahead == 'e') {
                    lexer->mark_end(lexer);
                    advance(lexer);
                    if (lexer->lookahead == 's') {
                        advance(lexer);
                        if (lexer->lookahead == 'a') {
                            advance(lexer);
                            if (lexer->lookahead == 'c') {
                                advance(lexer);
                                if (iswspace(lexer->lookahead)) {
                                    return false;
                                }
                            }
                        }
                    }
                } else {
                    advance(lexer);
                }
            }

            // -\w is just a word, find something else special
            if (lexer->lookahead == '-') {
                lexer->mark_end(lexer);
                advance(lexer);
                while (iswalnum(lexer->lookahead)) {
                    advance(lexer);
                }

                if (lexer->lookahead == ')' || lexer->lookahead == '\\' ||
                    lexer->lookahead == '.') {
                    return false;
                }
                lexer->mark_end(lexer);
            }

            // case item -) or *)
            if (lexer->lookahead == ')' &&
                scanner->last_glob_paren_depth == 0) {
                lexer->mark_end(lexer);
                advance(lexer);
                if (iswspace(lexer->lookahead)) {
                    lexer->result_symbol = EXTGLOB_PATTERN;
                    bool result = scanner->saw_pipe_in_case || was_non_alpha;
                    scanner->saw_pipe_in_case = false;
                    return result;
                }
            }

            if (iswspace(lexer->lookahead)) {
                lexer->mark_end(lexer);
                lexer->result_symbol = EXTGLOB_PATTERN;
                scanner->last_glob_paren_depth = 0;
                return true;
            }

            if (lexer->lookahead == '$') {
                lexer->mark_end(lexer);
                advance(lexer);
                if (lexer->lookahead == '{' || lexer->lookahead == '(') {
                    lexer->result_symbol = EXTGLOB_PATTERN;
                    return true;
                }
            }

            if (lexer->lookahead == '|') {
                lexer->mark_end(lexer);
                advance(lexer);
                scanner->saw_pipe_in_case = true;
                lexer->result_symbol = EXTGLOB_PATTERN;
                return true;
            }

            if (!iswalnum(lexer->lookahead) && lexer->lookahead != '(' &&
                lexer->lookahead != '"' && lexer->lookahead != '[' &&
                lexer->lookahead != '?' && lexer->lookahead != '/' &&
                lexer->lookahead != '\\' && lexer->lookahead != '_' &&
                lexer->lookahead != '*') {
                return false;
            }

            typedef struct {
                bool done;
                bool saw_non_alphadot;
                uint32_t paren_depth;
                uint32_t bracket_depth;
                uint32_t brace_depth;
            } State;

            State state = {false, was_non_alpha, scanner->last_glob_paren_depth,
                           0, 0};
            uint32_t start_col = lexer->get_column(lexer);
            while (!state.done) {
                // A quote outside any group or bracket class ends the piece
                // (quoted text is its own node: (?*' '*)). Inside a group
                // the piece runs on as before: *('`'|['<>=$']'(')*)
                // can't continue a group's alternatives after a quote.
                if ((lexer->lookahead == '\'' || lexer->lookahead == '"') &&
                    state.bracket_depth == 0 && state.paren_depth == 0) {
                    lexer->mark_end(lexer);
                    if (lexer->get_column(lexer) == start_col) {
                        return false;
                    }
                    lexer->result_symbol = EXTGLOB_PATTERN;
                    return true;
                }
                switch (lexer->lookahead) {
                case '\0':
                    return false;
                case '(':
                    state.paren_depth++;
                    break;
                case '[':
                    state.bracket_depth++;
                    break;
                case '{':
                    state.brace_depth++;
                    break;
                case ')':
                    if (state.paren_depth == 0) {
                        state.done = true;
                    }
                    state.paren_depth--;
                    break;
                case ']':
                    if (state.bracket_depth == 0) {
                        state.done = true;
                    }
                    state.bracket_depth--;
                    break;
                case '}':
                    if (state.brace_depth == 0) {
                        state.done = true;
                    }
                    state.brace_depth--;
                    break;
                }

                if (lexer->lookahead == '|') {
                    lexer->mark_end(lexer);
                    advance(lexer);
                    if (state.paren_depth == 0 && state.bracket_depth == 0 &&
                        state.brace_depth == 0) {
                        scanner->saw_pipe_in_case = true;
                        lexer->result_symbol = EXTGLOB_PATTERN;
                        return true;
                    }
                    state.saw_non_alphadot = true;
                    // Re-examine the character after '|' from the top: a
                    // ')' there (empty alternative, *(no|)keep*) must still
                    // close the group.
                    continue;
                }

                if (!state.done) {
                    bool was_space = iswspace(lexer->lookahead);
                    if (lexer->lookahead == '$') {
                        lexer->mark_end(lexer);
                        if (!iswalpha(lexer->lookahead) &&
                            lexer->lookahead != '.' &&
                            lexer->lookahead != '\\') {
                            state.saw_non_alphadot = true;
                        }
                        advance(lexer);
                        if (lexer->lookahead == '(' ||
                            lexer->lookahead == '{') {
                            lexer->result_symbol = EXTGLOB_PATTERN;
                            scanner->last_glob_paren_depth = state.paren_depth;
                            return state.saw_non_alphadot;
                        }
                    }
                    if (was_space) {
                        lexer->mark_end(lexer);
                        lexer->result_symbol = EXTGLOB_PATTERN;
                        scanner->last_glob_paren_depth = 0;
                        return state.saw_non_alphadot;
                    }
                    if (lexer->lookahead == '"') {
                        lexer->mark_end(lexer);
                        lexer->result_symbol = EXTGLOB_PATTERN;
                        scanner->last_glob_paren_depth = 0;
                        return state.saw_non_alphadot;
                    }
                    if (lexer->lookahead == '\\') {
                        if (!iswalpha(lexer->lookahead) &&
                            lexer->lookahead != '.' &&
                            lexer->lookahead != '\\') {
                            state.saw_non_alphadot = true;
                        }
                        advance(lexer);
                        if (iswspace(lexer->lookahead) ||
                            lexer->lookahead == '"') {
                            advance(lexer);
                        }
                    } else {
                        if (!iswalpha(lexer->lookahead) &&
                            lexer->lookahead != '.' &&
                            lexer->lookahead != '\\') {
                            state.saw_non_alphadot = true;
                        }
                        advance(lexer);
                    }
                    if (!was_space) {
                        lexer->mark_end(lexer);
                    }
                }
            }

            lexer->result_symbol = EXTGLOB_PATTERN;
            scanner->last_glob_paren_depth = 0;
            bool result = state.saw_non_alphadot || scanner->saw_pipe_in_case;
            scanner->saw_pipe_in_case = false;
            return result;
        }
        scanner->last_glob_paren_depth = 0;
#if DEBUG
        fprintf(stderr, "DEBUG: EXTGLOB not valid returning false\n");
#endif
        return false;
    }

expansion_word:
    if (valid_symbols[EXPANSION_WORD]) {
#if DEBUG
        fprintf(stderr,
                "DEBUG: EXPANSION_WORD handler called, context=%d, "
                "lookahead='%c'\n",
                get_current_context(scanner), lexer->lookahead);
#endif
        // If we just returned a variable name and encounter # or %,
        // don't consume them as expansion word - let them be operator tokens
        if (was_just_variable_name &&
            (lexer->lookahead == '#' || lexer->lookahead == '%')) {
#if DEBUG
            fprintf(stderr, "DEBUG: EXPANSION_WORD early return due to "
                            "variable_name + operator\n");
#endif
            return false;
        }
        bool advanced_once = false;
        bool advance_once_space = false;
        for (;;) {
            if (lexer->lookahead == '\"') {
                return false;
            }
            if (lexer->lookahead == '$') {
                lexer->mark_end(lexer);
                advance(lexer);
                if (lexer->lookahead == '{' || lexer->lookahead == '(' ||
                    lexer->lookahead == '\'' || iswalnum(lexer->lookahead)) {
                    lexer->result_symbol = EXPANSION_WORD;
                    return true;
                }
                advanced_once = true;
            }

            if (lexer->lookahead == '/' &&
                should_stop_at_pattern_slash(scanner)) {
                lexer->mark_end(lexer);
                lexer->result_symbol = EXPANSION_WORD;
                return true;
            }

            if (lexer->lookahead == '}' && in_parameter_expansion(scanner)) {
                // Track exiting parameter expansion context
                lexer->mark_end(lexer);
                lexer->result_symbol = EXPANSION_WORD;
                return true;
            }

            if (lexer->lookahead == '(' &&
                !(advanced_once || advance_once_space)) {
                lexer->mark_end(lexer);
                advance(lexer);
                while (lexer->lookahead != ')' && !lexer->eof(lexer)) {
                    // if we find a $( or ${ assume this is valid and is
                    // a garbage concatenation of some weird word + an
                    // expansion
                    // I wonder where this can fail
                    if (lexer->lookahead == '$') {
                        lexer->mark_end(lexer);
                        advance(lexer);
                        if (lexer->lookahead == '{' ||
                            lexer->lookahead == '(' ||
                            lexer->lookahead == '\'' ||
                            iswalnum(lexer->lookahead)) {
                            lexer->result_symbol = EXPANSION_WORD;
                            return true;
                        }
                        advanced_once = true;
                    } else {

                        // In parameter expansion, handle subscript boundaries
                        // and operators properly
                        if (should_stop_at_pattern_operators(scanner)) {
                            if (lexer->lookahead == ']') {
                                // Stop at ] to let it be handled as subscript
                                // terminator
                                lexer->mark_end(lexer);
                                lexer->result_symbol = EXPANSION_WORD;
                                return true;
                            }
                            if (lexer->lookahead == '#' ||
                                lexer->lookahead == '%') {
                                // Stop at operators to let them be handled
                                // separately
                                lexer->mark_end(lexer);
                                lexer->result_symbol = EXPANSION_WORD;
                                return true;
                            }
                            if (lexer->lookahead == ':') {
                                // Stop at colon to let it be handled separately
                                // for colon-based operations
                                lexer->mark_end(lexer);
                                lexer->result_symbol = EXPANSION_WORD;
                                return true;
                            }
                        }
                        advanced_once =
                            advanced_once || !iswspace(lexer->lookahead);
                        advance_once_space =
                            advance_once_space || iswspace(lexer->lookahead);
                        advance(lexer);
                    }
                }
                lexer->mark_end(lexer);
                if (lexer->lookahead == ')') {
                    advanced_once = true;
                    advance(lexer);
                    lexer->mark_end(lexer);
                } else {
                    return false;
                }
            }

            if (lexer->lookahead == '\'') {
                return false;
            }

            if (lexer->eof(lexer)) {
                return false;
            }

            // In parameter expansion, handle subscript boundaries and operators
            // properly
            if (should_stop_at_pattern_operators(scanner)) {
#if DEBUG
                fprintf(stderr,
                        "DEBUG: EXPANSION_WORD checking pattern operators, "
                        "lookahead='%c'\n",
                        lexer->lookahead);
#endif
                if (lexer->lookahead == ']') {
                    // Stop at ] to let it be handled as subscript terminator
#if DEBUG
                    fprintf(stderr, "DEBUG: EXPANSION_WORD stopping at ]\n");
#endif
                    lexer->mark_end(lexer);
                    lexer->result_symbol = EXPANSION_WORD;
                    return true;
                }
                if (lexer->lookahead == '#' || lexer->lookahead == '%' ||
                    lexer->lookahead == '/') {
                    context_type_t ctx = get_current_context(scanner);
                    if (lexer->lookahead == '/' &&
                        ctx == CTX_PARAMETER_PATTERN_SUBSTITUTE &&
                        // should_stop_at_pattern_operators(scanner) &&
                        !advanced_once) {
                        // Stop at operators to let them be handled separately
#if DEBUG
                        fprintf(
                            stderr,
                            "DEBUG: EXPANSION_WORD stopping at operator '%c'\n",
                            lexer->lookahead);
#endif
                        lexer->mark_end(lexer);
                        lexer->result_symbol = EXPANSION_WORD;
                        return true;
                    }
                }
            }

            advanced_once = advanced_once || !iswspace(lexer->lookahead);
            advance_once_space =
                advance_once_space || iswspace(lexer->lookahead);
            advance(lexer);
        }
    } else {
#if DEBUG
        fprintf(stderr, "DEBUG: EXPANSION_WORD not valid, skipping\n");
#endif
    }

// This handles ranges in braces
brace_start:
    if (valid_symbols[BRACE_EXPR_START] && !in_error_recovery(valid_symbols)) {
        skip_ws(lexer);

        if (lexer->lookahead == '{') {
            advance(lexer);
            lexer->mark_end(lexer);

            // Don't enter context - brace expressions are handled by grammar
            // The grammar will handle the entire {1..10} pattern itself

            while (isdigit(lexer->lookahead)) {
                advance(lexer);
            }

            if (lexer->lookahead != '.') {
                return false;
            }
            advance(lexer);

            if (lexer->lookahead != '.') {
                return false;
            }
            advance(lexer);

            while (isdigit(lexer->lookahead)) {
                advance(lexer);
            }

            if (lexer->lookahead != '}') {
                return false;
            }

            enter_context(scanner, CTX_BRACE_EXPANSION);
            lexer->result_symbol = BRACE_EXPR_START;
            return true;
        }
    }

    // ${x:fs#a#b#}: a modifier start no handler above took.
    if (valid_symbols[MODIFIER_OTHER] && !in_error_recovery(valid_symbols) &&
        lexer->lookahead == ':') {
        lexer->mark_end(lexer);
        advance(lexer);
        return scan_modifier_other(lexer);
    }

#if DEBUG
    fprintf(stderr, "SCANNER: scan returning false\n");
#endif

    return false;
}

void *tree_sitter_zsh_external_scanner_create() {
    Scanner *scanner = calloc(1, sizeof(Scanner));
    UINT32_MAX; // Initialize to invalid position
    array_init(&scanner->heredocs);
    array_init(&scanner->context_stack);
#if DEBUG
    fprintf(
        stderr,
        "DEBUG: Scanner created - heredocs.size=%u, context_stack.size=%u\n",
        scanner->heredocs.size, scanner->context_stack.size);
#endif
    return scanner;
}

bool tree_sitter_zsh_external_scanner_scan(void *payload, TSLexer *lexer,
                                           const bool *valid_symbols) {
    Scanner *scanner = (Scanner *)payload;
    return scan(scanner, lexer, valid_symbols);
}

unsigned tree_sitter_zsh_external_scanner_serialize(void *payload,
                                                    char *state) {
    Scanner *scanner = (Scanner *)payload;
    return serialize(scanner, state);
}

void tree_sitter_zsh_external_scanner_deserialize(void *payload,
                                                  const char *state,
                                                  unsigned length) {
    Scanner *scanner = (Scanner *)payload;
    deserialize(scanner, state, length);
}

#ifdef ZSH_SCANNER_AUDIT
// Audit build only: print a scanner's open contexts, plus the mismatched
// and underflowing pops counted (across all parse branches) since the
// last report. script/context-audit.c calls this on a scanner restored
// from the final tree's last external token, i.e. the state that is in
// force at the end of the file on the parse that won.
void tree_sitter_zsh_audit_report(void *payload) {
    Scanner *scanner = (Scanner *)payload;
    fprintf(stderr, "AUDIT depth=%u stack=", scanner->context_stack.size);
    for (uint32_t i = 0; i < scanner->context_stack.size; i++) {
        fprintf(stderr, "%s%s", i ? "," : "",
                ContextNames[*array_get(&scanner->context_stack, i)] + 4);
    }
    fprintf(stderr, " mismatch=");
    for (int f = 0; f < AUDIT_NCTX; f++) {
        for (int w = 0; w < AUDIT_NCTX; w++) {
            if (audit_mismatch[f][w]) {
                fprintf(stderr, "%s>%s:%u,", ContextNames[f] + 4,
                        ContextNames[w] + 4, audit_mismatch[f][w]);
                audit_mismatch[f][w] = 0;
            }
        }
    }
    fprintf(stderr, " underflow=");
    for (int w = 0; w < AUDIT_NCTX; w++) {
        if (audit_underflow[w]) {
            fprintf(stderr, "%s:%u,", ContextNames[w] + 4, audit_underflow[w]);
            audit_underflow[w] = 0;
        }
    }
    fprintf(stderr, " ");
}
#endif

void tree_sitter_zsh_external_scanner_destroy(void *payload) {
    Scanner *scanner = (Scanner *)payload;
    for (size_t i = 0; i < scanner->heredocs.size; i++) {
        Heredoc *heredoc = array_get(&scanner->heredocs, i);
        array_delete(&heredoc->current_leading_word);
        array_delete(&heredoc->delimiter);
    }
    array_delete(&scanner->heredocs);
    array_delete(&scanner->context_stack);
    free(scanner);
}
