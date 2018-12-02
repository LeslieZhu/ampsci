#include "HartreeFockClass.h"
#include "ElectronOrbitals.h"
#include "ATI_atomInfo.h"
#include "PRM_parametricPotentials.h"
#include "WIG_369j.h"
#include <vector>
#include <cmath>
//Add change update git

// XXX Rename to Coulomb Integrals??

//******************************************************************************
HartreeFock::HartreeFock(ElectronOrbitals &wf)
{

  //wf = in_wf;

  startingApprox(wf);
  m_ngp = wf.ngp;
  m_num_core_states = wf.num_core_states;

  form_Lambda_abk(wf.kappa);

  //store l, 2j, and "kappa_index" in arrays
  for(int i=0; i<wf.num_core_states; i++){
    twoj_list.push_back(ATI::twoj_k(wf.kappa[i]));
    kappa_index_list.push_back(index_from_kappa(wf.kappa[i]));
  }

  form_Lambda_abk(wf.kappa);

  //initialise the non-square, non-regular array:
  //XXX make into function!
  arr_v_bb0_r.clear();
  arr_v_bb0_r.resize(m_num_core_states, std::vector<double>(m_ngp));
  arr_v_abk_r.clear();
  arr_v_abk_r.resize(m_num_core_states);
  for(int a=0; a<m_num_core_states; a++){
    arr_v_abk_r[a].resize(a+1);
    int tja = twoj_list[a];
    for(int b=0; b<=a; b++){
      int tjb = twoj_list[b];
      int num_k = (tja>tjb) ? (tjb+1)/2 : (tja+1)/2;
      arr_v_abk_r[a][b].resize(num_k); //right? or +1? XXX
      for(int ik=0; ik<num_k; ik++){ //every second!
        arr_v_abk_r[a][b][ik].resize(m_ngp);
      }
    }
  }

  form_vabk(wf); //note: call other one if HART only!

  std::vector<std::vector<double> >
    vex(m_num_core_states, std::vector<double>(m_ngp));

  form_vdir(wf.vdir,wf,false);
  form_approx_vex_core(vex,wf);



}

//******************************************************************************
void HartreeFock::startingApprox(ElectronOrbitals &wf)
/*
Starting approx for HF. Uses Green parametric
Later, can put other options if you want.
*/
{
  wf.vdir.resize(wf.ngp); //make sure correct size

  //Get default values for Green potential
  double Gh,Gd;  //Green potential parameters
  PRM::defaultGreenCore(wf.Z,Gh,Gd);
  //Fill the the potential, using Greens PRM
  for(int i=0; i<wf.ngp; i++) wf.vdir[i] = PRM::green(wf.Z,wf.r[i],Gh,Gd);

  //First step: Solve each core state using above parameteric potential
  wf.solveInitialCore(1); //2, since don't need high accuray here [1 in 10^2]

}

//******************************************************************************
//XXX Explain index here
// int HartreeFock::kappa_from_index(int i){
//   // (-1,n+1) * (int(n/2)+1)
//   //this gives kappa, for n=0,1,2,3...
//   int sign = (i%2) ? 1 : -1;//if i is even, sign is negative
//   return sign*(int(i/2)+1);
// }
int HartreeFock::index_from_kappa(int ka) const{
  if(ka>0) return 2*ka-1;
  return 2*abs(ka)-2;
}
int HartreeFock::twoj_from_index(int i) const{
  return 2*int(i/2)+1;}

//******************************************************************************
void HartreeFock::form_Lambda_abk(const std::vector<int> &kappa)
// XXX For now, just core.
// New routine for valence? Or make so can re-call this one??
// THIS routine re-sizes the arr_Lambda_nmk array
{

  int max_kappa_index = 0;
  for(size_t i=0; i<kappa.size(); i++){
    int kappa_index = index_from_kappa(kappa[i]);
    if(kappa_index > max_kappa_index) max_kappa_index = kappa_index;
  }
  m_max_kappa_index_core = max_kappa_index;

  //xxx careful. For valence, this needs updating!
  arr_Lambda_nmk.clear(); //should already be empty!

  for(int n=0; n<=max_kappa_index; n++){
    int tja = twoj_from_index(n);
    std::vector<std::vector<double> > Lmk;
    for(int m=0; m<=n; m++){
      int tjb = twoj_from_index(m);
      int kmin = (tja - tjb)/2; //don't need abs, as m<=n => ja>=jb
      int kmax = (tja + tjb)/2;
      int ikmax = (kmax-kmin)/2;
      std::vector<double> Lk(ikmax+1);
      for(int ik=0; ik<=ikmax; ik++){
        int k = 2*ik + kmin;
        double tjs = WIG::threej_2(tja,tjb,2*k,-1,1,0);
        //WIG::parity(la,lb,k); //not needed! every 2nd k is good! XXX CHECK?
        Lk[ik] = tjs*tjs;
      }
      Lmk.push_back(Lk);
    }
    arr_Lambda_nmk.push_back(Lmk);
  }

}

