"""
1D Kalman filter for latent price recovery from noisy mid-quote observations.

State-space model:
    x_t = x_{t-1} + w_t,   w_t ~ N(0, Q)    (random walk in true price)
    y_t = x_t + v_t,       v_t ~ N(0, R)    (observation = state + bid-ask bounce)

The filter alternates predict (propagate state forward through dynamics) and
update (incorporate the new observation via Bayes' rule + Gaussian conjugacy).

Functions:
    predict(x_plus_prev, P_plus_prev, Q)
        Propagate the previous posterior forward by one timestep.
        Returns the prior mean and variance at time t.

    update(x_minus, P_minus, y, R)
        Combine prior with new observation y_t to get the posterior.
        Returns the posterior mean and variance at time t.

    run_filter(observations, x0, P0, Q, R)
        Run predict+update over an entire sequence of observations.
        Returns arrays of filtered estimates, posterior variances, and gains.
"""

import numpy as np


def predict(x_plus_prev, P_plus_prev, Q):
    """
    Predict step.

    Parameters
    ----------
    x_plus_prev : float
        Previous posterior mean estimate, x_{t-1}^+.
    P_plus_prev : float
        Previous posterior variance, P_{t-1}^+.
    Q : float
        Process noise variance.

    Returns
    -------
    x_minus : float
        Predicted (prior) mean at time t, x_t^-.
    P_minus : float
        Predicted (prior) variance at time t, P_t^-.
    """
    x_minus = x_plus_prev  # Random walk: mean doesn't change
    P_minus = P_plus_prev + Q  # Variance increases by process noise
    return x_minus, P_minus


def update(x_minus, P_minus, y, R):
    """
    Update step.

    Parameters
    ----------
    x_minus : float
        Prior mean, x_t^-.
    P_minus : float
        Prior variance, P_t^-.
    y : float
        New observation, y_t.
    R : float
        Observation noise variance.

    Returns
    -------
    x_plus : float
        Posterior mean, x_t^+.
    P_plus : float
        Posterior variance, P_t^+.
    """
    K = P_minus / (P_minus + R)  # Kalman gain
    x_plus = x_minus + K * (y - x_minus)  # Update mean with observation
    P_plus = (1 - K) * P_minus  # Update variance
    return x_plus, P_plus


def run_filter(observations, x0, P0, Q, R):
    """
    Run the Kalman filter over an entire sequence of observations.

    Parameters
    ----------
    observations : array-like, shape (n,)
        Sequence of observations y_1, y_2, ..., y_n.
    x0 : float
        Initial state estimate, x_0^+. Often set to the first observation.
    P0 : float
        Initial state variance, P_0^+. Set large if uncertain about x_0.
    Q : float
        Process noise variance.
    R : float
        Observation noise variance.

    Returns
    -------
    x_hat : ndarray, shape (n,)
        Filtered state estimates (posterior means) at each timestep.
    P : ndarray, shape (n,)
        Posterior variances at each timestep.
    K : ndarray, shape (n,)
        Kalman gains at each timestep.
    """
    observations = np.asarray(observations, dtype=float)
    n = len(observations)

    x_hat = np.zeros(n)
    P = np.zeros(n)
    K = np.zeros(n)

    x_plus = x0
    P_plus = P0

    for t in range(n):
        # Predict
        x_minus, P_minus = predict(x_plus, P_plus, Q)

        # Kalman gain (logged for analysis; update() recomputes it internally)
        K[t] = P_minus / (P_minus + R)

        # Update
        x_plus, P_plus = update(x_minus, P_minus, observations[t], R)

        # Record posterior
        x_hat[t] = x_plus
        P[t] = P_plus

    return x_hat, P, K