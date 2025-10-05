#include "tree_sitter/array.h"
#include "tree_sitter/parser.h"

#include <assert.h>
#include <ctype.h>
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
    IMMEDIATE_DOUBLE_HASH,
    ARRAY_STAR_TOKEN,
    ARRAY_AT_TOKEN,
    CLOSING_BRACE,
    CLOSING_BRACKET,
    CLOSING_PAREN,
    CLOSING_DOUBLE_PAREN,
    HEREDOC_ARROW,
    HEREDOC_ARROW_DASH,
    HASH_PATTERN,         // #pattern
    DOUBLE_HASH_PATTERN,  // ##pattern
    ENTER_PATTERN,        // implicit / etc
    PATTERN_START,        // After pattern operators, before pattern content
    PATTERN_SUFFIX_START, // After # operators, before pattern content
    NEWLINE,
    OPENING_PAREN,
    DOUBLE_OPENING_PAREN,
    OPENING_BRACKET,
    TEST_COMMAND_START, // [[
    TEST_COMMAND_END,   // ]]
    ESAC,
    ZSH_EXTENDED_GLOB_FLAGS,
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
    "IMMEDIATE_DOUBLE_HASH",
    "ARRAY_STAR_TOKEN",
    "ARRAY_AT_TOKEN",
    "CLOSING_BRACE",
    "CLOSING_BRACKET",
    "CLOSING_PAREN",
    "CLOSING_DOUBLE_PAREN",
    "HEREDOC_ARROW",
    "HEREDOC_ARROW_DASH",
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
    "TEST_COMMAND_END",
    "ESAC",
    "ZSH_EXTENDED_GLOB_FLAGS",
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
        7 // ${var/pattern/replacement} - substitution
} context_type_t;

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
    bool ext_was_in_double_quote;
    bool ext_saw_outside_quote;
    Array(context_type_t) context_stack; // Proper context stack
    bool just_returned_variable_name; // Track if we just returned VARIABLE_NAME
    bool just_returned_bare_dollar;   // Track if we just returned BARE_DOLLAR
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
    array_push(&scanner->context_stack, context);
}

static inline void exit_context(Scanner *scanner,
                                context_type_t expected_context) {
    if (scanner->context_stack.size > 0) {
        context_type_t current = *array_back(&scanner->context_stack);
        // Verify we're exiting the expected context (for debugging)
        if (current == expected_context) {
            array_pop(&scanner->context_stack);
        } else {
            // Gracefully handle mismatched contexts by popping anyway
            array_pop(&scanner->context_stack);
        }
    }
}

// Helper functions for checking contexts
static inline bool in_expansion_context(Scanner *scanner) {
    context_type_t ctx = get_current_context(scanner);
    return ctx == CTX_PARAMETER || ctx == CTX_ARITHMETIC || ctx == CTX_COMMAND;
}

static inline bool in_pattern_context(Scanner *scanner) {
    context_type_t ctx = get_current_context(scanner);
    return ctx == CTX_PARAMETER_PATTERN_SUFFIX ||
           ctx == CTX_PARAMETER_PATTERN_SUBSTITUTE;
}

static inline bool in_test_command(Scanner *scanner) {
    context_type_t ctx = get_current_context(scanner);
    return ctx == CTX_TEST;
}

static inline void skip(TSLexer *lexer) { lexer->advance(lexer, true); }

static inline bool in_error_recovery(const bool *valid_symbols) {
    return valid_symbols[ERROR_RECOVERY];
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
    scanner->ext_was_in_double_quote = false;
    scanner->ext_saw_outside_quote = false;
    scanner->context_stack.size = 0; // Clear context stack
    scanner->just_returned_variable_name = false;
    scanner->just_returned_bare_dollar = false;
    for (uint32_t i = 0; i < scanner->heredocs.size; i++) {
        reset_heredoc(array_get(&scanner->heredocs, i));
    }
#if DEBUG
    fprintf(stderr, "DEBUG: Reset done - heredocs.size after=%u %u\n",
            scanner->heredocs.size, scanner->context_stack.size);
#endif
}

