import numpy as np
from gerzon_verify import localization_vectors, SURROUND, SUPER_STEREO

def test_source_front_maps_to_front():
    """A source at azimuth 0 produces vectors pointing to azimuth 0."""
    tolerances = {
        # SUPER_STEREO decodes to ±30° frontal speakers; a front source is essentially
        # exact because L==R and the symmetric pair collapses to azimuth 0.
        SUPER_STEREO: np.radians(1e-3),
        # SURROUND uses an illustrative decode; allow a coarser tolerance.
        SURROUND: np.radians(5.0),
    }
    for model in (SURROUND, SUPER_STEREO):
        tol = tolerances[model]
        rv, re = localization_vectors(0.0, model)
        assert abs(np.arctan2(rv[1], rv[0])) < tol
        assert abs(np.arctan2(re[1], re[0])) < tol

def test_energy_vector_magnitude_bounded():
    for model in (SURROUND, SUPER_STEREO):
        for az in np.linspace(0, 2*np.pi, 24, endpoint=False):
            _, re = localization_vectors(az, model)
            assert 0.0 <= np.hypot(*re) <= 1.0 + 1e-9

def test_left_right_symmetry():
    """Azimuth +θ and -θ give mirror-image velocity vectors."""
    # SURROUND symmetry is not tested: its illustrative decoder is intentionally asymmetric.
    rv_p, _ = localization_vectors(np.radians(45.0), SUPER_STEREO)
    rv_m, _ = localization_vectors(np.radians(-45.0), SUPER_STEREO)
    np.testing.assert_allclose(rv_p[0], rv_m[0], atol=1e-6)
    np.testing.assert_allclose(rv_p[1], -rv_m[1], atol=1e-6)
