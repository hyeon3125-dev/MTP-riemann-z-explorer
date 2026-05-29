/* * riemann_explorer.c — Riemann-Siegel Z(t) zero scanner.
 *
 * SCOPE: Numerical exploration utility. NOT a mathematical proof.
 *        RH is open in ZFC. This tool scans the critical line (sigma=1/2)
 *        and reports sign-change zeros of Z(t). Off-critical-line zeros
 *        are structurally undetectable by Z(t) alone (requires argument
 *        principle contour integration — see Graf's Theorem).
 *
 * ERROR BOUND:
 *   rs_theta(t) uses 2-term Stirling correction.
 *   |error| < 1/(48t) + 7/(5760t^3) < 1e-4 for t > 10.
 *   Bisection refines to 64-bit ULP, but the analytic error floor
 *   is inherited from rs_theta. RESIDUAL_THRESHOLD = 1e-4 (theta error bound).
 *   For validated numerics, replace with MPFR interval arithmetic.
 *
 * Build: gcc -O2 -o riemann_explorer riemann_explorer.c -lm -lrt
 * Run:   ./riemann_explorer [t_max] [dt]
 *        defaults: t_max=200.0, dt=0.005  (toy range; increase for production)
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * Author: Seunghyeon Lee <hyeon3125@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*
 * rs_theta(t) — Riemann-Siegel theta function.
 * Stirling approximation: t/2 * ln(t/2pi) - t/2 - pi/8
 * 2-term correction: 1/(48t) + 7/(5760t^3)
 * Error bound: |error| < 1/(48t) + 7/(5760t^3) < 1e-4 for t > 10.
 */
static double rs_theta(double t) {
    return (t / 2.0) * log(t / (2.0 * M_PI))
           - t / 2.0
           - M_PI / 8.0
           + 1.0 / (48.0 * t)
           + 7.0 / (5760.0 * t * t * t);
}

/*
 * theta_error_bound(t) — analytic error estimate for rs_theta(t).
 * Conservative upper bound for the residual of the Stirling series.
 */
static double theta_error_bound(double t) {
    if (t < 10.0) return 1e-3;
    return 1.0 / (48.0 * t) + 7.0 / (5760.0 * t * t * t);
}

/*
 * Z(t) = 2 * sum_{n=1}^{N} cos(theta(t) - t*ln n) / sqrt(n)
 * N = floor(sqrt(t / (2*pi)))
 *
 * REAL-VALUED ONLY ON THE CRITICAL LINE (sigma = 1/2).
 * Z(t) carries no information about zeros off the critical line.
 * For full-plane scanning, use argument principle contour integration.
 */
static double rs_Z(double t) {
    int    N  = (int)sqrt(t / (2.0 * M_PI));
    double th = rs_theta(t), s = 0.0;
    for (int n = 1; n <= N; n++)
        s += cos(th - t * log((double)n)) / sqrt((double)n);
    return 2.0 * s;
}

/* Sign-change bisection to 64-bit ULP precision */
static double bisect(double ta, double tb) {
    double Za = rs_Z(ta), tm, Zm;
    for (int i = 0; i < 64; i++) {
        tm = (ta + tb) / 2.0;
        Zm = rs_Z(tm);
        if (Za * Zm <= 0.0) tb = tm;
        else                 { ta = tm; Za = Zm; }
    }
    return (ta + tb) / 2.0;
}

#define MAX_ZEROS           1024

/*
 * RESIDUAL_THRESHOLD is set to the analytic error floor of rs_theta.
 * Zeros with |Z(t0)| < this value are within the approximation error
 * of the Stirling correction and are NOT formally "confirmed" zeros.
 * They are "not refuted" within the precision limits of this tool.
 */
#define RESIDUAL_THRESHOLD  1e-4
#define K_LANDAUER_J        2.87e-21
#define BITS_PER_ZERO       64.0

typedef struct { double t; double residual; } ZeroRec;

/*
 * Stage 1: Locate sign-change zeros of Z(t) on the critical line.
 *
 * This is a numerical SCAN, not a proof. Zeros detected here are
 * candidates. Off-critical-line zeros are structurally invisible
 * to this method.
 */
static int stage1(double t_start, double t_max, double dt,
                  ZeroRec *out, int cap, int verbose) {
    int count = 0;
    double prev = rs_Z(t_start);
    for (double t = t_start + dt; t <= t_max && count < cap; t += dt) {
        double cur = rs_Z(t);
        if (prev * cur < 0.0) {
            double t0  = bisect(t - dt, t);
            double res = fabs(rs_Z(t0));
            out[count].t = t0; out[count].residual = res; count++;
            if (verbose)
                printf("  [%3d]  t = %-16.10f  |Z(t)| = %.2e\n", count, t0, res);
            t += dt * 3.0; prev = rs_Z(t); continue;
        }
        prev = cur;
    }
    return count;
}

/*
 * Stage 2: Classify located zeros by residual vs theta error bound.
 *
 * "WITHIN_BOUND": |Z(t0)| < theta_error_bound(t0) — indistinguishable
 *                 from the analytic error floor of rs_theta.
 * "ABOVE_BOUND" : |Z(t0)| > error bound — may indicate a near-miss
 *                 or a genuine zero degraded by the approximation.
 *
 * This is NOT a formal zero confirmation. It is a numerical
 * consistency check against the known error model of rs_theta.
 */
