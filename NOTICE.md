# Attribution and scope

DepthTrack packages research code and recorded results recovered from Aaron Wetzler's ZScanV02 depth-hand work, plus the new timestamp-aware tracking implementations and platform examples. Preserve source authorship and any notices inside the files.

- Historical `legacy/zscan` sources carry Aaron Wetzler2013/2014 attribution. Some headers are portable compatibility copies as described alongside the code; the original algorithm/header notices remain.
- `legacy/zscan/algorithm-audit/original/nanoflann.h` contains its BSD license and copyright notices; preserve them when redistributing that component.
- FPGA/vendor code and its manufacturer notice are documented under `backends/`; those notices apply to the relevant files, not the entire repository.
- The adaptive filter derives from the published1€ algorithm (Casiez,Roussel,Vogel,CHI2012), referenced in the tracker documentation. New implementations include additional lifecycle and vector-filter policies.
- The HandMeshRecord fixture is a recovered separate recording attributed to Aaron Wetzler. Training provenance, split membership and a HandNet license for that recording are not established. The HandNet parent repository retains its own original code/data terms.

No repository-wide license has been imposed over mixed historical/vendor components. Public availability does not replace the notices and permissions associated with each component. No proprietary SDK installers, licence files, credentials, email attachments or account inventories are included.

The optional live monocular browser page downloads Transformers.js3.8.1 (Apache2.0) and the Depth Anything V2 Small ONNX export (Apache2.0), pinned and linked in docs/monocular-live.md. Those external components retain their upstream terms; no weights or runtime bundle are included here. The recovered compact tree remains under the historical/component scope above. Estimated-depth region tracks do not establish anatomical accuracy or training provenance.
