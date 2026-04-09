# s390x ISA Lab

This stream is separate from the main s390x bring-up and remediation work.

- Local worktree: `/Users/kaitlyndavis/dev/github.com/k8ika0s/luajit2-s390x-isa-lab`
- Branch: `k8ika0s/s390x-isa-lab`
- Dedicated host: `kdz1`
- Dedicated remote root: `/root/luajit2-s390x-isa`
- Dedicated local artifacts root: `artifacts/s390x-isa/`

## Current Focus

The first execution lane is helper-based string and text kernels. This lets the ISA lab prove wins behind existing helper seams before any DynASM vector-register or backend reshaping.

Research-only lanes for now:

- guarded storage
- transactional execution
- PLO-backed runtime microprimitives

## Host Setup

Use the local wrapper:

```bash
tools/s390x/isa_lab_bootstrap.sh
```

That wrapper:

- creates `/root/luajit2-s390x-isa/{canon/repo,runs,archive}` on `kdz1`
- syncs the tracked repo into `/root/luajit2-s390x-isa/canon/repo`
- runs the shared remote bootstrap against a per-run archive root
- copies the bootstrap artifacts back under `artifacts/s390x-isa/bootstrap/<run-id>/`

The shared bootstrap now ensures these packages for the lab:

- `gcc`
- `clang`
- `make`
- `git`
- `rsync`
- `python3`
- `perl`
- `perl-Test-Harness`
- `curl`
- `gdb`
- `perf`
- `binutils`
- `elfutils`
- `elfutils-libelf-devel`
- `libunwind-devel`
- `ccache`
- `diffutils`
- `which`
- `jq`
- `util-linux`
- `kernel-tools`
- `ripgrep`

## Running The ISA Stream

Use the ISA wrapper instead of calling the driver directly:

```bash
tools/s390x/isa_lab_run.sh --stage contract
tools/s390x/isa_lab_run.sh --stage perf --compiler gcc --mode release --jit on --perf-family dispatch_trace
```

The wrapper pins the stream to:

- host `kdz1`
- local artifacts `artifacts/s390x-isa/`
- remote run roots under `/root/luajit2-s390x-isa/runs`
- stream label `isa-lab`
- default text overrides `LUAJIT_S390X_TEXT_PATTERN_MODE=span8` and `LUAJIT_S390X_TEXT_TRANSFORM_MODE=ascii8` unless explicitly overridden in the shell

## First Experiment

The first active text-kernel experiment is gated by:

- `LUAJIT_S390X_TEXT_MODE=generic`
- `LUAJIT_S390X_TEXT_MODE=libc`
- `LUAJIT_S390X_TEXT_MODE=scan2`
- `LUAJIT_S390X_TEXT_MODE=ascii8`
- `LUAJIT_S390X_TEXT_COMPARE_MODE=generic|libc`
- `LUAJIT_S390X_TEXT_FIND_MODE=generic|libc|scan2`
- `LUAJIT_S390X_TEXT_TRANSFORM_MODE=generic|libc|bswap64|ascii8`
- `LUAJIT_S390X_TEXT_PATTERN_MODE=generic|span8`

Current behavior:

- the aggregate `LUAJIT_S390X_TEXT_MODE` still sets all three lanes at once
- `LUAJIT_S390X_TEXT_COMPARE_MODE` overrides ordered compare and defaults to `generic`
- `LUAJIT_S390X_TEXT_FIND_MODE` overrides fixed-string search and defaults to `generic`
- `LUAJIT_S390X_TEXT_TRANSFORM_MODE` overrides `lower`/`upper`/`reverse` and defaults to `generic`
- `LUAJIT_S390X_TEXT_PATTERN_MODE` overrides simple `%class+` pattern spans and defaults to `generic`
- `libc` routes ordered string compare through `memcmp` and fixed-string find through `memmem`/`memchr`
- `scan2` routes fixed-string search through a first-byte scan with last-byte and middle-byte filtering
- `bswap64` routes `string.reverse` through eight-byte byte-swap chunks and leaves `lower`/`upper` on the scalar helper path
- `ascii8` routes `string.lower` and `string.upper` through an eight-byte ASCII fold helper and leaves `reverse` on the existing path
- `span8` routes simple escaped ASCII classes such as `%a+` and `%d+` through an eight-byte span probe and an eight-byte candidate-start prefilter, then falls back for unsupported classes and full pattern machinery
- `lower`/`upper`/`reverse` still use scalar helper copies for now so later scalar/vector kernels can land without changing Lua-visible call sites again

Current `kdz1` readout:

