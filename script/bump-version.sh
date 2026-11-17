#!/usr/bin/env bash
#
# Bump the grammar's version in FULL LOCKSTEP, then commit and tag.
#
# Writes ONE version into EVERY version-bearing file this repo has:
#   - tree-sitter.json     metadata.version ("version")
#   - package.json         "version"
#   - package-lock.json    the root package's two "version" entries
#   - Cargo.toml           [package] version = "X.Y.Z"
#   - Cargo.lock           this crate's own entry (not a `cargo update`)
#   - pyproject.toml       [project] version = "X.Y.Z"
#   - Makefile             VERSION := X.Y.Z
#   - CMakeLists.txt       project(VERSION "X.Y.Z"), any prerelease suffix
#                          dropped (CMake rejects one)
#   - src/parser.c         the language metadata (major/minor/patch), which
#                          `tree-sitter generate` writes from tree-sitter.json;
#                          patched in place so CI's generate check still matches
# and creates ONE release tag:
#   - vX.Y.Z               pushing it runs release.yml (GitHub release) and
#                          publish.yml (npm, crates.io, PyPI)
#
# A version with a prerelease suffix (0.65.0-dev.1) is the DEV RELEASE channel:
# release.yml marks its GitHub release as a prerelease (not latest) and
# publish.yml skips every registry. Only reachable as an explicit version;
# patch/minor/major never invent one.
#
# Baseline is the highest current version among the manifests, so nothing ever
# moves backward.
#
# Usage:
#   script/bump-version.sh 0.65.0        # explicit version
#   script/bump-version.sh 0.65.0-dev.1  # prerelease (GitHub release only)
#   script/bump-version.sh patch         # bump patch from the highest current
#   script/bump-version.sh minor         # bump minor, zero patch
#   script/bump-version.sh major         # bump major, zero minor+patch
#
# Options:
#   --no-tag       update + commit, skip the tag
#   --no-commit    update files only (implies --no-tag)
#   -n, --dry-run  print what would change; touch nothing
#
# Refuses a dirty tree (unless --no-commit) so the bump is its own clean commit.
# Pushing is left to you.

set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
die() {
	echo "error: $*" >&2
	exit 1
}

# file|kind pairs. Each kind is one exact pattern (see edit below); a pattern
# that matches the wrong number of times is an error, never a silent no-op.
FILES=(
	"tree-sitter.json|json"
	"package.json|json"
	"package-lock.json|lock-npm"
	"Cargo.toml|toml"
	"Cargo.lock|lock-cargo"
	"pyproject.toml|toml"
	"Makefile|make"
	"CMakeLists.txt|cmake"
	"src/parser.c|parser"
)
# Read the current version from these (the others can't carry a suffix, or
# just repeat one of these).
BASELINE_FILES=(tree-sitter.json package.json Cargo.toml pyproject.toml)

# ---- args -------------------------------------------------------------------
do_commit=1
do_tag=1
dry_run=0
arg=""
for a in "$@"; do case "$a" in
	--no-tag) do_tag=0 ;;
	--no-commit)
		do_commit=0
		do_tag=0
		;;
	-n | --dry-run) dry_run=1 ;;
	-h | --help)
		sed -n '2,43p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
		exit 0
		;;
	-*) die "unknown option: $a" ;;
	*)
		[ -z "$arg" ] || die "unexpected extra argument: $a"
		arg="$a"
		;;
	esac done
[ -n "$arg" ] || die "need a version or bump type (X.Y.Z | patch | minor | major); see --help"

# python3 for every read and write: BSD/macOS sed's -i and addressing differ
# from GNU's, and a sed that silently matches nothing "succeeds".
edit() { # $1=mode(read|write) $2=file $3=kind [$4=new]
	python3 - "$@" <<'PY'
import re, sys
mode, path, kind = sys.argv[1:4]
new = sys.argv[4] if len(sys.argv) > 4 else None
V = r'[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?'
# kind: (pattern with the version in group 2, expected match count)
pats = {
    "json":       (r'("version" *: *")(' + V + r')(")', 1),
    "lock-npm":   (r'("version" *: *")(' + V + r')(")', 2),  # root + packages[""]
    "toml":       (r'(?m)(^version *= *")(' + V + r')(")', 1),
    "lock-cargo": (r'(?m)(^name = "tree-sitter-zsh"\nversion = ")(' + V + r')(")', 1),
    "make":       (r'(?m)(^VERSION := )(' + V + r')()$', 1),
    "cmake":      (r'(?m)(^ +VERSION ")(' + V + r')(")', 1),
}
src = open(path).read()
if kind == "parser":
    pat = (r'(\.metadata = \{\s*\.major_version = )[0-9]+(,\s*\.minor_version = )'
           r'[0-9]+(,\s*\.patch_version = )[0-9]+(,)')
    m = list(re.finditer(pat, src))
    if len(m) != 1:
        sys.exit(f"error: language metadata not found in {path}")
    if mode == "read":
        print(".".join(re.findall(r'_version = ([0-9]+)', m[0].group(0))))
        sys.exit()
    M, mi, p = new.split("-")[0].split(".")
    out = src[:m[0].start()] + (m[0].expand(rf'\g<1>{M}\g<2>{mi}\g<3>{p}\g<4>')) + src[m[0].end():]
else:
    pat, want = pats[kind]
    found = list(re.finditer(pat, src))
    if mode == "read":
        if not found:
            sys.exit(f"error: no {kind} version in {path}")
        print(found[0].group(2))
        sys.exit()
    # The first `want` matches: json/toml take the first one only (later
    # "version" keys belong to other things, e.g. dependencies).
    if len(found) < want:
        sys.exit(f"error: expected {want} {kind} version match(es) in {path}, found {len(found)}")
    v = new.split("-")[0] if kind == "cmake" else new
    out, n = re.subn(pat, lambda m: m.group(1) + v + m.group(3), src, count=want)
    assert n == want
open(path, "w").write(out)
PY
}

