"""Fit the linearly transformed cosines (LTC) table for the showroom's
rectangular area lights, and write it as ltc.bin beside this file.

    python ltc_fit.py              # a few minutes; writes ltc.bin
    python ltc_fit.py --check      # also prints how well the fits match

Heitz, Dupuy, Hill and Neubelt, "Real-Time Polygonal-Light Shading with
Linearly Transformed Cosines" (SIGGRAPH 2016): the GGX lobe for one view
angle and roughness is close to a clamped cosine stretched by a 3x3 matrix
M. The integral of the cosine over a polygon has a closed form, so the
shader takes the light's corners through M's inverse and integrates there.
This script finds M for 64 x 64 (roughness, view angle) pairs the way the
paper's fitLTC.cpp does: Nelder-Mead on M's three free terms, each fit
starting from its neighbour's.

The lobe fitted is the pipeline's own: GGX with the separable Schlick-Smith
term, k = alpha / 2, which the IBL's brdfLUT integrates too. So the lights
take their scale and Fresnel from brdfLUT, and this table holds only the
inverse matrices.

ltc.bin: 64 x 64 vec4 of float32, little-endian, roughness across (x, the
perceptual roughness i / 63) and the view angle down (y, sqrt(1 - cos theta)
= j / 63), element j * 64 + i. Each is (a, b, c, d) of the inverse matrix
divided by its middle term:

    | a 0 c |
    | 0 1 0 |
    | b 0 d |

which GLSL builds as mat3(vec3(a, 0, b), vec3(0, 1, 0), vec3(c, 0, d)).
"""

import argparse
import math
import os
import sys
import time

import numpy as np

N = 64              # table size
SAMPLES = 32        # per axis, for the error and the lobe's average direction
MIN_ALPHA = 1e-5


# ── The pipeline's BRDF ───────────────────────────────────────────────────

def smith_g1(cos, alpha):
    """Schlick-GGX masking with k = alpha / 2 (brdf_lut.comp)."""
    k = alpha / 2.0
    return cos / (cos * (1.0 - k) + k)


def brdf_eval(V, L, alpha):
    """BRDF times cos(L), and the pdf of brdf_sample, for each row of L."""
    Lz = L[:, 2]
    H = V + L
    H /= np.linalg.norm(H, axis=1, keepdims=True)
    Hz = np.maximum(H[:, 2], 1e-7)
    slope2 = (H[:, 0] ** 2 + H[:, 1] ** 2) / (Hz * Hz)
    D = 1.0 / (1.0 + slope2 / (alpha * alpha))
    D = D * D / (math.pi * alpha * alpha * Hz ** 4)
    VdotH = np.maximum(H @ V, 1e-7)
    pdf = np.abs(D * Hz / (4.0 * VdotH))
    G = np.where(Lz > 0.0, smith_g1(V[2], alpha) * smith_g1(np.maximum(Lz, 0.0), alpha), 0.0)
    value = D * G / (4.0 * V[2])
    return np.where(Lz > 0.0, value, 0.0), pdf


def brdf_sample(V, alpha, u1, u2):
    """Directions from GGX's distribution of normals, reflected about V."""
    phi = 2.0 * math.pi * u1
    r = alpha * np.sqrt(u2 / (1.0 - u2))
    Nm = np.stack([r * np.cos(phi), r * np.sin(phi), np.ones_like(r)], axis=1)
    Nm /= np.linalg.norm(Nm, axis=1, keepdims=True)
    return -V + 2.0 * Nm * (Nm @ V)[:, None]


# ── The linearly transformed cosine ───────────────────────────────────────

