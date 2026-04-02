# Number Helper FORI Slot Map

- Timestamp: `2026-04-02 09:31:00 PDT`
- Host label: `local analysis`
- Commit basis: `fa40dbad`

## Bytecode

`number_helper_loop` bytecode dump from local `jit.util.funcbc()`:

```text
001 KSHORT A=1
002 KSHORT A=2
003 MOV    A=3
004 KSHORT A=4
005 FORI   A=2
006 UGET   A=6
007 TGETS  A=6
008 MULVN  A=8
009 ADDVV  A=8
010 CALL   A=6
011 MOV    A=1
012 FORL   A=2
013 UGET   A=2
014 TGETS  A=2
015 MOV    A=4
016 CALLT  A=2
```

## Slot Meaning

For `FORI/FORL A=2`, the hidden numeric-for slots are:

- `A+FORL_IDX = slot 2`
- `A+FORL_STOP = slot 3`
- `A+FORL_STEP = slot 4`
- `A+FORL_EXT = slot 5`

From that mapping:

- `sload_int ofs=8 extra=12` corresponds to hidden `STOP` (`n`)
- `sload_int ofs=16 extra=20` corresponds to hidden `STEP` (`1`)

## Correction

The repeated promoted-slice seam previously described as hidden `FORL_IDX`
reload is misattributed on this workload.

The front-most exact-taken `IR=SLOAD #4 TI` seam is the inherited hidden
numeric-for `STEP` slot, not the hidden index slot.
