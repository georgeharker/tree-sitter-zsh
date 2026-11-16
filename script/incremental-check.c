// Incremental-parse check for tree-sitter-zsh. Built by script/check-context.
//
// For each file, and for each line start: insert "# edit\n" there, and
// insert "q" after the line's first word; re-parse incrementally (reusing
// the tree of the unedited text) and compare with a fresh parse of the
// edited text. The two must agree whenever the edited text parses without
// errors (error recovery may legitimately differ). A disagreement means
// the scanner's tokens depend on something other than its serialized state
// and the valid symbols, e.g. unserialized state or a check on a symbol
// whose validity changes with how much of the tree was reused.
//
// Prints one line per disagreement: FILE<TAB>EDIT<TAB>OFFSET, and exits 1
// if there were any.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tree_sitter/api.h>

const TSLanguage *tree_sitter_zsh(void);

static TSPoint point_at(const char *s, uint32_t off) {
    TSPoint p = {0, 0};
    for (uint32_t i = 0; i < off; i++) {
        if (s[i] == '\n') {
            p.row++;
            p.column = 0;
        } else {
            p.column++;
        }
    }
    return p;
}

// 1 if the incremental and fresh parses of the edited text disagree.
static int disagrees(TSParser *p, const char *src, uint32_t n, uint32_t off,
                     const char *ins) {
    uint32_t il = (uint32_t)strlen(ins);
    TSTree *old = ts_parser_parse_string(p, NULL, src, n);
    char *dst = malloc(n + il + 1);
    memcpy(dst, src, off);
    memcpy(dst + off, ins, il);
    memcpy(dst + off + il, src + off, n - off);
    dst[n + il] = 0;
    TSInputEdit e = {
        .start_byte = off,
        .old_end_byte = off,
        .new_end_byte = off + il,
        .start_point = point_at(src, off),
        .old_end_point = point_at(src, off),
        .new_end_point = point_at(dst, off + il),
    };
    ts_tree_edit(old, &e);
    TSTree *incr = ts_parser_parse_string(p, old, dst, n + il);
    TSTree *fresh = ts_parser_parse_string(p, NULL, dst, n + il);
    int bad = 0;
    if (!ts_node_has_error(ts_tree_root_node(fresh))) {
        char *a = ts_node_string(ts_tree_root_node(incr));
        char *b = ts_node_string(ts_tree_root_node(fresh));
        bad = strcmp(a, b) != 0;
        free(a);
        free(b);
    }
    ts_tree_delete(fresh);
    ts_tree_delete(incr);
    ts_tree_delete(old);
    free(dst);
    return bad;
}

int main(int argc, char **argv) {
    int failures = 0;
    TSParser *p = ts_parser_new();
    ts_parser_set_language(p, tree_sitter_zsh());
    for (int a = 1; a < argc; a++) {
        FILE *f = fopen(argv[a], "rb");
        if (!f) {
            fprintf(stderr, "%s: unreadable\n", argv[a]);
            failures++;
            continue;
        }
        fseek(f, 0, SEEK_END);
        long len = ftell(f);
        fseek(f, 0, SEEK_SET);
        char *src = malloc(len + 1);
        size_t got = fread(src, 1, len, f);
        src[got] = 0;
        fclose(f);
        uint32_t n = (uint32_t)got;
        for (uint32_t off = 0; off < n; off++) {
            if (off != 0 && src[off - 1] != '\n') {
                continue;
            }
            if (disagrees(p, src, n, off, "# edit\n")) {
                printf("%s\tcomment-line\t%u\n", argv[a], off);
                failures++;
            }
            uint32_t j = off;
            while (j < n && (src[j] == ' ' || src[j] == '\t')) {
                j++;
            }
            while (j < n && src[j] != '\n' && src[j] != ' ') {
                j++;
            }
            if (j < n && src[j] == ' ' && disagrees(p, src, n, j + 1, "q")) {
                printf("%s\tinsert-char\t%u\n", argv[a], j + 1);
                failures++;
            }
        }
        free(src);
    }
    ts_parser_delete(p);
    return failures != 0;
}
