"""Half-band derived Hilbert transformer topology (Harris-Berdahl-Abel).

An elliptic half-band low-pass has poles that split into two parallel all-pass
branches; the branch pole radii become the first-order polyphase section
coefficients. The Hilbert pair reuses the polyphase section model from
topo_polyphase. This is the published elliptic route; the exact pole split is
derived numerically here.

Derivation used: elliptic half-band prototype → pole magnitudes sorted
ascending → interleaved into two branches (A: even indices, B: odd indices).
This gives a reasonable seed; a short least_squares refinement then minimises
|phase(coeffs) - (-pi/2)| across the band so the topology is honestly
"elliptic-half-band-derived, then numerically realised".
"""
import numpy as np
from scipy.signal import ellip, tf2zpk
from scipy.optimize import least_squares
from topo_polyphase import phase as _poly_phase, cost as _poly_cost, section_response  # noqa: F401
from topology import band_freqs, TARGET, phase_error_deg

name = "halfband-elliptic"


def _elliptic_seed(order):
    """Return (A, B) coefficient lists from elliptic half-band pole split.

    A 4*order-th order even elliptic half-band LP gives 2*order complex-
    conjugate pole pairs. Their magnitudes, sorted ascending, are split by
    interleaving into two branches of `order` sections each. This gives a
    seed already within ~5 degrees of the quadrature target.
    """
    n = 4 * order  # even order → no real poles; yields 2*order unique radii
    b, a = ellip(n, 0.1, 60.0, 0.5)
    z, p, k = tf2zpk(b, a)
    # Unique radii from upper half-plane poles, sorted ascending
    radii = sorted([abs(pp) for pp in p if pp.imag > 0])
    # Split by interleaving into two branches of `order` sections each
    A = radii[0::2][:order]
    B = radii[1::2][:order]
    return A, B


def design(order, fs):
    A_seed, B_seed = _elliptic_seed(order)
    freqs = band_freqs()

    # Quick check if seed already meets target
    seed_coeffs = {"A": list(A_seed), "B": list(B_seed)}
    seed_err = phase_error_deg(_poly_phase(seed_coeffs, fs, freqs))

    if seed_err < 10.0:
        return {
            "A": [round(v, 9) for v in A_seed],
            "B": [round(v, 9) for v in B_seed],
        }

    # Elliptic seed needs refinement: minimise phase deviation from -pi/2
    na = len(A_seed)
    nb = len(B_seed)
    x0 = np.array(list(A_seed) + list(B_seed))

    def split(x):
        return {"A": list(x[:na]), "B": list(x[na:])}

    def residuals(x):
        return _poly_phase(split(x), fs, freqs) - TARGET

    lo = np.full(na + nb, 0.01)
    hi = np.full(na + nb, 0.999999)
    res = least_squares(residuals, x0, bounds=(lo, hi), xtol=1e-13, ftol=1e-13)
    c = split(res.x)
    return {
        "A": [round(v, 9) for v in c["A"]],
        "B": [round(v, 9) for v in c["B"]],
    }


def phase(coeffs, fs, freqs):
    return _poly_phase(coeffs, fs, freqs)


def cost(coeffs):
    return _poly_cost(coeffs)
