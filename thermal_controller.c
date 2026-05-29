/*
 * thermal_controller.c — Entropy-aware embedded control loop.
 *
 * ORIGIN:
 *   Built while writing riemann_explorer.c. The Riemann-Siegel scanner
 *   uses three structural principles that map onto sensor fusion:
 *
 *   Riemann-Siegel                     This file
 *   ─────────────────────────────────────────────────────────────────
 *   N = floor(sqrt(t/2π)) terms      adaptive_window_size(derivative)
 *   stop at theta_error_bound(t)      stop at noise_floor_bound(N, sigma)
 *   further iteration adds entropy    entropy_cost(wasted_iters)
 *
 *   The mathematics is NOT equivalent. The design philosophy is analogous:
 *   iterate until you hit the precision floor, then stop.
 *   Past the floor, you generate heat without gaining information.
 *
 * WHAT THIS IS:
 *   Adaptive moving-average controller with noise-floor-aware early
 *   termination. Math basis: CLT (error ~ sigma/sqrt(N)) + Landauer proxy.
 *
 * WHAT THIS IS NOT:
 *   A derivation from number theory. The Riemann connection is structural
 *   inspiration, not a mathematical proof of optimality.
 *
 * Build:  gcc -O2 -o thermal_controller thermal_controller.c -lm
 * Run:    ./thermal_controller [scenario]
 *         scenario: 0=step  1=ramp  2=slow_sinusoid  (default: all three)
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * Author: Seunghyeon Lee <hyeon3125@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ── Physical constants ──────────────────────────────────────────── */
#define K_LANDAUER_J   2.87e-21   /* kT·ln2 @ 300K, joules            */
#define BITS_PER_ITER  64.0       /* bits flipped per double-prec iter */

/* ── Controller parameters ───────────────────────────────────────── */
#define SAMPLE_RATE_HZ 1000.0
#define DT             (1.0 / SAMPLE_RATE_HZ)
#define SIGMA_NOISE    0.005      /* sensor RMS noise, control units   */
#define N_MIN          4
#define N_MAX          128        /* hardware window limit             */
#define SIM_STEPS      500

/* ──────────────────────────────────────────────────────────────────
 * adaptive_window_size()
 *
 * Structural analogy: N = floor(sqrt(t/2π)) in Riemann-Siegel.
 *
 * In RS:   as t grows, more terms are needed to maintain quality.
 * Here:    fast-changing signal → small window (can't average over
 *          a moving target). Slow signal → large window (exploit
 *          stationarity for better SNR).
 *
 *   N = clamp( floor(1 / (|dx/dt| * DT)), N_MIN, N_MAX )
 *
 * Engineering heuristic — not a theorem.
 * ────────────────────────────────────────────────────────────────── */
static int adaptive_window_size(double signal_rate)
{
    double rate = fabs(signal_rate);
    int N = (rate < 1e-6) ? N_MAX : (int)(1.0 / (rate * DT));
    if (N < N_MIN) N = N_MIN;
    if (N > N_MAX) N = N_MAX;
    return N;
}

/* ──────────────────────────────────────────────────────────────────
 * noise_floor_bound()
 *
 * Structural analogy: theta_error_bound(t) in Riemann-Siegel.
 *
 * In RS:   below the Stirling floor, bisection cannot improve the
 *          result — the approximation error dominates.
 * Here:    CLT averaging floor: sigma / sqrt(N).
 *          Below this, adding more samples doesn't reduce error.
 *
 * This IS a real statistical result. No Riemann involvement.
 * ────────────────────────────────────────────────────────────────── */
static double noise_floor_bound(int N, double sigma)
{
    return sigma / sqrt((double)N);
}

/* ──────────────────────────────────────────────────────────────────
 * entropy_cost()
 *
 * Structural analogy: Stage 3 entropy accounting in riemann_explorer.
 *
 * In RS:   each zero beyond the non-refutation threshold costs
 *          ~64 bits * kT*ln2 with zero new information.
 * Here:    each iteration beyond the noise floor costs the same
 *          with no precision gain.
 *
 * Landauer heuristic — not a formal derivation.
 * ────────────────────────────────────────────────────────────────── */
static double entropy_cost(int wasted_iters)
{
    return (double)wasted_iters * BITS_PER_ITER * K_LANDAUER_J;
}

/* ── Minimal LCG for portable noise generation ───────────────────── */
static double lcg_noise(uint32_t *s)
{
    *s = *s * 1664525u + 1013904223u;
    return ((double)(int32_t)*s) / (double)0x80000000u;  /* ∈ (-1, 1) */
}

/* ── Signal generators ───────────────────────────────────────────── */
static double signal_step(int step)
{
    double t = step * DT;
    return (t < 0.2) ? 0.0 : 1.0 * (1.0 - exp(-(t - 0.2) / 0.05));
}

static double signal_ramp(int step)
{
    double t = step * DT;
    return (t < 0.5) ? t * 2.0 : 1.0;
}

static double signal_slow(int step)
{
    double t = step * DT;
    return 0.5 + 0.4 * sin(2.0 * M_PI * 3.0 * t);
}

/* ── Circular sample buffer ──────────────────────────────────────── */
typedef struct {
    double buf[N_MAX];
    int    head;
    int    count;
} RingBuf;

static void ring_push(RingBuf *r, double v)
{
    r->buf[r->head % N_MAX] = v;
    r->head++;
    if (r->count < N_MAX) r->count++;
}