- `libc` is about 1-3% faster on the short fixed-hit `find_fixed` cases, but about 3-5% slower on the longer miss-heavy `find_miss` cases
- `scan2` stayed close to generic, but did not win on either `find_fixed` or `find_miss`
- `bswap64` improved `reverse_ascii` by about 3.3-3.9% in the first same-revision run
- that same run also moved `compare_order` by about 5-6% and the find rows by about 0.3-4%, so the transform read is still noisy rather than clean
- in the isolated `text_casefold` family, `ascii8` improved `lower_ascii` by about 21-28% and `upper_ascii` by about 32-36%
- in the broader `string_kernels` family, `ascii8` still improved `lower_ascii` by about 10-14% and `upper_ascii` by about 13-14%, while `compare_order`, `reverse_ascii`, and `find_fixed` stayed roughly flat
- the same mixed-family run also improved `find_miss` by about 5-6%, which looks favorable but should be treated as secondary until it repeats
- in the first isolated `text_patterns` family, `span8` improved `match_alpha` by about 6-7% and `match_digit` by about 12-13%, but `gmatch_words` regressed by about 0.5-0.6%
- after widening the seam with a candidate-start prefilter and seek-heavy workloads, `span8` improved `match_alpha` by about 5-7%, `match_digit` by about 10-11%, `match_alpha_seek` by about 20-21%, `match_digit_seek` by about 14-15%, `gmatch_words` by about 26-27%, and `gmatch_words_sparse` by about 48-52%
- in the broader `text_mixed` family, `span8` improved `header_match` by about 18-21%, `log_words` by about 13-14%, `log_numbers` by about 22-24%, and `sparse_words` by about 45-46%
- the same mixed-family run left the unrelated `control_mix` row roughly flat to mildly favorable overall; the hot and small cases were faster by about 7%, while medium moved the other way by about 2.5%, which reads as noise rather than a clear collateral regression
- in the combined `text_combo` family with `span8+ascii8` enabled together, `casefold_bulk` improved by about 20-23%, `header_lower` by about 24-25%, `sparse_upper_tokens` by about 33-34%, and `log_lower_tokens` by about 6-7%
- the combined family left the unrelated `control_reverse_find` row effectively flat, within about +/-1%, which is the cleanest collateral read so far for the text lane
- in the first non-text carry check, `mixed_noffi` moved the wrong way by about 1.1-2.4% with the default-on text modes, which is small but real and should be treated as the current downside bound
- in the second non-text carry check, `dispatch_trace` moved the favorable way across every row: `hotexit_loop` improved by about 2-10%, `numeric_loop` by about 6.7-7.9%, and `side_exit_loop` by about 3.9-4.9%
- the `dispatch_trace` restamp repeated the same direction with generic-first ordering: `hotexit_loop` still improved by about 1.2-1.4%, `numeric_loop` by about 6.7-7.7%, and `side_exit_loop` by about 2.5-3.9%
- in the third non-text carry check, `iterator_table` split by shape: `pairs_array_sum` improved by about 7.1-7.7%, while `pairs_sum` regressed by about 4.4-5.9%
- in the fourth non-text carry check, `bitops_mix` stayed effectively flat, within about -0.4% to +0.7%, which is clean neutral carry behavior
- in the fifth non-text carry check, `vararg_paths` moved the favorable way across most rows: `retlast_loop` improved by about 0.8-3.2%, `sum_loop` by about 2.1-4.1%, and `retconst_loop` was flat to about 1.1% faster
- in the sixth non-text carry check, `be_helpers` moved the wrong way: `be_pack_loop` regressed by about 2.2-3.2%, while `number_helper_loop` ranged from flat to about 4.1% slower
- in the seventh non-text carry check, `mixed_ffi` moved the favorable way across all rows, improving by about 1.2-2.6%
- because the non-text carry checks now show two regression families, one mixed family, one neutral family, and three favorable families, the ISA lab wrapper keeps `span8+ascii8` enabled by default for now; this remains a lab-only policy, not a mainline default recommendation
- the current carry story is still positive enough to keep using the lab defaults for ongoing ISA exploration, but the helper-side regressions mean this is not ready for promotion outside the lab until more unrelated families have been qualified
- because neither variant is a clean policy win, fixed-string search stays opt-in for now
- because the transform signal is promising but not isolated yet, `bswap64` also stays opt-in for now
- because `ascii8` now wins in both isolated and mixed text families, it is the first transform lane that looks worth broader qualification; it still stays opt-in until we restamp it again and run it against higher-level workloads
- because the widened `span8` lane now helps both direct `string.match` and repeated `string.gmatch` scans, pattern acceleration has moved from narrow probe to serious candidate
- because the combined `span8+ascii8` family now shows broad wins with a flat neutral control row, the next step is to trial both as default-on in ISA-lab builds rather than keeping them as opt-in-only probes

