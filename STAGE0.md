# Stage 0 notes

ABC: `~/eda/abc/abc`

Larger netlist, balance cut the depth, rewrite cut the AND count and then mapped to LUT 6s
## i10.aig (`i/o = 257/224`, combinational)

| After     | Size     | lev |
|-----------|----------|-----|
| strash    | 2675 AND | 50  |
| balance   | 2396 AND | 37  |
| rewrite   | 2046 AND | 36  |
| if -K 6   | 575 LUT  | 9   |


3-bit ripple carry adder:
Goal was to create an extremely simple netlist, and observe how it changes as I call abc-specific commands. In this case a 3-bit ripple carry adder -> strash turn it into an AIG, both balance and rewrite didn't find any improvements, but turning it into LUT 4 using if -K 4 minimized it significantly. Will do more experimenting as I go.

balance, rewrite fully understood: balance attempts to compact the AIG in a way that can possibly shorten (stick or line ANDs are a good example), it doesn't shorten paths that aren't legal (carry adders good example here). Rewrite using its recipes for about 222 NPN classes on 4-input functions  tests certain rewrites to check if the gain is worth it (area, depth etc), it will also take into account other nodes that already exist by favoring them. locality doesn't matter here, that's for placer later (or even placer/mapper aware-synthesis)

cec is the abc for checking if networks are equivalent. Using a Miter + XOR to diff, converting to tseitin equations and then propagating constants to try and prove or disprove them is how it actually works in C++ world.

some obvious results: fatter lut -> fatter cone it can absorb, possibly limits lut layers. for example an adder of abc and xyz has 6 inputs, a LUT6 handles this much more gracefully than lut4s do.

Next: Stage 1 AIG + AIGER + `print_stats` vs the strash row.