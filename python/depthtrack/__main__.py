from pathlib import Path
import argparse, hashlib, json, math
from .tracker import AdaptivePointTracker, Parameters


def replay(path, output=None, verify=False):
    source=Path(path)
    fixture=json.loads(source.read_text())
    trackers=[AdaptivePointTracker(Parameters(**fixture.get('parameters',{}))) for _ in range(5)]
    prior_time=None
    for frame in fixture['frames']:
        t=float(frame['time_s'])
        if not math.isfinite(t) or (prior_time is not None and t<=prior_time):raise ValueError('Timestamps must increase')
        prior_time=t
        if len(frame['observations'])!=5:raise ValueError('Expected five fixed semantic classes')
        for point in frame['observations']:
            if point is not None and (len(point)!=3 or not all(math.isfinite(x) for x in point) or point[2]<=0):
                raise ValueError('Invalid cached observation')
    frames=[];worst=0.;accepted=visible=0
    for frame in fixture['frames']:
        states=[tracker.update(frame['time_s'],point) for tracker,point in zip(trackers,frame['observations'])]
        for c,state in enumerate(states):
            accepted+=state['accepted'];visible+=state['position'] is not None
            if verify:
                expected=frame['expected'][c]
                for key in ['status','accepted','reason','generation']:
                    if state[key]!=expected[key]:raise AssertionError(f"Frame {frame['frame']} class{c}: {key}")
                if (state['position'] is None)!=(expected['position'] is None):raise AssertionError('Visibility mismatch')
                if state['position'] is not None:
                    error=max(abs(a-b) for a,b in zip(state['position'],expected['position']));worst=max(worst,error)
                    if error>1.e-8:raise AssertionError('Position mismatch')
        frames.append(dict(frame=frame['frame'],time_s=frame['time_s'],states=states))
    result=dict(schema_version=1,parameters=fixture.get('parameters',{}),source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),frames=frames)
    if output:
        dest=Path(output);dest.parent.mkdir(parents=True,exist_ok=True)
        dest.write_text(json.dumps(result,separators=(',',':'))+'\n')
    summary=dict(frames=len(frames),visible_outputs=visible,accepted_observations=accepted,
                 held_outputs=visible-accepted,verified=verify,max_position_difference_mm=worst if verify else None,
                 ground_truth_accuracy_measured=False)
    print(json.dumps(summary,indent=2))
    return result


def main():
    parser=argparse.ArgumentParser(description='Replay timestamped depth-point observations')
    sub=parser.add_subparsers(dest='command',required=True)
    p=sub.add_parser('replay');p.add_argument('input',type=Path);p.add_argument('--output',type=Path);p.add_argument('--verify',action='store_true')
    args=parser.parse_args()
    replay(args.input,args.output,args.verify)

if __name__=='__main__':main()
