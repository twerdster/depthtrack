# Bounded historical ten-split model

`depth10/nodes.bin` contains1,023×12B packed nodes; `leaves.bin` contains1,024 precomputed uint8 labels:13,300B total. Hex files support RTL simulation. `model.json` documents fields, source tree dimensions, profile and hashes without private paths/account records.

`source_sections/` contains39,928B:16B source header,1,023 original24B nodes,1,024 original8B compact terminal records and1,024×7B histograms. They allow a literal float32 CPU evaluator and exact historical weighted voting without the large source file. These are records at the complete ten-split prefix's terminals, not a complete depth21 model. Original source used heap-index histogram lookup; its indexing quirks are preserved.

No training was performed. These fixtures/model do not establish a training relationship with HandNet, reconstruction of the training set, or semantic accuracy. The separate recorded tracking demo used different two/nine-class full-depth models. Retain provenance/rights limitations in `../NOTICE.md`; do not invent an upstream model licence.
