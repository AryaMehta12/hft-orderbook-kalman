"""
Sanity tests for kalman.py. Each test exercises one expected behavior.
Run from python/ directory: `python test_kalman.py`
"""

import numpy as np
from kalman import predict, update, run_filter


def assert_close(actual, expected, tol=1e-9, label=""):
    """Tiny helper since we're not using a test framework."""
    if abs(actual - expected) > tol:
        raise AssertionError(
            f"FAIL [{label}]: got {actual}, expected {expected} (tol={tol})"
        )


# ---------------------------------------------------------------------------
# Test 1: predict basic mechanics
# ---------------------------------------------------------------------------
def test_predict_identity_mean_and_variance_growth():
    x_minus, P_minus = predict(x_plus_prev=10.0, P_plus_prev=2.0, Q=1.0)
    assert_close(x_minus, 10.0, label="predict mean unchanged")
    assert_close(P_minus, 3.0, label="predict variance grows by Q")
    print("  ok  test_predict_identity_mean_and_variance_growth")


# ---------------------------------------------------------------------------
# Test 2: update with K=1 (zero observation noise) snaps to observation
# ---------------------------------------------------------------------------
def test_update_zero_R_snaps_to_observation():
    # R=0 means observations are perfect → posterior should equal observation
    x_plus, P_plus = update(x_minus=5.0, P_minus=10.0, y=7.0, R=0.0)
    assert_close(x_plus, 7.0, label="K=1 snaps mean to y")
    assert_close(P_plus, 0.0, label="K=1 collapses variance to 0")
    print("  ok  test_update_zero_R_snaps_to_observation")


# ---------------------------------------------------------------------------
# Test 3: update with P_minus=0 (perfect prior) ignores observation
# ---------------------------------------------------------------------------
def test_update_zero_prior_variance_ignores_observation():
    # P_minus=0 means we're certain of our prior → posterior = prior
    x_plus, P_plus = update(x_minus=5.0, P_minus=0.0, y=999.0, R=1.0)
    assert_close(x_plus, 5.0, label="K=0 keeps prior mean")
    assert_close(P_plus, 0.0, label="K=0 keeps prior variance (which was 0)")
    print("  ok  test_update_zero_prior_variance_ignores_observation")


# ---------------------------------------------------------------------------
# Test 4: update gain is correct
# ---------------------------------------------------------------------------
def test_update_kalman_gain_value():
    # P_minus=R=1 → K should be 0.5, posterior mean = (prior + obs)/2
    x_plus, P_plus = update(x_minus=10.0, P_minus=1.0, y=12.0, R=1.0)
    assert_close(x_plus, 11.0, label="K=0.5 means equal-weighted average")
    assert_close(P_plus, 0.5, label="P_plus = (1 - 0.5) * 1.0")
    print("  ok  test_update_kalman_gain_value")


# ---------------------------------------------------------------------------
# Test 5: run_filter on a constant sequence converges to the constant
# ---------------------------------------------------------------------------
def test_run_filter_converges_to_constant_signal():
    # Observations are noisy versions of a true constant. The filter should
    # converge close to the constant after enough steps.
    np.random.seed(0)
    true_value = 100.0
    noise_std = 2.0
    n = 1000
    observations = true_value + np.random.randn(n) * noise_std

    x_hat, P, K = run_filter(
        observations=observations,
        x0=0.0,           # deliberately wrong initial guess
        P0=1000.0,        # large initial uncertainty
        Q=0.0001,         # truth barely moves (we know it's constant)
        R=noise_std**2,   # we know the observation noise
    )

    # Final estimate should be within ~0.3 of the true value
    final_error = abs(x_hat[-1] - true_value)
    if final_error > 0.3:
        raise AssertionError(
            f"FAIL [filter converges]: final estimate {x_hat[-1]:.3f} "
            f"too far from true value {true_value} (error {final_error:.3f})"
        )
    print(f"  ok  test_run_filter_converges_to_constant_signal "
          f"(final estimate {x_hat[-1]:.3f}, true {true_value})")


# ---------------------------------------------------------------------------
# Test 6: run_filter on a random walk produces a smoothed estimate
# ---------------------------------------------------------------------------
def test_run_filter_smooths_random_walk():
    # Simulate a true random walk + observation noise, then check the filter
    # produces a lower-variance trajectory than the raw observations.
    np.random.seed(1)
    n = 5000
    Q_true = 1.0
    R_true = 4.0

    # Generate true state and noisy observations
    true_state = np.cumsum(np.random.randn(n) * np.sqrt(Q_true))
    observations = true_state + np.random.randn(n) * np.sqrt(R_true)

    x_hat, P, K = run_filter(
        observations=observations,
        x0=observations[0],
        P0=R_true,
        Q=Q_true,
        R=R_true,
    )

    # MSE of filter vs truth should be much smaller than MSE of raw obs vs truth
    mse_raw = np.mean((observations - true_state) ** 2)
    mse_filter = np.mean((x_hat - true_state) ** 2)

    if mse_filter >= mse_raw:
        raise AssertionError(
            f"FAIL: filter MSE ({mse_filter:.3f}) not better than raw "
            f"observations ({mse_raw:.3f})"
        )

    print(f"  ok  test_run_filter_smooths_random_walk "
          f"(raw MSE {mse_raw:.3f}, filter MSE {mse_filter:.3f}, "
          f"{(1 - mse_filter / mse_raw) * 100:.1f}% reduction)")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
if __name__ == "__main__":
    test_predict_identity_mean_and_variance_growth()
    test_update_zero_R_snaps_to_observation()
    test_update_zero_prior_variance_ignores_observation()
    test_update_kalman_gain_value()
    test_run_filter_converges_to_constant_signal()
    test_run_filter_smooths_random_walk()
    print("\nAll Kalman tests passed.")