#include "CoulombInts.hpp"
#include "Angular/Angular_tables.hpp"
#include "IO/SafeProfiler.hpp"
#include "Maths/Grid.hpp"
#include "Maths/NumCalc_quadIntegrate.hpp"
#include "Wavefunction/DiracSpinor.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <tuple>
#include <vector>

namespace CoulombInts {

//******************************************************************************
// Templates for faster method to calculate r^k
template <int k> static inline double powk_new(const double x) {
  return x * powk_new<k - 1>(x);
}
template <> inline double powk_new<0>(const double) { return 1.0; }

//******************************************************************************
template <int k>
static inline void yk_ijk_impl(const int l, const DiracSpinor &Fa,
                               const DiracSpinor &Fb, std::vector<double> &vabk,
                               const std::size_t maxi)
// Calculalates y^k_ab screening function.
// Note: is symmetric: y_ab = y_ba
//
// Stores in vabk (in/out parameter, reference to whatever)
//
// r_min := min(r,r')
// rho(r') := fa(r')*fb(r') + ga(r')gb(r')
// y^k_ab(r) = Int_0^inf [r_min^k/r_max^(k+1)]*rho(f') dr'
//           = Int_0^r [r'^k/r^(k+1)]*rho(r') dr'
//             + Int_r^inf [r^k/r'^(k+1)]*rho(r') dr'
//          := A(r)/r^(k+1) + B(r)*r^k
// A(r0)  = 0
// B(r0)  = Int_0^inf [r^k/r'^(k+1)]*rho(r') dr'
// A(r_n) = A(r_{n-1}) + (rho(r_{n-1})*r_{n-1}^k)*dr
// B(r_n) = A(r_{n-1}) + (rho(r_{n-1})/r_{n-1}^(k+1))*dr
// y^k_ab(rn) = A(rn)/rn^(k+1) + B(rn)*rn^k
//
// Also uses Quadrature integer rules! (Defined in NumCalc)
{
  const auto &gr = Fa.p_rgrid; // just save typing
  const auto du = gr->du;
  const auto num_points = gr->num_points;
  vabk.resize(num_points); // for safety
  const auto irmax = (maxi == 0 || maxi > num_points) ? num_points : maxi;

  // faster method to calculate r^k
  auto powk = [=](double x) {
    if constexpr (k < 0)
      return std::pow(x, l);
    else
      return powk_new<k>(x);
  };

  // Quadrature integration weights:
  auto w = [=](std::size_t i) {
    if (i < NumCalc::Nquad)
      return NumCalc::dq_inv * NumCalc::cq[i];
    if (i < num_points - NumCalc::Nquad)
      return 1.0;
    return NumCalc::dq_inv * NumCalc::cq[num_points - i - 1];
  };

  double Ax = 0.0, Bx = 0.0;

  const auto bmax = std::min(Fa.pinf, Fb.pinf);
  for (std::size_t i = 0; i < bmax; i++) {
    Bx += w(i) * gr->drduor[i] * (Fa.f[i] * Fb.f[i] + Fa.g[i] * Fb.g[i]) /
          powk(gr->r[i]);
  }

  vabk[0] = Bx * du * powk(gr->r[0]);
  for (std::size_t i = 1; i < irmax; i++) {
    const auto rm1_to_k = powk(gr->r[i - 1]);
    const auto inv_rm1_to_kp1 = 1.0 / (rm1_to_k * gr->r[i - 1]);
    const auto r_to_k = powk(gr->r[i]);
    const auto inv_r_to_kp1 = 1.0 / (r_to_k * gr->r[i]);
    const auto Fdr = gr->drdu[i - 1] *
                     (Fa.f[i - 1] * Fb.f[i - 1] + Fa.g[i - 1] * Fb.g[i - 1]);
    const auto wi = w(i - 1);
    Ax += wi * Fdr * rm1_to_k;
    Bx -= wi * Fdr * inv_rm1_to_kp1;
    vabk[i] = du * (Ax * inv_r_to_kp1 + Bx * r_to_k);
  }
  for (std::size_t i = irmax; i < num_points; i++) {
    vabk[i] = 0.0;
  }
}

//******************************************************************************
std::vector<double> yk_ab(const DiracSpinor &Fa, const DiracSpinor &Fb,
                          const int k, const std::size_t maxi) {
  std::vector<double> ykab; //
  yk_ab(Fa, Fb, k, ykab, maxi);
  return ykab;
}

//------------------------------------------------------------------------------
void yk_ab(const DiracSpinor &Fa, const DiracSpinor &Fb, const int k,
           std::vector<double> &vabk, const std::size_t maxi) {
  auto sp1 = SafeProfiler::profile(__func__);

  if (k == 0)
    yk_ijk_impl<0>(k, Fa, Fb, vabk, maxi);
  else if (k == 1)
    yk_ijk_impl<1>(k, Fa, Fb, vabk, maxi);
  else if (k == 2)
    yk_ijk_impl<2>(k, Fa, Fb, vabk, maxi);
  else if (k == 3)
    yk_ijk_impl<3>(k, Fa, Fb, vabk, maxi);
  else if (k == 4)
    yk_ijk_impl<4>(k, Fa, Fb, vabk, maxi);
  else if (k == 5)
    yk_ijk_impl<5>(k, Fa, Fb, vabk, maxi);
  else if (k == 6)
    yk_ijk_impl<6>(k, Fa, Fb, vabk, maxi);
  else if (k == 7)
    yk_ijk_impl<7>(k, Fa, Fb, vabk, maxi);
  else if (k == 8)
    yk_ijk_impl<8>(k, Fa, Fb, vabk, maxi);
  else
    yk_ijk_impl<-1>(k, Fa, Fb, vabk, maxi);
}

//******************************************************************************
//******************************************************************************
double Zk_abcd(const DiracSpinor &Fa, const DiracSpinor &Fb,
               const DiracSpinor &Fc, const DiracSpinor &Fd, const int k) {
  // Z^k_abcd = s ( Q^k_abcd + sum_l [k] 6j * Q^l_abdc)

  auto s = Angular::evenQ_2(Fa.twoj() + Fb.twoj() + 2) ? 1 : -1;
  auto Qkabcd = Qk_abcd(Fa, Fb, Fc, Fd, k);

  auto tkp1 = 2 * k + 1;

  auto min_twol = std::max(std::abs(Fd.twoj() - Fa.twoj()),
                           std::abs(Fc.twoj() - Fb.twoj()));

  auto max_twol = std::min(Fd.twoj() + Fa.twoj(), Fc.twoj() + Fb.twoj());

  double sum = 0.0;
  for (int tl = min_twol; tl <= max_twol; tl += 2) {
    auto sixj = Angular::sixj_2(Fc.twoj(), Fa.twoj(), 2 * k, //
                                Fd.twoj(), Fb.twoj(), tl);
    if (sixj == 0)
      continue;
    auto Qlabdc = Qk_abcd(Fa, Fb, Fd, Fc, tl / 2);
    sum += sixj * Qlabdc;
  }

  return s * (Qkabcd + tkp1 * sum);
}

//******************************************************************************
double Qk_abcd(const DiracSpinor &Fa, const DiracSpinor &Fb,
               const DiracSpinor &Fc, const DiracSpinor &Fd, const int k) {

  auto tCac = Angular::tildeCk_kk(k, Fa.k, Fc.k);
  if (tCac == 0.0)
    return 0.0;
  auto tCbd = Angular::tildeCk_kk(k, Fb.k, Fd.k);
  if (tCbd == 0.0)
    return 0.0;
  auto Rkabcd = Rk_abcd(Fa, Fb, Fc, Fd, k);
  auto m1tk = Angular::evenQ(k) ? 1 : -1;
  return m1tk * tCac * tCbd * Rkabcd;
}

//******************************************************************************
double Rk_abcd(const DiracSpinor &Fa, const DiracSpinor &Fb,
               const DiracSpinor &Fc, const DiracSpinor &Fd,
               const int k) //
{
  const auto yk_bd = yk_ab(Fb, Fd, k, std::min(Fa.pinf, Fc.pinf));
  return Rk_abcd(Fa, Fc, yk_bd);
}
//------------------------------------------------------------------------------
double Rk_abcd(const DiracSpinor &Fa, const DiracSpinor &Fc,
               const std::vector<double> &yk_bd) {
  const auto &drdu = Fa.p_rgrid->drdu;
  const auto i0 = std::max(Fa.p0, Fc.p0);
  const auto imax = std::min(Fa.pinf, Fc.pinf);
  const auto Rff = NumCalc::integrate(1.0, i0, imax, Fa.f, Fc.f, yk_bd, drdu);
  const auto Rgg = NumCalc::integrate(1.0, i0, imax, Fa.g, Fc.g, yk_bd, drdu);
  return (Rff + Rgg) * Fa.p_rgrid->du;
}

//******************************************************************************
DiracSpinor Rk_abcd_rhs(const DiracSpinor &Fa, const DiracSpinor &Fb,
                        const DiracSpinor &Fc, const DiracSpinor &Fd,
                        const int k) {
  const auto yk_bd = yk_ab(Fb, Fd, k, Fc.pinf);
  auto out = DiracSpinor(0, Fa.k, *(Fa.p_rgrid));
  out.pinf = Fc.pinf;
  out.f = NumCalc::mult_vectors(Fc.f, yk_bd);
  out.g = NumCalc::mult_vectors(Fc.g, yk_bd);
  return out;
}

} // namespace CoulombInts