// //******************************************************************************
// int HartreeFock::get_num_ks(int a, int b) const
// //NOTE: can only be called AFTER arr_v_abk_r is formed!
// {
//   if(a==b) return 1;
//   if(a>b) return arr_v_abk_r[a][b].size();
//   return arr_v_abk_r[b][a].size();
// }

//******************************************************************************
double HartreeFock::get_Lambda_abk(int a, int b, int k) const
//gets Lambda
//a nd b and STATE indexes
//NOTE: There are a few if's here. Maybe better not?
{

  int n = kappa_index_list[a];
  int m = kappa_index_list[b];

  int kmin = abs(twoj_list[a] - twoj_list[b])/2;
  if(k<kmin) return 0;
  if(k%2 != kmin%2) return 0; //if kmin is even, k must be even

  int kmax = (twoj_list[a] + twoj_list[b])/2;
  if(k>kmax) return 0;

  int ik = (k-kmin)/2;
  if(m<=n) return arr_Lambda_nmk[n][m][ik];
  return arr_Lambda_nmk[m][n][ik];

}

//******************************************************************************
void HartreeFock::calculate_v_abk(const ElectronOrbitals &wf,
  int a, int b, int k, std::vector<double> & vabk)
/*
Calculalates v^k_ab screening function.
Note: only call for b<=a, and for k's with non-zero angular coefs!
Since v_ab = v_ba !

Stores in vabk - must already be sized corectly!

r_min := min(r,r')
rho(r') := fa(r')*fb(r') + ga(r')gb(r')
v^k_ab(r) = Int_0^inf [r_min^k/r_max^(k+1)]*rho(f') dr'
          = Int_0^r [r'^k/r^(k+1)]*rho(r') dr'
            + Int_r^inf [r^k/r'^(k+1)]*rho(r') dr'
         := A(r)/r^(k+1) + B(r)*r^k
A(r0)  = 0
B(r0)  = Int_0^inf [r^k/r'^(k+1)]*rho(r') dr'
A(r_n) = A(r_{n-1}) + (rho(r_{n-1})*r_{n-1}^k)*dr
B(r_n) = A(r_{n-1}) + (rho(r_{n-1})/r_{n-1}^(k+1))*dr
v^k_ab(rn) = A(rn)/rn^(k+1) + B(rn)*rn^k
*/
{

  int irmax = std::min(wf.pinflist[a],wf.pinflist[b]); //? called too often?
  //could do k loop in here too? Nah

  double Ax=0, Bx=0;
  for(int i=0; i<irmax; i++) Bx += wf.drdt[i]*
    (wf.f[a][i]*wf.f[b][i]+wf.g[a][i]*wf.g[b][i])/pow(wf.r[i],k+1);

  vabk[0] = Bx;
  for(int i=1; i<irmax; i++){
    double Fdr = wf.drdt[i-1]*
      (wf.f[a][i-1]*wf.f[b][i-1]+wf.g[a][i-1]*wf.g[b][i-1]);
    Ax = Ax + Fdr*pow(wf.r[i-1],k);
    Bx = Bx - Fdr/pow(wf.r[i-1],k+1);
    vabk[i] = wf.h*(Ax/pow(wf.r[i],k+1) + Bx*pow(wf.r[i],k));
  }
  for(int i=irmax; i<wf.ngp; i++) vabk[i] = 0;

}

//******************************************************************************
void HartreeFock::form_vbb0(const ElectronOrbitals &wf)
// ONLY call this one if not doing HF!
{
  // arr_v_bb0_r //must already be correct size!
  for(int b=0; b<wf.num_core_states; b++){
    calculate_v_abk(wf,b,b,0,arr_v_bb0_r[b]);
  }
}

