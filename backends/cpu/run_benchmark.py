#!/usr/bin/env python3
"""Build and validate two C++17 backends; optional one-thread resident-frame timing."""
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import os
import platform
import shlex
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]
FLAGS=['-std=c++17','-O3','-ffp-contract=off','-fno-fast-math']


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def compiler_command():
    if os.environ.get('CXX'):
        return shlex.split(os.environ['CXX'])
    for name in ('clang++','g++','c++'):
        if shutil.which(name):return [shutil.which(name)]
    raise RuntimeError('A C++17 compiler is required; set CXX or install clang++/g++')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--validate-only',action='store_true',help='No warmups or timed samples')
    p.add_argument('--no-build',action='store_true')
    p.add_argument('--output',type=Path)
    args=p.parse_args()
    output=(args.output or ROOT/'_build/cpu').resolve();output.mkdir(parents=True,exist_ok=True)
    build=ROOT/'_build';build.mkdir(exist_ok=True)
    executable=build/'cpu_benchmark'
    compiler=compiler_command()
    command=compiler+FLAGS+[str(ROOT/'cpu/benchmark.cpp'),'-o',str(executable)]
    if not args.no_build:
        subprocess.run(command,check=True)
        subprocess.run(compiler+FLAGS+[str(ROOT/'cpu/infer.cpp'),'-o',str(build/'tree_cpu')],check=True)
    if not executable.is_file():raise RuntimeError('No built CPU executable; omit --no-build')
    run=[str(executable),str(ROOT),str(output)]
    if args.validate_only:run.append('--validate-only')
    subprocess.run(run,check=True)
    raw=output/('cpu-validation.json' if args.validate_only else 'cpu-benchmark-raw.json')
    report=json.loads(raw.read_text())
    # Independently exercise the reusable header/file interface on every fixture.
    for name in json.loads((ROOT/'fixtures/manifest.json').read_text())['order']:
        fixture=ROOT/'fixtures'/name;target=output/'file_cli'/name;target.mkdir(parents=True,exist_ok=True)
        subprocess.run([str(build/'tree_cpu'),str(ROOT/'model/depth10/nodes.bin'),str(ROOT/'model/depth10/leaves.bin'),str(fixture/'depth.bin'),str(target/'labels.bin'),str(target/'leafids.bin')],check=True,stdout=subprocess.DEVNULL)
        if (target/'labels.bin').read_bytes()!=(fixture/'depth10_labels.bin').read_bytes() or (target/'leafids.bin').read_bytes()!=(fixture/'depth10_leafids.bin').read_bytes():
            raise RuntimeError('Reusable CPU file-interface mismatch: '+name)
    report['reusable_header_file_cli_validation']={'passed':True,'pixels':98304,'labels_and_heap_traces':True}
    report['package_execution']={
        'created_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'host_system':platform.system(),'host_machine':platform.machine(),
        'compiler':Path(compiler[0]).name,'compiler_version':subprocess.check_output(compiler+['--version'],text=True).splitlines()[0],
        'flags':FLAGS,'build_performed':not args.no_build,
        'source_sha256':sha(ROOT/'cpu/benchmark.cpp'),
        'binary_sha256':sha(executable),
        'scope':'Actual packaged source, preloaded128x128 frame and model. Original full source-model oracle is preserved as prior evidence, not reread from external storage.'}
    report['output_sha256']={str(f.relative_to(output)):sha(f) for f in output.rglob('*.bin')}
    target=output/('validation.json' if args.validate_only else 'benchmark.json')
    target.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'passed':True,'report':str(target),'variants':[v['name'] for v in report['variants']]}))

if __name__=='__main__':main()
