#!/usr/bin/env python3
"""Validate packaged fixtures/model and optional real CPU/Metal execution. No board access."""
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import platform
import shutil
import subprocess
import sys
import time
try:
    from .reference import predict
except ImportError:
    from reference import predict

ROOT=Path(__file__).resolve().parent

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cpu',action='store_true',help='Compile and execute both CPU variants')
    parser.add_argument('--metal',action='store_true',help='Compile and execute real macOS Metal GPU; no CPU fallback')
    parser.add_argument('--output',type=Path,help='New directory, defaults to _build/validation-TIMESTAMP')
    args=parser.parse_args()
    stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    output=(args.output or ROOT/'_build'/('validation-'+stamp)).resolve()
    output.mkdir(parents=True,exist_ok=False)
    manifest=json.loads((ROOT/'fixtures/manifest.json').read_text())
    nodes=ROOT/'model/depth10/nodes.bin';leaves=ROOT/'model/depth10/leaves.bin'
    records=[];inputs=[]
    started=time.perf_counter()
    for fixture in manifest['fixtures']:
        name=fixture['id'];directory=ROOT/'fixtures'/name
        for filename,expected in fixture['files'].items():
            p=directory/filename
            if p.stat().st_size != expected['bytes'] or sha(p) != expected['sha256']:
                raise RuntimeError('Fixture hash mismatch: '+name+'/'+filename)
        image=(directory/'depth.bin').read_bytes();inputs.append(image)
        expected=(directory/'depth10_labels.bin').read_bytes()
        expected_leaves=(directory/'depth10_leafids.bin').read_bytes()
        actual,reached=predict(image,nodes.read_bytes(),leaves.read_bytes())
        if (actual,reached)!=(expected,expected_leaves):raise RuntimeError('Scalar fixture mismatch: '+name)
        physical=ROOT/'evidence/physical_labels'/(name+'.bin')
        if physical.read_bytes()!=expected:raise RuntimeError('Preserved physical evidence mismatch: '+name)
        records.append({'id':name,'pixels':16384,'scalar_label_mismatches':0,'scalar_heap_mismatches':0,
                        'prior_physical_labels_equal':True,'depth_sha256':sha(directory/'depth.bin'),
                        'labels_sha256':sha(directory/'depth10_labels.bin'),'leafids_sha256':sha(directory/'depth10_leafids.bin')})
    report={'passed':True,'created_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
            'host_system':platform.system(),'host_machine':platform.machine(),'fixtures':records,
            'pixels_per_engine':sum(r['pixels'] for r in records),'scalar_validation_wall_s':time.perf_counter()-started,
            'nodes_sha256':sha(nodes),'leaves_sha256':sha(leaves),
            'fpga_image_sha256':sha(ROOT/'fpga/images/mojo_depth10_50mhz.bin'),
            'physical_board_access':False,'prior_physical_evidence_reexecuted':False,
            'cpu':None,'metal':None}
    prior=json.loads((ROOT/'evidence/prior_baseline.json').read_text())
    if report['fpga_image_sha256'] != prior['bitstream']['sha256']:raise RuntimeError('Reviewed bitstream hash mismatch')
    if args.cpu:
        subprocess.run([sys.executable,str(ROOT/'cpu/run_benchmark.py'),'--validate-only','--output',str(output/'cpu')],check=True)
        cpu=json.loads((output/'cpu/validation.json').read_text())
        for variant in cpu['variants']:
            for fixture in manifest['order']:
                for actual_name,expected_name in [('labels.bin','depth10_labels.bin'),('leafids.bin','depth10_leafids.bin')]:
                    if (output/'cpu'/variant['name']/fixture/actual_name).read_bytes()!=(ROOT/'fixtures'/fixture/expected_name).read_bytes():
                        raise RuntimeError('CPU saved output mismatch')
        report['cpu']={'passed':True,'variants':[v['name'] for v in cpu['variants']],
                       'pixels_per_variant':98304,'report_sha256':sha(output/'cpu/validation.json'),
                       'execution':'Actual packaged C++17 CPU code; untimed numerical validation'}
    if args.metal:
        if platform.system()!='Darwin' or shutil.which('xcrun') is None:raise RuntimeError('Actual Metal validation requires macOS/Xcode command-line tools')
        subprocess.run(['bash',str(ROOT/'gpu/build.sh')],check=True)
        frames=output/'inputs.u16';frames.write_bytes(b''.join(inputs))
        gpuout=output/'metal'
        command=[str(ROOT/'_build/tree_metal'),'--input',str(frames),'--nodes',str(nodes),'--leaves',str(leaves),'--output-dir',str(gpuout),'--levels','10','--warmup','6','--iterations','12']
        subprocess.run(command,check=True)
        gpu=json.loads((gpuout/'benchmark.json').read_text())
        for i,name in enumerate(manifest['order']):
            for suffix,expected in [('labels','depth10_labels.bin'),('leafids','depth10_leafids.bin')]:
                if (gpuout/f'frame_{i:03d}.{suffix}.bin').read_bytes()!=(ROOT/'fixtures'/name/expected).read_bytes():raise RuntimeError('Metal saved output mismatch: '+name)
        if gpu['cpu_fallback'] or not gpu['timed_labels_equal_output_pass']:raise RuntimeError('Metal execution contract failure')
        report['metal']={'passed':True,'device':gpu['device']['name'],'cpu_fallback':False,'pixels':98304,
                         'report_sha256':sha(gpuout/'benchmark.json'),
                         'execution':'Actual Metal device, untimed labels+heap check and12 single-frame stability samples; packaging sanity check, not a controlled benchmark'}
    (output/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'passed':True,'pixels_per_engine':98304,'cpu':report['cpu'],'metal':report['metal'],'report':str(output/'validation.json')},indent=2))

if __name__=='__main__':main()