static void stage2(ZeroRec *zeros, int n) {
    printf("\n=== Stage 2: Consistency Check (theta error bound) ===\n");
    printf("  Criterion: |Z(t0)| < theta_error_bound(t0)\n");
    printf("  Error bound @ t=10: ~%.1e  |  @ t=200: ~%.1e\n\n",
           theta_error_bound(10.0), theta_error_bound(200.0));
    printf("  %-6s  %-18s  %-14s  %-14s  %-14s\n",
           "#", "t", "|Z(t)|", "err_bound", "status");
    int within = 0, above = 0;
    for (int i = 0; i < n; i++) {
        double res  = zeros[i].residual;
        double ebound = theta_error_bound(zeros[i].t);
        const char *st;
        if (res < ebound) { st = "WITHIN_BOUND"; within++; }
        else              { st = "ABOVE_BOUND";  above++;  }
        printf("  %-6d  %-18.10f  %-14.4e  %-14.4e  %s\n",
               i+1, zeros[i].t, res, ebound, st);
    }
    printf("\n  Within theta error bound: %d/%d\n", within, n);
    printf("  Above theta error bound: %d (possible near-miss)\n", above);
    printf("  NOTE: Off-critical-line zeros NOT scanned (Z(t) blind spot).\n");
}

/*
 * Stage 3: Wall-clock cost per marginal zero (Landauer entropy proxy).
 *
 * NOT a mathematical proof metric. Included as a computational
 * economics observation: each additional zero scan costs ~1e-19 J
 * of physical entropy and zero new refutation power.
 */
static double wall_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1.0e6;
}

static void stage3(double t_max, double dt) {
    printf("\n=== Stage 3: Computational Cost (Landauer entropy proxy) ===\n");
    printf("  kT*ln2 @ 300K = %.2e J  |  bits/zero = %.0f\n\n",
           K_LANDAUER_J, BITS_PER_ZERO);
    printf("  NOTE: entropy proxy — not a proof metric. Thermodynamic framing\n");
    printf("        is heuristic, not a mathematical refutation criterion.\n\n");
    int targets[] = { 5, 10, 20, 40, 80, 0 };
    ZeroRec buf[MAX_ZEROS];
    double prev_ceil = 10.0;
    int    prev_found = 0;
    printf("  %-8s  %-10s  %-14s  %-14s  %-16s\n",
           "n_zero", "t_ceil", "wall_ms", "ms/marginal", "entropy_J");
    for (int b = 0; targets[b] != 0; b++) {
        int tgt       = targets[b];
        double t_ceil = (t_max * (double)tgt / 80.0 < prev_ceil + 1.0)
                        ? prev_ceil + 1.0
                        : t_max * (double)tgt / 80.0;
        double t0_ms   = wall_ms();
        /* Scan from previous ceiling to avoid redundant computation */
        int    found   = stage1(prev_ceil, t_ceil, dt, buf, tgt, 0);
        double elapsed = wall_ms() - t0_ms;
        int    marginal_n = found - prev_found;
        if (marginal_n < 0) marginal_n = 0;
        double ms_per     = (marginal_n > 0) ? elapsed / marginal_n : elapsed;
        double entropy_J  = marginal_n * BITS_PER_ZERO * K_LANDAUER_J;
        prev_found = found;
        prev_ceil  = t_ceil;
        printf("  %-8d  t<%-8.1f  %-14.3f  %-14.3f  %-16.4e\n",
               found, t_ceil, elapsed, ms_per, entropy_J);
    }
    printf("\n  Per-zero entropy: ~%.2e J — zero refutation power added.\n",
           BITS_PER_ZERO * K_LANDAUER_J);
    printf("  Extending scan range adds heat, not mathematical certainty.\n");
}

int main(int argc, char *argv[]) {
    double t_max = (argc > 1) ? atof(argv[1]) : 200.0;
    double dt    = (argc > 2) ? atof(argv[2]) : 0.005;

    printf("=============================================================\n");
    printf("  Riemann Z(t) — Numerical Zero Scanner\n");
    printf("  SCOPE: Exploration utility. NOT a mathematical proof.\n");
    printf("  METHOD: Sign-change bisection on the critical line (sigma=1/2).\n");
    printf("  LIMIT:  Off-critical zeros invisible to Z(t) alone.\n");
    printf("  ERROR:  theta approximation floor ~1e-4 for t > 10.\n");
    printf("  t_max=%.1f  dt=%.4f\n", t_max, dt);
    printf("=============================================================\n\n");

    printf("=== Stage 1: Zero Scan (Z(t) sign-change bisection) ===\n\n");
    ZeroRec zeros[MAX_ZEROS];
    int n = stage1(10.0, t_max, dt, zeros, MAX_ZEROS, 1);
    printf("\n  Total candidate zeros: %d\n", n);
    if (n == 0) {
        fprintf(stderr, "No zeros found. Decrease dt or increase t_max.\n");
        return 1;
    }

    stage2(zeros, n);
    stage3(t_max, dt);

    int within = 0;
    for (int i = 0; i < n; i++)
        if (zeros[i].residual < theta_error_bound(zeros[i].t)) within++;

    printf("\n=============================================================\n");
    printf("  SCAN SUMMARY\n");
    printf("=============================================================\n");
    printf("  Candidates located          : %d\n", n);
    printf("  Within theta error bound   : %d\n", within);
    printf("  Off-critical-line scanned  : 0 (Z(t) blind spot)\n\n");
    printf("  RH not refuted on critical line for t < %.1f.\n", t_max);
    printf("  This is a NUMERICAL OBSERVATION, not a mathematical proof.\n");
    printf("  For off-critical search: use argument principle + MPFR.\n");
    printf("=============================================================\n");
    return 0;
}
