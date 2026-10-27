"""Nuclei found in the nucleons, fissions found between looks, and the
rules for neutron capture and emission.

Nuclei are the groups of nucleons that pairs closer than `bond` link. A
FissionTracker follows them from look to look: a nucleus whose nucleons now
lie in two or more groups of at least `min_fragment` nucleons has split.

Three rules add what the potentials do not produce (nuclear-sim-design.md,
sections 9.5, 9.6 and 9.10). Momentum is unchanged by each, and energy too
apart from what emission radiates:

- emission_kicks: each fragment of a fission gives off its outermost
  neutrons, as real fragments shed neutrons because of quantum level
  structure a classical drop does not have.
- CaptureRule: a heavy nucleus that has captured a neutron turns its heat
  into stretching along its long axis, whenever it is stretching, until it
  splits or its time is up: the excitation of a real compound nucleus flows
  into deformation. A heavy nucleus that has captured nothing is left alone.
- steer_kicks: a free neutron that comes within a capture radius of a heavy
  nucleus's centre is turned toward it, as a real slow neutron is captured
  over a cross-section much larger than the nucleus.

Each gives a kick per nucleon, applied as v' = v * scale + dv.
"""

import numpy as np
from scipy.sparse import coo_matrix
from scipy.sparse.csgraph import connected_components
from scipy.spatial import cKDTree

KICK = np.dtype([("dv", "<f4", 3), ("scale", "<f4")])


def find_nuclei(positions, bond=1.6):
    """Labels 0..k-1 of the groups that pairs closer than `bond` link."""
    count = len(positions)
    pairs = cKDTree(positions).query_pairs(bond, output_type="ndarray")
    graph = coo_matrix((np.ones(len(pairs), dtype=np.int8), (pairs[:, 0], pairs[:, 1])), shape=(count, count))
    return connected_components(graph, directed=False)[1]


class FissionTracker:
    """Follows nuclei from look to look and reports those that split into
    two or more large fragments.

    Every nucleon carries the id of the nucleus it belongs to, starting from
    the groups of the first look. A nucleus has split when its nucleons lie
    in two or more groups of at least `min_fragment`; each of those
    fragments then gets a new id. Fragments that touch again keep their own
    ids, so they are not reported again when they part. `bond` is three
    times the equilibrium distance, where the strong force has all but
    vanished: fragments still joined by a thin neck have not split."""

    def __init__(self, bond=3.0, min_fragment=20):
        self.bond = bond
        self.min_fragment = min_fragment
        self.nucleus = None
        self.next_id = 0

    def update(self, positions):
        """The fissions since the last call: for each, a list of fragments,
        each an array of the indices of the nucleons in its group. Also
        returns the groups' labels."""
        labels = find_nuclei(positions, self.bond)
        fissions = []
        if self.nucleus is None or len(self.nucleus) != len(labels):
            self.nucleus = labels.copy()
            self.next_id = int(labels.max()) + 1
            return fissions, labels
        for nucleus in np.unique(self.nucleus):
            members = np.nonzero(self.nucleus == nucleus)[0]
            if len(members) < 2 * self.min_fragment:
                continue
            now, sizes = np.unique(labels[members], return_counts=True)
            big = now[sizes >= self.min_fragment]
            if len(big) < 2:
                continue
            for group in big:
                self.nucleus[members[labels[members] == group]] = self.next_id
                self.next_id += 1
            fissions.append([np.nonzero(labels == group)[0] for group in big])
        return fissions, labels