static double ring_get(const RingBuf *r, int lag)
{
    return r->buf[((r->head - 1 - lag) % N_MAX + N_MAX) % N_MAX];
}

/* ──────────────────────────────────────────────────────────────────
 * control_step() — one control cycle.
 *
 *  1. Estimate signal derivative from last two samples.
 *  2. Choose window N via adaptive_window_size().
 *  3. Compute noise floor = sigma / sqrt(N).
 *  4. Grow moving average from window=2 to N; stop when
 *     improvement < noise floor (mirrors bisection stopping at
 *     theta_error_bound in riemann_explorer).
 *  5. Account for entropy saved vs naive N_MAX iterations.
 * ────────────────────────────────────────────────────────────────── */
typedef struct {
    double estimate;
    int    iters_used;
    double entropy_saved_J;
} CtrlResult;

static CtrlResult control_step(const RingBuf *ring)
{
    CtrlResult res = {0.0, 0, 0.0};

    if (ring->count < 2) {
        res.estimate = ring_get(ring, 0);
        return res;
    }

    /* 1. Derivative */
    double signal_rate = fabs(ring_get(ring, 0) - ring_get(ring, 1)) / DT;

    /* 2. Adaptive window */
    int N = adaptive_window_size(signal_rate);
    if (N > ring->count) N = ring->count;

    /* 3. Noise floor */
    double floor_eps = noise_floor_bound(N, SIGMA_NOISE);

    /* 4. Iterative moving average with early termination */
    double prev_est = ring_get(ring, 0);
    double cur_est  = prev_est;
    int    iters    = 0;

    for (int w = 2; w <= N; w++) {
        double sum = 0.0;
        for (int j = 0; j < w; j++)
            sum += ring_get(ring, j);
        cur_est = sum / (double)w;
        iters++;

        /* Below noise floor: further averaging adds heat, not precision */
        if (fabs(cur_est - prev_est) < floor_eps)
            break;
        prev_est = cur_est;
    }

    /* 5. Entropy accounting */
    int wasted = N - iters;
    if (wasted < 0) wasted = 0;

    res.estimate        = cur_est;
    res.iters_used      = iters;
    res.entropy_saved_J = entropy_cost(wasted);
    return res;
}

/* ── Scenario runner ─────────────────────────────────────────────── */
typedef double (*signal_fn)(int);

static void run_scenario(const char *name, signal_fn sig)
{
    RingBuf  ring  = {{0}, 0, 0};
    uint32_t rng   = 0xDEADBEEFu;

    double total_entropy_saved = 0.0;
    double total_error         = 0.0;
    long   total_iters         = 0;

    printf("\n── Scenario: %-40s ──\n", name);
    printf("  %-6s  %-10s  %-10s  %-8s  %-14s  error\n",
           "step", "true", "estimate", "iters", "entropy_saved_J");

    for (int s = 0; s < SIM_STEPS; s++) {
        double true_val  = sig(s);
        double noisy     = true_val + SIGMA_NOISE * lcg_noise(&rng);
        ring_push(&ring, noisy);

        CtrlResult r = control_step(&ring);

        double err = fabs(r.estimate - true_val);
        total_entropy_saved += r.entropy_saved_J;
        total_error         += err;
        total_iters         += r.iters_used;

        if (s % 50 == 0)
            printf("  %-6d  %-10.4f  %-10.4f  %-8d  %-14.3e  %.5f\n",
                   s, true_val, r.estimate, r.iters_used,
                   r.entropy_saved_J, err);
    }

    double naive_iters = (double)SIM_STEPS * N_MAX;
    printf("\n  Mean abs error  : %.5f  (noise sigma=%.4f)\n",
           total_error / SIM_STEPS, SIGMA_NOISE);
    printf("  Mean iters/step : %.1f  (N_MAX=%d)\n",
           (double)total_iters / SIM_STEPS, N_MAX);
    printf("  Iter reduction  : %.1f%%\n",
           100.0 * (1.0 - (double)total_iters / naive_iters));
    printf("  Entropy saved   : %.3e J total  (%.3e J/iter saved)\n",
           total_entropy_saved, K_LANDAUER_J * BITS_PER_ITER);
}

int main(int argc, char *argv[])
{
    printf("================================================================\n");
    printf("  Entropy-Aware Embedded Control Loop\n");
    printf("  Structural origin: riemann_explorer.c\n");
    printf("  Math basis: CLT (sigma/sqrt(N)) + Landauer proxy\n");
    printf("  Note: Riemann link = design inspiration, not derivation\n");
    printf("────────────────────────────────────────────────────────────────\n");
    printf("  sigma=%.4f  DT=%.4fs  N=[%d,%d]  steps=%d\n",
           SIGMA_NOISE, DT, N_MIN, N_MAX, SIM_STEPS);
    printf("================================================================\n");

    int scenario = (argc > 1) ? atoi(argv[1]) : -1;

    if (scenario < 0 || scenario == 0) run_scenario("Step response (fast transient)", signal_step);
    if (scenario < 0 || scenario == 1) run_scenario("Ramp (linearly increasing)",     signal_ramp);
    if (scenario < 0 || scenario == 2) run_scenario("Slow sinusoid (3 Hz)",           signal_slow);

    printf("\n================================================================\n");
    printf("  Principle demonstrated:\n");
    printf("  Stopping at noise floor (sigma/sqrt(N)) wastes no precision\n");
    printf("  and saves entropy vs iterating to N_MAX unconditionally.\n");
    printf("================================================================\n");
    return 0;
}
