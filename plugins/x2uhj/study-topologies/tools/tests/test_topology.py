import numpy as np
from topology import STD_RATES, F_LO, F_HI, TARGET, phase_error_deg

def test_constants():
    assert STD_RATES[0] == 44100.0 and STD_RATES[-1] == 192000.0
    assert (F_LO, F_HI) == (20.0, 20000.0)
    assert abs(TARGET + np.pi/2) < 1e-12

def test_phase_error_deg_zero_when_on_target():
    freqs = np.geomspace(F_LO, F_HI, 64)
    diff = np.full_like(freqs, TARGET)
    assert phase_error_deg(diff) < 1e-9

def test_phase_error_deg_reports_max_deviation():
    freqs = np.geomspace(F_LO, F_HI, 64)
    diff = np.full_like(freqs, TARGET)
    diff[10] = TARGET + np.radians(3.0)
    assert abs(phase_error_deg(diff) - 3.0) < 1e-6
