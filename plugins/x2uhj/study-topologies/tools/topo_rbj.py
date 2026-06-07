"""RBJ second-order biquad all-pass cascade topology.

Two cascades of order RBJ all-pass biquads, designed per sample rate by a
digital minimax phase fit (the same procedure as design_quadrature_perfs.py).
"""
import numpy as np
from scipy.optimize import least_squares
from rbj import cascade_phase
from topology import F_LO, F_HI, TARGET, band_freqs, phase_error_deg

name = "rbj-biquad"

# First three entries are validated against the shipped per-rate table — keep
# them exactly. Entries 4-6 use band-spanning seeds geometrically spread
# across 20–20000 Hz with Q≈0.3 to give least_squares a reasonable starting
# point for order=4,5,6.
_SEED_HR = [(141.9, 0.2019), (671.7, 0.2122), (18654.0, 0.3031),
            (44.7, 0.30), (1414.0, 0.30), (14142.0, 0.30)]
_SEED_HI = [(24.0, 0.3090), (2992.0, 0.3848), (3220.0, 0.0963),
            (89.4, 0.30), (2828.0, 0.30), (20000.0, 0.30)]


def design(order, fs, **_ignored):  # accepts and ignores extra keyword args so the harness can call every topology uniformly
    """Fit a pair of RBJ all-pass biquad cascades for quadrature at *fs*.

    Returns a dict ``{"order": int, "H_R": [[f,q],...], "H_I": [[f,q],...]}``
    whose structure is private to this topology module — the harness accesses
    it only through :func:`phase` and :func:`cost`.

    *order* must be in 2..6 (seed table has 6 entries).
    """
    if not (2 <= order <= 6):
        raise ValueError(
            f"order must be in 2..6 (seed table has 6 entries); got {order}"
        )
    hr_seed = _SEED_HR[:order]
    hi_seed = _SEED_HI[:order]
    x0 = np.array([v for fq in hr_seed for v in fq] +
                  [v for fq in hi_seed for v in fq])
    freqs = band_freqs()

    def unpack(x):
        hr = [(x[2*i], x[2*i+1]) for i in range(order)]
        hi = [(x[2*(order+i)], x[2*(order+i)+1]) for i in range(order)]
        return hr, hi

    def residuals(x):
        hr, hi = unpack(x)
        return cascade_phase(hi, fs, freqs) - cascade_phase(hr, fs, freqs) - TARGET

    lo = np.array([10.0, 0.01] * (2 * order))
    ub = np.array([fs/2 - 1.0, 5.0] * (2 * order))
    res = least_squares(residuals, x0, bounds=(lo, ub), xtol=1e-13, ftol=1e-13)
    hr, hi = unpack(res.x)
    result = {"order": order,
              "H_R": [[round(f, 6), round(q, 6)] for f, q in hr],
              "H_I": [[round(f, 6), round(q, 6)] for f, q in hi]}

    err = phase_error_deg(phase(result, fs, band_freqs()))
    if err > 10.0:
        raise RuntimeError(
            f"rbj design did not converge at order={order}, fs={fs}: {err:.1f} deg"
        )
    return result


def phase(coeffs, fs, freqs):
    """Return phase(H_I) - phase(H_R) in radians at *freqs*."""
    hr = [tuple(s) for s in coeffs["H_R"]]
    hi = [tuple(s) for s in coeffs["H_I"]]
    return cascade_phase(hi, fs, freqs) - cascade_phase(hr, fs, freqs)


def cost(coeffs):
    """Multiply count per sample: 4 multiplies per direct-form-II all-pass biquad."""
    # 4 multiplies per direct-form-II all-pass biquad
    return (len(coeffs["H_R"]) + len(coeffs["H_I"])) * 4
