"""Known-motion and failure-case checks; no recorded-sequence parameter fitting."""
from pathlib import Path
import json, math
import numpy as np
from depthtrack.tracker import AdaptivePointTracker, Parameters, alpha

def run():
    checks={}
    tr=AdaptivePointTracker()
    p=[30.,-20.,350.]
    assert tr.update(0,p)['position']==p
    for t in [.1,.23,.41,.55,1.01]:
        state=tr.update(t,p)
        assert np.allclose(state['position'],p)
    checks['nonzero_stationary_initialization_no_origin_pull']=True
    before=tr.position.copy()
    for bad in [1.01,1.0,float('nan')]:
        try:tr.update(bad,p)
        except ValueError:pass
        else:raise AssertionError('Bad timestamp accepted')
        assert np.array_equal(tr.position,before) and tr.input_time==1.01
    checks['invalid_timestamp_rejected_atomically']=True
    tr=AdaptivePointTracker();tr.update(0,p)
    held=tr.update(.1);lost=tr.update(.3);back=tr.update(.6,[50.,10.,360.])
    assert held['status']=='held' and not held['accepted'] and held['position']==p
    assert lost['status']=='lost' and lost['position'] is None
    assert back['status']=='reacquired' and back['generation']==2
    assert len(back['history'])==1
    checks['bounded_missing_hold_expiry_reacquisition']=True
    tr=AdaptivePointTracker();tr.update(0,p)
    spike=tr.update(.1,[800.,-20.,350.]);normal=tr.update(.2,p)
    assert spike['reason']=='jump' and not spike['accepted'] and spike['position']==p
    assert normal['accepted'] and np.allclose(normal['position'],p)
    tr=AdaptivePointTracker();tr.update(0,p);tr.update(.1,[800.,-20.,350.])
    relocated=tr.update(.2,[805.,-20.,350.])
    assert relocated['status']=='reacquired' and relocated['position'][0]==805
    checks['single_spike_rejected_coherent_relocation_confirmed']=True
    tr=AdaptivePointTracker();tr.update(0,p)
    bad=tr.update(.1,[float('nan'),0,350])
    assert bad['reason']=='invalid' and not bad['accepted'] and bad['position']==p
    checks['nonfinite_point_is_missing_not_measurement']=True
    rng=np.random.default_rng(441)
    ts=np.cumsum(rng.uniform(.095,.21,200))
    truth=np.tile(p,(200,1));raw=truth+rng.normal(0,4,truth.shape)
    tr=AdaptivePointTracker();filtered=np.array([tr.update(t,z)['position'] for t,z in zip(ts,raw)])
    rmse=lambda a,b:float(np.sqrt(np.mean(np.sum((a-b)**2,axis=1))))
    jitter_raw=rmse(raw[20:],truth[20:]);jitter_filtered=rmse(filtered[20:],truth[20:])
    assert jitter_filtered<.8*jitter_raw
    checks['stationary_noise_reduction']=dict(raw_rmse_mm=jitter_raw,filtered_rmse_mm=jitter_filtered)
    ts=np.cumsum(rng.uniform(.095,.21,200))
    truth=np.column_stack([70*np.sin(2*np.pi*.8*ts),50*np.cos(2*np.pi*.8*ts),350+10*np.sin(2*np.pi*.4*ts)])
    raw=truth+rng.normal(0,2,truth.shape);tr=AdaptivePointTracker()
    filtered=np.array([tr.update(t,z)['position'] for t,z in zip(ts,raw)])
    fixed=[raw[0]]
    for k in range(1,len(ts)):
        a=alpha(Parameters().min_cutoff_hz,ts[k]-ts[k-1]);fixed.append(a*raw[k]+(1-a)*fixed[-1])
    moving=rmse(filtered[20:],truth[20:]);fixed_error=rmse(np.array(fixed)[20:],truth[20:])
    assert moving<fixed_error*.6
    checks['known_moving_path_adapts_better_than_fixed_smoothing']=dict(adaptive_rmse_mm=moving,fixed_cutoff_rmse_mm=fixed_error)
    # A prefix produces identical outputs without access to later observations.
    tr=AdaptivePointTracker();prefix=[tr.update(t,z) for t,z in zip(ts[:50],raw[:50])]
    tr=AdaptivePointTracker();full=[tr.update(t,z) for t,z in zip(ts,raw)]
    assert prefix==full[:50]
    checks['causal_prefix_invariance']=True
    # Explicitly cover changes in capture cadence and a long interruption.
    tr=AdaptivePointTracker();t0=0
    for step in [.01,.2,.03,.15,.45,.1]:
        t0+=step;state=tr.update(t0,[10+t0*100,20,350]);assert np.isfinite(state['position']).all()
    checks['irregular_capture_intervals_finite']=True
    result=dict(passed=True,parameters=Parameters().__dict__,checks=checks,synthetic_only=True)
    out=Path(__file__).resolve().parents[2]/'build/python-validation';out.mkdir(exist_ok=True)
    (out/'synthetic_validation.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))

if __name__=='__main__': run()