Initial validation commands:

```bash
LUAJIT_S390X_TEXT_MODE=generic tools/s390x/isa_lab_run.sh --stage interp --suite pure_lua --run-id isa-text-generic-purelua
LUAJIT_S390X_TEXT_MODE=libc tools/s390x/isa_lab_run.sh --stage interp --suite pure_lua --run-id isa-text-libc-purelua

LUAJIT_S390X_TEXT_MODE=generic tools/s390x/isa_lab_run.sh --stage perf --jit off --suite perf_bench --perf-family string_kernels --run-id isa-text-generic-perf
LUAJIT_S390X_TEXT_MODE=libc tools/s390x/isa_lab_run.sh --stage perf --jit off --suite perf_bench --perf-family string_kernels --run-id isa-text-libc-perf

LUAJIT_S390X_TEXT_COMPARE_MODE=generic \
LUAJIT_S390X_TEXT_FIND_MODE=libc \
tools/s390x/isa_lab_run.sh --stage interp --suite pure_lua --run-id isa-text-split-purelua

LUAJIT_S390X_TEXT_COMPARE_MODE=generic \
LUAJIT_S390X_TEXT_FIND_MODE=libc \
tools/s390x/isa_lab_run.sh --stage perf --jit off --suite perf_bench --perf-family string_kernels --run-id isa-text-split-perf

LUAJIT_S390X_TEXT_COMPARE_MODE=generic \
LUAJIT_S390X_TEXT_FIND_MODE=scan2 \
tools/s390x/isa_lab_run.sh --stage interp --suite pure_lua --run-id isa-text-scan2-purelua

LUAJIT_S390X_TEXT_COMPARE_MODE=generic \
LUAJIT_S390X_TEXT_FIND_MODE=scan2 \
tools/s390x/isa_lab_run.sh --stage perf --jit off --suite perf_bench --perf-family string_kernels --run-id isa-text-scan2-perf

LUAJIT_S390X_TEXT_TRANSFORM_MODE=bswap64 \
tools/s390x/isa_lab_run.sh --stage interp --suite pure_lua --run-id isa-text-bswap64-purelua

LUAJIT_S390X_TEXT_TRANSFORM_MODE=bswap64 \
tools/s390x/isa_lab_run.sh --stage perf --jit off --suite perf_bench --perf-family string_kernels --run-id isa-text-bswap64-v1-perf

LUAJIT_S390X_TEXT_TRANSFORM_MODE=ascii8 \
tools/s390x/isa_lab_run.sh --stage interp --suite pure_lua --run-id isa-text-ascii8-purelua

LUAJIT_S390X_TEXT_TRANSFORM_MODE=generic \
tools/s390x/isa_lab_run.sh --stage perf --jit off --suite perf_bench --perf-family text_casefold --run-id isa-casefold-generic-v1-perf

LUAJIT_S390X_TEXT_TRANSFORM_MODE=ascii8 \
tools/s390x/isa_lab_run.sh --stage perf --jit off --suite perf_bench --perf-family text_casefold --run-id isa-casefold-ascii8-v1-perf

LUAJIT_S390X_TEXT_COMPARE_MODE=generic \
LUAJIT_S390X_TEXT_FIND_MODE=generic \
LUAJIT_S390X_TEXT_TRANSFORM_MODE=generic \
tools/s390x/isa_lab_run.sh --stage perf --jit off --suite perf_bench --perf-family string_kernels --run-id isa-text-kernels-generic-v3-perf

LUAJIT_S390X_TEXT_COMPARE_MODE=generic \
LUAJIT_S390X_TEXT_FIND_MODE=generic \
LUAJIT_S390X_TEXT_TRANSFORM_MODE=ascii8 \
tools/s390x/isa_lab_run.sh --stage perf --jit off --suite perf_bench --perf-family string_kernels --run-id isa-text-kernels-ascii8-v1-perf

LUAJIT_S390X_TEXT_PATTERN_MODE=span8 \
tools/s390x/isa_lab_run.sh --stage interp --suite pure_lua --run-id isa-text-span8-purelua

LUAJIT_S390X_TEXT_PATTERN_MODE=generic \
tools/s390x/isa_lab_run.sh --stage perf --jit off --suite perf_bench --perf-family text_patterns --run-id isa-text-patterns-generic-v1-perf

LUAJIT_S390X_TEXT_PATTERN_MODE=span8 \
tools/s390x/isa_lab_run.sh --stage perf --jit off --suite perf_bench --perf-family text_patterns --run-id isa-text-patterns-span8-v1-perf

LUAJIT_S390X_TEXT_PATTERN_MODE=span8 \
tools/s390x/isa_lab_run.sh --stage interp --suite pure_lua --run-id isa-text-span8-prefilter-purelua

LUAJIT_S390X_TEXT_PATTERN_MODE=generic \
tools/s390x/isa_lab_run.sh --stage perf --jit off --suite perf_bench --perf-family text_patterns --run-id isa-text-patterns-generic-v2-perf

LUAJIT_S390X_TEXT_PATTERN_MODE=span8 \
tools/s390x/isa_lab_run.sh --stage perf --jit off --suite perf_bench --perf-family text_patterns --run-id isa-text-patterns-span8-v2-perf

LUAJIT_S390X_TEXT_PATTERN_MODE=span8 \
tools/s390x/isa_lab_run.sh --stage interp --suite pure_lua --run-id isa-text-mixed-span8-purelua-v2

LUAJIT_S390X_TEXT_PATTERN_MODE=generic \
tools/s390x/isa_lab_run.sh --stage perf --jit off --suite perf_bench --perf-family text_mixed --run-id isa-text-mixed-generic-v2-perf

LUAJIT_S390X_TEXT_PATTERN_MODE=span8 \
tools/s390x/isa_lab_run.sh --stage perf --jit off --suite perf_bench --perf-family text_mixed --run-id isa-text-mixed-span8-v2-perf

LUAJIT_S390X_TEXT_PATTERN_MODE=span8 \
LUAJIT_S390X_TEXT_TRANSFORM_MODE=ascii8 \
tools/s390x/isa_lab_run.sh --stage interp --suite pure_lua --run-id isa-text-combo-span8-ascii8-purelua-v1

LUAJIT_S390X_TEXT_PATTERN_MODE=generic \
LUAJIT_S390X_TEXT_TRANSFORM_MODE=generic \
tools/s390x/isa_lab_run.sh --stage perf --jit off --suite perf_bench --perf-family text_combo --run-id isa-text-combo-generic-v1-perf

LUAJIT_S390X_TEXT_PATTERN_MODE=span8 \
LUAJIT_S390X_TEXT_TRANSFORM_MODE=ascii8 \
tools/s390x/isa_lab_run.sh --stage perf --jit off --suite perf_bench --perf-family text_combo --run-id isa-text-combo-span8-ascii8-v1-perf

LUAJIT_S390X_TEXT_PATTERN_MODE=generic \
LUAJIT_S390X_TEXT_TRANSFORM_MODE=generic \
tools/s390x/isa_lab_run.sh --stage perf --jit off --suite perf_bench --perf-family mixed_noffi --run-id isa-carry-mixed-noffi-generic-v1

tools/s390x/isa_lab_run.sh --stage perf --jit off --suite perf_bench --perf-family mixed_noffi --run-id isa-carry-mixed-noffi-default-v1

tools/s390x/isa_lab_run.sh --stage perf --jit off --suite perf_bench --perf-family dispatch_trace --run-id isa-carry-dispatch-default-v1

LUAJIT_S390X_TEXT_PATTERN_MODE=generic \
LUAJIT_S390X_TEXT_TRANSFORM_MODE=generic \
tools/s390x/isa_lab_run.sh --stage perf --jit off --suite perf_bench --perf-family dispatch_trace --run-id isa-carry-dispatch-generic-v1

LUAJIT_S390X_TEXT_PATTERN_MODE=generic \
LUAJIT_S390X_TEXT_TRANSFORM_MODE=generic \
tools/s390x/isa_lab_run.sh --stage perf --jit off --suite perf_bench --perf-family dispatch_trace --run-id isa-carry-dispatch-generic-v2

tools/s390x/isa_lab_run.sh --stage perf --jit off --suite perf_bench --perf-family dispatch_trace --run-id isa-carry-dispatch-default-v2

LUAJIT_S390X_TEXT_PATTERN_MODE=generic \
LUAJIT_S390X_TEXT_TRANSFORM_MODE=generic \
tools/s390x/isa_lab_run.sh --stage perf --jit off --suite perf_bench --perf-family iterator_table --run-id isa-carry-iterator-generic-v1

tools/s390x/isa_lab_run.sh --stage perf --jit off --suite perf_bench --perf-family iterator_table --run-id isa-carry-iterator-default-v1

LUAJIT_S390X_TEXT_PATTERN_MODE=generic \
LUAJIT_S390X_TEXT_TRANSFORM_MODE=generic \
tools/s390x/isa_lab_run.sh --stage perf --jit off --suite perf_bench --perf-family bitops_mix --run-id isa-carry-bitops-generic-v1

tools/s390x/isa_lab_run.sh --stage perf --jit off --suite perf_bench --perf-family bitops_mix --run-id isa-carry-bitops-default-v1

LUAJIT_S390X_TEXT_PATTERN_MODE=generic \
LUAJIT_S390X_TEXT_TRANSFORM_MODE=generic \
tools/s390x/isa_lab_run.sh --stage perf --jit off --suite perf_bench --perf-family vararg_paths --run-id isa-carry-vararg-generic-v1

tools/s390x/isa_lab_run.sh --stage perf --jit off --suite perf_bench --perf-family vararg_paths --run-id isa-carry-vararg-default-v1

LUAJIT_S390X_TEXT_PATTERN_MODE=generic \
LUAJIT_S390X_TEXT_TRANSFORM_MODE=generic \
tools/s390x/isa_lab_run.sh --stage perf --jit off --suite perf_bench --perf-family be_helpers --run-id isa-carry-behelpers-generic-v1

tools/s390x/isa_lab_run.sh --stage perf --jit off --suite perf_bench --perf-family be_helpers --run-id isa-carry-behelpers-default-v1

LUAJIT_S390X_TEXT_PATTERN_MODE=generic \
LUAJIT_S390X_TEXT_TRANSFORM_MODE=generic \
tools/s390x/isa_lab_run.sh --stage perf --jit off --suite perf_bench --perf-family mixed_ffi --run-id isa-carry-mixedffi-generic-v1

tools/s390x/isa_lab_run.sh --stage perf --jit off --suite perf_bench --perf-family mixed_ffi --run-id isa-carry-mixedffi-default-v1
```

