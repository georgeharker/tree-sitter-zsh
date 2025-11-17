#!/bin/bash

if [ `uname` != "Darwin" ]; then
    inplace=(-i)
else
    inplace=(-i '')
fi

version=$1
tree-sitter version ${version}

npm install --package-lock-only
cargo update