class LTC:
    def __init__(self):
        self.m11 = 1.0
        self.m22 = 1.0
        self.m13 = 0.0
        self.X = np.array([1.0, 0.0, 0.0])
        self.Y = np.array([0.0, 1.0, 0.0])
        self.Z = np.array([0.0, 0.0, 1.0])
        self.magnitude = 1.0
        self.update()

    def update(self):
        frame = np.stack([self.X, self.Y, self.Z], axis=1)
        self.M = frame @ np.array([[self.m11, 0.0, self.m13],
                                   [0.0, self.m22, 0.0],
                                   [0.0, 0.0, 1.0]])
        self.invM = np.linalg.inv(self.M)
        self.detM = abs(np.linalg.det(self.M))

    def eval(self, L):
        original = L @ self.invM.T
        original /= np.linalg.norm(original, axis=1, keepdims=True)
        stretched = original @ self.M.T
        length = np.linalg.norm(stretched, axis=1)
        jacobian = self.detM / length ** 3
        D = np.maximum(original[:, 2], 0.0) / math.pi
        return self.magnitude * D / jacobian

    def sample(self, u1, u2):
        theta = np.arccos(np.sqrt(u1))
        phi = 2.0 * math.pi * u2
        d = np.stack([np.sin(theta) * np.cos(phi), np.sin(theta) * np.sin(phi), np.cos(theta)], axis=1)
        L = d @ self.M.T
        return L / np.linalg.norm(L, axis=1, keepdims=True)


_u = (np.arange(SAMPLES) + 0.5) / SAMPLES
U1, U2 = (a.ravel() for a in np.meshgrid(_u, _u))


def average_terms(V, alpha):
    """The BRDF's directional albedo and its average direction."""
    L = brdf_sample(V, alpha, U1, U2)
    value, pdf = brdf_eval(V, L, alpha)
    weight = np.where(pdf > 0.0, value / np.maximum(pdf, 1e-30), 0.0)
    norm = weight.mean()
    direction = (weight[:, None] * L).sum(axis=0)
    direction[1] = 0.0
    return norm, direction / np.linalg.norm(direction)


def fit_error(ltc, V, alpha):
    """fitLTC.cpp's error: the cubed difference, importance sampled from
    both distributions."""
    error = 0.0
    for L in (ltc.sample(U1, U2), brdf_sample(V, alpha, U1, U2)):
        brdf, pdf_brdf = brdf_eval(V, L, alpha)
        value = ltc.eval(L)
        pdf_ltc = value / ltc.magnitude
        error += (np.abs(brdf - value) ** 3 / np.maximum(pdf_ltc + pdf_brdf, 1e-30)).sum()
    return error / (SAMPLES * SAMPLES)


def nelder_mead(f, start, delta, tolerance, iterations):
    dim = len(start)
    simplex = [np.array(start, dtype=float)]
    for i in range(dim):
        p = np.array(start, dtype=float)
        p[i] += delta
        simplex.append(p)
    values = [f(p) for p in simplex]
    for _ in range(iterations):
        order = np.argsort(values)
        simplex = [simplex[i] for i in order]
        values = [values[i] for i in order]
        lo, hi = abs(values[0]), abs(values[-1])
        if 2.0 * abs(lo - hi) < (lo + hi) * tolerance:
            break
        centre = sum(simplex[:-1]) / dim
        reflected = centre + (centre - simplex[-1])
        fr = f(reflected)
        if fr < values[-2]:
            if fr < values[0]:
                expanded = centre + 2.0 * (centre - simplex[-1])
                fe = f(expanded)
                if fe < fr:
                    simplex[-1], values[-1] = expanded, fe
                    continue
            simplex[-1], values[-1] = reflected, fr
            continue
        contracted = centre - 0.5 * (centre - simplex[-1])
        fc = f(contracted)
        if fc < values[-1]:
            simplex[-1], values[-1] = contracted, fc
            continue
        for k in range(1, dim + 1):
            simplex[k] = simplex[0] + 0.5 * (simplex[k] - simplex[0])
            values[k] = f(simplex[k])
    best = int(np.argmin(values))
    return simplex[best], values[best]


def fit(ltc, V, alpha, isotropic):
    def apply(params):
        m11 = max(params[0], 1e-7)
        m22 = max(params[1], 1e-7)
        if isotropic:
            ltc.m11, ltc.m22, ltc.m13 = m11, m11, 0.0
        else:
            ltc.m11, ltc.m22, ltc.m13 = m11, m22, params[2]
        ltc.update()

    def error(params):
        apply(params)
        return fit_error(ltc, V, alpha)

    best, _ = nelder_mead(error, [ltc.m11, ltc.m22, ltc.m13], 0.05, 1e-5, 100)
    apply(best)