# Semver precedence, not plain numeric sort: a prerelease ranks BELOW the
# release it leads to (0.65.0-dev.1 < 0.65.0).
highest() {
	printf '%s\n' "$@" | python3 -c '
import sys
def key(v):
    core, _, pre = v.strip().partition("-")
    nums = [int(x) if x.isdigit() else 0 for x in (core.split(".") + ["0", "0"])[:3]]
    if not pre:
        return (nums, 1, [])
    ids = [(0, int(p), "") if p.isdigit() else (1, 0, p) for p in pre.split(".")]
    return (nums, 0, ids)
print(max((l for l in sys.stdin if l.strip()), key=key).strip())
'
}

curs=()
for f in "${BASELINE_FILES[@]}"; do
	for e in "${FILES[@]}"; do [ "${e%%|*}" = "$f" ] && kind="${e#*|}"; done
	curs+=("$(edit read "$ROOT/$f" "$kind")") || die "could not read version from $f"
done
base="$(highest "${curs[@]}")"

case "$arg" in
major | minor | patch)
	IFS=. read -r M m p <<<"$base"
	[[ "$M" =~ ^[0-9]+$ && "$m" =~ ^[0-9]+$ && "$p" =~ ^[0-9]+$ ]] ||
		die "baseline '$base' not X.Y.Z; pass an explicit version"
	case "$arg" in major)
		M=$((M + 1))
		m=0
		p=0
		;;
	minor)
		m=$((m + 1))
		p=0
		;;
	patch) p=$((p + 1)) ;; esac
	new="$M.$m.$p"
	;;
*)
	[[ "$arg" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z][0-9A-Za-z.-]*)?$ ]] ||
		die "'$arg' is not a valid X.Y.Z or X.Y.Z-prerelease version"
	new="$arg"
	;;
esac
TAG="v$new"

echo "files:"
for e in "${FILES[@]}"; do
	f="${e%%|*}"
	kind="${e#*|}"
	printf '  %-14s %s\n' "$(edit read "$ROOT/$f" "$kind")" "$f"
done
echo "baseline (max) : $base"
echo "new version    : $new   (tag $TAG)"
case "$new" in *-*) echo "channel        : prerelease (GitHub prerelease only; no registry publish)" ;;
*) echo "channel        : release (GitHub release + npm, crates.io, PyPI)" ;; esac

if [ "$(highest "$base" "$new")" = "$base" ] && [ "$new" != "$base" ]; then
	die "new version $new is lower than baseline $base; refusing to go backwards"
fi
[ "$new" != "$base" ] || die "already at $new"

if [ -f "$ROOT/CHANGELOG.md" ]; then
	grep -qE "^## \[$(printf '%s' "$new" | sed 's/[.]/\\./g')\]" "$ROOT/CHANGELOG.md" ||
		die "CHANGELOG.md has no '## [$new]' heading; add the release notes first"
fi

if [ "$dry_run" = 1 ]; then
	echo "(dry run) no files changed"
	exit 0
fi

if [ "$do_commit" = 1 ]; then
	excl=()
	for e in "${FILES[@]}"; do excl+=(":!${e%%|*}"); done
	if [ -n "$(git -C "$ROOT" status --porcelain -- "${excl[@]}")" ]; then
		die "working tree has unrelated changes; commit or stash them first"
	fi
fi

if [ "$do_tag" = 1 ] && git -C "$ROOT" rev-parse -q --verify "refs/tags/$TAG" >/dev/null; then
	die "tag $TAG already exists"
fi

paths=()
for e in "${FILES[@]}"; do
	f="${e%%|*}"
	kind="${e#*|}"
	edit write "$ROOT/$f" "$kind" "$new"
	paths+=("$f")
done
echo "updated ${#FILES[@]} files -> $new"

[ "$do_commit" = 0 ] && {
	echo "files updated; skipped commit (--no-commit)"
	exit 0
}

git -C "$ROOT" add -- "${paths[@]}"
git -C "$ROOT" commit -q -m "release: $TAG"
echo "committed."

if [ "$do_tag" = 1 ]; then
	git -C "$ROOT" tag -a "$TAG" -m "tree-sitter-zsh $new"
	echo "tagged $TAG"
	echo
	echo "push with:  git push --atomic origin main $TAG"
else
	echo "skipped tag (--no-tag)"
fi
