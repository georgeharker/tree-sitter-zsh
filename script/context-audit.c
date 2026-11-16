// Context-stack audit harness for the zsh external scanner.
// Built by script/context-audit with -DZSH_SCANNER_AUDIT, against
// tree-sitter's single-file library build so it can read the final tree's
// scanner state (an internal, not part of the public API).
//
// For each file it prints one line:
//   FILE<TAB>AUDIT depth=N stack=... mismatch=... underflow=... errors=N
// depth/stack are the contexts still open in the scanner state stored on
// the tree's last external token: the state the parser would restore for
// a token at the end of the file, on the parse that won. (Reading the live
// scanner object instead would report whichever GLR branch scanned last,
// which can be a branch that lost.) A complete file should have depth=0.
#include <stdio.h>
#include <stdlib.h>

#include "api.h"
#include "subtree.h"
#include "tree.h"

const TSLanguage *tree_sitter_zsh(void);
void *tree_sitter_zsh_external_scanner_create(void);
void tree_sitter_zsh_external_scanner_destroy(void *);
void tree_sitter_zsh_external_scanner_deserialize(void *, const char *,
                                                  unsigned);
void tree_sitter_zsh_audit_report(void *);

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
        TSParser *p = ts_parser_new();
        ts_parser_set_language(p, tree_sitter_zsh());
        TSTree *t = ts_parser_parse_string(p, NULL, src, (uint32_t)n);

        void *scanner = tree_sitter_zsh_external_scanner_create();
        Subtree last = ts_subtree_last_external_token(t->root);
        if (last.ptr) {
            const ExternalScannerState *st = &last.ptr->external_scanner_state;
            tree_sitter_zsh_external_scanner_deserialize(
                scanner, ts_external_scanner_state_data(st), st->length);
        }
        fprintf(stderr, "%s\t", argv[a]);
        tree_sitter_zsh_audit_report(scanner);
        fprintf(stderr, "errors=%u\n", count_errors(ts_tree_root_node(t)));
        tree_sitter_zsh_external_scanner_destroy(scanner);

        ts_tree_delete(t);
        ts_parser_delete(p);
        free(src);
    }
    return 0;
}