def fit_table(progress=True):
    matrices = np.zeros((N, N, 3, 3))       # [view angle j, roughness i]
    magnitudes = np.zeros((N, N))
    ltc = LTC()
    started = time.time()
    for a in range(N - 1, -1, -1):          # rough to smooth: each starts from the last
        for t in range(N):
            x = t / (N - 1)
            theta = min(1.57, math.acos(1.0 - x * x))
            V = np.array([math.sin(theta), 0.0, math.cos(theta)])
            roughness = a / (N - 1)
            alpha = max(roughness * roughness, MIN_ALPHA)
            ltc.magnitude, direction = average_terms(V, alpha)
            if t == 0:
                # Facing the view the lobe is round, around the normal.
                ltc.X = np.array([1.0, 0.0, 0.0])
                ltc.Y = np.array([0.0, 1.0, 0.0])
                ltc.Z = np.array([0.0, 0.0, 1.0])
                if a == N - 1:
                    ltc.m11, ltc.m22 = 1.0, 1.0
                else:
                    previous = matrices[0, a + 1]
                    ltc.m11, ltc.m22 = previous[0, 0], previous[1, 1]
                ltc.m13 = 0.0
                ltc.update()
                isotropic = True
            else:
                ltc.X = np.array([direction[2], 0.0, -direction[0]])
                ltc.Y = np.array([0.0, 1.0, 0.0])
                ltc.Z = direction
                ltc.update()
                isotropic = False
            fit(ltc, V, alpha, isotropic)
            matrices[t, a] = ltc.M
            magnitudes[t, a] = ltc.magnitude
        if progress:
            done = N - a
            left = (time.time() - started) / done * (N - done)
            print("\rroughness %2d/%d, %3.0f s left " % (done, N, left), end="", file=sys.stderr, flush=True)
    if progress:
        print(file=sys.stderr)
    return matrices, magnitudes


def pack(matrices):
    table = np.zeros((N, N, 4), dtype=np.float32)
    for j in range(N):
        for i in range(N):
            inv = np.linalg.inv(matrices[j, i])
            inv /= inv[1, 1]
            # numpy is row-major: inv[row, col]. GLSL's columns are (a, 0, b)
            # and (c, 0, d).
            table[j, i] = (inv[0, 0], inv[2, 0], inv[0, 2], inv[2, 2])
    return table


def check(matrices, magnitudes):
    """The integral of each fitted lobe against the BRDF's, over a few
    spherical caps around the lobe's peak: how well a light would match."""
    worst = 0.0
    for j in (8, 32, 63):
        for i in (8, 32, 63):
            x = j / (N - 1)
            theta = min(1.57, math.acos(1.0 - x * x))
            V = np.array([math.sin(theta), 0.0, math.cos(theta)])
            alpha = max((i / (N - 1)) ** 2, MIN_ALPHA)
            ltc = LTC()
            ltc.magnitude = magnitudes[j, i]
            ltc.M = matrices[j, i]
            ltc.invM = np.linalg.inv(ltc.M)
            ltc.detM = abs(np.linalg.det(ltc.M))
            L = ltc.sample(U1, U2)
            brdf, _ = brdf_eval(V, L, alpha)
            ratio = np.mean(brdf / np.maximum(ltc.eval(L) / ltc.magnitude, 1e-30)) / ltc.magnitude
            worst = max(worst, abs(1.0 - ratio))
            print("roughness %.2f, cos theta %.2f: BRDF/LTC over the LTC's lobe %.3f"
                  % (i / (N - 1), V[2], ratio))
    print("largest difference %.1f%%" % (worst * 100.0))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "ltc.bin"))
    parser.add_argument("--check", action="store_true", help="print how closely the fits match the BRDF")
    args = parser.parse_args()
    matrices, magnitudes = fit_table()
    table = pack(matrices)
    if not np.all(np.isfinite(table)):
        sys.exit("the fit produced non-finite values")
    table.astype("<f4").tofile(args.out)
    print("wrote %s (%d bytes)" % (args.out, table.nbytes))
    if args.check:
        check(matrices, magnitudes)


if __name__ == "__main__":
    main()