//******************************************************************************
void HartreeFock::form_vabk(const ElectronOrbitals &wf)
//Does k=0 case too, so don't call form_vbb0 as well!
{
  // arr_v_abk_r //must already be correct size!
  for(int a=0; a<m_num_core_states; a++){
    for(int b=0; b<a; b++){
      int kmin = abs(twoj_list[a] - twoj_list[b])/2;
      int kmax = (twoj_list[a] + twoj_list[b])/2;
      for(int k=kmin; k<=kmax; k+=2){ //every second!
        int ik = (k-kmin)/2;
        calculate_v_abk(wf,a,b,k,arr_v_abk_r[a][b][ik]);
      }
    }
    calculate_v_abk(wf,a,a,0,arr_v_bb0_r[a]);
  }
}

//******************************************************************************
std::vector<double>& HartreeFock::get_v_abk(int a, int b, int k)
//XXX make const?
//XXX Put 'pragma' debug in here - turned on/off @ compile! (no slow-down!)
//XXX Check if k out of bounds??
{
  if(a==b) return arr_v_bb0_r[a];

  //I might already know kmin! XXX
  int kmin = abs(twoj_list[a] - twoj_list[b])/2;
  int ik = (k-kmin)/2;

  if(a>b) return arr_v_abk_r[a][b][ik];
  return arr_v_abk_r[b][a][ik]; //check! Is a copy hapenning?? XXX

  // if(k<kmin) return 0;
  // if(k%2 != kmin%2) return 0; //if kmin is even, k must be even
  // int kmax = (twoj_list[a] + twoj_list[b])/2;
  // if(k>kmax) return 0;

}


//******************************************************************************
void HartreeFock::form_vdir(std::vector<double> &vdir,
  const ElectronOrbitals &wf, bool re_scale)
{

  //clear old values
  for(int i=0; i<m_ngp; i++) vdir[i] = 0;

  double sf = re_scale? (1. - (1.)/wf.num_core_electrons) : 1;

  //put OMP here, with critical?
  //OR omp below?
  //OR diff loop structure??
  for(int b=0; b<m_num_core_states; b++){
    int rmax = wf.pinflist[b];
    double f = (twoj_list[b]+1)*wf.occ_frac[b];
    std::vector<double> &v0bb = get_v_abk(b,b,0); // is there a copy?????
    for(int i=0; i<rmax; i++){
      vdir[i] += f*v0bb[i]*sf;
    }
  }

}

//******************************************************************************
void HartreeFock::form_approx_vex_core(std::vector<std::vector<double> > &vex,
  const ElectronOrbitals &wf)
{
  //#pragma omp parallel for
  for(int a=0; a<m_num_core_states; a++){
    form_approx_vex_a(a,vex[a],wf); //XXX remember to size first!
  }
}

//******************************************************************************
void HartreeFock::form_approx_vex_a(int a, std::vector<double> &vex_a,
  const ElectronOrbitals &wf)
{

  for(int i=0; i<m_ngp; i++) vex_a[i] = 0;

  //Can play w/ OMP + critical here..or not (shouldn't matter!)
  for(int b=0; b<m_num_core_states; b++){
    if(b==a) continue;
    double x_tjbp1 = (twoj_list[b]+1)*wf.occ_frac[b]; //what about ocf[a]??? XXX
    int irmax = std::min(wf.pinflist[a],wf.pinflist[b]);
    //int Nk = get_num_ks(a,b);
    int kmin = abs(twoj_list[a] - twoj_list[b])/2;
    int kmax = (twoj_list[a] + twoj_list[b])/2;
    for(int k=kmin; k<=kmax; k+=2){ //not including k=0
      std::vector<double> &vabk = get_v_abk(a,b,k); // is there a copy?????
      double f1 = -1*x_tjbp1*get_Lambda_abk(a,b,k);
      for(int i=0; i<irmax; i++){
        double Fab=1;
        if(fabs(wf.f[a][i])>1.e-3){
          double fac_top = wf.f[a][i]*wf.f[b][i] + wf.g[a][i]*wf.g[b][i];
          double fac_bot = wf.f[a][i]*wf.f[a][i] + wf.g[a][i]*wf.g[a][i];
          Fab = fac_top/fac_bot;
        }
        vex_a[i] += f1*vabk[i]*Fab;
      }
    }
  }
  //now, do a=b, k=0 case - ONLY if a is in the core!
  if(a<m_num_core_states){
    double x_tjbp1 = (twoj_list[a]+1)*wf.occ_frac[a]; //what about ocf[a]??? XXX
    int irmax = wf.pinflist[a];
    std::vector<double> &vabk = get_v_abk(a,a,0); // is there a copy?????
    double f1 = -1*x_tjbp1*get_Lambda_abk(a,a,0);
    for(int i=0; i<irmax; i++) vex_a[i] += f1*vabk[i];
  }

}





























