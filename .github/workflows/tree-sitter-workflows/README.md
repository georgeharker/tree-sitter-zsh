# Reusable workflows

This repository contains reusable and reference workflows for parsers.

## Release workflow

This workflow creates a GitHub release and uploads the source code and Wasm binaries.

```yaml
name: Create release

on:
  push:
    tags: ["*"]

concurrency:
  group: ${{github.workflow}}-${{github.ref}}
  cancel-in-progress: true

permissions:
  contents: write
  id-token: write
  attestations: write

jobs:
  release:
    uses: tree-sitter/workflows/.github/workflows/release.yml@main
    with:
      attestations: true
```

### options

```yaml
inputs:
  emscripten-version:
    description: The Emscripten version
    default: ${{vars.EMSCRIPTEN_VERSION || '3.1.64'}}
    type: string
  generate:
    description: Generate the parser artifacts
    default: false
    type: boolean
  attestations:
    description: Generate attestations for artifacts
    default: false
    type: boolean
  release-body:
    description: The body of the release notes
    type: string
  abi-version:
    description: The tree-sitter ABI version
    default: "15"
    type: string
```

<details>
<summary>Verifying attestations</summary>

```sh
gh attestation verify $file --repo $repo --signer-workflow tree-sitter/workflows/.github/workflows/release.yml
  ```

</details>

## Packaging workflows

These workflows can be used to publish parser packages to registries.<br>
Remove the jobs you don't need and create an environment for each job you do.

```yaml
name: Publish package

on:
  push:
    tags: ["*"]

concurrency:
  group: ${{github.workflow}}-${{github.ref}}
  cancel-in-progress: true

jobs:
  npm:
    uses: tree-sitter/workflows/.github/workflows/package-npm.yml@main
    secrets:
      NODE_AUTH_TOKEN: ${{secrets.NPM_TOKEN}}
  crates:
    uses: tree-sitter/workflows/.github/workflows/package-crates.yml@main
    secrets:
      CARGO_REGISTRY_TOKEN: ${{secrets.CARGO_TOKEN}}
  pypi:
    uses: tree-sitter/workflows/.github/workflows/package-pypi.yml@main
    secrets:
      PYPI_API_TOKEN: ${{secrets.PYPI_TOKEN}}
```

### npm options

```yaml
inputs:
  package-name:
    description: The name of the package
    default: ${{github.event.repository.name}}
    type: string
  environment-name:
    description: The name of the environment
    default: npm
    type: string
  node-version:
    description: The NodeJS version
    default: ${{vars.NODE_VERSION || '22'}}
    type: string
  emscripten-version:
    description: The Emscripten version
    default: ${{vars.EMSCRIPTEN_VERSION || '3.1.64'}}
    type: string
  generate:
    description: Generate the parser artifacts
    default: false
    type: boolean
  abi-version:
    description: The tree-sitter ABI version
    default: "15"
    type: string
  attestations:
    description: Generate attestations for the package
    default: false
    type: boolean
secrets:
  NODE_AUTH_TOKEN:
    description: An authentication token for npm
    required: true
```

### crates options

```yaml
inputs:
  package-name:
    description: The name of the package
    default: ${{github.event.repository.name}}
    type: string
  environment-name:
    description: The name of the environment
    default: crates
    type: string
  rust-toolchain:
    description: The Rust toolchain
    default: ${{vars.RUST_TOOLCHAIN || 'stable'}}
    type: string
  generate:
    description: Generate the parser artifacts
    default: false
    type: boolean
  abi-version:
    description: The tree-sitter ABI version
    default: "15"
    type: string
secrets:
  CARGO_REGISTRY_TOKEN:
    description: An authentication token for crates.io
    required: true
```

### pypi options

```yaml
inputs:
  package-name:
    description: The name of the package
    default: ${{github.event.repository.name}}
    type: string
  environment-name:
    description: The name of the environment
    default: pypi
    type: string
  python-version:
    description: The Python version
    default: ${{vars.PYTHON_VERSION || '3.x'}}
    type: string
  generate:
    description: Generate the parser artifacts
    default: false
    type: boolean
  abi-version:
    description: The tree-sitter ABI version
    default: "15"
    type: string
secrets:
  PYPI_API_TOKEN:
    description: An authentication token for pypi
    required: true
```

## Regenerate workflow

This workflow can be used alongside dependabot updates to regenerate the parser.

```yaml
name: Regenerate parser

on:
  pull_request:

permissions:
  contents: write
  pull-requests: write

concurrency:
  group: ${{github.workflow}}-${{github.ref}}
  cancel-in-progress: true

jobs:
  regenerate:
    uses: tree-sitter/workflows/.github/workflows/regenerate.yml@main
    if: github.actor == 'dependabot[bot]'
```

<details>
<summary>Sample <code>.github/dependabot.yml</code> file</summary>

