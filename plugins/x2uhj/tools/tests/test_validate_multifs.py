import numpy as np
from validate_multifs import fixed_set_errors, per_rate_errors, RATES

def test_tables_have_one_entry_per_rate():
    assert set(fixed_set_errors().keys()) == set(RATES)
    assert set(per_rate_errors().keys()) == set(RATES)

def test_per_rate_table_stays_flat():
    """The per-rate design holds the quadrature at every rate."""
    for fs, err in per_rate_errors().items():
        assert np.isfinite(err) and err < 2.5

def test_fixed_set_drifts():
    """A fixed coefficient set drifts away from its design rate at every other rate."""
    errs = fixed_set_errors()
    assert errs[48000.0] < 2.0   # accurate at its design rate
    off_design = [errs[fs] for fs in RATES if fs != 48000.0]
    assert all(v > 5.0 for v in off_design)   # every off-design rate drifts
