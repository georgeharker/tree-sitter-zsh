// Context-stack audit harness for the zsh external scanner.
// Built by script/context-audit with -DZSH_SCANNER_AUDIT. For each file it
// prints the ERROR/MISSING count; the scanner then reports (on destroy) the
// contexts still open at the end and any mismatched pops. A complete file
// should end with depth=0.
#include <stdio.h>
#include <stdlib.h>
#include <tree_sitter/api.h>

const TSLanguage *tree_sitter_zsh(void);

static unsigned count_errors(TSNode n) {
    unsigned c = ts_node_is_error(n) || ts_node_is_missing(n);
    for (uint32_t i = 0; i < ts_node_child_count(n); i++)
        c += count_errors(ts_node_child(n, i));
    return c;
}

int main(int argc, char **argv) {
    for (int a = 1; a < argc; a++) {
        FILE *f = fopen(argv[a], "rb");
        if (!f) {
            fprintf(stderr, "%s\tunreadable\n", argv[a]);
            continue;
        }
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        char *src = malloc(n + 1);
        fread(src, 1, n, f);
        src[n] = 0;
        fclose(f);
        // The scanner is created and destroyed within the parse call, so
        // its "AUDIT depth=... stack=... mismatch=..." report is printed
        // during parsing: name the file first, add the error count after.
        fprintf(stderr, "%s\t", argv[a]);
        TSParser *p = ts_parser_new();
        ts_parser_set_language(p, tree_sitter_zsh());
        TSTree *t = ts_parser_parse_string(p, NULL, src, (uint32_t)n);
        fprintf(stderr, "errors=%u\n", count_errors(ts_tree_root_node(t)));
        ts_tree_delete(t);
        ts_parser_delete(p);
        free(src);
    }
    return 0;
}
