"""Causal, timestamp-aware per-class 3D tracking (millimetres and seconds).

Speed-adaptive vector low-pass based on Casiez/Roussel/Vogel's 1€ filter.
Shared XYZ cutoff, capped cutoff, explicit missing/outlier lifecycle and
two-observation relocation confirmation are extensions, not original ZScan code.
No camera, ground truth, future frames, file IO or model inference is required.
"""
from dataclasses import dataclass, asdict
import math
import numpy as np


@dataclass(frozen=True)
class Parameters:
    min_cutoff_hz: float = 1.2
    speed_beta: float = 0.02  # Hz per mm/s, shared norm across XYZ
    derivative_cutoff_hz: float = 1.0
    max_cutoff_hz: float = 10.0
    jump_slack_mm: float = 25.0
    max_speed_mm_s: float = 1200.0
    hold_seconds: float = 0.25
    reset_gap_seconds: float = 0.45
    trail_seconds: float = 0.65

    def __post_init__(self):
        for key, value in asdict(self).items():
            if not math.isfinite(value) or value < 0:
                raise ValueError(f"Invalid {key}")
        if min(self.min_cutoff_hz,self.derivative_cutoff_hz) <= 0:
            raise ValueError("Cutoffs must be positive")
        if self.max_cutoff_hz < self.min_cutoff_hz:
            raise ValueError("Maximum cutoff below minimum")
        if self.reset_gap_seconds < self.hold_seconds:
            raise ValueError("Reset gap below hold duration")


def alpha(cutoff_hz, dt):
    x = 2.0 * math.pi * cutoff_hz * dt
    return x / (1.0 + x)


class AdaptivePointTracker:
    def __init__(self, parameters=None):
        self.parameters = parameters or Parameters()
        self.position = None
        self.raw = None
        self.velocity = np.zeros(3)
        self.accepted_time = None
        self.input_time = None
        self.pending = None
        self.pending_time = None
        self.generation = 0
        self.ever_initialized = False
        self.history = []

    def _initialize(self, t, z, status):
        self.position=z.copy(); self.raw=z.copy(); self.velocity=np.zeros(3)
        self.accepted_time=t; self.pending=None; self.pending_time=None
        self.generation+=1; self.ever_initialized=True
        self.history=[(t,z.copy())]
        return self._snapshot(t,status,True,None)

    def _snapshot(self, t, status, accepted, reason):
        age=None if self.accepted_time is None else t-self.accepted_time
        visible=self.position is not None and age<=self.parameters.hold_seconds
        if not visible: status='lost'
        self.history=[(tt,p) for tt,p in self.history if t-tt<=self.parameters.trail_seconds]
        return dict(status=status,accepted=accepted,reason=reason,
                    position=self.position.tolist() if visible else None,
                    observation_age_s=age,generation=self.generation,
                    history=[dict(time_s=tt,position=p.tolist()) for tt,p in self.history])

    def update(self, time_s, point=None):
        t=float(time_s)
        if not math.isfinite(t) or (self.input_time is not None and t<=self.input_time):
            raise ValueError("Input timestamps must be finite and strictly increasing")
        z=None
        invalid=False
        if point is not None:
            z=np.asarray(point,dtype=float)
            if z.shape!=(3,): raise ValueError("Expected one XYZ point")
            if not np.isfinite(z).all() or z[2]<=0:
                z=None; invalid=True
        self.input_time=t
        p=self.parameters
        if z is None:
            self.pending=None;self.pending_time=None
            return self._snapshot(t,'held',False,'invalid' if invalid else 'missing')
        if self.position is None or t-self.accepted_time>p.reset_gap_seconds:
            return self._initialize(t,z,'reacquired' if self.ever_initialized else 'initialized')
        dt=t-self.accepted_time
        gate=p.jump_slack_mm+p.max_speed_mm_s*dt
        if np.linalg.norm(z-self.raw)>gate:
            # Do not drag the filter toward a single impossible jump. A coherent
            # second observation can establish a genuine relocation causally.
            confirmed=(self.pending is not None and
                       t-self.pending_time<=p.reset_gap_seconds and
                       np.linalg.norm(z-self.pending)<=p.jump_slack_mm+
                       p.max_speed_mm_s*(t-self.pending_time))
            if confirmed:
                return self._initialize(t,z,'reacquired')
            self.pending=z.copy();self.pending_time=t
            return self._snapshot(t,'held',False,'jump')
        self.pending=None;self.pending_time=None
        derivative=(z-self.raw)/dt
        ad=alpha(p.derivative_cutoff_hz,dt)
        self.velocity=ad*derivative+(1-ad)*self.velocity
        cutoff=min(p.max_cutoff_hz,p.min_cutoff_hz+p.speed_beta*float(np.linalg.norm(self.velocity)))
        a=alpha(cutoff,dt)
        self.position=a*z+(1-a)*self.position
        self.raw=z.copy();self.accepted_time=t
        self.history.append((t,self.position.copy()))
        state=self._snapshot(t,'updated',True,None)
        state['cutoff_hz']=cutoff
        return state
