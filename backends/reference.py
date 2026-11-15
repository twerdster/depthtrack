#!/usr/bin/env python3
"""Scalar packed-tree reference. Standard library only; no device/file side effects on import."""
from pathlib import Path
import argparse
import struct

ROOT = Path(__file__).resolve().parent


def trunc_zero(n, d):
    return -(abs(n)//d) if n < 0 else n//d


def predict(depth_bytes, node_bytes, leaves, width=128, height=128, levels=10):
    if len(depth_bytes) != width*height*2 or len(node_bytes) != ((1<<levels)-1)*12 or len(leaves) != 1<<levels:
        raise ValueError('Incorrect image/model byte counts')
    image = struct.unpack('<'+str(width*height)+'H', depth_bytes)
    nodes = list(struct.iter_unpack('<5hH', node_bytes))
    for *_, flags in nodes:
        if not flags & 32 or flags & 0xffc0 or flags & 24 == 24:
            raise ValueError('Invalid model node flags')
    output = bytearray([7])*(width*height)
    reached = [65535]*(width*height)
    for i,z in enumerate(image):
        if z == 0:
            continue
        row,col = divmod(i,width)
        node = 0
        for _ in range(levels):
            dr1,dc1,dr2,dc2,cut,flags = nodes[node]
            r1,c1 = trunc_zero(row*z+dr1,z),trunc_zero(col*z+dc1,z)
            r2,c2 = trunc_zero(row*z+dr2,z),trunc_zero(col*z+dc2,z)
            a = image[r1*width+c1] if 0 <= r1 < height and 0 <= c1 < width else 0
            b = image[r2*width+c2] if 0 <= r2 < height and 0 <= c2 < width else 0
            if a == 0 and b == 0:right = bool(flags&1)
            elif a == 0:right = bool(flags&2)
            elif b == 0:right = bool(flags&4)
            elif flags&8:right = False
            elif flags&16:right = True
            else:right = a-b > cut
            node = 2*node+1+int(right)
        output[i] = leaves[node-((1<<levels)-1)]
        reached[i] = node
    return bytes(output),struct.pack('<'+str(len(reached))+'H',*reached)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--depth',type=Path,required=True)
    p.add_argument('--model',type=Path,default=ROOT/'model/depth10')
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--leaf-output',type=Path)
    args=p.parse_args()
    labels,reached=predict(args.depth.read_bytes(),(args.model/'nodes.bin').read_bytes(),(args.model/'leaves.bin').read_bytes())
    args.output.write_bytes(labels)
    if args.leaf_output:args.leaf_output.write_bytes(reached)
    print(f'Wrote {len(labels)} labels')

if __name__=='__main__':main()
