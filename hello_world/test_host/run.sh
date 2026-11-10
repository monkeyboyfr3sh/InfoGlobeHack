#!/bin/sh
# C/JS agreement check for the custom animations (globe_anim.c vs web/src/lib/anim.js).
# From the repo root (Git Bash on Windows works):   sh hello_world/test_host/run.sh
# Regenerates the vectors from anim.js (needs node), builds the C side with the
# host compiler (CC, default gcc; MSYS2: CC=/c/msys64/ucrt64/bin/gcc.exe) and compares.
set -e
here=$(cd "$(dirname "$0")" && pwd)
out="$here/../build/test_host"
mkdir -p "$out"
CC=${CC:-gcc}
if [ -x /c/msys64/ucrt64/bin/gcc.exe ] && ! command -v "$CC" >/dev/null 2>&1; then CC=/c/msys64/ucrt64/bin/gcc.exe; fi
case "$CC" in */*) PATH="$(dirname "$CC"):$PATH";; esac   # the compiler's DLLs
node "$here/gen_vectors.mjs"
"$CC" -std=c11 -Wall -Wextra -Werror -O2 -I "$here/../main/inc" \
    "$here/../main/src/globe_anim.c" "$here/anim_host_test.c" -o "$out/anim_host_test"
"$out/anim_host_test" "$here/anim_vectors.txt"
