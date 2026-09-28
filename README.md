# mini-ls

Small **logic synthesis** engine: AIG in, optimized AIG and K-LUT netlist out.

This is the front half of a personal CAD stack. A separate repo,
[mini-pd](https://github.com/CKBird1/mini-pd), already has a bookshelf placer
(quadratic + Abacus legalize) and a g-cell router.

Personal C++ engine for FPGA / EDA CAD (combinational AIG → K-LUT). MIT license.

Daily log: [NOTES.md](NOTES.md). ABC lab notes: [STAGE0.md](STAGE0.md).

## Flow

```text
AIGER (.aig / .aag)
  → unique-table AIG (const-fold, dangling sweep)
  → Huffman AND-tree balance
  → DAG-aware 4-input rewrite (cuts, TT, NPN, MFFC)
  → priority-cut K-LUT map (K = 2..6)
  → .aig (frozen AIG)  |  .blif (LUT netlist)  |  .bench (mini-pd)
```

CLI runs `balance`, `rewrite`, and `map` in the order given. Mapping covers the
AIG; it does not rewrite AND nodes in place. `print_stats` still reports AIG
size and AND depth. `map` prints LUT count and mapped depth (`luts` / `lev`).

## Engine

- **AIG** (`src/aig.cpp`): structural hashing, const-fold (`x&0`, `x&1`, `x&x`,
  `x&~x`), tombstone sweep, fanin/fanout, PI/PO/const0.
- **AIGER I/O** (`src/aiger.cpp`): binary `.aig` and ASCII `.aag`. Combinational
  only; latches rejected.
- **Balance** (`src/balance.cpp`): associativity/commutativity on AND trees.
  Huffman combine by level. No OR-trees, no NPN.
- **Rewrite** (`src/rewrite.cpp`, `src/rwlib.*`, `src/rwgen.cpp`): 4-input cuts,
  16-bit truth tables, brute-force NPN, exclusive MFFC gain, swing + rollback.
  Library is `data/npn4.txt` (222 class keys) and `data/rwlib4.txt` (12 NPN
  classes / 60 graphs, `max_ands=5`).
- **K-LUT map** (`src/map.cpp`): priority cuts, delay then area-flow, reverse
  cover from POs. Snapshot (`MappedLut`) feeds `write_blif` and `write_bench`.
  Default `-K 6`; 64-bit truth tables, so K is 2..6.

## Build and run

```bash
make -j
./build/mini-ls tests/and2.aag /tmp/and2.blif -c map
./build/mini-ls ~/eda/abc/i10.aig /tmp/i10.blif -c "balance rewrite map"
./build/mini-ls ~/eda/abc/i10.aig /tmp/i10.bench -c "balance rewrite map"
```

```text
usage: mini-ls <in.aig> [out.aig|out.blif|out.bench] [-c <cmds>]
  commands:  balance, rewrite, map
  -K N       LUT size for map (default 6, 2..6)
```

Output suffix selects the writer. Rewrite loads `data/npn4.txt` and
`data/rwlib4.txt` from the working directory (`--npn` / `--rwlib` override).
`i10.aig` is the ABC sample at `~/eda/abc/i10.aig` (not in this repo).

CEC against the original AIG, PI/PO by order (`cec -n`). ABC wants `.aig` or
`.blif`; dump those rather than feeding `.aag`:

```bash
abc -c "cec -n ~/eda/abc/i10.aig /tmp/i10.blif"
```

Handoff into mini-pd is the `.bench` writer. Smoke artifacts from that path
live under `tests/` (`and2` and `i10` `.bench` / `.placed.svg` / `.qor.txt`).

## Results

`i10` (`i/o = 257/224`, combinational). mini-ls numbers from
`./build/mini-ls ~/eda/abc/i10.aig` with the `-c` sequence in the first column.
ABC numbers from [STAGE0.md](STAGE0.md) (`strash` / `balance` / `rewrite` /
`if -K 6`).

| After | mini-ls | ABC |
|---|---|---|
| read / strash | 2675 AND, lev 50 | 2675 AND, lev 50 |
| `balance` | 2427 AND, lev 37 | 2396 AND, lev 37 |
| `balance rewrite` | 2223 AND, lev 37 | 2046 AND, lev 36 |
| `balance rewrite map` (`-K 6`) | 723 LUT, lev 10 | 575 LUT, lev 9 |

ABC `cec -n` vs original `i10.aig`: equivalent on the rewritten AIG dump and on
the mapped BLIF.

The AND gap is mostly the rewrite library (12 of 222 4-input NPN classes at
`max_ands=5`, versus ABC’s practical table). Huffman balance also keeps a few
more ANDs (2427 vs 2396) at the same depth 37. The LUT column is whole-flow:
ABC `if` runs on ABC’s rewritten AIG (2046 ANDs). Mapping here is one
delay-then-area-flow cover with AIG fanouts; no area-recovery round.

`tests/and2.aag -c map`: `luts = 1 lev = 1`.

mini-pd on the checked-in `.bench` files (quadratic + Abacus, g-cell router):

| Netlist | cells | nets | legal | HPWL | overflow |
|---|---|---|---|---|---|
| `tests/and2.bench` | 4 | 3 | 4/4 | 16 | 0 |
| `tests/i10.bench` | 1205 | 981 | 1187/1205 | 162858 | 12323 |

i10’s 18 overlaps are the invented unit-cell floorplan versus Abacus (die
348×140, 14 rows). The netlist parsed and placed.

## Layout

| Path | Role |
|---|---|
| `src/aig.hpp` `src/aig.cpp` | AIG graph, unique table, sweep, stats |
| `src/aiger.cpp` | AIGER read / write |
| `src/balance.cpp` | Huffman AND-tree balance |
| `src/rewrite.cpp` | 4-cuts, NPN, MFFC, swing |
| `src/rwlib.*` `src/rwgen.cpp` | rewrite library + generator |
| `src/map.cpp` | K-LUT map, BLIF / bench writers |
| `src/main.cpp` | CLI |
| `data/npn4.txt` | 222 NPN class keys |
| `data/rwlib4.txt` | 12-class / 60-graph library |
| `tests/and2.aag` | ASCII AIGER fixture |
| `tests/*.bench` `*.placed.svg` `*.qor.txt` | mini-pd smoke |

## Limits

Combinational AIGER. LUT K = 2..6. Rewrite library is the 12-class
`max_ands=5` snapshot. Mapping does not build a second in-memory LUT graph
beyond the cover snapshot. No Liberty STA, no CLB packing, no Verilog parser
in this repo.

## Notes

Had AI 'beautify' this README with formatting and some nicer wording.

## Next

SDC-like constraints on this mapper (`create_clock`, `set_max_delay` /
`set_false_path`, `set_dont_touch`): same design unconstrained vs constrained,
different `lev` / LUT choices, still CEC-equivalent.
