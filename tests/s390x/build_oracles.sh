#!/usr/bin/env sh
set -eu

CC_BIN="${CC:-cc}"
CFLAGS_EXTRA="${CFLAGS:--O2 -g}"

mkdir -p tests/s390x/ffi_abi/build tests/s390x/callbacks/build

"$CC_BIN" -shared -fPIC -std=c11 $CFLAGS_EXTRA \
  -o tests/s390x/ffi_abi/build/liboracle.so \
  tests/s390x/ffi_abi/oracle.c -lm

"$CC_BIN" -shared -fPIC -std=c11 $CFLAGS_EXTRA \
  -o tests/s390x/callbacks/build/libcallback_oracle.so \
  tests/s390x/callbacks/callback_oracle.c -lm
