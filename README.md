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
`--period N` is a unit-delay required time at every PO: delay-optimal cover,
then one slack-aware area recovery pass, then `wns`. `--max-delay i:N` (repeatable)
caps PO `i` (`_pos` order) to `N`; with both, that PO’s required time is
`min(period, N)`. `--false-path i` (repeatable) drops PO `i` from required time
and WNS (`set_false_path`). `--dont-touch N` (repeatable) keeps AIG node `N`:
balance does not flatten it, rewrite does not swing it, and map fanouts may not
absorb it (`set_dont_touch`).

## Engine

- **AIG** (`src/aig.cpp`): structural hashing, const-fold (`x&0`, `x&1`, `x&x`,
  `x&~x`), tombstone sweep, fanin/fanout, PI/PO/const0.
- **AIGER I/O** (`src/aiger.cpp`): binary `.aig` and ASCII `.aag`. Combinational
  only; latches rejected. Reads `i`/`o`/`a` symbols into a per-netlist name
  table (`src/name.hpp`); unnamed nodes get `n{id}`. Write dumps non-generated
  symbols. BLIF/bench emit those names (`V321(2)` → `V321_2_`).
- **Balance** (`src/balance.cpp`): associativity/commutativity on AND trees.
  Huffman combine by level. `--dont-touch N` skips flattening `N` and treats it
  as a leaf in other trees. No OR-trees, no NPN.
- **Rewrite** (`src/rewrite.cpp`, `src/rwlib.*`, `src/rwgen.cpp`): 4-input cuts,
  16-bit truth tables, brute-force NPN, exclusive MFFC gain, swing + rollback.
  `--dont-touch N` still enumerates cuts at `N`, then skips swing; fanouts union
  only its identity cut. Library is `data/npn4.txt` (222 class keys) and
  `data/rwlib4.txt` (12 NPN classes / 60 graphs, `max_ands=5`).
- **K-LUT map** (`src/map.cpp`): priority cuts, delay then area-flow, reverse
  cover from POs. Optional `--period N` and `--max-delay i:N`: required time at
  PO drivers (per-PO cap, `min` if both), then re-rank cuts that still meet
  that budget by area-flow. `--false-path i` makes `po_cap(i)` unconstrained, so
  that PO is not seeded and is skipped in WNS. `--dont-touch N` lets fanouts of
  `N` union only its identity cut, so covering hops to `N` as a LUT root.
  Snapshot (`MappedLut`) feeds `write_blif` and `write_bench`. Default `-K 6`;
  64-bit truth tables, so K is 2..6.

## Build and run

```bash
make -j
./build/mini-ls tests/and2.aag /tmp/and2.blif -c map
./build/mini-ls ~/eda/abc/i10.aig /tmp/i10.blif -c "balance rewrite map"
./build/mini-ls ~/eda/abc/i10.aig /tmp/i10.blif -c "balance rewrite map" --period 10
./build/mini-ls ~/eda/abc/i10.aig /tmp/i10.blif -c "balance rewrite map" --period 12 --max-delay 0:10
./build/mini-ls ~/eda/abc/i10.aig /tmp/i10.blif -c "balance rewrite map" --max-delay 0:8
./build/mini-ls ~/eda/abc/i10.aig /tmp/i10.blif -c "balance rewrite map" --period 10 --false-path 11
./build/mini-ls ~/eda/abc/i10.aig /tmp/i10.blif -c "balance rewrite map" --dont-touch 259
./build/mini-ls ~/eda/abc/i10.aig /tmp/i10.blif -c map --dont-touch 259
./build/mini-ls ~/eda/abc/i10.aig /tmp/i10.bench -c "balance rewrite map"
```

