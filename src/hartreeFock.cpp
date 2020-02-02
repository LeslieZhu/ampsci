#include "Dirac/Wavefunction.hpp"
#include "IO/ChronoTimer.hpp"
#include "IO/UserInput.hpp"
#include "Modules/Module_runModules.hpp"
#include <iostream>
#include <string>
// #include "Physics/AtomData.hpp" //need for testing basis only
#include "Dirac/Operators.hpp"
#include "HF/ExternalField.hpp"

int main(int argc, char *argv[]) {
  ChronoTimer timer("\nhartreeFock");
  std::string input_file = (argc > 1) ? argv[1] : "hartreeFock.in";
  std::cout << "Reading input from: " << input_file << "\n";

  // Input options
  UserInput input(input_file);

  // Get + setup atom parameters
  auto input_ok = input.check("Atom", {"Z", "A", "varAlpha2"});
  auto atom_Z = input.get<std::string>("Atom", "Z");
  auto atom_A = input.get("Atom", "A", -1);
  auto var_alpha = [&]() {
    auto varAlpha2 = input.get("Atom", "varAlpha2", 1.0);
    return (varAlpha2 > 0) ? std::sqrt(varAlpha2) : 1.0e-25;
  }();

  // Get + setup Grid parameters
  input_ok = input_ok && input.check("Grid", {"r0", "rmax", "num_points",
                                              "type", "b", "fixed_du"});
  auto r0 = input.get("Grid", "r0", 1.0e-5);
  auto rmax = input.get("Grid", "rmax", 150.0);
  auto num_points = input.get("Grid", "num_points", 1600ul);
  auto du_tmp = input.get("Grid", "fixed_du", -1.0); // >0 means calc num_points
  if (du_tmp > 0)
    num_points = 0;
  auto b = input.get("Grid", "b", 0.33 * rmax);
  auto grid_type = input.get<std::string>("Grid", "type", "loglinear");
  if (b <= r0 || b >= rmax)
    grid_type = "logarithmic";

  // Get + setup nuclear parameters
  input_ok =
      input_ok && input.check("Nucleus", {"A", "rrms", "skin_t", "type"});
  atom_A = input.get("Nucleus", "A", atom_A); // over-writes "atom" A
  auto nuc_type = input.get<std::string>("Nucleus", "type", "Fermi");
  auto rrms = input.get("Nucleus", "rrms", -1.0); /*<0 means lookup default*/
  auto skint = input.get("Nucleus", "skin_t", -1.0);

  // create wavefunction object
  Wavefunction wf(atom_Z, {num_points, r0, rmax, b, grid_type, du_tmp},
                  {atom_Z, atom_A, nuc_type, rrms, skint}, var_alpha);

  // Parse input for HF method
  input_ok =
      input_ok &&
      input.check("HartreeFock", {"core", "valence", "convergence", "method",
                                  "Green_H", "Green_d", "Tietz_g", "Tietz_t",
                                  "orthonormaliseValence", "sortOutput"});
  auto str_core = input.get<std::string>("HartreeFock", "core", "[]");
  auto eps_HF = input.get("HartreeFock", "convergence", 1.0e-12);
  auto HF_method = HartreeFock::parseMethod(
      input.get<std::string>("HartreeFock", "method", "HartreeFock"));

  if (!input_ok)
    return 1;

  std::cout << "\nRunning for " << wf.atom() << "\n"
            << wf.nuclearParams() << "\n"
            << wf.rgrid.gridParameters() << "\n"
            << "********************************************************\n";
  // For when using Hartree, or a parametric potential:
  double H_d = 0.0, g_t = 0.0;
  if (HF_method == HFMethod::GreenPRM) {
    H_d = input.get("HartreeFock", "Green_H", 0.0);
    g_t = input.get("HartreeFock", "Green_d", 0.0);
    std::cout << "Using Greens Parametric Potential\n";
  } else if (HF_method == HFMethod::TietzPRM) {
    H_d = input.get("HartreeFock", "Tietz_g", 0.0);
    g_t = input.get("HartreeFock", "Tietz_t", 0.0);
    std::cout << "Using Tietz Parametric Potential\n";
  } else if (HF_method == HFMethod::Hartree) {
    std::cout << "Using Hartree Method (no Exchange)\n";
  }

  // Use QED radiatve potential?
  input_ok = input_ok && input.check("RadPot", {"Ueh", "SE_h", "SE_l", "SE_m",
                                                "rcut", "scale_rN"});
  auto x_Ueh = input.get("RadPot", "Ueh", 0.0);
  auto x_SEe_h = input.get("RadPot", "SE_h", 0.0);
  auto x_SEe_l = input.get("RadPot", "SE_l", 0.0);
  auto x_SEm = input.get("RadPot", "SE_m", 0.0);
  auto rcut = input.get("RadPot", "rcut", 1.0);
  auto scale_rN = input.get("RadPot", "scale_rN", 1.0);
  if (input_ok)
    wf.radiativePotential(x_Ueh, x_SEe_h, x_SEe_l, x_SEm, rcut, scale_rN);

  { // Solve Hartree equations for the core:
    ChronoTimer t(" core");
    wf.hartreeFockCore(HF_method, str_core, eps_HF, H_d, g_t);
  }

  // Adds effective polarision potential to direct potential
  // (After HF core, before HF valence)
  auto a_eff = input.get("dV", "a_eff", 0.0);
  if (a_eff > 0) { // a=0.61 works well for Cs ns, n=6-18
    auto r_cut = input.get("dV", "r_cut", 1.0);
    auto dV = [=](double x) { return -0.5 * a_eff / (x * x * x * x + r_cut); };
    for (auto i = 0u; i < wf.rgrid.num_points; ++i) {
      wf.vdir[i] += dV(wf.rgrid.r[i]);
    }
  }

  // Solve for the valence states:
  auto valence_list = (wf.Ncore() < wf.Znuc())
                          ? input.get<std::string>("HartreeFock", "valence", "")
                          : "";
  if (valence_list != "") {
    // 'if' is only for output format, nothing bad happens if below are called
    ChronoTimer t("  val");
    wf.hartreeFockValence(valence_list);
    if (input.get("HartreeFock", "orthonormaliseValence", false))
      wf.orthonormaliseOrbitals(wf.valence_orbitals, 2);
  }

  // Output results:
  std::cout << "\nHartree Fock: " << wf.atom() << "\n";
  auto sorted = input.get("HartreeFock", "sortOutput", true);
  wf.printCore(sorted);
  wf.printValence(sorted);

  input.check("Basis", {"number", "order", "r0", "rmax", "states", "print"});
  auto n_spl = input.get("Basis", "number", 0ul);
  auto k_spl = input.get("Basis", "order", 0ul);
  auto r0_spl = input.get("Basis", "r0", 0.0);
  auto rmax_spl = input.get("Basis", "rmax", 0.0);
  auto basis_states = input.get<std::string>("Basis", "states", "");
  auto print = input.get("Basis", "print", false);
  if (n_spl > 0) {
    wf.formBasis(basis_states, n_spl, k_spl, r0_spl, rmax_spl);
    if (print)
      wf.printBasis();
  }

  // run each of the modules
  Module::runModules(input, wf);

  auto h = E1Operator(wf.rgrid);
  // auto h = HyperfineOperator(1.0, 0.5, 4.0 / PhysConst::aB_fm, wf.rgrid);
  // auto h = PNCnsiOperator(5.67073, 2.3, wf.rgrid);

  auto omega = 0.00;
  auto tdhf = ExternalField(&h, wf.core_orbitals,
                            NumCalc::add_vectors(wf.vnuc, wf.vdir),
                            wf.get_alpha(), omega);

  // tdhf.solve_TDHFcore();
  // tdhf.solve_TDHFcore_matrix(wf);
  // tdhf.solve_TDHFcore_matrix(wf);
  // tdhf.solve_TDHFcore();
  // tdhf.solve_TDHFcore();

  const auto *psis = wf.getState(6, -1);
  const auto *psip1 = wf.getState(6, 1);
  const auto *psip3 = wf.getState(6, -2);

  auto me1 = h.reducedME(*psis, *psip1);
  auto me1b = h.reducedME(*psip1, *psis);
  auto me3 = h.reducedME(*psis, *psip3);
  auto me3b = h.reducedME(*psip3, *psis);

  std::cout << h.angularF((*psis).k, (*psip1).k) << " "
            << h.radialIntegral(*psis, *psip1) << "\n";
  std::cout << h.angularF((*psis).k, (*psip3).k) << " "
            << h.radialIntegral(*psis, *psip3) << "\n";

  auto prev = 0.0;
  auto targ = 1.0e-6;
  auto max_its = 100;
  // for (int i = 0; i < max_its; i++) {
  //   // tdhf.solve_TDHFcore_matrix(wf);
  //   tdhf.solve_TDHFcore();
  //   // std::cin.get();
  //   auto dv1 = tdhf.dV_ab(*psis, *psip1);
  //   auto dv1b = tdhf.dV_ab(*psip1, *psis);
  //   auto dv3 = tdhf.dV_ab(*psis, *psip3);
  //   auto dv3b = tdhf.dV_ab(*psip3, *psis);
  //   auto eps = std::abs((dv1 - prev) / prev);
  //   prev = dv1;
  //   std::cout << me1 << " + " << dv1 << " = " << me1 + dv1 << "\n";
  //   std::cout << me1b << " + " << dv1b << " = " << me1b + dv1b << "\n";
  //   std::cout << me3 << " + " << dv3 << " = " << me3 + dv3 << "\n";
  //   std::cout << me3b << " + " << dv3b << " = " << me3b + dv3b << "\n";
  //   std::cout << i << " : " << eps << "\n";
  //   if (eps < targ)
  //     break;
  // }
  {
    // tdhf.solve_TDHFcore_matrix(wf);
    tdhf.solve_TDHFcore(1);
    // std::cin.get();
    auto dv1_0 = tdhf.dV_ab(*psis, *psip1);
    auto dv1b_0 = tdhf.dV_ab(*psip1, *psis);
    auto dv3_0 = tdhf.dV_ab(*psis, *psip3);
    auto dv3b_0 = tdhf.dV_ab(*psip3, *psis);
    tdhf.solve_TDHFcore();
    auto dv1 = tdhf.dV_ab(*psis, *psip1);
    auto dv1b = tdhf.dV_ab(*psip1, *psis);
    auto dv3 = tdhf.dV_ab(*psis, *psip3);
    auto dv3b = tdhf.dV_ab(*psip3, *psis);
    std::cout << me1 << " + " << dv1 << " = " << me1 + dv1 << "  ("
              << me1 + dv1_0 << ")\n";
    std::cout << me1b << " + " << dv1b << " = " << me1b + dv1b << "  ("
              << me1b + dv1b_0 << ")\n";
    std::cout << me3 << " + " << dv3 << " = " << me3 + dv3 << "  ("
              << me3 + dv3_0 << ")\n";
    std::cout << me3b << " + " << dv3b << " = " << me3b + dv3b << "  ("
              << me3b + dv3b_0 << ")\n";
  }

  // auto &psis2 = wf.getState(2, -1);
  // auto &psip2 = wf.getState(2, 1);
  // auto dpsisX = tdhf.get_dPsi_x(psis2, dPsiType::X, psip2.k);
  // auto dpsisY = tdhf.get_dPsi_x(psis2, dPsiType::Y, psip2.k);
  //
  // auto metha = psip2 * dpsisX;
  // auto methb = h.reducedME(psip2, psis2) / (psis2.en - psip2.en + omega);
  // std::cout << metha << " - " << methb << " => "
  //           << 2.0 * (metha - methb) / (metha + methb) << "\n";
  //
  // metha = dpsisY * psip2;
  // methb = h.reducedME(psis2, psip2) / (psis2.en - psip2.en - omega);
  // std::cout << metha << " - " << methb << " => "
  //           << 2.0 * (metha - methb) / (metha + methb) << "\n";

  //*********************************************************
  //               TESTS
  //*********************************************************

  // needs: #include "Physics/AtomData.hpp" (for AtomData::listOfStates_nk)
  //
  // bool test_hf_basis = false;
  // if (test_hf_basis) {
  //   auto basis_lst = AtomData::listOfStates_nk("9spd8f");
  //   std::vector<DiracSpinor> basis = wf.core_orbitals;
  //   HartreeFock hfbasis(wf, basis, 1.0e-6);
  //   hfbasis.verbose = false;
  //   for (const auto &nk : basis_lst) {
  //     if (wf.isInCore(nk.n, nk.k))
  //       continue;
  //     basis.emplace_back(DiracSpinor(nk.n, nk.k, wf.rgrid));
  //     auto tmp_vex = std::vector<double>{};
  //     hfbasis.hf_valence_approx(basis.back(), tmp_vex);
  //   }
  //   wf.orthonormaliseOrbitals(basis, 2);
  //   wf.printValence(false, basis);
  //   std::cout << "\n Total time: " << timer.reading_str() << "\n";
  // }

  return 0;
}

//******************************************************************************
