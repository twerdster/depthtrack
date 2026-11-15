# Thesis tree transport v4.0

500,000baud8N1, stock Mojo AVR bridge. One request then one complete response; no command pipelining. The physical UART cannot stall incoming bytes. Application DTR is asserted once before port open; RTS keeps its normal default. No auto-programming, reset toggle sequence or persistent writes are part of the host client.

Request `A5 command sequence length payload CRClo CRChi`; response `5A (command|80) sequence length payload CRClo CRChi`. CRC16/CCITT-FALSE(poly1021,initFFFF,no reflection/xorout), over command/sequence/length/payload only; check `123456789→29B1`. Multibyte fields/node records are little-endian. Maximum request66payload bytes; maximum response67. Oversized requests are fully drained and rejected. Bad CRC causes no memory/RUN side effects. Parser interbyte timeout5,000,000clocks(nominal100ms) silently drops an incomplete request. TX holds byte and valid until accepted.

|Cmd|Request|Success response payload, including status0|
|---|---|---|
|01 PING|empty|0,major4,minor0,width16,height16,frame_capacity_words16,node_count16,leaf_count16,max_levels8,capabilities8=1(15B)|
|02 BEGIN|empty|0; clears depth prefix and result, retains model|
|03 DEPTH|word_address16,1–32uint16 values(2–64data bytes)|0,loaded_depth_words16|
|04 RUN|levels8(1..10,within build MAX_LEVELS)|0,cycles32,valid_pixels32,core_error8=0|
|05 READ|label_address16,count8(1–64)|0,label_address16,count label bytes|
|06 MODEL|type8(0node/1leaf),entry_address16,data|0,node_count16,leaf_count16(5B)|
|07 MODEL_BEGIN|empty|0; clears wrapper/core model prefixes and result, retains depth|

MODEL node data is1–5packed12byte records; legal request lengths are exactly15,27,39,51,63. A small case decoder implements these, with no division or modulus. Leaf data is1–63label bytes (request4..66bytes). Model addresses are16bit entry addresses, checked against build capacity before narrowing to the core10bit port. At MAX_LEVELS10, node addresses0..1022 and leaf addresses0..1023 are valid; node count1023/leaf count1024 are16bit quantities. See INTERFACE.md for exact signed offsets/cut, flags and model-RAM semantics. Centre depth0 produces original background label7; generic leaf255 remains unknown/no-vote.

Depth addresses are uint16-word offsets; READ addresses are label-byte offsets. Uploads must append exactly at the loaded prefix or overwrite entirely within it. Gaps, extending overlaps and overflow are rejected. Successful writes invalidate prior labels. MODEL_BEGIN pulses core model_clear once while idle; RAM is not cleared wholesale. RUN requires the complete frame,2^levels−1nodes and2^levels leaves, then waits for actual done. Successful result validity permits repeated READs until the next successful write, BEGIN/MODEL_BEGIN, reset or new run. Reset clears upload/result guards, so stale RAM cannot be returned as a completed result.

Failure response is one status byte:1CRC,2unknown command,3length/type/levels,4address/order,5incomplete frame,6core error,7no valid result,8incomplete model. No other response fields are valid after failure.

`cycles` counts core accepted-start→done, excluding serial/frame/model upload and label download. Conversion at50MHz is nominal clock timing, not an independent clock-frequency measurement. Benchmark host prepared-frame→readable-label timing separately, identify preprocessing/capture/model-load exclusions, and report USB timing separately from core cycles.

A128×128 frame takes512full DEPTH requests; labels take256READ requests. A1023node model takes205node requests;1024leaves take17leaf requests. Host serial writes are split into at most63bytes with1ms interchunk pacing to avoid the verified legacy64byte USB ZLP stall. Maximum DEPTH request72wirebytes becomes63+9; maximum node request69wirebytes becomes63+6. Serial framing itself is unchanged.

Tests, all simulation/mock unless expressly recorded elsewhere:

- `sim/tb_tree_protocol.sv`: recording stub core,252packets including complete1023/1024uploads,16bit counts/addresses, overflow rejection, CRC, sync64byte read, backpressure, timeout and model_clear. Compile with tree_protocol.v alone, excluding the real core.
- `sim/tb_tree_protocol_core.sv`: actual core/helpers, complete ten-level model,4..10decision runs reaching rightmost leaf1023, zero-depth background7, sync label reads and cleared core prefixes.
- `sim/tb_tree_uart.sv`: actual top/core/helpers/local bridge/manufacturer serial modules;500kbaud bit decode, delayed AVR ACK, startup high impedance, max depth/model payloads,16bit model-gap rejection and interrupted-reset recovery.
- `sim/run.py --uart`: production Python codec checks all14 genuine simulated UART response CRCs; capture stays in the disposable output directory, not a physical capture.