```text
usage: mini-ls <in.aig> [out.aig|out.blif|out.bench] [-c <cmds>]
  commands:     balance, rewrite, map
  -K N          LUT size for map (default 6, 2..6)
  --period N    required LUT depth at every PO (omit = unconstrained)
  --max-delay i:N  cap PO i (0-based _pos order) to N LUT delays (repeatable)
  --false-path i   ignore PO i for timing (0-based _pos order, repeatable)
  --dont-touch N   mark AIG node id N dont-touch (repeatable)
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
the mapped BLIF (including after `--period`, `--max-delay`, `--false-path`, and
`--dont-touch`). Map does not rewrite AND nodes.

The AND gap is mostly the rewrite library (12 of 222 4-input NPN classes at
`max_ands=5`, versus ABC’s practical table). Huffman balance also keeps a few
more ANDs (2427 vs 2396) at the same depth 37. The unconstrained LUT column is
whole-flow: ABC `if` runs on ABC’s rewritten AIG (2046 ANDs). Mapping here is
one delay-then-area-flow cover; `--period` / `--max-delay` add one slack-aware
recovery pass on the same AIG.

Same `balance rewrite map` subject, `--period N` in LUT delays. First `map`
line is delay-optimal (723 / 10); second line is recovery. AIG stays 2223 AND,
lev 37. Delay-optimal mapping treats every node as critical. Recovery keeps
those fast cuts on paths that need them, and on nodes with slack it takes a
cheaper (usually wider) cut, so fewer LUT roots.

| `map` | luts | lev | wns |
|---|---|---|---|
| unconstrained | 723 | 10 | — |
| `--period 10` | 678 | 10 | 0 |
| `--period 8` | 689 | 10 | −2 |
| `--period 12` | 669 | 12 | 0 |

Period 10 spends off-critical slack (45 fewer LUTs, depth stays 10). Period 12
lets the old critical path grow two levels (669 LUTs, WNS 0). Period 8 cannot
beat lev 10, so WNS stays −2; short POs still recover some area (689).

`--max-delay i:N` is a cap on one PO (`set_max_delay`). WNS is the min slack
over POs that have a cap, not `period - lev`. Recovered line, same AIG:

| `map` | luts | lev | wns | what it shows |
|---|---|---|---|---|
| unconstrained | 723 | 10 | — | delay-optimal, every node treated as critical |
| `--max-delay 0:8` | 718 | 10 | 2 | only PO 0 has a cap; chip depth stays 10; WNS is that PO’s slack (arrival 6) |
| `--period 12` | 669 | 12 | 0 | every PO may use 12 |
| `--period 12 --max-delay 0:10` | 669 | 12 | 0 | same chip QoR; PO 0 is off-critical (still cannot exceed 10; WNS would be −2 if it had) |

`--max-delay 0:8` delay-optimal line is `wns = 3` (PO 0 arrival 5); recovery
spends one level of that slack. `--period 12 --max-delay 0:10` matches
`--period 12` at chip `luts`/`lev` because PO 0 is not the longest path.

`--false-path i` is `set_false_path` on PO `i`. That endpoint is not seeded and
is omitted from WNS. `lev` is still max arrival over every PO. Recovered line,
same `balance rewrite map` AIG. Delay-optimal line for period 12 is `wns = 2`
(critical arrival 10); false-path 11 drops that PO, so delay-optimal WNS is 3.

| `map` | luts | lev | wns | what it shows |
|---|---|---|---|---|
| `--period 12` | 669 | 12 | 0 | every PO may use 12 |
| `--period 12 --false-path 0` | 669 | 12 | 0 | same chip QoR; PO 0 is off-critical |
| `--period 12 --false-path 11` | 659 | 12 | 0 | PO 11 was delay-opt critical; 10 fewer LUTs |
| `--period 10` | 678 | 10 | 0 | every PO may use 10 |
| `--period 10 --false-path 11` | 670 | 10 | 0 | delay-opt WNS 1; 8 fewer LUTs at lev 10 |
| `--false-path 11` | 723 | 10 | — | no clock / max-delay: unconstrained cover |

`--dont-touch N` is `set_dont_touch` on AIG node id `N` (const 0, then PIs, then
ANDs). Balance does not flatten `N`; rewrite does not swing it; map fanouts
union only its identity cut, so covering implements `N` as a LUT. The id is in
the graph at read (rewrite does not replace a marked node).

Same i10, `--dont-touch 259`:

| command | without | with 259 |
|---|---|---|
| `balance` | 2427 AND, lev 37 | 2428 AND, lev 37 |
| `balance rewrite` | 2223 AND, lev 37 | 2224 AND, lev 37 |
| `balance rewrite map` | 723 LUT, lev 10 | 723 LUT, lev 10 (AIG 2224) |
| `-c map` (raw 2675 AND) | 674 LUT, lev 11 | 675 LUT, lev 12 |

`--dont-touch 1` on `-c map` is a no-op (PI is already a pin).

`tests/and2.aag -c map`: `luts = 1 lev = 1`. `--period 1` is WNS 0; `--period 0`
is WNS −1. `--period 1 --max-delay 0:0` is WNS −1 (`min(1,0)` on the only PO).
`--period 1 --false-path 0` still prints `wns = 0` (period keeps timing on; the
only PO is skipped). `--dont-touch 3` is the AND, still 1 LUT.

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
| `src/name.hpp` `src/name.cpp` | NameMan: id ↔ name, owned by the AIG |
| `src/aiger.cpp` | AIGER read / write (including `i`/`o`/`a` symbols) |
| `src/balance.cpp` | Huffman AND-tree balance |
| `src/rewrite.cpp` | 4-cuts, NPN, MFFC, swing |
| `src/rwlib.*` `src/rwgen.cpp` | rewrite library + generator |
| `src/map.cpp` | K-LUT map, BLIF / bench writers |
| `src/sdc.hpp` `src/sdc.cpp` | Constraints: period, max-delay, false-path, dont-touch |
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

Names are in (AIGER symbols, `n{id}` for the rest, BLIF/bench emit). Constraints
still bind by index. Next: a subset `.sdc` reader, then LUT packing and
timing-driven remap.
