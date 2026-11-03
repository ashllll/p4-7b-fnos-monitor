#!/usr/bin/env bash
# Baseline machine gate shared by every repo on this NAS.
#   scripts/preflight.sh            full run  (pre-push and CI)
#   scripts/preflight.sh --staged   fast run  (pre-commit)
#
# Design: the full run only checks things that cannot be wrong in a healthy
# tree (conflict markers, portability-hostile paths). Everything that depends
# on history — large blobs, credentials — is gated on the staged diff, because
# a repo may legitimately carry a 20 MB PDF or an empty __init__.py from before
# this gate existed.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1

MODE=full
[ "${1:-}" = "--staged" ] && MODE=staged

fail=0
note() { printf '  %-30s %s\n' "$1" "$2"; }
bad()  { printf '  %-30s FAIL\n' "$1"; printf '%s\n' "$2" | sed -n '1,6p' | sed 's/^/      /'; fail=1; }

# 1. merge-conflict markers anywhere in the tracked tree
hits=$(git grep -lE '^(<<<<<<< |>>>>>>> |=======$)' -- . 2>/dev/null || true)
[ -z "$hits" ] && note conflict-markers pass || bad conflict-markers "$hits"

# 2. paths that break on Windows or are otherwise not portable
# awk instead of a case inside $(): bash 3.2 (macOS /bin/bash) fails to parse
# `case` nested in a command substitution pipeline.
hits=$(git ls-files 2>/dev/null | awk -F/ '$NF ~ /^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$/ { printf "%s (windows reserved name)\n", $0 }' || true)
[ -z "$hits" ] && note path-sanity pass || bad path-sanity "$hits"

# 3-4. staged-only checks: these judge what you are about to commit
if [ "$MODE" = staged ]; then
  staged=$(git diff --cached --name-only --diff-filter=ACMR 2>/dev/null)

  hits=$(printf '%s\n' "$staged" | while IFS= read -r f; do
    [ -n "$f" ] && [ -f "$f" ] || continue
    sz=$(wc -c < "$f" 2>/dev/null | tr -d '[:space:]')
    sz=${sz:-0}
    [ "$sz" -gt 10485760 ] && echo "$f ($((sz / 1048576)) MB)"
  done)
  [ -z "$hits" ] && note blobs-over-10MB pass || bad blobs-over-10MB "$hits"

  # 占位符只对“命中的密钥片段”生效，不能按整行丢弃：
  # 一行里同时有真密钥和 example 时，整行过滤会静默漏检。
  cred=$(git diff --cached -U0 2>/dev/null | grep -E '^\+' | grep -vE '^\+\+\+' | sed 's/^+//' \
    | grep -nE '(-----BEGIN [A-Z ]*PRIVATE KEY-----)|(gh[pousr]_[A-Za-z0-9]{20,})|(sk-[A-Za-z0-9]{20,})|(AKIA[0-9A-Z]{16})|(xox[abprs]-[A-Za-z0-9-]{10,})' \
    | python3 -c 'import re,sys
pat=re.compile(r"-----BEGIN [A-Z ]*PRIVATE KEY-----|gh[pousr]_[A-Za-z0-9]{20,}|sk-[A-Za-z0-9]{20,}|AKIA[0-9A-Z]{16}|xox[abprs]-[A-Za-z0-9-]{10,}")
ph=("<","xxx","redacted","example","your_")
for line in sys.stdin:
    if any(not any(h in m.group(0).lower() for h in ph) for m in pat.finditer(line)):
        sys.stdout.write(line)' || true)
  [ -z "$cred" ] && note credential-scan pass || bad credential-scan "$cred"
fi

echo
if [ "$fail" -eq 0 ]; then echo "preflight: baseline passed ($MODE)"; exit 0; fi
echo "preflight: baseline FAILED ($MODE)" >&2
exit 1