// //******************************************************************************
// void HartreeFock::formVexA(const ElectronOrbitals &wf, int a,
//   std::vector<double> &vex_a)
// {
//   for(int b=0; b<wf.num_core_states; b++){
//     int kmin = abs(twoj_list[a] - twoj_list[b])/2;
//     int kmax = (twoj_list[a] + twoj_list[b])/2;
//     for(int k=kmin; k<=kmax; k+=2){
//       int ik = (k-kmin)/2;
//       std::vector<double> &v0kab = v_abk(a,b,ik);
//
//       //(twoj_list[a]+1)*unsafe_Lambda_abk(a,b,ik)*
//     }
//   }
// }






// //******************************************************************************
// double HartreeFock::unsafe_Lambda_abk(int a, int b, int ik) const
// //No checks at all...
// //If not any faster, just kill this version!!
// {
//   int n = kappa_index_list[a];
//   int m = kappa_index_list[b];
//   //int ik = (k-kmin)/2
//   if(m<=n) return arr_Lambda_nmk[n][m][ik];
//   return arr_Lambda_nmk[m][n][ik];
// }

// //******************************************************************************
// void HartreeFock::form_v_abk(const ElectronOrbitals &wf)
// {
//
//   // #pragma omp parallel for
//   for(int a=0; a<wf.num_core_states; a++){
//     for(int b=0; b<=a; b++){
//       int irmax = std::min(wf.pinflist[a],wf.pinflist[b]);
//
//
//       int kmin = abs(twoj_list[a] - twoj_list[b])/2;
//       int kmax = (twoj_list[a] + twoj_list[b])/2;
//       for(int k=kmin; k<=kmax; k+=2){ //every second!
//
//         int ik = (k-kmin)/2;
//         //double Labk = unsafe_Lambda_abk(a,b,ik);
//
//         double Ax=0, Bx=0;
//         for(int i=0; i<irmax; i++) Bx += wf.drdt[i]*
//           (wf.f[a][i]*wf.f[b][i]+wf.g[a][i]*wf.g[b][i])/pow(wf.r[i],k+1);
//
//           arr_v_abk_r[a][b][ik][0] = 0;
//           for(int i=1; i<irmax; i++){
//             double Fdr = wf.drdt[i-1]*
//               (wf.f[a][i-1]*wf.f[b][i-1]+wf.g[a][i-1]*wf.g[b][i-1]);
//               Ax = Ax + Fdr*pow(wf.r[i-1],k);
//               Bx = Bx - Fdr/pow(wf.r[i-1],k+1);
//               arr_v_abk_r[a][b][ik][i]
//                 = wf.h*(Ax/pow(wf.r[i],k+1) + Bx*pow(wf.r[i],k));
//           }
//           for(int i=irmax; i<wf.ngp; i++) arr_v_abk_r[a][b][ik][i] = 0;
//           //std::cout<<arr_v_abk_r[a][b][ik][100]<<"\n";
//
//       }
//     }
//   }
// }

// //******************************************************************************
// std::vector<double>& HartreeFock::v_abk(int a, int b, int k)
// {
//   int n = kappa_index_list[a];
//   int m = kappa_index_list[b];
//
//   int kmin = abs(twoj_list[a] - twoj_list[b])/2;
//   // if(k<kmin) return 0;
//   // if(k%2 != kmin%2) return 0; //if kmin is even, k must be even
//   //
//   // int kmax = (twoj_list[a] + twoj_list[b])/2;
//   // if(k>kmax) return 0;
//
//   int ik = (k-kmin)/2;
//   if(n<=m) return arr_v_abk_r[n][m][ik];
//   return arr_v_abk_r[m][n][ik];
// }
