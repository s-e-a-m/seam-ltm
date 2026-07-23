//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · Common · seam_airabsorption — atmospheric absorption (ISO 9613-1)
//
// Atmospheric absorption coefficient alpha(f, T, RH, p) in dB/m, and the
// minimum-phase filters that render alpha*d over distance. The magnitude
// target is physics; the phase is the minimum phase that physics implies.
//
// ISO REFERENCE: ISO 9613-1:1993 "Attenuation of sound during propagation
// outdoors — Part 1: Calculation of the absorption of sound by the
// atmosphere." Constants below are TRANSCRIBED and MUST be verified
// constant-by-constant against the standard (Bass, Sutherland, Zuckerwar
// behind it). See doc/math/ (documentation debt).
//
// FAUST REFERENCE (seam.filters.lib): the air-absorption function (roadmap).
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include <cmath>

namespace Seam { namespace air {

// ISO 9613-1 atmospheric absorption coefficient, dB per metre.
//   fHz       : frequency (Hz)
//   tempC     : temperature (deg C)
//   rhPercent : relative humidity (%)
//   paKPa     : atmospheric pressure (kPa), default 1 atm
inline double alphaISO9613(double fHz, double tempC, double rhPercent,
                           double paKPa = 101.325) {
    const double pr = 101.325;                 // reference pressure, kPa
    const double T  = tempC + 273.15;          // temperature, K
    const double T0 = 293.15;                   // reference temperature (20 C), K
    const double T01 = 273.16;                  // triple-point isotherm, K
    const double f2 = fHz * fHz;
    const double pRatio = paKPa / pr;
    const double tRatio = T / T0;

    // Saturation vapour pressure ratio psat/pr (ISO 9613-1 Annex B).
    const double C = -6.8346 * std::pow(T01 / T, 1.261) + 4.6151;
    const double psatRatio = std::pow(10.0, C);

    // Molar concentration of water vapour, % (h).
    const double h = rhPercent * psatRatio / pRatio;

    // Relaxation frequencies (oxygen, nitrogen).
    const double frO = pRatio * (24.0 + 4.04e4 * h * (0.02 + h) / (0.391 + h));
    const double frN = pRatio * std::pow(tRatio, -0.5)
                     * (9.0 + 280.0 * h * std::exp(-4.170 * (std::pow(tRatio, -1.0/3.0) - 1.0)));

    // Absorption coefficient, nepers-based term times 8.686 -> dB/m.
    const double classical = 1.84e-11 * (1.0 / pRatio) * std::sqrt(tRatio);
    const double oxygen = 0.01275 * std::exp(-2239.1 / T) / (frO + f2 / frO);
    const double nitro  = 0.1068  * std::exp(-3352.0 / T) / (frN + f2 / frN);
    const double relax = std::pow(tRatio, -2.5) * (oxygen + nitro);

    return 8.686 * f2 * (classical + relax);   // dB/m
}

}} // namespace Seam::air
