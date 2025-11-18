#!/bin/bash

if [ `uname` != "Darwin" ]; then
    inplace=(-i)
else
    inplace=(-i '')
fi

version=$1
tree-sitter version ${version}
tree-sitter generate

npm install --package-lock-only
cargo update