## Hash Experiment

The next active ISA lane is sparse string hashing:

- `LUAJIT_S390X_HASH_MODE=generic`
- `LUAJIT_S390X_HASH_MODE=mix64`

Current behavior:

- `generic` keeps the stock sparse hash
- `mix64` swaps in an s390x-only sparse hash that samples head, tail, middle, and three-quarter blocks
- the current benchmark pair is `intern_collision_long` and `intern_varied_long`
- this lane is opt-in until a same-host `kdz1` run shows a clear win

Current `kdz1` readout:

- `mix64` is about 10% faster on the constructed `intern_collision_long` workload
- `mix64` is about 4-6% slower on the general `intern_varied_long` workload
- because the win is narrow and the general case regresses, hash stays opt-in for now

Initial validation commands:

```bash
LUAJIT_S390X_HASH_MODE=generic tools/s390x/isa_lab_run.sh --stage interp --suite pure_lua --run-id isa-hash-generic-purelua
LUAJIT_S390X_HASH_MODE=mix64 tools/s390x/isa_lab_run.sh --stage interp --suite pure_lua --run-id isa-hash-mix64-purelua

LUAJIT_S390X_HASH_MODE=generic tools/s390x/isa_lab_run.sh --stage perf --jit off --suite perf_bench --perf-family string_hash --run-id isa-hash-generic-perf
LUAJIT_S390X_HASH_MODE=mix64 tools/s390x/isa_lab_run.sh --stage perf --jit off --suite perf_bench --perf-family string_hash --run-id isa-hash-mix64-perf
```

## Initial kdz1 Probe

Current probe results:

- host: `kdz1.dev.fyre.ibm.com`
- OS: `Red Hat Enterprise Linux 9.6 (Plow)`
- kernel: `5.14.0-570.62.1.el9_6.s390x`
- machine type: `8561`
- arch: `s390x`
- CPU count: `16`
- facilities observed in `lscpu`: `dfp te vx vxd vxe gs vxe2 vxp sort dflt`
- `perf_event_paranoid`: `2`

Package/tool gaps observed before bootstrap:

- `clang`
- `perf`
- `perl`
- `perl-Test-Harness`
- `libunwind-devel`
- `ccache`
- `ripgrep`

Bootstrap result on `2026-04-08`:

- installed and verified: `gcc`, `clang`, `perf`, `perl`, `perl-Test-Harness`, `jq`, `util-linux`, `kernel-tools`
- `perf stat true` passed
- not available from the currently enabled `kdz1` repos: `libunwind-devel`, `ccache`, `ripgrep`
