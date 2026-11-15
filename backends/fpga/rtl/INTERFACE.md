# Complete ten-decision tree core

Verilog2001 sources: thesis_tree_core.v,thesis_coord_pipe.v,thesis_reciprocal.v.
Defaults: WIDTH128,HEIGHT128,ADDR_WIDTH14,MAX_LEVELS10,CONTEXTS4.
Context parameter supports1...16, but4 is the hardware resource target.

Ports in declaration order:

```
clk,reset,
frame_we,frame_addr[ADDR_WIDTH-1:0],frame_data[15:0],
model_clear,model_we,model_is_leaf,model_addr[9:0],model_data[95:0],
start,levels[3:0],busy,done,error,cycles[31:0],valid_pixels[31:0],
model_node_count[10:0],model_leaf_count[10:0],
label_addr[ADDR_WIDTH-1:0],label_data[7:0]
```

Frame uint16mm,row-major. Background centre0 returns7. Leaf byte255 remains
unknown/no-vote. Other leaf values are generic8bit. Wrapper must enforce complete
frame upload. All writes/clear accepted idle only; active attempts are ignored.

Model has1023internal nodes0...1022 and1024leaves0...1023 in synchronous BRAM.
Node memory padded to1024addresses for inference; host node address1023 ignored.
Idle model_clear resets both11bit prefix counts, invalidates output, and dominates
simultaneous model_we/start. For each separate memory: addr==count appends and
increments, addr<count overwrites without increment, addr>count ignored. Counts
reset on reset; RAM contents never reset. Counts do not change while busy.

Start accepted idle with no write/clear, levels1...MAX_LEVELS(up to10), counts at
least2^levels−1 nodes and2^levels leaves. Otherwise done+error pulses, cycles0.
Runtime uses implicit heap root0,left2n+1,right2n+2; accumulated path bits index
leaf after exactly levels decisions. No early-leaf encoding. Invalid encountered
node/missing prefix aborts with done+error and invalid outputs. Done is1cycle;
cycles counts accepted start through done; valid_pixels counts nonzero centres.

Node96bit/12byte little-endian format unchanged:

|Bits|Meaning|
|---|---|
|15:0,31:16,47:32,63:48|signed16 row1,col1,row2,col2 offsets|
|79:64|signed16 rawCut|
|80,81,82|right for bothmissing,firstmissing,secondmissing|
|83,84|normal alwaysleft,alwaysright (both together invalid)|
|85|nodeValid,must1|
|95:86|reserved,must0|

Leaf write uses low8bits only. Coordinate truncation occurs AFTER adding rational
normalized offset to base. OOB neighbours0; normal branch signed17(d1−d2)>rawCut
unless alwaysflag. Missing branches use exported flags. Integer-profile exporter
matches recovered maxDiff200/fMAX126/float-threshold semantics; rows precede cols.

Node-fetch BRAM stage/tag precedes unchanged five-stage coordinate pipeline.
Synchronous leaf-fetch stage/tag precedes completion. Four tagged contexts overlap
traversals; only visited nodes evaluated. One16step exact reciprocal per valid
pixel, eight16x16 DSP products. Not one completed pixel per clock. Single true
dualport image stores32768B, labels16384B; neighbours get priority over centres.
Intended BRAM map16image+8labels+6nodes+0.5leaf=30.5RAMB18; verify actualISE.

After successful done, set label_addr and allow one rising edge. Registered-address
wrappers use assign/wait/capture. Label output0 during reset/busy or after idle
write/clear/start until successful completion. All RAM contents persist through
reset; prefix counts/result guards prevent stale access. The preserved prior ISE14.7
build passed routed50MHz timing (minimum17.055ns); see ../../evidence/. Packaging
validates unchanged RTL by simulation, without rebuilding ISE or programming a board.
