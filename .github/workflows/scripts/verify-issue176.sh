#!/usr/bin/env bash
# Regression checks for llvm-msvc issue #176 (32-bit Linux ISel crash).
set -euo pipefail

INSTALL="${1:?usage: $0 /path/to/install/prefix}"
REPO="${2:-.}"

BIN="$INSTALL/bin"
export PATH="$BIN:$PATH"

CLANG="$BIN/clang++"
LLC="$BIN/llc"

if [[ ! -x "$CLANG" ]]; then
  echo "clang++ not found under $BIN" >&2
  exit 1
fi
if [[ ! -x "$LLC" ]]; then
  echo "llc not found under $BIN" >&2
  exit 1
fi

TEST_LL="$REPO/llvm/test/CodeGen/X86/issue176-abort-i686-linux.ll"
if [[ ! -f "$TEST_LL" ]]; then
  echo "missing test IR: $TEST_LL" >&2
  exit 1
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

run() {
  echo ">>> $*"
  "$@"
}

echo "=== toolchain ==="
run "$CLANG" --version
run "$LLC" --version

echo "=== clang: i686-linux __builtin_abort @ -O1 ==="
cat >"$TMP/issue176.cpp" <<'EOF'
void f() { __builtin_abort(); }
EOF
run "$CLANG" --target=i686-pc-linux-gnu -O1 -c "$TMP/issue176.cpp" -o "$TMP/issue176.o"
test -s "$TMP/issue176.o"

echo "=== llc: issue176-abort-i686-linux.ll (3 triples) ==="
for triple in i686-pc-linux-gnu i686-unknown-linux-gnu i686-linux-android; do
  echo "--- $triple ---"
  OUT="$TMP/issue176-${triple//[^a-zA-Z0-9]/_}.s"
  run "$LLC" <"$TEST_LL" -mtriple="$triple" -relocation-model=pic -o "$OUT"
  grep -q calll "$OUT"
done

echo "issue #176 regression checks passed"
