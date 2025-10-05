package tree_sitter_zsh_test

import (
	"testing"

	tree_sitter "github.com/tree-sitter/go-tree-sitter"
	tree_sitter_zsh "github.com/tree-sitter/tree-sitter-zsh/bindings/go"
)

func TestCanLoadGrammar(t *testing.T) {
	language := tree_sitter.NewLanguage(tree_sitter_zsh.Language())
	if language == nil {
		t.Errorf("Error loading Bash grammar")
	}
}