static unsigned serialize(Scanner *scanner, char *buffer) {
    uint32_t size = 0;

    buffer[size++] = (char)scanner->last_glob_paren_depth;
    buffer[size++] = (char)scanner->ext_was_in_double_quote;
    buffer[size++] = (char)scanner->ext_saw_outside_quote;
    buffer[size++] = (char)scanner->context_stack.size;
    buffer[size++] = (char)scanner->heredocs.size;
    buffer[size++] = (char)scanner->just_returned_variable_name;
    buffer[size++] = (char)scanner->just_returned_bare_dollar;

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
            "DEBUG: Deserialize called - length=%u, scanner->heredocs.size "
            "before=%u %u\n",
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
        fprintf(
            stderr,
            "DEBUG: Deserialize - context_stack_size=%u, heredoc_count=%u\n",
            context_stack_size, heredoc_count);
#endif
        scanner->just_returned_variable_name = buffer[size++];
        scanner->just_returned_bare_dollar = buffer[size++];

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

static inline bool scan_raw_dollar(TSLexer *lexer) {
    while (iswspace(lexer->lookahead) && lexer->lookahead != '\n' &&
           !lexer->eof(lexer)) {
        skip(lexer);
    }

    if (lexer->lookahead == '$') {
        advance(lexer);
        lexer->result_symbol = BARE_DOLLAR;
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

static bool scan(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols) {
#if DEBUG
    fprintf(stderr, "SCANNER: invoked lookahead='%c'\n", lexer->lookahead);
    for (int i = 0; i <= ERROR_RECOVERY; i++) {
        if (valid_symbols[i]) {
            fprintf(stderr, "SCANNER: valid symbol: %s\n", TokenNames[i]);
        }
    }
    // Debug when BARE_DOLLAR is valid
    if (valid_symbols[BARE_DOLLAR]) {
        fprintf(stderr, "SCANNER: BARE_DOLLAR valid, lookahead='%c'\n",
                lexer->lookahead);
    }
#endif

    // Clear flag at start and capture its previous value
    bool was_just_variable_name = scanner->just_returned_variable_name;
    scanner->just_returned_variable_name = false;

    bool was_just_bare_dollar = scanner->just_returned_bare_dollar;
    scanner->just_returned_bare_dollar = false;

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
        if (lexer->lookahead == '\n') {
            while (lexer->lookahead == '\n') {
                skip(lexer);
            }
            lexer->mark_end(lexer);
            lexer->result_symbol = NEWLINE;
        }
    }

    // Dedicated context-aware brace handler - handles closing braces for
    // different contexts
    if (lexer->lookahead == '}' && valid_symbols[CLOSING_BRACE] &&
        !in_error_recovery(valid_symbols)) {
        context_type_t active = get_current_context(scanner);

#if DEBUG
        fprintf(stderr,
                "SCANNER: Detected } closing brace, active "
                "context=%d\n",
                active);
#endif
        if (active == CTX_PARAMETER || active == CTX_PARAMETER_PATTERN_SUFFIX ||
            active == CTX_PARAMETER_PATTERN_SUBSTITUTE) {
#if DEBUG
            fprintf(stderr,
                    "SCANNER: Exiting parameter expansion context on }\n");
#endif
            exit_context(scanner, active);
            lexer->result_symbol = CLOSING_BRACE;
            advance(lexer);
            return true;
        }

        // Note: CTX_BRACE_EXPANSION closing braces are handled by grammar as
        // token.immediate('}')
    }

    if (valid_symbols[CONCAT] && !in_error_recovery(valid_symbols)) {
        context_type_t ctx = get_current_context(scanner);
        if (!(lexer->lookahead == 0 || iswspace(lexer->lookahead) ||
              lexer->lookahead == '>' || lexer->lookahead == '<' ||
              (lexer->lookahead == ')' && valid_symbols[CLOSING_PAREN]) ||
              lexer->lookahead == '(' || lexer->lookahead == ';' ||
              lexer->lookahead == '&' || lexer->lookahead == '|' ||
              (lexer->lookahead == '}' &&
               (ctx == CTX_PARAMETER || ctx == CTX_PARAMETER_PATTERN_SUFFIX ||
                ctx == CTX_PARAMETER_PATTERN_SUBSTITUTE ||
                ctx == CTX_BRACE_EXPANSION)) ||
              (lexer->lookahead == ']' && valid_symbols[CLOSING_BRACKET]) ||
              (lexer->lookahead == '[' &&
               was_just_variable_name))) { // Suppress CONCAT after $var when [
                                           // follows
#if DEBUG
            fprintf(stderr, "SCANNER: CONCAT\n");
#endif

            lexer->result_symbol = CONCAT;
            // So for a`b`, we want to return a concat. We check if the
            // 2nd backtick has whitespace after it, and if it does we
            // return concat.
            if (lexer->lookahead == '`') {
                lexer->mark_end(lexer);
                advance(lexer);
                while (lexer->lookahead != '`' && !lexer->eof(lexer)) {
                    advance(lexer);
                }
                if (lexer->eof(lexer)) {
                    return false;
                }
                if (lexer->lookahead == '`') {
                    advance(lexer);
                }
                return iswspace(lexer->lookahead) || lexer->eof(lexer);
            }
            // strings w/ expansions that contains escaped quotes or
            // backslashes need this to return a concat
            if (lexer->lookahead == '\\') {
                lexer->mark_end(lexer);
                advance(lexer);
                if (lexer->lookahead == '"' || lexer->lookahead == '\'' ||
                    lexer->lookahead == '\\') {
                    return true;
                }
                if (lexer->eof(lexer)) {
                    return false;
                }
            } else {
                return true;
            }
        }
        if (iswspace(lexer->lookahead) &&
            (ctx == CTX_PARAMETER || ctx == CTX_PARAMETER_PATTERN_SUFFIX ||
             ctx == CTX_PARAMETER_PATTERN_SUBSTITUTE ||
             ctx == CTX_BRACE_EXPANSION) &&
            !valid_symbols[EXPANSION_WORD]) {
            lexer->result_symbol = CONCAT;
            return true;
        }
    }

    // Handle BARE_DOLLAR for parameter expansion: $ followed by {
    if (valid_symbols[BARE_DOLLAR] && !in_error_recovery(valid_symbols)) {
#if DEBUG
        fprintf(stderr,
                "SCANNER: Entering BARE_DOLLAR handler, lookahead='%c'\n",
                lexer->lookahead);
#endif

        // Only skip whitespace if we're starting with whitespace
        // This preserves whitespace significance for concatenation
        if (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
            while ((lexer->lookahead == ' ' || lexer->lookahead == '\t') &&
                   !lexer->eof(lexer)) {
                skip(lexer);
            }
        }

        if (lexer->lookahead == '$') {
#if DEBUG
            fprintf(stderr, "SCANNER: Found $ character\n");
#endif
            advance(lexer);
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
            return false;
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
            // If not after $, let the brace_start handler at the bottom deal
            // with it
        }
    }

    // Handle OPENING_PAREN after BARE_DOLLAR
    if ((valid_symbols[OPENING_PAREN] || valid_symbols[DOUBLE_OPENING_PAREN] ||
         valid_symbols[ZSH_EXTENDED_GLOB_FLAGS]) &&
        !in_error_recovery(valid_symbols)) {
        while (iswspace(lexer->lookahead)) {
            skip(lexer);
        }
        if (lexer->lookahead == '(') {
            advance(lexer);
            lexer->mark_end(lexer);

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
            } else if (valid_symbols[OPENING_PAREN] ||
                       valid_symbols[ZSH_EXTENDED_GLOB_FLAGS]) {
                // Handle ZSH_EXTENDED_GLOB_FLAGS - (#flags) patterns
                if (lexer->lookahead == '#' &&
                    valid_symbols[ZSH_EXTENDED_GLOB_FLAGS]) {
                    advance(lexer);

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
                    was_just_bare_dollar = scanner->just_returned_bare_dollar =
                        false; // Reset flag
                    lexer->result_symbol = OPENING_PAREN;
                    return true;
                }
            }
        }
    }

    if ((valid_symbols[OPENING_BRACKET] || valid_symbols[TEST_COMMAND_START]) &&
        !in_error_recovery(valid_symbols)) {
        while (iswspace(lexer->lookahead)) {
            skip(lexer);
        }
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
            } else if (valid_symbols[OPENING_BRACKET]) {
                // This is single [
                was_just_bare_dollar = scanner->just_returned_bare_dollar =
                    false; // Reset flag
                lexer->result_symbol = OPENING_BRACKET;
                lexer->mark_end(lexer);

#if DEBUG
                fprintf(stderr, "DEBUG: Detected OPENING_BRACKET [\n");
#endif
                return true;
            }
        }
    }

    // Handle TEST_COMMAND_END ]]
    if ((valid_symbols[TEST_COMMAND_END] || valid_symbols[CLOSING_BRACKET]) &&
        !in_error_recovery(valid_symbols)) {
        while (iswspace(lexer->lookahead)) {
            skip(lexer);
        }
        if (lexer->lookahead == ']') {
            advance(lexer);
            if (lexer->lookahead == ']' && valid_symbols[TEST_COMMAND_END]) {
                advance(lexer);
                lexer->result_symbol = TEST_COMMAND_END;
                lexer->mark_end(lexer);

#if DEBUG
                fprintf(stderr, "DEBUG: Detected TEST_COMMAND_END ]]\n");
#endif
                // Exit test command context
                exit_context(scanner, CTX_TEST);
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
                return true;
            }
            // If only one ], don't consume it - let normal parsing handle it
            return false;
        }
    }

    if ((valid_symbols[CLOSING_PAREN] || valid_symbols[CLOSING_DOUBLE_PAREN]) &&
        !in_error_recovery(valid_symbols)) {
        while (iswspace(lexer->lookahead)) {
            skip(lexer);
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

                // Exit arithmetic context
                // Note: we treat subscript similarly
                exit_context(scanner, CTX_ARITHMETIC);
                return true;
            }
            // If only one ], don't consume it - let normal parsing handle it
            return false;
        }
    }

    // Handle PATTERN_START - emitted after pattern operators in parameter
    // expansions
    if (valid_symbols[PATTERN_START] && !in_error_recovery(valid_symbols)) {
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
            enter_context(scanner, CTX_PARAMETER_PATTERN_SUBSTITUTE);
#if DEBUG
            fprintf(stderr, "DEBUG: Context after transition: %d\n",
                    get_current_context(scanner));
#endif
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

    // Handle context-sensitive tokens for parameter expansion
    // Handle context-sensitive tokens for parameter expansion
    // Handle colon operations in parameter expansion context
    if (in_parameter_expansion(scanner) && lexer->lookahead == ':' &&
        !in_error_recovery(valid_symbols)) {
        advance(lexer); // consume the :

#if DEBUG
        fprintf(stderr, "SCANNER: Colon not matched, lookahead='%c'\n",
                lexer->lookahead);
#endif
        // If we got here, the colon didn't match any known pattern
        return false;
    }

    // Handle hash operations in parameter expansion context
    if (in_parameter_expansion(scanner) && lexer->lookahead == '#' &&
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

    if (valid_symbols[IMMEDIATE_DOUBLE_HASH] &&
        !in_error_recovery(valid_symbols)) {
        // advance two # and ensure not } after
        if (lexer->lookahead == '#') {
            lexer->mark_end(lexer);
            advance(lexer);
            if (lexer->lookahead == '#') {
                advance(lexer);
                if (lexer->lookahead != '}') {
                    lexer->result_symbol = IMMEDIATE_DOUBLE_HASH;
                    lexer->mark_end(lexer);
                    return true;
                }
            }
        }
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
            lexer->lookahead == ';' || lexer->lookahead == '&') {
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
                "DEBUG: HEREDOC_START check - heredocs.size=%zu, "
                "in_error_recovery=%s\n",
                scanner->heredocs.size,
                in_error_recovery(valid_symbols) ? "true" : "false");
#endif
        return scan_heredoc_start(array_back(&scanner->heredocs), lexer);
    }

    if (valid_symbols[TEST_OPERATOR] && !valid_symbols[EXPANSION_WORD]) {
        while (iswspace(lexer->lookahead) && lexer->lookahead != '\n') {
            skip(lexer);
        }

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
            scan_raw_dollar(lexer)) {
            return true;
        }
    }

    if (valid_symbols[SIMPLE_VARIABLE_NAME] &&
        !in_error_recovery(valid_symbols)) {
        while (iswspace(lexer->lookahead)) {
            skip(lexer);
        }
        if (iswalpha(lexer->lookahead) || lexer->lookahead == '_') {
            int consumed = 0;
            while (iswalnum(lexer->lookahead) || lexer->lookahead == '_') {
                advance(lexer);
                consumed++;
            }

            if (consumed > 0) {
                lexer->mark_end(lexer);
                was_just_bare_dollar = scanner->just_returned_bare_dollar =
                    false;
                lexer->result_symbol = SIMPLE_VARIABLE_NAME;
                return true;
            }
        }
    }

    if (valid_symbols[SPECIAL_VARIABLE_NAME] &&
        !in_error_recovery(valid_symbols)) {
        // '*', '@', '?', '!', '#', '-', '$', '0', '_'
        while (iswspace(lexer->lookahead)) {
            skip(lexer);
        }
        bool in_param_expand = in_parameter_expansion_context(scanner);
        if (lexer->lookahead == '*' ||
            lexer->lookahead == '@' ||
            lexer->lookahead == '?' ||
            lexer->lookahead == '!' ||
            lexer->lookahead == '#' ||
            lexer->lookahead == '-' ||
            lexer->lookahead == '$' ||
            lexer->lookahead == '_' ||
            iswdigit(lexer->lookahead)) {
            bool flag_char = (lexer->lookahead == '#' ||
                             lexer->lookahead == '!');
            advance(lexer);
            if (!(in_param_expand && flag_char)) {
                lexer->mark_end(lexer);
                was_just_bare_dollar = scanner->just_returned_bare_dollar =
                    false;
                lexer->result_symbol = SPECIAL_VARIABLE_NAME;
                return true;

            } else {
                return false;
            }
        }
    }

    if ((valid_symbols[VARIABLE_NAME] || valid_symbols[FILE_DESCRIPTOR] ||
         valid_symbols[HEREDOC_ARROW]) &&
        !valid_symbols[REGEX_NO_SLASH] && !in_error_recovery(valid_symbols)) {
        for (;;) {
            if ((lexer->lookahead == ' ' || lexer->lookahead == '\t' ||
                 lexer->lookahead == '\r' ||
                 (lexer->lookahead == '\n' && !valid_symbols[NEWLINE])) &&
                !valid_symbols[EXPANSION_WORD]) {
                skip(lexer);
            } else if (lexer->lookahead == '\\') {
                skip(lexer);

                if (lexer->eof(lexer)) {
                    lexer->mark_end(lexer);
                    was_just_bare_dollar = scanner->just_returned_bare_dollar =
                        false; // Reset flag
                    lexer->result_symbol = VARIABLE_NAME;
                    scanner->just_returned_variable_name = true;
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

        // no '*', '@', '?', '-', '$', '0', '_', '#'
        if (!valid_symbols[EXPANSION_WORD] &&
            (lexer->lookahead == '*' || lexer->lookahead == '@' ||
             lexer->lookahead == '?' || lexer->lookahead == '-' ||
             lexer->lookahead == '0' || lexer->lookahead == '_' ||
             lexer->lookahead == '#')) {
            lexer->mark_end(lexer);
            advance(lexer);
            if (lexer->lookahead == '=' || lexer->lookahead == '[' ||
                lexer->lookahead == ':' || lexer->lookahead == '-' ||
                lexer->lookahead == '%' || lexer->lookahead == '/') {
                return false;
            }
            if (valid_symbols[EXTGLOB_PATTERN] && iswspace(lexer->lookahead)) {
                lexer->mark_end(lexer);
                lexer->result_symbol = EXTGLOB_PATTERN;
                return true;
            }
        }

        if (valid_symbols[HEREDOC_ARROW] && lexer->lookahead == '<') {
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
                            "now=%zu\n",
                            scanner->heredocs.size);
#endif
                    lexer->result_symbol = HEREDOC_ARROW_DASH;
                } else if (lexer->lookahead == '<' || lexer->lookahead == '=') {
                    return false;
                } else {
                    Heredoc heredoc = heredoc_new();
                    array_push(&scanner->heredocs, heredoc);
#if DEBUG
                    fprintf(
                        stderr,
                        "DEBUG: HEREDOC_ARROW - added heredoc, size now=%zu\n",
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
        } else if (iswalpha(lexer->lookahead) || lexer->lookahead == '_') {
            is_number = false;
            advance(lexer);
        } else {
            if (lexer->lookahead == '{') {
                goto brace_start;
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
            } else if (iswalpha(lexer->lookahead) || lexer->lookahead == '_') {
                is_number = false;
                advance(lexer);
            } else {
                break;
            }
        }

        if (is_number && valid_symbols[FILE_DESCRIPTOR] &&
            (lexer->lookahead == '>' || lexer->lookahead == '<')) {
            lexer->result_symbol = FILE_DESCRIPTOR;
            return true;
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
                    lexer->result_symbol = VARIABLE_NAME;
                    was_just_bare_dollar = scanner->just_returned_bare_dollar =
                        false;
                    scanner->just_returned_variable_name = true;
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
                lexer->result_symbol = VARIABLE_NAME;
                was_just_bare_dollar = scanner->just_returned_bare_dollar =
                    false;
                scanner->just_returned_variable_name = true;
                return true;
            }

            if (lexer->lookahead == '?') {
                lexer->mark_end(lexer);
                advance(lexer);
                lexer->result_symbol = VARIABLE_NAME;
                was_just_bare_dollar = scanner->just_returned_bare_dollar =
                    false;
                scanner->just_returned_variable_name = true;
                return iswalpha(lexer->lookahead);
            }
        }

        return false;
    }

    if (valid_symbols[BARE_DOLLAR] && !in_error_recovery(valid_symbols) &&
        scan_raw_dollar(lexer)) {
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

            lexer->mark_end(lexer);

            State state = {false, false, false, false, false, 0, 0, 0};
            while (!state.done) {
                if (state.in_single_quote) {
                    if (lexer->lookahead == '\'') {
                        state.in_single_quote = false;
                        advance(lexer);
                        lexer->mark_end(lexer);

                        // Track entering parameter expansion context
                        enter_context(scanner, CTX_PARAMETER);
                    }
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
                    if (state.paren_depth == 0) {
                        state.done = true;
                    }
                    state.paren_depth--;
                    state.last_was_escape = false;
                    break;
                case ']':
                    if (state.bracket_depth == 0) {
                        state.done = true;
                    }
                    state.bracket_depth--;
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
                    // Enter or exit a single-quoted string.
                    state.in_single_quote = !state.in_single_quote;
                    advance(lexer);
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
                            if (!lexer->eof(lexer) && lexer->lookahead != '[' &&
                                lexer->lookahead != '/') {
                                advance(lexer);
                                lexer->mark_end(lexer);

                                // Track entering parameter expansion context
                                enter_context(scanner, CTX_PARAMETER);
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
                return false;
            }
            return true;
        }
    }

extglob_pattern:
    // Block EXTGLOB_PATTERN in parameter expansion contexts
    if (in_parameter_expansion_context(scanner) &&
        valid_symbols[EXTGLOB_PATTERN]) {
        return false; // Don't generate EXTGLOB_PATTERN inside ${...}
    }
    if (valid_symbols[EXTGLOB_PATTERN] && !in_error_recovery(valid_symbols) &&
        !valid_symbols[REGEX] && !valid_symbols[REGEX_NO_SLASH] &&
        !valid_symbols[REGEX_NO_SPACE]) {
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
                    return was_non_alpha;
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
            while (!state.done) {
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
                        lexer->result_symbol = EXTGLOB_PATTERN;
                        return true;
                    }
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
            return state.saw_non_alphadot;
        }
        scanner->last_glob_paren_depth = 0;

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
    if (valid_symbols[BRACE_START] && !in_error_recovery(valid_symbols)) {
        while (iswspace(lexer->lookahead)) {
            skip(lexer);
        }

        if (lexer->lookahead != '{') {
            return false;
        }

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

        lexer->result_symbol = BRACE_START;
        return true;
    }

    return false;
}

void *tree_sitter_zsh_external_scanner_create() {
    Scanner *scanner = calloc(1, sizeof(Scanner));
    array_init(&scanner->heredocs);
    array_init(&scanner->context_stack);
#if DEBUG
    fprintf(
        stderr,
        "DEBUG: Scanner created - heredocs.size=%zu, context_stack.size=%zu\n",
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