```yaml
version: 2
updates:
  - package-ecosystem: github-actions
    directory: /
    schedule:
      interval: weekly
      day: sunday
    commit-message:
      prefix: ci
    labels:
      - dependencies
    groups:
      actions:
        patterns: ["*"]

  - package-ecosystem: npm
    versioning-strategy: increase
    directory: /
    schedule:
      interval: weekly
      day: sunday
    commit-message:
      prefix: build(deps)
    labels:
      - dependencies
    groups:
      npm:
        patterns: ["*"]

  # - package-ecosystem: cargo
  #   directory: /
  #   schedule:
  #     interval: weekly
  #     day: sunday
  #   commit-message:
  #     prefix: build(deps)
  #   labels:
  #     - dependencies
  #   groups:
  #     cargo:
  #       patterns: ["*"]

  # - package-ecosystem: pip
  #   directory: /
  #   schedule:
  #     interval: weekly
  #     day: sunday
  #   commit-message:
  #     prefix: build(deps)
  #   labels:
  #     - dependencies
  #   groups:
  #     pip:
  #       patterns: ["*"]

  # - package-ecosystem: swift
  #   directory: /
  #   schedule:
  #     interval: weekly
  #     day: sunday
  #   commit-message:
  #     prefix: build(deps)
  #   labels:
  #     - dependencies
  #   groups:
  #     swift:
  #       patterns: ["*"]

  # - package-ecosystem: gomod
  #   directory: /
  #   schedule:
  #     interval: weekly
  #     day: sunday
  #   commit-message:
  #     prefix: build(deps)
  #   labels:
  #     - dependencies
  #   groups:
  #     go:
  #       patterns: ["*"]
```

*You can also uncomment any other ecosystems you want to keep up to date.*

</details>

### options

```yaml
inputs:
  abi-version:
    description: The tree-sitter ABI version
    default: "15"
    type: string
  node-version:
    description: The NodeJS version
    default: ${{vars.NODE_VERSION || 'lts/*'}}
    type: string
  update-scanner:
    description: Update the scanner
    default: false
    type: boolean
  commit-message:
    description: The commit message
    default: "build: regenerate parser [dependabot skip]"
    type: string
  commit-author-name:
    description: The commit author's username
    default: dependabot[bot]
    type: string
  commit-author-email:
    description: The commit author's email
    default: 49699333+dependabot[bot]@users.noreply.github.com
    type: string
```

## Reference workflows

These workflows make use of our parser actions.

### CI workflow

This workflow uses all the following actions:

- [setup](https://github.com/tree-sitter/setup-action)
- [parser-test](https://github.com/tree-sitter/parser-test-action)
- [parse](https://github.com/tree-sitter/parse-action)
- [fuzz](https://github.com/tree-sitter/fuzz-action)

```yaml
name: CI

on:
  push:
    branches: [master]
    paths:
      - grammar.js
      - src/**
      - test/**
      - bindings/**
      - tree-sitter.json
  pull_request:
    paths:
      - grammar.js
      - src/**
      - test/**
      - bindings/**
      - tree-sitter.json

concurrency:
  group: ${{github.workflow}}-${{github.ref}}
  cancel-in-progress: true

jobs:
  test:
    name: Test parser
    runs-on: ${{matrix.os}}
    strategy:
      fail-fast: false
      matrix:
        os: [ubuntu-latest, windows-latest, macos-latest]
    steps:
      - name: Checkout repository
        uses: actions/checkout@v4
      - name: Set up tree-sitter
        uses: tree-sitter/setup-action/cli@v2
      - name: Run parser and binding tests
        uses: tree-sitter/parser-test-action@v2
        with:
          test-rust: true
      - name: Parse sample files
        uses: tree-sitter/parse-action@v4
        id: parse-files
        with:
          files: examples/**
      - name: Upload failures artifact
        uses: actions/upload-artifact@v4
        if: "!cancelled() && steps.parse-files.outcome == 'failure'"
        with:
          name: failures-${{runner.os}}
          path: ${{steps.parse-files.outputs.failures}}
  fuzz:
    name: Fuzz scanner
    runs-on: ubuntu-latest
    steps:
      - name: Checkout repository
        uses: actions/checkout@v4
        with:
          fetch-depth: 2
      - name: Check for scanner changes
        id: scanner-check
        run: |-
          if git diff --quiet HEAD^ -- src/scanner.c; then
            printf 'changed=false\n' >> "$GITHUB_OUTPUT"
          else
            printf 'changed=true\n' >> "$GITHUB_OUTPUT"
          fi
      - name: Run the fuzzer
        uses: tree-sitter/fuzz-action@v4
        if: steps.scanner-check.outputs.changed == 'true'
```

### Dependency update workflow

This workflow uses the [parser-update](https://github.com/tree-sitter/parser-update-action) action.

> [!TIP]
> It's recommended to use dependabot with the [regenerate workflow][] instead.

[regenerate workflow]: #regenerate-workflow

```yaml
name: Update dependencies

on:
  schedule:
    - cron: "0 0 * * 0" # every week
  workflow_dispatch:

permissions:
  contents: write
  pull-requests: write

jobs:
  update:
    runs-on: ubuntu-latest
    steps:
      - name: Checkout repository
        uses: actions/checkout@v4
      - name: Set up NodeJS
        uses: actions/setup-node@v4
      - name: Update dependencies
        uses: tree-sitter/parser-update-action@v1.1
        with:
          parent-name: c
          language-name: cpp
```
