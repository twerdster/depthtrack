"""Losslessly unpack the bundled metric depth fixture with a hash check."""
from pathlib import Path
import argparse,gzip,hashlib

root=Path(__file__).resolve().parent.parent
parser=argparse.ArgumentParser()
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
data=gzip.decompress((root/'samples/handmesh/depth.u16.gz').read_bytes())
expected='eae21f0a8da80edc96cd55468a5cd634dc9746415b50995f56640c3675816317'
if len(data)!=130*240*320*2 or hashlib.sha256(data).hexdigest()!=expected:raise SystemExit('Depth fixture verification failed')
args.output.parent.mkdir(parents=True,exist_ok=True)
args.output.write_bytes(data)
print(f'{args.output}:130x240x320 LEuint16 millimetres;SHA256 {expected}')
