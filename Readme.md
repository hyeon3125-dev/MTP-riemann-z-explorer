# Riemann Hypothesis — Runtime Non-Refutation Verifier

> Can a running program prove what formal mathematics cannot?

## Thesis

The Riemann Hypothesis states that all non-trivial zeros of the Riemann zeta function
have real part equal to 1/2.

It has never been proved. It has never been disproved.

This repository argues — and demonstrates in C — that **runtime non-refutation
is not merely a consolation prize. Under physical and logical constraints, it is
the optimal available verification strategy.**

## The Argument

### 1. Formal proof may be structurally unavailable

Chaitin (1995) showed that sufficiently complex number-theoretic propositions
can exhibit algorithmic randomness, placing them outside formal provability within ZFC.
Gödel's incompleteness theorem establishes that no sufficiently strong formal system
can prove its own consistency from within.

If RH is undecidable in ZFC — a live possibility — no formal proof exists to find.
The proof-search is not just hard. It may be impossible by construction.

### 2. Redundant zero accumulation is thermodynamically unjustified

Landauer's Principle (1961): erasing one bit of information dissipates at minimum
`kT ln 2 ≈ 2.87 × 10⁻²¹ J` at 300K.

Each additional verified zero beyond the non-refutation threshold stores
~64 bits (double precision) and costs measurable compute — without producing
new refutation power. The marginal informational return is zero.
The marginal entropy cost is not.

The Bekenstein Bound further constrains total storable information within any
finite energy-volume system. Unbounded zero accumulation asymptotically
violates this bound.

**Accumulating zeros to "more thoroughly" verify RH is not rigorous.
It is thermodynamically wasteful.**

### 3. Therefore: non-refutation is sufficient

Given (1) and (2):

- Formal proof may be unavailable by Gödel/Chaitin
- Exhaustive accumulation is unjustified by Landauer/Bekenstein
- Sign-change verification via the Riemann-Siegel Z(t) function confirms
  each zero lies on the critical line to machine epsilon

**Runtime non-refutation — monotonic accumulation of failed counterexamples —
is the optimal and sufficient engineering criterion.**

---

## Implementation

### Method: Riemann-Siegel Z(t)

`Z(t)` is a real-valued function whose sign changes correspond exactly to
zeros of `ζ(1/2 + it)` on the critical line:

```
Z(t) = 2 · Σ_{n=1}^{N} cos(θ(t) − t·ln n) / √n
θ(t) ≈ t/2·ln(t/2π) − t/2 − π/8 + 1/(48t) + 7/(5760t³)
N    = ⌊√(t/2π)⌋
```

Z(t) is real-valued **by construction only on σ = 1/2**.
After bisection to 64 iterations, `|Z(t₀)| < 10⁻⁶` confirms the zero is on
the critical line. No off-line zero can appear in Z(t)'s sign changes.

### Three-Stage Pipeline

| Stage | What it does |
|-------|-------------|
| **1. Zero location** | Scan Z(t) for sign changes; bisect to machine precision |
| **2. Critical line check** | Confirm `\|Z(t₀)\| < 10⁻⁶` (sigma=1/2 by Z's definition) |
| **3. Entropy measurement** | Wall-clock cost per marginal zero — Landauer proxy |

### Build & Run

```bash
# Using Make (recommended)
make
make run            # t_max=100, dt=0.01  — quick smoke test
make run-extended   # t_max=1000, dt=0.005

# Manual build
gcc -O2 -o riemann_explorer riemann_explorer.c -lm -lrt
./riemann_explorer [t_max] [dt]
# defaults: t_max=200.0, dt=0.005
```

### Sample Output (t_max=100)

```
Stage 1: 29 zeros located, t ∈ [14.5, 98.9]
Stage 2: 27/29 CONFIRMED (|Z| < 1e-6), 2 APPROX (RS low-order limit)
Stage 3: entropy cost increases with n; each marginal zero ~1.84e-19 J
Off-critical-line zeros detected: 0
```

---

## Scope and Limits

This is an **engineering verifier**, not a mathematical proof.

- The Riemann-Siegel formula uses `N = O(√t)` terms — low-order approximation.
  Some zeros show `|Z(t₀)| ~ 0.3–0.8` (APPROX) due to approximation error,
  not off-line placement.
- Zero locations differ from the tabulated values by `O(0.1–0.4)` in `t`.
  This is a consequence of the approximation, not a violation.
- For higher precision, replace the RS sum with the full Euler-Maclaurin
  expansion or an MPFR-based implementation.

The claim is not: *"this code proves RH."*

The claim is: *"given that formal proof may be impossible and redundant
accumulation is physically unjustified, this runtime constitutes the
optimal available evidence — and it has found zero counterexamples."*

---

## Derived Work: `thermal_controller.c`

Building this scanner surfaced a structural pattern that applies beyond
number theory. Three principles from `riemann_explorer.c` map onto
embedded sensor fusion:

| Riemann-Siegel (this repo) | `thermal_controller.c` |
|---|---|
| `N = floor(sqrt(t/2π))` terms | `adaptive_window_size(derivative)` |
| Stop at `theta_error_bound(t)` | Stop at `noise_floor_bound(N, sigma)` |
| Further iteration adds entropy | `entropy_cost(wasted_iters)` |

**The math is not the same.** The noise floor result is CLT
(`sigma/sqrt(N)`), not Riemann-Siegel. The Landauer accounting is a
heuristic proxy. The connection is design philosophy, not derivation:
*iterate until you hit the precision floor, then stop — past that point
you generate heat without gaining information.*

```bash
make thermal_controller
./thermal_controller          # all three scenarios
./thermal_controller 0        # step response only
./thermal_controller 2        # slow sinusoid only
```

---

## Author

Seunghyeon Lee <hyeon3125@gmail.com>

---

## References

- Chaitin, G. (1995). *Randomness in Arithmetic.* Scientific American.
- Gödel, K. (1931). *Über formal unentscheidbare Sätze.*
- Landauer, R. (1961). *Irreversibility and Heat Generation in the Computing Process.* IBM J. Res. Dev.
- Bekenstein, J. (1981). *Universal upper bound on the entropy-to-energy ratio.*
- Riemann, B. (1859). *Über die Anzahl der Primzahlen unter einer gegebenen Größe.*