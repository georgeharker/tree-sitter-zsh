# tree-sitter-zsh

[![CI](https://github.com/georgeharker/tree-sitter-zsh/actions/workflows/ci.yml/badge.svg)](https://github.com/georgeharker/tree-sitter-zsh/actions/workflows/ci.yml)
[![discord][discord]](https://discord.gg/w7nTvsVJhm)
[![matrix][matrix]](https://matrix.to/#/#tree-sitter-chat:matrix.org)
[![crates][crates]](https://crates.io/crates/tree-sitter-zsh)
[![npm][npm]](https://www.npmjs.com/package/tree-sitter-zsh)
[![pypi][pypi]](https://pypi.org/project/tree-sitter-zsh)

Zsh grammar for [tree-sitter](https://github.com/tree-sitter/tree-sitter).

> [!NOTE]
> this is not the same as [tree-sitter/tree-sitter-zsh](https://github.com/tree-sitter-grammars/tree-sitter-zsh) 
> which has been archived, but rather a complete reworking starting
> from the bash grammar.

### Known issues:
 
Glob qualifier delimeters for 's::' and 'n::' and 'b::' must be ':' rather
than arbitrary paired chars.

## Installation

For neovim, add the following to your config 

```lua
    vim.api.nvim_create_autocmd("User", {
        pattern = "TSUpdate",
        callback = function()
            require("nvim-treesitter.parsers").zsh = {
                install_info = {
                    "https://github.com/georgeharker/tree-sitter-zsh",
                    generate_from_json = false, -- only needed if repo does not contain `src/grammar.json` either
                    queries = 'nvim-queries', -- also install queries from given directory
                },
                tier = 3,
            }
        end,
    })

```

In addition to enabling treesitter for zsh files.

## Development

Install the dependencies:

```sh
npm install
```

Build and run the tests:

```sh
npm run build
npm run test
```

Run the build and tests in watch mode:

```sh
npm run test:watch
```

### References

- [Zsh reference docs](https://zsh.sourceforge.io/Doc/Release/zsh_toc.html)
- [Shell command language specification](http://pubs.opengroup.org/onlinepubs/9699919799/utilities/V3_chap02.html)
- [mvdnan/sh - a shell parser in go](https://github.com/mvdan/sh)
- [tree-sitter-bash](https://github.com/tree-sitter/tree-sitter-bash)

[ci]: https://img.shields.io/github/actions/workflow/status/tree-sitter/tree-sitter-zsh/ci.yml?logo=github&label=CI
[discord]: https://img.shields.io/discord/1063097320771698699?logo=discord&label=discord
[matrix]: https://img.shields.io/matrix/tree-sitter-chat%3Amatrix.org?logo=matrix&label=matrix
[npm]: https://img.shields.io/npm/v/tree-sitter-zsh?logo=npm
[crates]: https://img.shields.io/crates/v/tree-sitter-zsh?logo=rust
[pypi]: https://img.shields.io/pypi/v/tree-sitter-zsh?logo=pypi&logoColor=ffd242
