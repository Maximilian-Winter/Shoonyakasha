"""Fixed-tick playback and incremental reset/replay of registered models."""
import math
from typing import Protocol


class Simulation(Protocol):
    def reset(self, seed: int) -> None: ...
    def step(self, dt: float) -> None: ...


class Clock:
    def __init__(self, step=1.0 / 120.0, max_steps=12):
        if not math.isfinite(step) or step <= 0 or not isinstance(max_steps, int) or max_steps < 1:
            raise ValueError("invalid clock settings")
        self.step, self.max_steps = step, max_steps
        self.tick = 0
        self.accumulator = 0.0
        self._speed = 1.0

    @property
    def time(self):
        return self.tick * self.step

    @property
    def speed(self):
        return self._speed

    @speed.setter
    def speed(self, value):
        if not math.isfinite(value) or value < 0:
            raise ValueError("speed must be finite and nonnegative")
        self._speed = value

    def consume(self, dt):
        if not math.isfinite(dt) or dt < 0:
            raise ValueError("dt must be finite and nonnegative")
        self.accumulator += dt * self.speed
        count = min(self.max_steps, int((self.accumulator + self.step * 1e-9) / self.step))
        self.accumulator = max(0.0, self.accumulator - count * self.step)
        return count

    def reset(self):
        self.tick, self.accumulator = 0, 0.0


class Playback:
    def __init__(self, simulation=None, *, timeline=None, parameters=None, seed=0,
                 step=1.0 / 120.0, max_steps=12, replay_batch=240):
        if not isinstance(replay_batch, int) or replay_batch < 1:
            raise ValueError("replay_batch must be positive")
        self.simulation, self.timeline, self.parameters = simulation, timeline, parameters
        self.seed, self.replay_batch = seed, replay_batch
        self.clock = Clock(step, max_steps)
        self.playing = False
        self._target = None
        self._seek_time = 0.0
        self._resume = False
        self._export_locked = False
        self.reset()

    @property
    def time(self):
        return self.clock.time

    @property
    def duration(self):
        return self.timeline.duration if self.timeline else math.inf

    @property
    def seeking(self):
        return self._target is not None

    @property
    def progress(self):
        return min(1.0, self.clock.tick / max(1, self._target)) if self.seeking else 1.0

    def _lock(self):
        if self.timeline:
            self.timeline.lock(self.playing or self.seeking or self._export_locked)

    def play(self):
        if self.seeking:
            self._resume = True
        else:
            if self.timeline:
                self.timeline.evaluate(self.time)
            self.playing = True
        self._lock()

    def pause(self):
        self.playing = self._resume = False
        self._lock()

    def reset(self):
        self.playing = False
        self._resume = False
        self._target = None
        self.clock.reset()
        if self.parameters:
            self.parameters.reset()
        if self.timeline:
            self.timeline.reset()
            self.timeline.evaluate(0.0)
        if self.simulation:
            self.simulation.reset(self.seed)
        self._lock()

    def restart(self):
        self.reset()
        self.play()

    def _step(self):
        # Parameters at the beginning of each interval drive its integration.
        if self.timeline:
            self.timeline.evaluate(self.time)
        if self.simulation:
            self.simulation.step(self.clock.step)
        self.clock.tick += 1
        if self.timeline:
            self.timeline.evaluate(min(self.time, self.duration))

    def single_step(self):
        self.pause()
        if not self.seeking and self.time + self.clock.step <= self.duration + 1e-9:
            self._step()

    def seek(self, time, *, resume=False):
        if not math.isfinite(time) or time < 0:
            raise ValueError("seek time must be finite and nonnegative")
        time = min(time, self.duration)
        self.reset()
        self._target = int(math.floor(time / self.clock.step + 1e-9))
        self._seek_time, self._resume = time, resume
        self._lock()

    def cancel_seek(self):
        self._target = None
        self.pause()

    def advance_to(self, time):
        """Advance toward an absolute sample in bounded work; return readiness."""
        if not math.isfinite(time) or time < 0 or time > self.duration + 1e-9:
            raise ValueError("invalid sample time")
        target = int(math.floor(time / self.clock.step + 1e-9))
        if self.clock.tick > target:
            self.seek(time)
        count = min(self.replay_batch, target - self.clock.tick)
        for _ in range(count):
            self._step()
        ready = self.clock.tick == target
        if ready and self.timeline:
            self.timeline.evaluate(time)
        return ready

    def update(self, dt):
        if self.seeking:
            if self.advance_to(self._seek_time):
                resume = self._resume
                self._target = None
                self.playing = resume
                self._lock()
            return
        if self.playing:
            for _ in range(self.clock.consume(dt)):
                if self.time + self.clock.step > self.duration + 1e-9:
                    if self.timeline:
                        self.timeline.evaluate(self.duration)
                    self.pause()
                    break
                self._step()
            if self.time >= self.duration - 1e-9:
                self.pause()
