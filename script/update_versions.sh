#!/bin/bash

if [ `uname` != "Darwin" ]; then
    inplace=(-i)
else
    inplace=(-i '')
fi

version=$1
sed "${inplace[@]}" -Ee 's/version *= *"([0-9]+\.[0-9]+\.[0-9]+)"/version = "'${version}'"/' Cargo.toml
sed "${inplace[@]}" -Ee 's/"version": *"([0-9]+\.[0-9]+\.[0-9]+)"/"version": "'${version}'"/' package.json
sed "${inplace[@]}" -Ee 's/version *= *"([0-9]+\.[0-9]+\.[0-9]+)"/version = "'${version}'"/' pyproject.toml

npm install --package-lock-only
cargo update
