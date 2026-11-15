#!/usr/bin/env python3
"""Full native/Python saved-state parity, using only Python's standard library."""
import argparse,csv,json,math,subprocess,tempfile
from pathlib import Path

def close(a,b):
    if not (math.isfinite(float(a)) and math.isfinite(float(b)) and abs(float(a)-float(b))<=1e-8):
        raise AssertionError(f'numerical mismatch: {a} vs {b}')

def main():
    p=argparse.ArgumentParser();p.add_argument('--cli',required=True);p.add_argument('--fixture',required=True);args=p.parse_args()
    fixture=json.loads(Path(args.fixture).read_text());expected=[]
    with tempfile.TemporaryDirectory(prefix='depthtrack-parity-') as tmp:
        source=Path(tmp)/'input.csv';dest=Path(tmp)/'output.csv'
        with source.open('w',newline='') as f:
            w=csv.writer(f);w.writerow(['frame','time_s','class_id','has_observation','x','y','z'])
            for frame in fixture['frames']:
                assert len(frame['observations']) == len(frame['expected']), 'Malformed golden frame'
                for c,(obs,state) in enumerate(zip(frame['observations'],frame['expected'])):
                    w.writerow([frame['frame'],frame['time_s'],c,int(obs is not None),*(obs or [0,0,0])]);expected.append(state)
        flags={'min_cutoff_hz':'--min-cutoff','speed_beta':'--speed-beta','derivative_cutoff_hz':'--derivative-cutoff','max_cutoff_hz':'--max-cutoff','jump_slack_mm':'--jump-slack','max_speed_mm_s':'--max-speed','hold_seconds':'--hold','reset_gap_seconds':'--reset-gap','trail_seconds':'--trail'}
        cmd=[args.cli]
        for key,value in fixture.get('parameters',{}).items():
            if key in flags:cmd.extend([flags[key],str(value)])
        subprocess.run(cmd+[str(source),str(dest)],check=True,capture_output=True,text=True)
        rows=list(csv.DictReader(dest.open()))
        assert len(rows)==len(expected)>0
        for i,(row,state) in enumerate(zip(rows,expected)):
            try:
                assert row['status']==state['status']
                assert int(row['accepted'])==int(state['accepted'])
                assert int(row['generation'])==state['generation']
                assert (row['reason'] or None)==state.get('reason')
                assert int(row['visible'])==int(state['position'] is not None)
                if state['position'] is not None:
                    for key,value in zip(['out_x','out_y','out_z'],state['position']):close(row[key],value)
                for key,source_key in [('observation_age_s','observation_age_s'),('cutoff_hz','cutoff_hz')]:
                    value=state.get(source_key)
                    if value is None:assert row[key]==''
                    else:close(row[key],value)
                history=json.loads(row['history_json']);assert len(history)==len(state['history'])
                for a,b in zip(history,state['history']):
                    close(a['time_s'],b['time_s'])
                    for x,y in zip(a['position'],b['position']):close(x,y)
            except (AssertionError,ValueError) as e:
                raise AssertionError(f'Golden state {i}: {e}') from e
        print(f'native full JSON parity passed {len(rows)} states, including histories; tolerance1e-8mm')
if __name__=='__main__':main()
