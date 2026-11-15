#!/usr/bin/env python3
"""Actual Icarus simulation: one/full core fixtures, guards, CRC and optional UART. No device access."""
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import shutil
import struct
import subprocess

FPGA=Path(__file__).resolve().parents[1]
BACKENDS=FPGA.parent

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--all-fixtures',action='store_true',help='All six128x128 fixtures instead of historical_000')
    parser.add_argument('--uart',action='store_true',help='Also simulate actual manufacturer UART/top and AVR acknowledgement')
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    iverilog=shutil.which('iverilog');vvp=shutil.which('vvp')
    if not iverilog or not vvp:raise SystemExit('Install Icarus Verilog (iverilog and vvp) and put it on PATH')
    stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    output=(args.output or BACKENDS/'_build'/('rtl-'+stamp)).resolve()
    output.mkdir(parents=True,exist_ok=False)
    (output/'sim').mkdir() # UART test capture is local to this disposable directory.
    core=[FPGA/'rtl'/f for f in ['thesis_tree_core.v','thesis_coord_pipe.v','thesis_reciprocal.v']]
    cases=[]
    def run_case(name,sources,parameters=(),plusargs=()):
        executable=output/(name+'.vvp')
        command=[iverilog,'-g2012','-s',name,*parameters,'-o',str(executable),*[str(p) for p in sources]]
        compile_result=subprocess.run(command,capture_output=True,text=True,timeout=60)
        (output/(name+'-compile.log')).write_text(compile_result.stdout+compile_result.stderr)
        if compile_result.returncode:raise RuntimeError(compile_result.stdout+compile_result.stderr)
        process=subprocess.run([vvp,str(executable),*plusargs],cwd=output,capture_output=True,text=True,timeout=90)
        (output/(name+'.log')).write_text(process.stdout+process.stderr)
        if process.returncode or 'PASS' not in process.stdout:raise RuntimeError(process.stdout+process.stderr)
        cases.append({'testbench':name,'stdout':process.stdout.strip()})
        print(process.stdout.strip(),flush=True)
    # Stub test deliberately does not link the real core; it isolates parser side effects.
    run_case('tb_tree_protocol',[FPGA/'sim/tb_tree_protocol.sv',FPGA/'rtl/tree_protocol.v'])
    run_case('tb_tree_protocol_core',[FPGA/'sim/tb_tree_protocol_core.sv',FPGA/'rtl/tree_protocol.v',*core])
    run_case('tb_thesis_controls',[FPGA/'sim/tb_thesis_controls.sv',*core])
    manifest=json.loads((BACKENDS/'fixtures/manifest.json').read_text())
    fixtures=manifest['order'] if args.all_fixtures else ['historical_000']
    fixture_results=[]
    # Compile once for all complete128x128 fixture images.
    executable=output/'tb_thesis_core.vvp'
    command=[iverilog,'-g2012','-s','tb_thesis_core','-Ptb_thesis_core.WIDTH=128','-Ptb_thesis_core.HEIGHT=128','-Ptb_thesis_core.ADDR_WIDTH=14','-Ptb_thesis_core.CONTEXTS=4','-o',str(executable),str(FPGA/'sim/tb_thesis_core.sv'),*[str(p) for p in core]]
    subprocess.run(command,check=True,capture_output=True,text=True,timeout=60)
    for name in fixtures:
        source=BACKENDS/'fixtures'/name;case=output/name;case.mkdir()
        depth=(source/'depth.bin').read_bytes();labels=(source/'depth10_labels.bin').read_bytes();leaves=(source/'depth10_leafids.bin').read_bytes()
        for filename,values in [('image.hex',struct.unpack('<16384H',depth)),('expected.hex',labels),('leafids.hex',struct.unpack('<16384H',leaves))]:
            digits=2 if filename=='expected.hex' else 4
            (case/filename).write_text(''.join(f'{x:0{digits}x}\n' for x in values))
        command=[vvp,str(executable),f'+IMAGE={case}/image.hex',f'+NODES={BACKENDS}/model/depth10/nodes.hex',f'+LEAVES={BACKENDS}/model/depth10/leaves.hex',f'+EXPECTED={case}/expected.hex',f'+OUTPUT={case}/actual.hex','+LEVELS=10',f'+LEAFIDS={case}/leafids.hex']
        process=subprocess.run(command,cwd=output,capture_output=True,text=True,timeout=90)
        (case/'simulation.log').write_text(process.stdout+process.stderr)
        if process.returncode or 'PASS' not in process.stdout:raise RuntimeError(process.stdout+process.stderr)
        actual=bytes(int(x,16) for x in (case/'actual.hex').read_text().split())
        if actual!=labels:raise RuntimeError('RTL label mismatch '+name)
        fixture_results.append({'fixture':name,'pixels':16384,'labels_sha256':hashlib.sha256(actual).hexdigest(),'stdout':process.stdout.strip()})
        print(name,process.stdout.strip(),flush=True)
    if args.uart:
        vendor=FPGA/'vendor/mojo-base-project/src'
        run_case('tb_tree_uart',[FPGA/'sim/tb_tree_uart.sv',*sorted((FPGA/'rtl').glob('*.v')),FPGA/'board/depth_tree_avr_bridge.v',*[vendor/f for f in ['serial_rx.v','serial_tx.v','cclk_detector.v','spi_slave.v']]])
        # Independent production codec check of captured packet CRCs.
        import sys
        sys.path.insert(0,str(FPGA/'host'))
        from client import decode_packet
        lines=(output/'sim/uart-replies.hex').read_text().splitlines()
        if len(lines)!=14:raise RuntimeError('Expected14 UART responses')
        for line in lines:decode_packet(bytes.fromhex(line))
        cases[-1]['production_codec_crc_packets']=len(lines)
    report={'passed':True,'execution':'Actual Icarus RTL simulation, not physical FPGA execution','hardware_access':False,'levels':10,'contexts':4,'tests':cases,'fixtures':fixture_results,'pixels_checked':16384*len(fixtures)}
    (output/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'passed':True,'report':str(output/'validation.json'),'fixtures':len(fixtures),'tests':len(cases)}))

if __name__=='__main__':main()