def emission_kicks(state, fragments, per_fragment=1, speed=2.5, radiate=0.0, mass=1.0, kicks=None):
    """Kicks that make each fragment give off its `per_fragment` outermost
    neutrons at `speed` (relative to the fragment) straight out from its
    centre, the rest of the fragment recoiling, and then radiate the fraction
    `radiate` of the fragment's remaining internal kinetic energy.

    The neutrons are paid for from the fragment's motion about its centre of
    mass: momentum is conserved, and so is energy, apart from what is
    radiated. A neutron gets less than `speed` when the fragment's internal
    energy cannot pay for it. Writes into and returns `kicks` (one KICK per
    nucleon of `state`, made with no effect when None), the indices of the
    neutrons given off, and the energy radiated."""
    if kicks is None:
        kicks = np.zeros(len(state), dtype=KICK)
        kicks["scale"] = 1.0
    emitted, radiated = [], 0.0
    for members in fragments:
        neutrons = members[state["type"][members] < 0.5]
        if len(neutrons) == 0:
            continue
        pos = state["position"][members].astype(np.float64)
        vel = state["velocity"][members].astype(np.float64)
        center, v_center = pos.mean(axis=0), vel.mean(axis=0)
        r = np.linalg.norm(pos[np.isin(members, neutrons)] - center, axis=1)
        chosen = neutrons[np.argsort(r)[::-1][:per_fragment]]
        rest = np.setdiff1d(members, chosen)
        u_rest = state["velocity"][rest].astype(np.float64) - v_center      # velocities about the centre
        u_rest_mean = u_rest.mean(axis=0)
        k_before = 0.5 * mass * np.sum((vel - v_center) ** 2)
        k_rest = 0.5 * mass * np.sum((u_rest - u_rest_mean) ** 2)            # the rest's internal motion

        out = state["position"][chosen].astype(np.float64) - center
        out /= np.maximum(np.linalg.norm(out, axis=1, keepdims=True), 1e-6)
        # Neutrons at w along `out`; the rest recoils by delta so the centre keeps its velocity.
        recoil = out.sum(axis=0) / len(rest)                                 # delta = -w * recoil
        per_w2 = 0.5 * mass * (len(chosen) + len(rest) * float(recoil @ recoil))
        w = min(speed, np.sqrt(k_before / per_w2))
        left = max(0.0, k_before - per_w2 * w * w)                           # for the rest's internal motion
        s = np.sqrt(left * (1.0 - radiate) / k_rest) if k_rest > 0 else 0.0
        radiated += left * radiate if k_rest > 0 else left

        delta = -w * recoil
        kicks["scale"][rest] = s
        kicks["dv"][rest] = v_center - s * (v_center + u_rest_mean) + delta
        kicks["scale"][chosen] = 0.0
        kicks["dv"][chosen] = v_center + out * w
        emitted.extend(chosen.tolist())
    return kicks, np.array(emitted, dtype=np.int64), radiated


def stretch_kicks(state, members, fraction=1.0, mass=1.0, kicks=None):
    """Kicks that turn the fraction `fraction` of the internal kinetic energy
    of the nucleons `members` into a stretch along their long axis: a
    velocity b (x . u) u about the centre of mass, x the position about it
    and u the axis. Momentum and energy are unchanged. Writes into and
    returns `kicks` (one KICK per nucleon of `state`, made with no effect
    when None)."""
    if kicks is None:
        kicks = np.zeros(len(state), dtype=KICK)
        kicks["scale"] = 1.0
    pos = state["position"][members].astype(np.float64)
    vel = state["velocity"][members].astype(np.float64)
    x = pos - pos.mean(axis=0)
    v_center = vel.mean(axis=0)
    internal = vel - v_center
    axis = np.linalg.eigh(x.T @ x)[1][:, -1]
    field = (x @ axis)[:, None] * axis[None, :]
    field -= field.mean(axis=0)
    keep = np.sqrt(1.0 - fraction)
    # b > 0 with |keep * internal + b * field|^2 = |internal|^2
    qa = 0.5 * mass * np.sum(field ** 2)
    qb = mass * np.sum(keep * internal * field)
    qc = 0.5 * mass * (keep * keep - 1.0) * np.sum(internal ** 2)
    b = (-qb + np.sqrt(max(qb * qb - 4.0 * qa * qc, 0.0))) / (2.0 * qa) if qa > 0 else 0.0
    kicks["scale"][members] = keep
    kicks["dv"][members] = (1.0 - keep) * v_center + b * field
    return kicks


def stretching(state, members, ahead=0.0, mass=1.0):
    """Whether the nucleons `members` are lengthening along their long axis
    `ahead` time units after `state`: the rate L = sum (x . u)(v . u), x and
    v about the centre of mass and u the axis, carried forward with its own
    rate of change, sum (v . u)^2 + (x . u)(F . u) / mass, from the forces
    in `state`."""
    x = state["position"][members].astype(np.float64)
    x -= x.mean(axis=0)
    v = state["velocity"][members].astype(np.float64)
    v -= v.mean(axis=0)
    f = state["force"][members].astype(np.float64)
    f -= f.mean(axis=0)
    axis = np.linalg.eigh(x.T @ x)[1][:, -1]
    xu, vu, fu = x @ axis, v @ axis, f @ axis
    rate = float((xu * vu).sum())
    change = float((vu * vu).sum() + (xu * fu).sum() / mass)
    return rate + ahead * change > 0.0


