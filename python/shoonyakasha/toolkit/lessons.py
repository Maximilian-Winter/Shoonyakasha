"""Ordered timed chapters, using the same tracks as standalone timelines."""
from dataclasses import dataclass
import math
from .timeline import Binding, Timeline, Track


class _ChapterTrack:
    def __init__(self, segments):
        self.segments = segments
        self.binding = segments[0][2].binding
        self.baseline = getattr(self.binding.parameter, "default", self.binding.get())
        self.duration = segments[-1][0] + segments[-1][1]

    def sample(self, time):
        for start, duration, track in reversed(self.segments):
            if time >= start:
                return track.sample(min(duration, time - start))
        return self.baseline


@dataclass
class Chapter:
    title: str
    duration: float
    timeline: object = None
    caption: str = ""


class Lesson:
    def __init__(self, *chapters, title=None, caption=None):
        if not chapters:
            raise ValueError("a lesson requires chapters")
        self.chapters = tuple(chapters)
        self.starts = []
        self.timeline = Timeline()
        grouped = {}
        offset = 0.0
        for chapter in chapters:
            if not math.isfinite(chapter.duration) or chapter.duration <= 0:
                raise ValueError("chapter durations must be positive")
            self.starts.append(offset)
            if chapter.timeline:
                if chapter.timeline.duration > chapter.duration:
                    raise ValueError("chapter tracks exceed chapter duration")
                for track in chapter.timeline.tracks:
                    key = track.binding.key
                    grouped.setdefault(key, (track, []))[1].append((offset, chapter.duration, track))
            offset += chapter.duration
        self.timeline._duration = offset
        for original, segments in grouped.values():
            self.timeline.add(_ChapterTrack(segments))
        for target, attr in ((title, "title"), (caption, "caption")):
            if target is not None:
                self.timeline.add(Track(Binding.attribute(target, "text"),
                                        [(t, getattr(c, attr)) for t, c in zip(self.starts, chapters)], "hold"))

    @property
    def duration(self):
        return self.timeline.duration

    def chapter_at(self, time):
        return max(i for i, start in enumerate(self.starts) if start <= max(0, time))

    def navigate(self, playback, index):
        if not 0 <= index < len(self.chapters):
            raise IndexError(index)
        playback.seek(self.starts[index], resume=False)

    def next(self, playback):
        self.navigate(playback, min(len(self.chapters) - 1, self.chapter_at(playback.time) + 1))

    def previous(self, playback):
        self.navigate(playback, max(0, self.chapter_at(playback.time) - 1))
