import numpy as np
from validate_multifs import phase_error_table, RATES

def test_table_has_one_row_per_rate():
    table = phase_error_table()
    assert set(table.keys()) == set(RATES)

def test_errors_finite_and_bounded():
    table = phase_error_table()
    for fs, err in table.items():
        assert np.isfinite(err)
        assert 0.0 <= err < 5.0   # degrees; generous bound