class CaptureRule:
    """Finds heavy nuclei that capture a neutron and stretches them.

    A nucleus of at least `min_size` nucleons captures when it takes in
    nucleons that were, at the last look, in a group of at most
    `max_captured`. For `window` time units after that, update gives it
    kicks that turn the fraction `fraction` of its internal kinetic energy
    into stretching (stretch_kicks) while it will be lengthening when they
    land (stretching, carried forward from the look), from looks
    taken at least `interval` after its last kicks landed: a stretch
    replaces the nucleus's internal motion, so one worked out from a look
    older than the last would undo it."""

    def __init__(self, min_size=200, max_captured=4, window=200.0, interval=0.5, fraction=0.5):
        self.min_size = min_size
        self.max_captured = max_captured
        self.window = window
        self.interval = interval
        self.fraction = fraction
        self.group_size = None          # per nucleon, the size of its group at the last look
        self.excited_until = None
        self.last_stretch = None

    def update(self, state, labels, time, now=None, mass=1.0, kicks=None):
        """Kicks for this look, given `labels` (find_nuclei's), the
        simulation time `time` the look was taken at and the time `now` its
        kicks land at (`time` when None); the first look only records the
        groups. Returns
        `kicks` (or None when nothing was kicked and none was given), the
        number of captures and of nuclei stretched."""
        now = time if now is None else now
        count = len(labels)
        sizes = np.bincount(labels)
        if self.group_size is None or len(self.group_size) != count:
            self.group_size = sizes[labels]
            self.excited_until = np.full(count, -np.inf)
            self.last_stretch = np.full(count, -np.inf)
            return kicks, 0, 0
        captures = stretched = 0
        for group in np.nonzero(sizes >= self.min_size)[0]:
            members = np.nonzero(labels == group)[0]
            if self.group_size[members].min() <= self.max_captured:
                self.excited_until[members] = time + self.window
                captures += 1
            if (self.excited_until[members].max() > time
                    and time - self.last_stretch[members].max() >= self.interval
                    and stretching(state, members, now - time, mass)):
                if kicks is None:
                    kicks = np.zeros(count, dtype=KICK)
                    kicks["scale"] = 1.0
                stretch_kicks(state, members, self.fraction, mass=mass, kicks=kicks)
                self.last_stretch[members] = now
                stretched += 1
        self.group_size = sizes[labels]
        return kicks, captures, stretched


def steer_kicks(state, labels, radius=10.0, min_size=200, aimed=0.99, mass=1.0, kicks=None):
    """Kicks that turn each free neutron within `radius` of the centre of a
    nucleus of at least `min_size` nucleons toward that centre.

    A free neutron is a neutron alone in its group of `labels`. The turn is
    an elastic one between the neutron and the nucleus: in their centre of
    mass frame their relative velocity keeps its length and points from the
    neutron to the nucleus's centre, so momentum and energy are unchanged
    and the nucleus recoils. A neutron already heading for the centre
    (cosine above `aimed`) is left alone. Writes into and returns `kicks`
    (one KICK per nucleon of `state`, made with no effect when None) and the
    number of neutrons turned."""
    if kicks is None:
        kicks = np.zeros(len(state), dtype=KICK)
        kicks["scale"] = 1.0
    sizes = np.bincount(labels)
    heavy = np.nonzero(sizes >= min_size)[0]
    free = np.nonzero((sizes[labels] == 1) & (state["type"] < 0.5))[0]
    if len(heavy) == 0 or len(free) == 0:
        return kicks, 0
    positions = state["position"].astype(np.float64)
    velocities = state["velocity"].astype(np.float64)
    members = [np.nonzero(labels == group)[0] for group in heavy]
    centers = np.array([positions[m].mean(axis=0) for m in members])
    distance, nearest = cKDTree(centers).query(positions[free], distance_upper_bound=radius)
    turned = 0
    for n, d, k in zip(free, distance, nearest):
        if not np.isfinite(d) or d < 1e-6:
            continue
        nucleus = members[k]
        big = mass * len(nucleus)
        v_nucleus = velocities[nucleus].mean(axis=0)
        relative = velocities[n] - v_nucleus
        speed = np.linalg.norm(relative)
        inward = (centers[k] - positions[n]) / d
        if speed < 1e-9 or relative @ inward > aimed * speed:
            continue
        v_cm = (mass * velocities[n] + big * v_nucleus) / (mass + big)
        relative = speed * inward
        kicks["scale"][n] = 0.0
        kicks["dv"][n] = v_cm + big / (mass + big) * relative
        kicks["dv"][nucleus] += v_cm - mass / (mass + big) * relative - v_nucleus
        turned += 1
    return kicks, turned


def apply_kicks(state, kicks):
    """`state` with `kicks` applied, as the viewer's kick pass applies them."""
    out = state.copy()
    out["velocity"] = out["velocity"] * kicks["scale"][:, None] + kicks["dv"]
    out["energy"] = 0.5 * np.einsum("ij,ij->i", out["velocity"], out["velocity"])
    return out
