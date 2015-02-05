
#include <LO_BCTYPES.H>
#include <ParmParse.H>
#include "Radiation.H"
#include "RadSolve.H"

#include "Castro_F.H"

#undef BL_USE_ARLIM

#include "RAD_F.H"
#include "PROB_AMR_F.H"

#include <Using.H>

// Radiation test problems and static parameters.  Some of these are
// initialized with inputs values in the function read_static_params.
// This is called from Castro::read_params in Castro.cpp.  The reason
// for initializing these early is that some of them are used in
// Castro::variableSetUp and affect the construction of the state.

int RadTests::do_thermal_wave_cgs = 0;
int RadTests::do_rad_sphere = 0;

Radiation::Solver_Type Radiation::SolverType = Radiation::InvalidSolver;
Radiation::Radiation_Type Radiation::radiation_type = Radiation::Unknown;

Real Radiation::radtoE = 0.;
//Real Radiation::radtoJ = 0.;
Real Radiation::Etorad = 0.;
Real Radiation::radfluxtoF = 0.;

int Radiation::do_multigroup = 0;
int Radiation::nGroups = 1;
int Radiation::nNeutrinoSpecies = 0;
Array<int> Radiation::nNeutrinoGroups(0);
int Radiation::plot_neutrino_group_energies_per_MeV = 1;
int Radiation::plot_neutrino_group_energies_total   = 0;
int Radiation::accelerate = 1;
int Radiation::rad_hydro_combined = 0;
int Radiation::comoving = 1;
int Radiation::Er_Lorentz_term = 1;
int Radiation::fspace_advection_type = 2;
int Radiation::Test_Type_lambda = 0;
int Radiation::Test_Type_Flux = 0;
int Radiation::Test_Type_Flux_lab = 0;
int Radiation::filter_lambda_T = 0;
int Radiation::filter_lambda_S = 0;
int Radiation::filter_prim_int = 0;
int Radiation::filter_prim_T = 4;
int Radiation::filter_prim_S = 0;

// These physical constants get their values in the Radiation constructor:
Real Radiation::convert_MeV_erg = 0.0;
Real Radiation::clight          = 0.0;
Real Radiation::hPlanck         = 0.0;
Real Radiation::kBoltz          = 0.0;
Real Radiation::Avogadro        = 0.0;

Real Radiation::c               = 0.0;
Real Radiation::sigma           = 0.0;
Real Radiation::aRad            = 0.0;

int Radiation::current_group_number = -1;
std::string Radiation::current_group_name = "Radiation";

Real Radiation:: flatten_pp_threshold = -1.0;
int Radiation::first_order_hydro = 0;
int Radiation::pure_hydro = 0;

// static initialization, must be called before Castro::variableSetUp

void Radiation::read_static_params()
{
  ParmParse pp("radiation");

  pp.query("do_thermal_wave_cgs",  RadTests::do_thermal_wave_cgs);
  pp.query("do_rad_sphere",    RadTests::do_rad_sphere);

  {
    int solver_type = Radiation::SolverType;
    pp.get("SolverType", solver_type);
    SolverType = static_cast<Solver_Type>(solver_type);
  }

  if (Radiation::SolverType == Radiation::MGFLDSolver) {
    do_multigroup = 1;
  }
  else {
    do_multigroup = 0;    
  }

  if (Radiation::SolverType == Radiation::MGFLDSolver) {
    accelerate = 2;
  }
  pp.query("accelerate", accelerate);

  if (Radiation::SolverType == Radiation::SGFLDSolver ||
      Radiation::SolverType == Radiation::MGFLDSolver ) {
    Radiation::rad_hydro_combined = 1;
    pp.query("rad_hydro_combined", rad_hydro_combined);
  }

  if (SolverType == MGFLDSolver) {
    comoving = 1;    
    fspace_advection_type = 2;
    pp.query("fspace_advection_type", fspace_advection_type);
  }
  else if (SolverType == SGFLDSolver) {
    comoving = 1;  
    fspace_advection_type = 1;
    pp.query("comoving", comoving);
    if (comoving) {
      Er_Lorentz_term = 0;
    }
    else {
      Er_Lorentz_term = 1;
      pp.query("Er_Lorentz_term", Er_Lorentz_term);
    }
  }

  pp.query("Test_Type_lambda", Test_Type_lambda);
  pp.query("Test_Type_Flux", Test_Type_Flux);
  pp.query("Test_Type_Flux_lab", Test_Type_Flux_lab);

  pp.query("filter_lambda_T", filter_lambda_T);
  filter_lambda_S = filter_lambda_T - 1;
  pp.query("filter_lambda_S", filter_lambda_S);
  if (filter_lambda_T > 4) {
    BoxLib::Error("filter_lambda_T > 4");
  }
  if (filter_lambda_T < 0) {
    BoxLib::Error("filter_lambda_T < 0");
  }
  if (filter_lambda_T > 0) {
    if (filter_lambda_S >= filter_lambda_T) {
      BoxLib::Error("Invalid filter_lambda_S; S must be less than T when T > 0.");
    }
  }

  pp.query("filter_prim_int", filter_prim_int);
  pp.query("filter_prim_T", filter_prim_T);
  filter_prim_S = filter_prim_T - 1;
  pp.query("filter_prim_S", filter_prim_S);
  if (filter_prim_T > 4) {
    BoxLib::Error("filter_prim_T > 4");
  }
  if (filter_prim_T < 0) {
    BoxLib::Error("filter_prim_T < 0");
  }
  if (filter_prim_T > 0) {
    if (filter_prim_S >= filter_prim_T) {
      BoxLib::Error("Invalid filter_prim_S; S must be less than T when T > 0.");
    }
  }

  if (rad_hydro_combined) {
    if (Castro::use_colglaz) {
      BoxLib::Error("Castro::use_colglaz and rad_hydro_combined cannot both be true.");
    }
  }

  if (Radiation::SolverType == Radiation::MGFLDSolver) {

#ifdef NEUTRINO
    radiation_type = Neutrino;
    std::string rad_type_s("Unknown");
    pp.query("radiation_type", rad_type_s);
    if (rad_type_s.compare("PHOTON") == 0 ||
	rad_type_s.compare("Photon") == 0 ||
	rad_type_s.compare("photon") == 0 ) {
      radiation_type = Photon;
    }

    if (radiation_type == Neutrino) {
      pp.get("nNeutrinoSpecies", Radiation::nNeutrinoSpecies);
      BL_ASSERT(Radiation::nNeutrinoSpecies > 0);
      
      Radiation::nNeutrinoGroups.resize(Radiation::nNeutrinoSpecies);
      pp.getarr("nNeutrinoGroups", Radiation::nNeutrinoGroups,
		0, Radiation::nNeutrinoSpecies);
      
      Radiation::nGroups = 0;
      for (int n = 0; n < Radiation::nNeutrinoSpecies; n++) {
	Radiation::nGroups += Radiation::nNeutrinoGroups[n];
      }
      
      pp.query("plot_neutrino_group_energies_per_MeV",
	       Radiation::plot_neutrino_group_energies_per_MeV);
      pp.query("plot_neutrino_group_energies_total",
	       Radiation::plot_neutrino_group_energies_total);    
    }
    else {
      pp.query("nGroups", Radiation::nGroups);
      BL_ASSERT(Radiation::nGroups > 1); 
    }
#else
    radiation_type = Photon;
    pp.query("nGroups", Radiation::nGroups);
    //    BL_ASSERT(Radiation::nGroups > 1); 
#endif
  }
  else if (Radiation::SolverType != Radiation::SingleGroupSolver &&
	   Radiation::SolverType != Radiation::SGFLDSolver) {
      BoxLib::Error("Unknown Radiation::SolverType");    
  }
}

Radiation::Radiation(Amr* Parent, Castro* castro, int restart)
  : parent(Parent),
    flux_cons(PArrayManage),
    flux_cons_old(PArrayManage), 
    flux_trial(PArrayManage),
    dflux(PArrayManage)
{
  // castro is passed in, rather than obtained from parent, because this
  // routine will be called in some cases before any AmrLevels have
  // been installed into the parent's array of levels.

  ParmParse pp("radiation");

  do_sync = 1; pp.query("do_sync", do_sync);

  use_analytic_solution = 0;
  pp.query("use_analytic_solution", use_analytic_solution);

  {
    Real stefbol;

    int J_is_used = 0;

    BL_FORT_PROC_CALL(CA_INITRADCONSTANTS,ca_initradconstants)
      (M_PI, clight, hPlanck, kBoltz, stefbol, Avogadro, convert_MeV_erg,
       J_is_used);

    aRad = 4.*stefbol/clight;

    c        = clight;         
    sigma    = stefbol;        

    if (!do_multigroup) {
	// In single group and abstract test problems we can play with
	// c and sigma independent of physical reality, but messing with
	// them in a multigroup problem is likely to be bad.
	pp.query("c", c);
	pp.query("sigma", sigma);
    }
  }

  radtoE = 1.0;
  //    radtoJ = c/(4.*M_PI);
  Etorad = 1.0;
  radfluxtoF = 1.0;

  reltol   = 1.e-6;          pp.query("reltol", reltol);
  if (SolverType == SGFLDSolver || SolverType == MGFLDSolver) {
    abstol = 0.0;
  }
  else {
    abstol   = 1.e-6;          
  }
  pp.query("abstol", abstol);
  maxiter  = 50;             pp.query("maxiter", maxiter);
  miniter  =  1;             pp.query("miniter", miniter);
  convergence_check_type = 0; 
  pp.query("convergence_check_type", convergence_check_type);
  limiter  = 2;              pp.query("limiter", limiter);
  if (SolverType == SGFLDSolver && limiter%10 == 1) {
    BoxLib::Abort("SGFLDSolver does not supports limiter = 1");    
  }
  if (SolverType == MGFLDSolver && limiter%10 == 1) {
    BoxLib::Abort("MGFLDSolver does not supports limiter = 1");    
  }

  closure = 3;
  pp.query("closure", closure);

  BL_FORT_PROC_CALL(CA_INITFLUXLIMITER, ca_initfluxlimiter)
    (&limiter, &closure);

  inner_update_limiter = 0;
  pp.query("inner_update_limiter", inner_update_limiter);

  update_opacity    = 1000;
  
  if (SolverType == SGFLDSolver || SolverType == MGFLDSolver) {
    update_planck     = 1000;    
    update_rosseland  = 1000;    
    update_limiter    = 1000;     
  }
  else {
    update_planck     = 50;    
    update_rosseland  = 50;    
    update_limiter    = 4;     
  }
  pp.query("update_planck", update_planck);
  pp.query("update_rosseland", update_rosseland);
  pp.query("update_opacity", update_opacity);
  pp.query("update_limiter", update_limiter);

  dT  = 1.0;                 pp.query("delta_temp", dT);
  surface_average = 2;       pp.query("surface_average", surface_average);

  // for inner iterations of neutrino J equation
  relInTol = 1.e-4;          pp.query("relInTol", relInTol);
  if (SolverType == SGFLDSolver || SolverType == MGFLDSolver) {
    absInTol = 0.0;
  }
  else {
    absInTol = 1.e-4;     
  }
  pp.query("absInTol", absInTol);
  maxInIter = 30;            pp.query("maxInIter", maxInIter);
  minInIter =  1;            pp.query("minInIter", minInIter);

  skipAccelAllowed = 0;
  pp.query("skipAccelAllowed", skipAccelAllowed);

  matter_update_type = 0;
  pp.query("matter_update_type", matter_update_type);

  n_bisect = 1000;
  pp.query("n_bisect", n_bisect);
  dedT_fac = 1.0;
  dedY_fac = 1.0;
  pp.query("dedT_fac", dedT_fac);
  pp.query("dedY_fac", dedY_fac);

  inner_convergence_check = 2;
  pp.query("inner_convergence_check", inner_convergence_check);

  delta_e_rat_dt_tol = 100.0;
  pp.query("delta_e_rat_dt_tol", delta_e_rat_dt_tol);
  delta_T_rat_dt_tol = 100.0;
  pp.query("delta_T_rat_dt_tol", delta_T_rat_dt_tol);
  delta_Ye_dt_tol = 100.0;
  pp.query("delta_Ye_dt_tol", delta_Ye_dt_tol);

  underfac = 1.0;    pp.query("underfac", underfac);

  integrate_Planck = 1;
  pp.query("integrate_Planck", integrate_Planck);

  use_WiensLaw = 0;
  pp.query("use_WiensLaw", use_WiensLaw);
  Tf_Wien = -1.0;
  pp.query("Tf_Wien", Tf_Wien);

  verbose  = 0;      pp.query("v", verbose);  pp.query("verbose", verbose);

  // these "constants" are really variables, this is just a
  // mechanism for setting defaults:

  const_c_v.resize(    1, -1.0);
  const_kappa_p.resize(1, -1.0);
  const_kappa_r.resize(1, -1.0);
  const_scattering.resize(1, 0.0);

  c_v_exp_m.resize(    1, 0.0);
  c_v_exp_n.resize(    1, 0.0);
  kappa_p_exp_m.resize(1, 0.0);
  kappa_p_exp_n.resize(1, 0.0);
  kappa_p_exp_p.resize(1, 0.0);
  kappa_r_exp_m.resize(1, 0.0);
  kappa_r_exp_n.resize(1, 0.0);
  kappa_r_exp_p.resize(1, 0.0);
  scattering_exp_m.resize(1, 0.0);
  scattering_exp_n.resize(1, 0.0);
  scattering_exp_p.resize(1, 0.0);

  prop_temp_floor.resize(1, 0.0);

  use_opacity_table_module = 0;
  pp.query("use_opacity_table_module", use_opacity_table_module);

  do_kappa_stm_emission = 0;
  pp.query("do_kappa_stm_emission", do_kappa_stm_emission);

  pp.queryarr("const_c_v",       const_c_v,       0, 1);
  pp.queryarr("const_kappa_p",   const_kappa_p,   0, 1);
  pp.queryarr("const_kappa_r",   const_kappa_r,   0, 1);
  pp.queryarr("const_scattering", const_scattering, 0, 1);
  pp.queryarr("c_v_exp_m",       c_v_exp_m,       0, 1);
  pp.queryarr("c_v_exp_n",       c_v_exp_n,       0, 1);
  pp.queryarr("kappa_p_exp_m",   kappa_p_exp_m,   0, 1);
  pp.queryarr("kappa_p_exp_n",   kappa_p_exp_n,   0, 1);
  pp.queryarr("kappa_r_exp_m",   kappa_r_exp_m,   0, 1);
  pp.queryarr("kappa_r_exp_n",   kappa_r_exp_n,   0, 1);
  pp.queryarr("scattering_exp_m", scattering_exp_m, 0, 1);
  pp.queryarr("scattering_exp_n", scattering_exp_n, 0, 1);
  pp.queryarr("prop_temp_floor", prop_temp_floor, 0, 1);
  if (nGroups > 1) {
    pp.queryarr("kappa_p_exp_p",   kappa_p_exp_p,   0, 1);
    pp.queryarr("kappa_r_exp_p",   kappa_r_exp_p,   0, 1);
    pp.queryarr("scattering_exp_p", scattering_exp_p, 0, 1);
  }

  use_dkdT = (kappa_p_exp_n[0] == 0.0) ? 0 : 1;
  pp.query("use_dkdT", use_dkdT);

  if (Radiation::nNeutrinoSpecies == 0 ||
      Radiation::nNeutrinoGroups[0] == 0) {
    // photon problem
    if (SolverType == SGFLDSolver || SolverType == MGFLDSolver) {
      kappa_r_floor = 0.0;
    }
    else {
      kappa_r_floor = 1.e-4;
    }
  }
  else {
    // neutrino problem
    kappa_r_floor = 0.0;
  }
  pp.query("kappa_r_floor", kappa_r_floor);

  temp_floor    = 1.e-10;  pp.query("temp_floor", temp_floor);

  if (verbose >= 1 && ParallelDescriptor::IOProcessor()) {
    int n;
    cout << "const_c_v     =";
    for (n = 0; n < 1; n++) cout << " " << const_c_v[n];
    cout << endl;
    cout << "const_kappa_p =";
    for (n = 0; n < 1; n++) cout << " " << const_kappa_p[n];
    cout << endl;
    cout << "const_kappa_r =";
    for (n = 0; n < 1; n++) cout << " " << const_kappa_r[n];
    cout << endl;
    cout << "const_scattering =";
    for (n = 0; n < 1; n++) cout << " " << const_scattering[n];
    cout << endl;
    cout << "c_v_exp_m     =";
    for (n = 0; n < 1; n++) cout << " " << c_v_exp_m[n];
    cout << endl;
    cout << "c_v_exp_n     =";
    for (n = 0; n < 1; n++) cout << " " << c_v_exp_n[n];
    cout << endl;
    cout << "kappa_p_exp_m =";
    for (n = 0; n < 1; n++) cout << " " << kappa_p_exp_m[n];
    cout << endl;
    cout << "kappa_p_exp_n =";
    for (n = 0; n < 1; n++) cout << " " << kappa_p_exp_n[n];
    cout << endl;
    cout << "kappa_p_exp_p =";
    for (n = 0; n < 1; n++) cout << " " << kappa_p_exp_p[n];
    cout << endl;
    cout << "kappa_r_exp_m =";
    for (n = 0; n < 1; n++) cout << " " << kappa_r_exp_m[n];
    cout << endl;
    cout << "kappa_r_exp_n =";
    for (n = 0; n < 1; n++) cout << " " << kappa_r_exp_n[n];
    cout << endl;
    cout << "kappa_r_exp_p =";
    for (n = 0; n < 1; n++) cout << " " << kappa_r_exp_p[n];
    cout << endl;
    cout << "scattering_exp_m =";
    for (n = 0; n < 1; n++) cout << " " << scattering_exp_m[n];
    cout << endl;
    cout << "scattering_exp_n =";
    for (n = 0; n < 1; n++) cout << " " << scattering_exp_n[n];
    cout << endl;
    cout << "scattering_exp_p =";
    for (n = 0; n < 1; n++) cout << " " << scattering_exp_p[n];
    cout << endl;
    cout << "prop_temp_floor =";
    for (n = 0; n < 1; n++) cout << " " << prop_temp_floor[n];
    cout << endl;
    cout << "kappa_r_floor = " << kappa_r_floor << endl;
    cout << "temp_floor    = " << temp_floor << endl;
  }

  if (SolverType == MGFLDSolver ) {
    do_real_eos = 1;
  }
  else {
    do_real_eos = (const_c_v[0] < 0.) ? 1 : 0;
  }
  pp.query("do_real_eos", do_real_eos);
  
  if (! do_real_eos) {
    BL_ASSERT(const_c_v[0] > 0.); 
  }

  if (verbose > 2) {
    Array<int> temp;
    if (pp.queryarr("spot",temp,0,BL_SPACEDIM)) {
      IntVect tempi(temp);
      spot = tempi;
    }
    if (ParallelDescriptor::IOProcessor()) cout << "Spot: " << spot << endl;
  }

  // This call stores the value of surface_average in the kavg
  // routine.  The first three arguments are irrelevant here.
  Real foo=0.0;
  FORT_KAVG(foo, foo, foo, surface_average);

  if (verbose > 0 && ParallelDescriptor::IOProcessor()) {
    cout << "Creating Radiation object" << endl;
  }
  if (verbose >= 1 && ParallelDescriptor::IOProcessor()) {
    cout << "processors = " << ParallelDescriptor::NProcs() << endl;
    cout << "do_sync            = " << do_sync << endl;
    cout << "use_analytic_solution = " << use_analytic_solution << endl;

    cout << "c        = " << c << endl;
    cout << "sigma    = " << sigma << endl;
    cout << "reltol   = " << reltol << endl;
    cout << "abstol   = " << abstol << endl;
    cout << "maxiter  = " << maxiter << endl;
    cout << "relInTol = " << relInTol << endl;
    cout << "absInTol = " << absInTol << endl;
    cout << "maxInIter = " << maxInIter << endl;
    cout << "delta_e_rat_dt_tol = " << delta_e_rat_dt_tol << endl;
    cout << "delta_T_rat_dt_tol = " << delta_T_rat_dt_tol << endl;
    cout << "delta_Ye_dt_tol    = " << delta_Ye_dt_tol    << endl;
    cout << "limiter  = " << limiter << endl;
    cout << "closure  = " << closure << endl;
    cout << "update_limiter   = " << update_limiter << endl;
    cout << "update_planck    = " << update_planck << endl;
    cout << "update_rosseland = " << update_rosseland << endl;
    cout << "delta_temp = " << dT << endl;
    cout << "surface_average = " << surface_average << endl;
    cout << "underfac = " << underfac << endl;
    cout << "do_real_eos = " << do_real_eos << endl;
    cout << "do_multigroup = " << do_multigroup << endl;
    cout << "accelerate = " << accelerate << endl;
    cout << "verbose  = " << verbose << endl;
    if (RadTests::do_thermal_wave_cgs)
      cout << "do_thermal_wave_cgs = " << RadTests::do_thermal_wave_cgs << endl;
    if (RadTests::do_rad_sphere)
      cout << "do_rad_sphere = " << RadTests::do_rad_sphere << endl;
    if (SolverType == SingleGroupSolver) {
      cout << "SolverType = 0: SingleGroupSolver " << endl;
    }
    else if (SolverType == SGFLDSolver) {
      cout << "SolverType = 5: SGFLDSolver " << endl;
    }
    else if (SolverType == MGFLDSolver) {
      cout << "SolverType = 6: MGFLDSolver " << endl;
    }
    if (SolverType == MGFLDSolver || SolverType == SGFLDSolver) {
      cout << "rad_hydro_combined = " << rad_hydro_combined << endl;
      cout << "comoving = " << comoving << endl;
    }
    if (SolverType == MGFLDSolver) {
      cout << "fspace_advection_type = " << fspace_advection_type << endl;
    }
    if (SolverType == SGFLDSolver && comoving == 0) {
      cout << "Er_Lorentz_term = " << Er_Lorentz_term << endl;
    }
  }

  if (do_multigroup) {

    get_groups(verbose);

#ifdef NEUTRINO
    if (SolverType == MGFLDSolver && radiation_type == Neutrino) {
      // load opacities from (Burrows) table
      int iverb = (verbose >= 1 && ParallelDescriptor::IOProcessor());
      //      int iverb = 0;
      if (iverb) {
        cout << "reading opacity tables..." << endl;
      }
      FORT_INIT_OPACITY_TABLE(iverb);
    }
#endif
  }
  else {
    BL_FORT_PROC_CALL(CA_INITSINGLEGROUP, ca_initsinglegroup)(nGroups);

    // xnu is a dummy for single group
    xnu.resize(2, 1.0);
    nugroup.resize(1, 1.0);
  }

  // current implementation of the Radiation boundary condition reads
  // incoming flux information in the RadBndry constructor.  we just
  // set the boundary condition type here:

  Array<int> lo_bc(BL_SPACEDIM), hi_bc(BL_SPACEDIM);
  pp.getarr("lo_bc",lo_bc,0,BL_SPACEDIM);
  pp.getarr("hi_bc",hi_bc,0,BL_SPACEDIM);
  for (int i = 0; i < BL_SPACEDIM; i++) {
    rad_bc.setLo(i,lo_bc[i]);
    rad_bc.setHi(i,hi_bc[i]);
    if (verbose > 1 && ParallelDescriptor::IOProcessor()) {
      cout << "dimension " << i << " rad boundary conditions = "
            << lo_bc[i] << ", " << hi_bc[i] << endl;
    }
  }

  // size flux register arrays and persistent MultiFabs:

  int levels = parent->maxLevel() + 1; // maximum allowable number of levels

  flux_cons.resize(levels);
  flux_cons_old.resize(levels);
  flux_trial.resize(levels);

  dflux.resize(levels);

  delta_t_old.resize(levels, 0.0);

  delta_e_rat_level.resize(levels, 0.0);
  delta_T_rat_level.resize(levels, 0.0);
  delta_Ye_level.resize(   levels, 0.0);

  Density   = castro->Density;
  Xmom      = castro->Xmom;
  Eden      = castro->Eden;
  Eint      = castro->Eint;
  Temp      = castro->Temp;
  FirstSpec = castro->FirstSpec;
  FirstAux  = castro->FirstAux;
  NUM_STATE = castro->NUM_STATE;

  FORT_SETLAYOUT(Density, Xmom, Eden, Eint, Temp, FirstSpec, FirstAux, NUM_STATE);

  pp.query("flatten_pp_threshold", flatten_pp_threshold);
  pp.query("first_order_hydro", first_order_hydro);
  pp.query("pure_hydro", pure_hydro);

  BL_FORT_PROC_CALL(CA_INIT_RADHYDRO_PARS, ca_init_radhydro_pars)
      (fspace_advection_type, comoving, first_order_hydro, flatten_pp_threshold);
}

void Radiation::regrid(int level, const BoxArray& grids)
{
  BL_PROFILE("Radiation::Regrid");
  if (verbose > 0 && ParallelDescriptor::IOProcessor()) {
    cout << "Regridding radiation object at level " << level
	 << "..." << endl;
  }
  if (level > 0) {
    IntVect crse_ratio = parent->refRatio(level-1);

    if (flux_cons.defined(level))
      delete flux_cons.remove(level);
    flux_cons.set(level,
                  new FluxRegister(grids, crse_ratio, level, nGroups));
    flux_cons[level].setVal(0.0);

    // For deferred sync, flux_cons_old does not need to be defined here.
    // It will be set in the deferred_sync_setup routine.

    if (flux_trial.defined(level))
      delete flux_trial.remove(level);
    flux_trial.set(level,
                   new FluxRegister(grids, crse_ratio, level, nGroups));
    flux_trial[level].setVal(0.0);

  }

  if (dflux.defined(level))
    delete dflux.remove(level);
  dflux.set(level, new MultiFab(grids, 1, 0));

  // This array will not be used on the finest level.  I create it here,
  // though, in case a finer level is created before this level is next
  // regridded:

  if (verbose > 1 && ParallelDescriptor::IOProcessor()) {
    cout << "                                         done" << endl;
  }
}

void Radiation::close(int level)
{
  // Only appropriate when a level disappears, otherwise see regrid:
  if (level > parent->finestLevel()) {
    if (verbose > 0 && ParallelDescriptor::IOProcessor()) {
      cout << "Clearing radiation object at level " << level
            << "..." << endl;
    }
    if (flux_cons.defined(level))
      delete flux_cons.remove(level);
    if (flux_trial.defined(level))
      delete flux_trial.remove(level);

    // flux_cons_old is not deleted here because if it exists it still
    // has energy in it.  It will be deleted once it is finally used.

    if (dflux.defined(level))
      delete dflux.remove(level);
    if (verbose > 1 && ParallelDescriptor::IOProcessor()) {
      cout << "                                       done" << endl;
    }

    // When a level is closed, then we have to make sure there is
    // consistent flux information in level-1 to start the next
    // time step.  We need to do this if we are in the middle of
    // a level-2 timestep, or if there is no level-2.  In either
    // case the operations in init_flux are appropriate.

    BL_ASSERT(level > 0);
    int clev = level - 1;

    // Check in case more than one level was removed:
    if (clev == parent->finestLevel()) {
      int ncycle    = parent->nCycle(clev);
      int iteration = parent->levelSteps(clev);
      iteration = (clev > 0) ? iteration % ncycle : iteration;
      if (iteration > 0) {
	init_flux(clev, ncycle);
      }
    }
  }
}

void Radiation::restart(int level,
                        const std::string& dir,
                        std::istream& is)
{
  //
  // With the deferred sync option, we have to restart the rad flux register
  //

  std::string Path, aString;

  do {
    is >> aString;
    if (aString.find("delta_e_rat") == 0) {
      is >> delta_e_rat_level[level];
    }
    else if (aString.find("delta_Ye") == 0) {
      is >> delta_Ye_level[level];    
    }
    else if (aString.find("delta_T_rat") == 0) {
      is >> delta_T_rat_level[level];          
    }
    else { 
      Path = aString;
    }
  } while (Path.empty());

  //
  // Read flux register only if present in the chkfile.
  //
  std::string Flag;
  is >> Flag;

  if (Flag == "Present") {
    BL_ASSERT(level > 0);
    //
    // Read delta_t associated with this flux information.
    //
    is >> delta_t_old[level-1];
    //
    // Prepend the name of the chkfile directory.
    //
    std::string FullPathName = dir;
    if (!dir.empty() && dir[dir.length()-1] != '/')
        FullPathName += '/';
    FullPathName += Path;
    //
    // Input conservation flux register.
    //
    flux_cons_old.set(level, new FluxRegister());
    flux_cons_old[level].read(FullPathName, is);
  }
}

void Radiation::checkPoint(int level,
                           const std::string& dir,
                           std::ostream&  os,
                           VisMF::How     how)
{
  //
  // With the deferred sync option, we have to restart the rad flux register
  //

  char buf[64];

  //
  // Write deltas to header for timestep control.
  //
  if (ParallelDescriptor::IOProcessor()) {
    int oldprec = os.precision(20);
    sprintf(buf, "delta_e_rat_level[%d]= ", level);
    std::string DeltaString = buf;
    os << DeltaString << delta_e_rat_level[level] << '\n';
    sprintf(buf, "delta_T_rat_level[%d]= ", level);
    DeltaString = buf;
    os << DeltaString << delta_T_rat_level[level] << '\n';
    sprintf(buf, "delta_Ye_level[%d]= ", level);
    DeltaString = buf;
    os << DeltaString << delta_Ye_level[level] << '\n';
    os.precision(oldprec);
  }

  // Path name construction stolen from AmrLevel::checkPoint

  sprintf(buf, "Level_%d", level);
  std::string Level = buf;

  //
  // Write name of conservation flux register to header.
  //
  sprintf(buf, "/RadFlux");

  std::string PathNameInHeader = Level;
  PathNameInHeader += buf;
  if (ParallelDescriptor::IOProcessor()) {
    os << PathNameInHeader;
  }

  if (flux_cons_old.defined(level)) {
    //
    // Conservation flux register exists.
    //
    if (ParallelDescriptor::IOProcessor()) {
      BL_ASSERT(level > 0);
      int oldprec = os.precision(20);
      os << " Present  " << delta_t_old[level-1] << '\n';
      os.precision(oldprec);
    }
    //
    // This is the full pathname for the written FluxRegister.
    //
    std::string FullPathName = dir;
    if (!FullPathName.empty() &&
        FullPathName[FullPathName.length()-1] != '/') {
      FullPathName += '/';
    }
    FullPathName += Level;
    FullPathName += buf;
    //
    // Output conservation flux register.
    //
    flux_cons_old[level].write(FullPathName, os /* , how */ );
  }
  else {
    //
    // Conservation flux register does not exist.
    //
    if (ParallelDescriptor::IOProcessor()) {
      os << " Absent\n";
    }
  }
}


void Radiation::analytic_solution(int level)
{
  if (!use_analytic_solution)
    return;

  Castro *castro        = dynamic_cast<Castro*>(&parent->getLevel(level));
  const BoxArray& grids = castro->boxArray();

  Real time = castro->get_state_data(Test_Type).curTime();

  if (RadTests::do_thermal_wave_cgs) {

    ParmParse pp("radiation");

    Real rhocv, Q;
    pp.get("thermal_wave_rhocv", rhocv);
    pp.get("thermal_wave_Eexp", Q);
    Q /= rhocv;

    Real a = (16.0 * sigma) / (3.0 * const_kappa_r[0]) / rhocv;
    Real p = kappa_r_exp_n[0] + 3.0;

    Real pfac = exp(lgamma(2.5 + 1.0 / p) - lgamma(1.0 + 1.0 / p) - lgamma(1.5));

    Real xi0  = pow((3.0 * p + 2.0) / (pow(2.0, p - 1.0) * p *
                                      pow(M_PI, p)),
                   1.0 / (3.0 * p + 2.0)) *
           pow(pfac, p / (3.0 * p + 2.0));
    Real xf = xi0 * pow(a * pow(Q,p) * time, 1.0 / (3.0 * p + 2.0));

    Real Tbar = Q / pow(xf,3);
    // Factor of (4 pi / 3) ignored in Tbar because it is cancelled in the following
    // In ZelDovich & Raizer, the \sqrt in the equation following 10.44 should 
    //    /. 
    Real Tc = pow(xi0,3)*pow((p*xi0*xi0/(6.*p+4.)),1./p)*Tbar;

    //cout << xi0 << " " << pfac << endl;
    if (verbose > 1 && level == 0 && ParallelDescriptor::IOProcessor()) {
      cout << "Front velocity = " << xf / (time * (3.0 * p + 2.0)) 
	   << "   Front position = " << xf << endl;
    }

    MultiFab& S_new = castro->get_new_data(State_Type);
    MultiFab& T_new = castro->get_new_data(Test_Type);
    
    MultiFab temp(grids,1,0);
    
    {
      MultiFab fkp(grids,1,0);
      
      get_frhoe(temp, S_new);
      get_planck_and_temp(fkp, temp, S_new);
    }
 
    const Geometry& geom = parent->Geom(level);
    const Real *dx = geom.CellSize();

    for (MFIter ti(T_new); ti.isValid(); ++ti) {
      int i = ti.index();
      const Box &reg = grids[i];

      RealBox gridloc = RealBox(grids[i], geom.CellSize(), geom.ProbLo());

      FORT_ANATW2(T_new[ti].dataPtr(), dimlist(reg),
		  temp[ti].dataPtr(),
		  p, xf, Tc, dx, gridloc.lo(), reg.loVect());
    }

    return;
  }
}

void Radiation::post_init(int level)
{
    return;
}

void Radiation::pre_timestep(int level)
{
  BL_PROFILE("Radiation::pre_timestep");
  int fine_level = parent->finestLevel();
  int ncycle     = parent->nCycle(level);

  static int done = 0;
  if (level < fine_level) {
      // For deferred sync, we may have moved flux_cons into flux_cons_old
      // and not rebuilt flux_cons, so check for that here.
      
      int flevel = level + 1;
      if (!flux_cons.defined(flevel)) {
	  const BoxArray& grids = parent->getLevel(flevel).boxArray();
	  const IntVect& crse_ratio = parent->refRatio(level);
	  flux_cons.set(flevel,
			new FluxRegister(grids, crse_ratio, flevel, nGroups));
	  flux_cons[flevel].setVal(0.0);
      }
  }
  
  // If we aren't doing a multilevel solve, we still need to initialize
  // dflux and load the flux registers at each level.  For a single-level
  // calculation this only needs to be done once per run, whether
  // at initialization or at restart.  We can't trust iteration to
  // tell us, since at restart iteration is not 0.
  int iteration = parent->levelSteps(level);
  iteration = (level > 0) ? iteration % ncycle : done;
  if (level < fine_level || iteration == 0) {
      init_flux(level, ncycle);
      done = 1;
  }
}

void Radiation::init_flux(int level, int ncycle)
{
  BL_PROFILE("Radiation::init_flux");
  if (verbose > 0 && ParallelDescriptor::IOProcessor()) {
    cout << "Radiation flux initialization at level " << level
	 << "..." << endl;
  }

  int fine_level = parent->finestLevel();

  dflux[level].setVal(0.0);

  if (level < fine_level) {
      for (OrientationIter face; face; ++face) {
	  Orientation ori = face();
	  flux_cons[level+1][ori].setVal(0.0);
      }
  }

  if (verbose > 1 && ParallelDescriptor::IOProcessor()) {
    cout << "                                           done" << endl;
  }
}

void Radiation::clear_internal_borders(FluxRegister& fr)
{
  int ncomp = fr.nComp();

  for (int dir = 0; dir < BL_SPACEDIM; dir++) {
    Orientation lo(dir, Orientation::low);
    Orientation hi(dir, Orientation::high);

    const BoxArray& grids = fr.boxes();
    for (int j = 0; j < grids.size(); j++) {
      Box jbox = BoxLib::bdryHi(grids[j], dir);
      for (FabSetIter fsi(fr[lo]); fsi.isValid(); ++fsi) {
	Box ibox = fr[lo][fsi].box();
	ibox &= jbox;
	if (ibox.ok()) {
	  fr[lo][fsi].setVal(0.0, ibox, 0, ncomp);
	}
      }
      jbox = BoxLib::bdryLo(grids[j], dir);
      for (FabSetIter fsi(fr[hi]); fsi.isValid(); ++fsi) {
	Box ibox = fr[hi][fsi].box();
	ibox &= jbox;
	if (ibox.ok()) {
	  fr[hi][fsi].setVal(0.0, ibox, 0, ncomp);
	}
      }
    }
  }
}


// Overwrites temperature with exchange term, exch = temp on input:

// This version used by single group, multigroup

void Radiation::compute_exchange(MultiFab& exch,
                                 MultiFab& Er,
                                 MultiFab& fkp, int igroup)
{
  const BoxArray& grids = exch.boxArray();

  for (MFIter exi(exch); exi.isValid(); ++exi) {
    int i = exi.index();
    const Box& reg = grids[i];
    const Box& ebox = Er[exi].box();
    FORT_CEXCH(exch[exi].dataPtr(), dimlist(reg),
	       Er[exi].dataPtr(0), dimlist(ebox),
	       fkp[exi].dataPtr(), sigma, c);
  }
}

void Radiation::compute_eta(MultiFab& eta, MultiFab& etainv,
                            MultiFab& state, MultiFab& temp,
                            MultiFab& fkp, MultiFab& Er,
                            Real delta_t, Real c,
                            Real underrel, int lag_planck, int igroup)
{
  const BoxArray& grids = eta.boxArray();

  // violating these just needs a fancier Fortran call:
  BL_ASSERT(eta.nGrow()   == 0);
  BL_ASSERT(temp.nGrow()  == 0);
  BL_ASSERT(fkp.nGrow()   == 0);

  for (MFIter mfi(eta); mfi.isValid(); ++mfi) {
    int i = mfi.index();
    const Box& reg = grids[i];

    if (lag_planck) {
      eta[mfi].copy(fkp[mfi]);
    }
    else {
      // This is the only case where we need a direct call for
      // Planck mean as a function of temperature.
      temp[mfi].plus(dT, 0, 1);
      get_planck_from_temp(eta[mfi], temp[mfi], state[mfi], reg, igroup);
      temp[mfi].plus(-dT, 0, 1);
    }

    // This is the only case in the radiation algorithm (outside of
    // Jeff's branch) where we need to return c_v.
    Fab c_v(reg);
    get_c_v(c_v, temp[mfi], state[mfi], reg);

    const Box& sbox = state[mfi].box();
    const Box& ebox = Er[mfi].box();
    FORT_CETA2(eta[mfi].dataPtr(), etainv[mfi].dataPtr(), dimlist(reg),
	       state[mfi].dataPtr(Density), dimlist(sbox),
	       temp[mfi].dataPtr(), c_v.dataPtr(),
	       fkp[mfi].dataPtr(), Er[mfi].dataPtr(igroup), dimlist(ebox),
	       dT, delta_t, sigma, c,
	       underrel, lag_planck);
  }
}

void Radiation::internal_energy_update(Real& relative, Real& absolute,
                                       MultiFab& frhoes,
                                       MultiFab& frhoem,
                                       MultiFab& eta,
                                       MultiFab& etainv,
                                       MultiFab& dflux_old,
                                       MultiFab& dflux_new,
                                       MultiFab& exch,
                                       Real delta_t)
{
  BL_PROFILE("Radiation::internal_energy_update");
  const BoxArray& grids = frhoes.boxArray();

  relative = 0.0;
  absolute = 0.0;
  Real theta = 1.0;

  for (MFIter mfi(eta); mfi.isValid(); ++mfi) {
    int i = mfi.index();
    const Box &reg = grids[i];

    FORT_CEUP(relative, absolute, frhoes[mfi].dataPtr(), dimlist(reg),
	      frhoem[mfi].dataPtr(), eta[mfi].dataPtr(), etainv[mfi].dataPtr(),
	      dflux_old[mfi].dataPtr(), dflux_new[mfi].dataPtr(),
	      exch[mfi].dataPtr(), delta_t, theta);
  }

  ParallelDescriptor::ReduceRealMax(relative);
  ParallelDescriptor::ReduceRealMax(absolute);
}

void Radiation::internal_energy_update(Real& relative, Real& absolute,
				       MultiFab& frhoes,
				       MultiFab& frhoem,
				       MultiFab& eta,
				       MultiFab& etainv,
				       MultiFab& dflux_old,
				       MultiFab& dflux_new,
				       MultiFab& exch,
				       MultiFab& Dterm,
				       Real delta_t)
{
  BL_PROFILE("Radiation::internal_energy_update_d");
  const BoxArray& grids = frhoes.boxArray();

  relative = 0.0;
  absolute = 0.0;
  Real theta = 1.0;

  for (MFIter mfi(eta); mfi.isValid(); ++mfi) {
    int i = mfi.index();
    const Box &reg = grids[i];

    FORT_CEUPDTERM(relative, absolute, frhoes[mfi].dataPtr(), dimlist(reg),
		   frhoem[mfi].dataPtr(), eta[mfi].dataPtr(), etainv[mfi].dataPtr(),
		   dflux_old[mfi].dataPtr(), dflux_new[mfi].dataPtr(),
		   exch[mfi].dataPtr(), Dterm[mfi].dataPtr(), delta_t, theta);
  }

  ParallelDescriptor::ReduceRealMax(relative);
  ParallelDescriptor::ReduceRealMax(absolute);
}

void Radiation::nonconservative_energy_update(Real& relative, Real& absolute,
                                              MultiFab& frhoes,
                                              MultiFab& frhoem,
                                              MultiFab& eta,
                                              MultiFab& etainv,
                                              MultiFab& Er_new,
                                              MultiFab& dflux_old,
                                              MultiFab& dflux_new,
                                              MultiFab& temp,
                                              MultiFab& fkp,
                                              MultiFab& state,
                                              Real delta_t)
{
  const BoxArray& grids = frhoes.boxArray();

  relative = 0.0;
  absolute = 0.0;
  Real theta = 1.0;

  for (MFIter mfi(eta); mfi.isValid(); ++mfi) {
    int i = mfi.index();
    const Box &reg = grids[i];

    Fab c_v(reg);
    get_c_v(c_v, temp[mfi], state[mfi], reg);

    const Box& sbox = state[mfi].box();
    const Box& ebox = Er_new[mfi].box();
    FORT_NCEUP(relative, absolute, frhoes[mfi].dataPtr(), dimlist(reg),
	       frhoem[mfi].dataPtr(), eta[mfi].dataPtr(), etainv[mfi].dataPtr(),
	       Er_new[mfi].dataPtr(), dimlist(ebox),
               dflux_old[mfi].dataPtr(), dflux_new[mfi].dataPtr(),
	       temp[mfi].dataPtr(), fkp[mfi].dataPtr(), c_v.dataPtr(),
	       state[mfi].dataPtr(), dimlist(sbox),
               sigma, c, delta_t, theta);
  }

  ParallelDescriptor::ReduceRealMax(relative);
  ParallelDescriptor::ReduceRealMax(absolute);
}

void Radiation::state_update(MultiFab& state,
                             MultiFab& frhoes,
			     MultiFab& temp)
{
  const BoxArray& grids = frhoes.boxArray();

  for (MFIter si(state); si.isValid(); ++si) {
    int i = si.index();
    const Box& sbox = state[si].box();
    const Box& reg = grids[i];
    FORT_CETOT(state[si].dataPtr(), dimlist(sbox), dimlist(reg),
	       frhoes[si].dataPtr());

    if (do_real_eos == 0) {

      temp[si].copy(frhoes[si]);

      BL_FORT_PROC_CALL(CA_COMPUTE_TEMP_GIVEN_CV, ca_compute_temp_given_cv)
	(reg.loVect(), reg.hiVect(), 
	 BL_TO_FORTRAN(temp[si]), 
	 BL_TO_FORTRAN(state[si]),
	 &const_c_v[0], &c_v_exp_m[0], &c_v_exp_n[0]);

      state[si].copy(temp[si],0,Temp,1);
    }
  }
}


void Radiation::extrapolateBorders(MultiFab& f, int indx)
{
  BL_PROFILE("Radiation::extrapolateBorders");

  BL_ASSERT(f.nGrow() >= 1);

  for(MFIter mfi(f); mfi.isValid(); ++mfi) {
    int i = mfi.index();
    const Box& fbox = f[mfi].box();
    const Box& reg  = f.box(i);

    FORT_BEXTRP(f[mfi].dataPtr(indx), dimlist(fbox), dimlist(reg));
  }
}


void Radiation::getBndryData(RadBndry& bd, MultiFab& Er,
                             Real time, int level)
{
  BL_PROFILE("Radiation::getBndryData");
  Castro      *castro = (Castro*)&parent->getLevel(level);
  const BoxArray& grids = castro->boxArray();

  if (level == 0) {
    bd.setBndryValues(Er, Rad, 0, 1, rad_bc); // Rad=0
  }
  else {
    BoxArray cgrids(grids);
    IntVect crse_ratio = parent->refRatio(level-1);
    cgrids.coarsen(crse_ratio);
    BndryRegister crse_br(cgrids, 0, 1, 1, 1);
    crse_br.setVal(1.0e30);
    filBndry(crse_br, level-1, time);

    bd.setBndryValues(crse_br, 0, Er, Rad, 0, 1, crse_ratio, rad_bc);
  }

  // We do this last, in case Er has ghost cells which get written into
  // the boundary values:

  bd.setTime(time);

  bd.setBndryFluxConds(rad_bc);
}

void Radiation::getBndryDataMG(MGRadBndry& mgbd, MultiFab& Er,
			       Real time, int level)
{
  BL_PROFILE("Radiation::getBndryDataMG");
  Castro *castro = dynamic_cast<Castro*>(&parent->getLevel(level));
  const BoxArray& grids = castro->boxArray();

  if(level == 0) {
    mgbd.setBndryValues(Er, 0, 0, Radiation::nGroups, rad_bc);
  }
  else {
    BoxArray cgrids(grids);
    IntVect crse_ratio = parent->refRatio(level-1);
    cgrids.coarsen(crse_ratio);
    BndryRegister crse_br(cgrids, 0, 1, 1, Radiation::nGroups);
    crse_br.setVal(1.0e30);
    filBndry(crse_br, level-1, time);

    mgbd.setBndryValues(crse_br, 0, Er, 0,
                        0, Radiation::nGroups, crse_ratio, rad_bc);
  }

  mgbd.setTime(time);
  mgbd.setBndryFluxConds(rad_bc);
}

void Radiation::getBndryDataMG_ga(MGRadBndry& mgbd, MultiFab& Er, int level)
{
  BL_PROFILE("Radiation::getBndryDataMG_ga");
  Castro *castro = dynamic_cast<Castro*>(&parent->getLevel(level));
  const BoxArray& grids = castro->boxArray();

  if(level == 0) {
    mgbd.setBndryValues(Er, 0, 0, 1, rad_bc);
  }
  else {
    BoxArray cgrids(grids);
    IntVect crse_ratio = parent->refRatio(level-1);
    cgrids.coarsen(crse_ratio);
    BndryRegister crse_br(cgrids, 0, 1, 1, 1);
    crse_br.setVal(0.0);

    mgbd.setBndryValues(crse_br, 0, Er, 0,
                        0, 1, crse_ratio, rad_bc);
  }

  mgbd.setBndryFluxConds(rad_bc);
}

void Radiation::filBndry(BndryRegister& bdry, int level, Real time)
{
  BL_PROFILE("Radiation::filBndry");
  // in this routine "level" is the coarse level

  Castro      *castro = (Castro*)&parent->getLevel(level);
  const BoxArray& grids = castro->boxArray();
  const Geometry& geom  = parent->Geom(level);

  Real old_time = castro->get_state_data(Rad_Type).prevTime();
  Real new_time = castro->get_state_data(Rad_Type).curTime();
  Real eps = (new_time > old_time) ? 0.001*(new_time - old_time) : 1.0;

  BL_ASSERT( (time > old_time-eps) && (time < new_time + eps));

  MultiFab& S_new = castro->get_new_data(Rad_Type);
  // the next line is OK even if S_old is not defined yet
  MultiFab& S_old = castro->get_old_data(Rad_Type);

  if (!geom.isAnyPeriodic()) {
    int n_ghost = 0;

    if (time > new_time - eps) {
      //bdry.copyFrom(S_new, n_ghost, 0, 0, 1);
      bdry.copyFrom(S_new, n_ghost, 0, 0, Radiation::nGroups);
    }
    else if (time < old_time + eps) {
      //bdry.copyFrom(S_old, n_ghost, 0, 0, 1);
      bdry.copyFrom(S_old, n_ghost, 0, 0, Radiation::nGroups);
    }
    else {
      Real a = (new_time - time) / (new_time - old_time);
      Real b = (time - old_time) / (new_time - old_time);
      //bdry.linComb(a, S_old, 0, b, S_new, 0, 0, 1, n_ghost);
      bdry.linComb(a, S_old, 0, b, S_new, 0, 0, Radiation::nGroups, n_ghost);
    }
  }
  else {

    // older version, ghost cell seems unnecessary (perhaps a holdover from
    // when we put boundary conditions in ghost cells):

    // (later) resurrected because ghost cell needed for periodic case:

    int need_old_data = (time <= new_time - eps);
    int need_new_data = (time >= old_time + eps);

    int n_grow = 1;

    MultiFab sold_tmp;
    MultiFab snew_tmp;

    if (need_old_data) {
      sold_tmp.define(grids, 1, n_grow, Fab_allocate);
      sold_tmp.setVal(0.0); // need legal numbers for linComb below
      sold_tmp.copy(S_old, Rad, 0, 1);
      sold_tmp.FillBoundary();
      geom.FillPeriodicBoundary(sold_tmp, false);
    }

    if (need_new_data) {
      snew_tmp.define(grids, 1, n_grow, Fab_allocate);
      snew_tmp.setVal(0.0); // need legal numbers for linComb below
      snew_tmp.copy(S_new, Rad, 0, 1);
      snew_tmp.FillBoundary();
      geom.FillPeriodicBoundary(snew_tmp, false);
    }

    int n_ghost = 1;

    if (time > new_time - eps) {
      bdry.copyFrom(snew_tmp, n_ghost, 0, 0, 1);
    }
    else if (time < old_time + eps) {
      bdry.copyFrom(sold_tmp, n_ghost, 0, 0, 1);
    }
    else {
      Real a = (new_time - time) / (new_time - old_time);
      Real b = (time - old_time) / (new_time - old_time);
      bdry.linComb(a, sold_tmp, 0, b, snew_tmp, 0, 0, 1, n_ghost);
    }
  }
}

void Radiation::get_frhoe(Fab& frhoe,
                          Fab& state,
                          const Box& reg)
{
    BL_ASSERT(reg == frhoe.box());
    const Box& sbox = state.box();
    FORT_CFRHOE(frhoe.dataPtr(), dimlist(reg), state.dataPtr(), dimlist(sbox));
}

void Radiation::get_c_v(Fab& c_v, Fab& temp, Fab& state,
                        const Box& reg)
{
    BL_ASSERT(reg == c_v.box());

    if (do_real_eos == 1) {
	BL_FORT_PROC_CALL(CA_COMPUTE_C_V,ca_compute_c_v)
	    (reg.loVect(), reg.hiVect(),
	     BL_TO_FORTRAN(c_v), BL_TO_FORTRAN(temp), BL_TO_FORTRAN(state));
    }
    else if (do_real_eos == 0) {
	if (c_v_exp_m[0] == 0.0 && c_v_exp_n[0] == 0.0) {
	    c_v.setVal(const_c_v[0]);
	}
	else {
	    FORT_GCV(dimlist(reg),
		     c_v.dataPtr(), dimlist(c_v.box()),
		     temp.dataPtr(), dimlist(temp.box()),
		     const_c_v.dataPtr(),
		     c_v_exp_m.dataPtr(), c_v_exp_n.dataPtr(),
		     prop_temp_floor.dataPtr(),
		     state.dataPtr(), dimlist(state.box()));
	}
    }
    else {
	BoxLib::Error("ERROR Radiation::get_c_v  do_real_eos < 0");
    }
}

// temp contains frhoe on input:
void Radiation::get_planck_and_temp(Fab& fkp, Fab& temp,
                                    Fab& state, const Box& reg,
				    int igroup, Real delta_t)
{
  BL_ASSERT(reg == fkp.box());
  BL_ASSERT(reg == temp.box());

  const Box& sbox = state.box();

  if (do_real_eos > 0) {
    BL_FORT_PROC_CALL(CA_COMPUTE_TEMP_GIVEN_RHOE, ca_compute_temp_given_rhoe)
      (reg.loVect(), reg.hiVect(), BL_TO_FORTRAN(temp), BL_TO_FORTRAN(state));
  }
  else if (do_real_eos == 0) {
    FORT_GTEMP(temp.dataPtr(), dimlist(reg),
	       const_c_v.dataPtr(),
	       c_v_exp_m.dataPtr(), c_v_exp_n.dataPtr(),
	       state.dataPtr(), dimlist(sbox));
  }
  else {
    BoxLib::Error("ERROR Radiation::get_planck_and_temp  do_real_eos < 0");
  }

  if (use_opacity_table_module) {
    BL_FORT_PROC_CALL(CA_COMPUTE_PLANCK, ca_compute_planck)
	(reg.loVect(), reg.hiVect(), BL_TO_FORTRAN(fkp), BL_TO_FORTRAN(state));
  }
  else {
    FORT_FKPN(fkp.dataPtr(), dimlist(reg),
	      const_kappa_p.dataPtr(),
	      kappa_p_exp_m.dataPtr(), kappa_p_exp_n.dataPtr(),
	      kappa_p_exp_p.dataPtr(), nugroup[igroup],
	      prop_temp_floor.dataPtr(),
	      temp.dataPtr(), state.dataPtr(), dimlist(sbox));
  }

  int numfloor = 0;
  FORT_NFLOOR(temp.dataPtr(), dimlist(reg), dimlist(reg),
              numfloor, temp_floor, temp.nComp());
  if (verbose > 2 && numfloor > 0) {
    cout << numfloor << " temperatures raised to floor" << endl;
  }
}

void Radiation::get_rosseland_and_temp(Fab& kappa_r,
                                       Fab& temp,
                                       Fab& state,
                                       const Box& reg,
				       int igroup)
{
  BL_ASSERT(reg == kappa_r.box());
  BL_ASSERT(reg == temp.box());
  BL_ASSERT(kappa_r.nComp() == Radiation::nGroups);

  const Box& kbox = kappa_r.box();
  const Box& sbox = state.box();

  if (do_real_eos > 0) {
    BL_FORT_PROC_CALL(CA_COMPUTE_TEMP_GIVEN_RHOE, ca_compute_temp_given_rhoe)
      (reg.loVect(), reg.hiVect(), BL_TO_FORTRAN(temp), BL_TO_FORTRAN(state));
  }
  else if (do_real_eos == 0) {
    FORT_GTEMP(temp.dataPtr(), dimlist(reg),
	       const_c_v.dataPtr(),
	       c_v_exp_m.dataPtr(), c_v_exp_n.dataPtr(),
	       state.dataPtr(), dimlist(sbox));
  }
  else {
    BoxLib::Error("ERROR Radiation::get_rosseland_and_temp  do_real_eos < 0");
  }

  state.copy(temp,0,Temp,1);

  if (use_opacity_table_module) {
    BL_FORT_PROC_CALL(CA_COMPUTE_ROSSELAND, ca_compute_rosseland)
      (reg.loVect(), reg.hiVect(), BL_TO_FORTRAN(kappa_r), BL_TO_FORTRAN(state));
  }
  else if (const_scattering[0] > 0.0) {
    FORT_ROSSE1S(kappa_r.dataPtr(igroup), dimlist(kbox), dimlist(reg),
		 const_kappa_r.dataPtr(),
		 kappa_r_exp_m.dataPtr(), kappa_r_exp_n.dataPtr(),
		 kappa_r_exp_p.dataPtr(), 
		 const_scattering.dataPtr(),
		 scattering_exp_m.dataPtr(), scattering_exp_n.dataPtr(),
		 scattering_exp_p.dataPtr(), 
		 nugroup[igroup],
		 prop_temp_floor.dataPtr(), kappa_r_floor,
		 temp.dataPtr(), state.dataPtr(), dimlist(sbox));
  }
  else {
    FORT_ROSSE1(kappa_r.dataPtr(igroup), dimlist(kbox), dimlist(reg),
		const_kappa_r.dataPtr(),
		kappa_r_exp_m.dataPtr(), kappa_r_exp_n.dataPtr(),
		kappa_r_exp_p.dataPtr(), nugroup[igroup],
		prop_temp_floor.dataPtr(), kappa_r_floor,
		temp.dataPtr(), state.dataPtr(), dimlist(sbox));
  }
}

// temp contains temp on input:

void Radiation::get_planck_from_temp(Fab& fkp, Fab& temp,
                                     Fab& state, const Box& reg,
				     int igroup)
{
  BL_ASSERT(reg == fkp.box());
  BL_ASSERT(reg == temp.box());

  const Box& sbox = state.box();

  if (use_opacity_table_module) {
    BL_FORT_PROC_CALL(CA_COMPUTE_PLANCK, ca_compute_planck)
      (reg.loVect(), reg.hiVect(), BL_TO_FORTRAN(fkp), BL_TO_FORTRAN(state));
  }
  else {
    FORT_FKPN(fkp.dataPtr(), dimlist(reg),
	      const_kappa_p.dataPtr(),
	      kappa_p_exp_m.dataPtr(), kappa_p_exp_n.dataPtr(),
	      kappa_p_exp_p.dataPtr(), nugroup[igroup],
	      prop_temp_floor.dataPtr(),
	      temp.dataPtr(), state.dataPtr(), dimlist(sbox));
  }
}

void Radiation::get_rosseland_from_temp(Fab& kappa_r,
                                        Fab& temp,
                                        Fab& state,
                                        const Box& reg,
					int igroup)
{
  BL_ASSERT(reg == temp.box());
  BL_ASSERT(kappa_r.nComp() == Radiation::nGroups);

  const Box& kbox = kappa_r.box();
  const Box& sbox = state.box();

  state.copy(temp,0,Temp,1);

  if (use_opacity_table_module) {
    BL_FORT_PROC_CALL(CA_COMPUTE_ROSSELAND, ca_compute_rosseland)
	(reg.loVect(), reg.hiVect(), BL_TO_FORTRAN(kappa_r), BL_TO_FORTRAN(state));
  }
  else if (const_scattering[0] > 0.0) {
    FORT_ROSSE1S(kappa_r.dataPtr(igroup), dimlist(kbox), dimlist(reg),
		 const_kappa_r.dataPtr(),
		 kappa_r_exp_m.dataPtr(), kappa_r_exp_n.dataPtr(),
		 kappa_r_exp_p.dataPtr(), 
		 const_scattering.dataPtr(),
		 scattering_exp_m.dataPtr(), scattering_exp_n.dataPtr(),
		 scattering_exp_p.dataPtr(), 
		 nugroup[igroup],
		 prop_temp_floor.dataPtr(), kappa_r_floor,
		 temp.dataPtr(), state.dataPtr(), dimlist(sbox));
  }
  else {
    FORT_ROSSE1(kappa_r.dataPtr(igroup), dimlist(kbox), dimlist(reg),
		const_kappa_r.dataPtr(),
		kappa_r_exp_m.dataPtr(), kappa_r_exp_n.dataPtr(),
		kappa_r_exp_p.dataPtr(), nugroup[igroup],
		prop_temp_floor.dataPtr(), kappa_r_floor,
		temp.dataPtr(), state.dataPtr(), dimlist(sbox));
  }
}

void Radiation::get_frhoe(MultiFab& frhoe,
                          MultiFab& state)
{
  const BoxArray& grids = frhoe.boxArray();
  for (MFIter si(state); si.isValid(); ++si) {
    int i = si.index();
    get_frhoe(frhoe[si], state[si], grids[i]);
  }
}

// temp contains frhoe on input:

void Radiation::get_planck_and_temp(MultiFab& fkp,
                                    MultiFab& temp,
                                    MultiFab& state,
				    int igroup, Real delta_t)
{
  BL_PROFILE("Radiation::get_planck_and_temp");
  const BoxArray& grids = fkp.boxArray();
  for (MFIter si(state); si.isValid(); ++si) {
    int i = si.index();
    get_planck_and_temp(fkp[si], temp[si], state[si], grids[i],
			igroup, delta_t);
  }
}

// Uses filPatch to fill state data in a ghost cell around each grid
// so that kappa_r can be constructed everywhere.  Values across
// physical boundaries will not be used, however.

void Radiation::get_rosseland(MultiFab& kappa_r,
                              AmrLevel* castro,
			      int igroup)
{
  BL_PROFILE("Radiation::get_rosseland");

  BL_ASSERT(kappa_r.nGrow() == 1);

  if(Radiation::nGroups == 0) kappa_r.setVal(0.);

  Real time = castro->get_state_data(State_Type).curTime();
  MultiFab& S_new = castro->get_new_data(State_Type);
  int nstate = S_new.nComp();

  FillPatchIterator fpi(*castro, S_new, 1, time, State_Type, 0, nstate);
  for( ; fpi.isValid(); ++fpi) {

    FArrayBox &state = fpi();

    const Box& reg = kappa_r[fpi].box();

    Fab temp(reg);
    get_frhoe(temp, state, reg);

    get_rosseland_and_temp(kappa_r[fpi], temp, state, reg, igroup);
  }
}

// Only updates interior cells of current level, leaves kappa_r unchanged
// in ghost cells bordering coarse grids or physical boundaries.

void Radiation::update_rosseland_from_temp(MultiFab& kappa_r,
                                           MultiFab& temp,
                                           MultiFab& state,
                                           const Geometry& geom,
					   int igroup)
{

  BL_ASSERT(kappa_r.nGrow() == 1);
  BL_ASSERT(temp.nGrow()    == 0);

  const BoxArray& grids = kappa_r.boxArray();
  for (MFIter si(state); si.isValid(); ++si) {
    int i = si.index();
    get_rosseland_from_temp(kappa_r[si], temp[si], state[si], grids[i], igroup);
  }

  kappa_r.FillBoundary();
  if (geom.isAnyPeriodic()) {
    geom.FillPeriodicBoundary(kappa_r, false);
  }
}

void Radiation::deferred_sync_setup(int crse_level)
{
  BL_PROFILE("Radiation::deferred_sync_setup");
  if (verbose && ParallelDescriptor::IOProcessor()) {
    cout << "Radiation deferred sync setup for coarse level " << crse_level << "..." << endl;
  }

  // sync is only called if this is not the finest level

  int level = crse_level + 1; 

  const Orientation ori(0, Orientation::low);

  int do_swap = (flux_cons_old.defined(level) &&
                 (flux_cons_old[level].coarsenedBoxes() ==
                  flux_cons[level].coarsenedBoxes()) &&
                 (flux_cons_old[level][ori].DistributionMap() ==
                  flux_cons[level][ori].DistributionMap()));

  if (do_swap) {
    // flux_cons_old exists and is based on same grids as flux_cons

    FluxRegister *tmp = flux_cons_old.remove(level);
    flux_cons_old.set(level, flux_cons.remove(level));
    flux_cons.set(level, tmp);
  }
  else {
    if (flux_cons_old.defined(level))
      delete flux_cons_old.remove(level);
    flux_cons_old.set(level, flux_cons.remove(level));

    // leave flux_cons undefined because we may be about to regrid anyway
  }

  delta_t_old[crse_level] = parent->dtLevel(crse_level);

  if (verbose && ParallelDescriptor::IOProcessor()) {
    if (do_sync) {
      cout << "                                                   done"
           << endl;
    }
    else {
      cout << "                                                   zeroed out"
           << endl;
    }
  }
}

void Radiation::deferred_sync(int level, MultiFab& rhs, int indx)
{
  int fine_level = parent->finestLevel();
  Castro *castro = dynamic_cast<Castro*>(&parent->getLevel(level));
  const BoxArray& grids = castro->boxArray();
  Real delta_t = parent->dtLevel(level);

  if (level < parent->maxLevel() &&
      flux_cons_old.defined(level+1)) {

    FluxRegister& sync_flux = flux_cons_old[level+1];

    if (indx == 0) {
      // clean up fine-fine interfaces (does all groups at once)
      clear_internal_borders(sync_flux);
    }

    Real scale = delta_t_old[level] / delta_t;
    if (!do_sync) scale = 0.0;

    sync_flux.Reflux(rhs, scale, indx, 0, 1, castro->Geom());

    // If there is a sync source from a still finer level,
    // test for an intersection that will not be caught by
    // an intermediate level and print a message to standard
    // output if one is found.

    if (level+1 < parent->maxLevel() &&
        flux_cons_old.defined(level+2)) {

      BoxArray fgrids;

      if (level+1 <= fine_level) {

        // We know here that level+1 still exists.
        // (If it does not exist, fgrids remains empty. The operations
        // used below are well-defined for an empty BoxArray.)

        fgrids = parent->getLevel(level+1).boxArray();
      }

      const IntVect& rat = sync_flux.refRatio();
      BoxArray old_boxes = sync_flux.coarsenedBoxes();
      old_boxes.refine(rat); // now refined to level+1

      // If level+1 grids have not changed, any higher level
      // flux registers must be properly nested within them
      // and can be ignored.

      if (fgrids != old_boxes) {

        // level+1 grids have changed.  Check whether level+1
        // still contains the region any higher registers
        // will reflux into:

        IntVect ref_rat = IntVect::TheUnitVector();

        for (int flev = level+1; flev < parent->maxLevel(); flev++) {

          // ref_rat is the ratio between flev and level

          ref_rat *= parent->refRatio(flev-1);

          if (flux_cons_old.defined(flev+1)) {

            if (flev > level+1) {
              fgrids.refine(parent->refRatio(flev-1));
              // fgrids is now at refinement level flev
            }

            FluxRegister& ff_sync = flux_cons_old[flev+1];
            BoxArray ffgr = ff_sync.coarsenedBoxes();
            ffgr.grow(1); // these are the cells to reflux into

            // we don't reflux into cells outside the domain
            const Box& domain = parent->Geom(flev).Domain();

            // this may be inefficient, so do it box by box instead
            //ffgr = BoxLib::intersect(ffgr, domain);
            for (int i = 0; i < ffgr.size(); i++) {
              ffgr.set(i, (ffgr[i] & domain));
            }

            // For large parallel jobs, the following
            // BoxArray::contains() function may be
            // prohibitively expensive.
            // We could optimize by only doing it once and
            // reusing the result for all the groups.

            if (fgrids.contains(ffgr)) {
              // The higher level flux register is still contained
              // within level+1, so we can ignore it and any
              // finer levels here.

              break;
            }
            else {
              // Either level+1 does not exist, or it does not
              // contain the flux register from level flev+1.

              // I don't think we can use FluxRegister to reflux
              // from flev+1 to level, not even if we constructed
              // an intermediate FluxRegister at some other resolution
              // and initialized it from ff_sync (as we do below in
              // the coarse-to-fine direction).  The problem is that
              // for an extreme difference in levels, the faces in
              // ff_sync may not lie on cell faces in level, and the
              // FluxRegister mechanism assumes that they do (and
              // can't be fooled into doing the right thing anyway).

              // Fortunately this is a rare case---it apparently can
              // only happen when there is a change in the refinement
              // criteria.  What I'm going to do is construct a
              // MultiFab at level flev, reflux ff_sync into it, and
              // then average the result down to level.  I have to
              // duplicate some code here from Castro::avgDown to
              // accomplish this, but at least I can call the same
              // Fortran.  This is much simpler than it would be to
              // re-implement reflux from the FluxRegister class for
              // this special circumstance.

              // clear_internal_borders may not have been done yet.
              // We'll do it again when we advance finer levels up to
              // flev, so there is some duplication of effort, but
              // this is an unusual situation.

              if (indx == 0) {
                // clean up fine-fine interfaces (does all groups at once)
                clear_internal_borders(ff_sync);
              }

              BoxArray coarsened_grids(ffgr);

              // coarsened_grids is now at flev resolution, and
              // large enough to contain all of the cells that
              // ff_sync would coarsen into.  (It's tempting to just
              // use a refined copy of grids at level, but that
              // could be too large to refine.)

              coarsened_grids.coarsen(ref_rat);

              // The way the coarsen method is defined, if the fine
              // BoxArray does not coarsen evenly by the specified ratio,
              // the coarsened result will be just a little bit larger
              // so that it still fully contains the fine region.

              BoxArray refined_grids(coarsened_grids);
              refined_grids.refine(ref_rat);

              // refined_grids is now back to flev resolution, but is
              // enlarged so that it evenly covers whole cells at the
              // resolution of level.

              MultiFab flev_data(refined_grids, 1, 0);
              flev_data.setVal(0.0);

              // Reflux into this expanded (flev resolution) MultiFab.

              // The units of the data in ff_sync are
              // <conserved quantity> per (flev) timestep.
              // The averaging process will preserve the volume
              // integral of <conserved quantity>.

              // So the remaining scale factor is determined only
              // by the ratio of the (flev) timestep for which
              // this flux data was originally prepared, to the
              // current timestep.

              Real scale = delta_t_old[flev] / delta_t;
              if (!do_sync) scale = 0.0;

              ff_sync.Reflux(flev_data, scale, indx, 0, 1,
                             parent->Geom(flev));

              // Coarsen to level resolution.

              MultiFab crse_data(coarsened_grids, 1, 0);

              // Construct volumes.  The data we are coarsening
              // already has the metric factor built in, so the
              // volumes we need reflect only the number of fine
              // cells per coarse cell.

              MultiFab flev_volume(refined_grids, 1, 0);
              MultiFab crse_volume(coarsened_grids, 1, 0);

              flev_volume.setVal(1.0);
              crse_volume.setVal(D_TERM(ref_rat[0], *ref_rat[1], *ref_rat[2]));

              // Call fortran here:  based on Castro::avgDown

              for (MFIter mfi(flev_data); mfi.isValid(); ++mfi) {
                const int        i        = mfi.index();
                const int        ncomp    = 1;
                const Box&       ovlp     = coarsened_grids[i];
                FArrayBox&       crse_fab = crse_data[mfi];
                const FArrayBox& crse_vol = crse_volume[mfi];
                const FArrayBox& fine_fab = flev_data[mfi];
                const FArrayBox& fine_vol = flev_volume[mfi];

                BL_FORT_PROC_CALL(CA_AVGDOWN,ca_avgdown)
                  (BL_TO_FORTRAN(crse_fab), ncomp,
                   BL_TO_FORTRAN(crse_vol),
                   BL_TO_FORTRAN(fine_fab),
                   BL_TO_FORTRAN(fine_vol),
                   ovlp.loVect(),ovlp.hiVect(),
                   ref_rat.getVect());
              }

              // Add coarsened result into rhs.

              MultiFab rhs_tmp(grids, 1, 0);
              rhs_tmp.setVal(0.0);     // clear garbage
              rhs_tmp.copy(crse_data); // not defined on same BoxArray

              rhs.plus(rhs_tmp, 0, 1, 0);
            }
          }
        } // end loop over finer levels
      }

    } // end refluxing from levels higher than level+1

  } // end refluxing from all finer levels

  // If there is a sync source from this level or a coarser
  // level that now intersects this level, reflux it too:

  IntVect ref_rat = IntVect::TheUnitVector();

  for (int flev = level; flev > 0; flev--) {

    // delta_t_old is used here as a hack indicating that the
    // data in this flux register is "fresh".  See the end of
    // the update routine where delta_t_old is zeroed out after
    // the data becomes stale.

    if (flux_cons_old.defined(flev) &&
        delta_t_old[flev-1] > 0.0) {

      FluxRegister& crse_sync_flux = flux_cons_old[flev];

      const IntVect& rat = crse_sync_flux.refRatio();
      BoxArray old_boxes = crse_sync_flux.coarsenedBoxes();
      old_boxes.refine(rat); // now refined to level flev

      const BoxArray& fgrids = parent->getLevel(flev).boxArray();
      if (fgrids == old_boxes) {

        // If the grids at level flev have not changed, then the
        // current level must still be properly nested within it
        // and we don't need to go any further.

        break;
      }
      else {

        // ref_rat is the ratio between level and flev

        old_boxes.refine(ref_rat); // now refined to current level

        // flev grids have changed, so check if the current level
        // is still properly nested within them.  If so, no
        // refluxing is necessary from this or coarser levels.

        // For large parallel jobs, the following
        // BoxArray::contains() function may be
        // prohibitively expensive.
        // We could optimize by only doing it once and
        // reusing the result for all the groups.

        if (old_boxes.contains(grids)) {
          break;
        }

        // We now know we have to reflux from crse_sync_flux

        FluxRegister ref_sync_flux(old_boxes,
                                   IntVect::TheUnitVector(),
                                   level+1, // this is not used
                                   1,
                                   crse_sync_flux.DistributionMap());
        ref_rat *= rat;

        // ref_rat is now the ratio between level and flev-1

        // Fill in each Fab in ref_sync_flux from
        // the corresponding Fab in crse_sync_flux, refined
        // with piecewise-constant (area-adjusted)
        // interpolation with ratio given by ref_rat.

        for (int dir = 0; dir < BL_SPACEDIM; dir++) {
          const Orientation lo_face(dir,Orientation::low);
          const Orientation hi_face(dir,Orientation::high);

          for (FabSetIter fsi(ref_sync_flux[lo_face]);
               fsi.isValid(); ++fsi) {
            const Box& fbox = ref_sync_flux[lo_face][fsi].box();
            const Box& cbox = crse_sync_flux[lo_face][fsi].box();
            FORT_RFFACE(ref_sync_flux[lo_face][fsi].dataPtr(),
                        dimlist(fbox),
                        crse_sync_flux[lo_face][fsi].dataPtr(indx),
                        dimlist(cbox),
                        dir, ref_rat.getVect());
          }

          for (FabSetIter fsi(ref_sync_flux[hi_face]);
               fsi.isValid(); ++fsi) {
            const Box& fbox = ref_sync_flux[hi_face][fsi].box();
            const Box& cbox = crse_sync_flux[hi_face][fsi].box();
            FORT_RFFACE(ref_sync_flux[hi_face][fsi].dataPtr(),
                        dimlist(fbox),
                        crse_sync_flux[hi_face][fsi].dataPtr(indx),
                        dimlist(cbox),
                        dir, ref_rat.getVect());
          }
        }

        // clear_internal_borders has already been done---it
        // was done when refluxing this data to level flev-1.

        // The units of the data in crse_sync_flux are
        // <conserved quantity> per (coarse) timestep.  The
        // RFFACE routine divides each value by the number of
        // times it is being duplicated, leaving the same
        // total sum as before.

        // So the remaining scale factor is determined only
        // by the ratio of the (coarse) timestep for which
        // this flux data was originally prepared, to the
        // current timestep.

        Real scale = delta_t_old[flev-1] / delta_t;
        if (!do_sync) scale = 0.0;

        ref_sync_flux.Reflux(rhs, scale, 0, 0, 1, castro->Geom());

      } // end if "this register may overlap current level"

    } // end if "this register exists and has data"

  } // end loop over coarser flux register levels

  // All refluxing into the rhs is now done.
}

void Radiation::reflux(int level)
{
  return;
}

// Computes the scaled gradient for use in flux limiters

void Radiation::scaledGradient(int level,
                               Tuple<MultiFab, BL_SPACEDIM>& R,
                               MultiFab& kappa_r, int kcomp,
                               MultiFab& Er, int igroup,
                               int limiter, int nGrow_Er, int Rcomp)
{
  BL_PROFILE("Radiation::scaledGradient");
  const BoxArray& grids = parent->boxArray(level);

  BL_ASSERT(kappa_r.nGrow() == 1);

  int Ercomp = igroup;

  MultiFab Erbtmp;
  if (nGrow_Er == 0) { // default value
    if (limiter > 0) {
      Erbtmp.define(grids,1,1,Fab_allocate);
      for (MFIter mfi(Erbtmp); mfi.isValid(); ++mfi) {
	int i = mfi.index();
	const Box &bx = grids[i];
	Erbtmp[mfi].setVal(-1.0);
	Erbtmp[mfi].copy(Er[mfi], bx, igroup, bx, 0, 1);
      }
      // Values in ghost cells are set to -1, indicating that one-sided
      // differences should be used in computing the gradient term for
      // the flux limiter.  In order to make the solution independent
      // of the grid layout, we now go back and overwrite values in
      // those cells bordering grids at the same level:
      
      Erbtmp.FillBoundary();
      
      if (parent->Geom(level).isAnyPeriodic()) {
	parent->Geom(level).FillPeriodicBoundary(Erbtmp, true);
      }
    }
    Ercomp = 0;
  }

  MultiFab& Erborder = (nGrow_Er==0) ? Erbtmp : Er;

  const Real* dx = parent->Geom(level).CellSize();

  for (int idim = 0; idim < BL_SPACEDIM; idim++) {

    for (MFIter mfi(Er); mfi.isValid(); ++mfi) {
      int i = mfi.index();
      const Box &rbox = R[idim][mfi].box();
      const Box &reg  = grids[i];
      const Box &kbox = kappa_r[mfi].box();

      if (limiter == 0) {
        R[idim][mfi].setVal(0.0);
      }
      else if (limiter%10 == 1) {
        FORT_SCGRD1(R[idim][mfi].dataPtr(Rcomp), dimlist(rbox), dimlist(reg),
                    idim, kappa_r[mfi].dataPtr(kcomp), dimlist(kbox),
                    Erborder[mfi].dataPtr(Ercomp), dx);
      }
      else if (limiter%10 == 2) {
#if (BL_SPACEDIM >= 2)
        Fab dtmp(kbox, BL_SPACEDIM - 1);
#endif
        FORT_SCGRD2(R[idim][mfi].dataPtr(Rcomp), dimlist(rbox), dimlist(reg),
                    idim, kappa_r[mfi].dataPtr(kcomp), dimlist(kbox),
                    D_DECL(Erborder[mfi].dataPtr(Ercomp),
                           dtmp.dataPtr(0), dtmp.dataPtr(1)),
                    dx);
      }
      else {
#if (BL_SPACEDIM >= 2)
        Fab dtmp(kbox, BL_SPACEDIM - 1);
#endif
        FORT_SCGRD3(R[idim][mfi].dataPtr(Rcomp), dimlist(rbox), dimlist(reg),
                    idim, kappa_r[mfi].dataPtr(kcomp), dimlist(kbox),
                    D_DECL(Erborder[mfi].dataPtr(Ercomp),
                           dtmp.dataPtr(0), dtmp.dataPtr(1)),
                    dx);
      }
    }
  }
}

// On input, lambda should contain scaled gradient.
// On output this will be overwritten with the flux limiter.

void Radiation::fluxLimiter(int level,
                            Tuple<MultiFab, BL_SPACEDIM>& lambda,
                            int limiter, int lamcomp)
{
  BL_PROFILE("Radiation::fluxLimiter");
  const BoxArray& grids = parent->boxArray(level);

  for (int idim = 0; idim < BL_SPACEDIM; idim++) {

    for (MFIter mfi(lambda[idim]); mfi.isValid(); ++mfi) {
      int i = mfi.index();
      const Box &rbox = lambda[idim][mfi].box();
      const Box &reg  = grids[i];

      FORT_FLXLIM(lambda[idim][mfi].dataPtr(lamcomp), dimlist(rbox),
                  dimlist(reg), limiter, idim);
    }
  }
}

void Radiation::get_rosseland_v_dcf(MultiFab& kappa_r, MultiFab& v, MultiFab& dcf,
				    Real delta_t, Real c,
				    AmrLevel* castro, int igroup)
{
    BL_ASSERT(kappa_r.nGrow() == 1);
    BL_ASSERT(      v.nGrow() == 1);
    BL_ASSERT(    dcf.nGrow() == 1);
    
    const BoxArray& grids = kappa_r.boxArray();
    int nstate = castro->get_new_data(State_Type).nComp();
    Real time = castro->get_state_data(State_Type).curTime();
    
    FillPatchIterator fpi_r(*castro, kappa_r, 1, time, Rad_Type, 0, 1);
    MultiFab& Er = fpi_r.get_mf();
    
    FillPatchIterator fpi_s(*castro, kappa_r, 1, time, State_Type, 0, nstate); 
    MultiFab& S = fpi_s.get_mf();
    
#ifdef _OPENMP
#pragma omp parallel
#endif
    {
	Fab temp, c_v, kp, kp2;
	for (MFIter mfi(kappa_r,true); mfi.isValid(); ++mfi)
	{
	    const Box& reg = mfi.growntilebox(1);
	    
	    temp.resize(reg);
	    get_frhoe(temp, S[mfi], reg);
	    
	    if (do_real_eos > 0) {
		BL_FORT_PROC_CALL(CA_COMPUTE_TEMP_GIVEN_RHOE, ca_compute_temp_given_rhoe)
		    (reg.loVect(), reg.hiVect(), BL_TO_FORTRAN(temp), BL_TO_FORTRAN(S[mfi]));
	    }
	    else if (do_real_eos == 0) {
		FORT_GTEMP(temp.dataPtr(), dimlist(reg),
			   const_c_v.dataPtr(),
			   c_v_exp_m.dataPtr(), c_v_exp_n.dataPtr(),
			   S[mfi].dataPtr(), dimlist(S[mfi].box()));
	    }
	    
	    c_v.resize(reg);
	    get_c_v(c_v, temp, S[mfi], reg);
	    
	    S[mfi].copy(temp,reg,0,reg,Temp,1);
	    
	    // compute rosseland
	    if (use_opacity_table_module) {
		BL_FORT_PROC_CALL(CA_COMPUTE_ROSSELAND, ca_compute_rosseland)
		    (reg.loVect(), reg.hiVect(), BL_TO_FORTRAN(kappa_r[mfi]), BL_TO_FORTRAN(S[mfi]));
	    }
	    else if (const_scattering[0] > 0.0) {
		FORT_ROSSE1S(kappa_r[mfi].dataPtr(igroup), dimlist(kappa_r[mfi].box()), 
			     dimlist(reg),
			     const_kappa_r.dataPtr(),
			     kappa_r_exp_m.dataPtr(), kappa_r_exp_n.dataPtr(),
			     kappa_r_exp_p.dataPtr(), 
			     const_scattering.dataPtr(),
			     scattering_exp_m.dataPtr(), scattering_exp_n.dataPtr(),
			     scattering_exp_p.dataPtr(), 
			     nugroup[igroup],
			     prop_temp_floor.dataPtr(), kappa_r_floor,
			     temp.dataPtr(), 
			     S[mfi].dataPtr(), dimlist(S[mfi].box()));
	    }
	    else {
		FORT_ROSSE1(kappa_r[mfi].dataPtr(igroup), dimlist(kappa_r[mfi].box()), 
			    dimlist(reg),
			    const_kappa_r.dataPtr(),
			    kappa_r_exp_m.dataPtr(), kappa_r_exp_n.dataPtr(),
			    kappa_r_exp_p.dataPtr(), nugroup[igroup],
			    prop_temp_floor.dataPtr(), kappa_r_floor,
			    temp.dataPtr(), 
			    S[mfi].dataPtr(), dimlist(S[mfi].box()));
	    }
	    
	    if (use_opacity_table_module) {
		cout << "SGFLDSolver Solver does not support both use_opacity_table_module=1 "
		     << "and Er_Lorentz_term=1; try comoving=1 or Er_Lorentz_term=0 "
		     << "when use_opacity_table_module=1"<<endl;
		BoxLib::Abort("SGFLDSolver: try comoving=1 or Er_Lorentz_term=0");
	    }
	    
	    
	    kp.resize(reg);
	    FORT_FKPN(kp.dataPtr(), dimlist(reg),
		      const_kappa_p.dataPtr(),
		      kappa_p_exp_m.dataPtr(), kappa_p_exp_n.dataPtr(),
		      kappa_p_exp_p.dataPtr(), nugroup[igroup],
		      prop_temp_floor.dataPtr(),
		      temp.dataPtr(), 
		      S[mfi].dataPtr(), dimlist(S[mfi].box()));
	    kp2.resize(reg);
	    temp.plus(dT, 0, 1);
	    FORT_FKPN(kp2.dataPtr(), dimlist(reg),
		      const_kappa_p.dataPtr(),
		      kappa_p_exp_m.dataPtr(), kappa_p_exp_n.dataPtr(),
		      kappa_p_exp_p.dataPtr(), nugroup[igroup],
		      prop_temp_floor.dataPtr(),
		      temp.dataPtr(), 
		      S[mfi].dataPtr(), dimlist(S[mfi].box()));
	    temp.plus(-dT, 0, 1);
	    
	    BL_FORT_PROC_CALL(CA_GET_V_DCF, ca_get_v_dcf)
		(reg.loVect(), reg.hiVect(),
		 BL_TO_FORTRAN(Er[mfi]),
		 BL_TO_FORTRAN(S[mfi]),
		 BL_TO_FORTRAN(temp),
		 BL_TO_FORTRAN(c_v),
		 BL_TO_FORTRAN(kappa_r[mfi]),
		 BL_TO_FORTRAN(kp),
		 BL_TO_FORTRAN(kp2),
		 &dT, &delta_t, &sigma, &c,
		 BL_TO_FORTRAN(v[mfi]),
		 BL_TO_FORTRAN(dcf[mfi]));
	}
    }
}

void Radiation::update_dcf(MultiFab& dcf, MultiFab& etainv, MultiFab& kp, MultiFab& kr,
			   const Geometry& geom)
{
    int ng = dcf.nGrow();
#ifdef _OPENMP
#pragma omp parallel
#endif
    for (MFIter mfi(dcf,true); mfi.isValid(); ++mfi) {
	const Box& bx = mfi.growntilebox(ng);
	BL_FORT_PROC_CALL(CA_UPDATE_DCF, ca_update_dcf)
	    (bx.loVect(), bx.hiVect(),
	     BL_TO_FORTRAN(dcf[mfi]),
	     BL_TO_FORTRAN(etainv[mfi]),
	     BL_TO_FORTRAN(kp[mfi]),
	     BL_TO_FORTRAN(kr[mfi]));
    }
    
    dcf.FillBoundary();
    if (geom.isAnyPeriodic()) {
	geom.FillPeriodicBoundary(dcf, false);
    }
}

void Radiation::EstTimeStep(Real & estdt, int level)
{
  Castro *castro        = dynamic_cast<Castro*>(&parent->getLevel(level));
  const Geometry& geom = parent->Geom(level);
  Real time = castro->get_state_data(Rad_Type).curTime();

  if (nNeutrinoSpecies > 0 &&
      nNeutrinoGroups[0] > 0) {
//    Real derat = deltaEnergyRatMax(level);
    Real dTrat = deltaTRatMax(level);
    Real dye   = deltaYeMax(level);

    //    Real fac = std::min(deltaEnergyTol() / (derat + 1.e-20),
    //		deltaTTol()      / (dTrat + 1.e-20));
    //    fac = std::min(fac, deltaYeTol()     / (dye   + 1.e-20));
    Real fac = deltaTTol()  / (dTrat + 1.e-20);
    fac = std::min(fac, deltaYeTol()     / (dye   + 1.e-20));

    Real estdt_rad = parent->dtLevel(level);
    estdt_rad *= fac;
    
    if (verbose && ParallelDescriptor::IOProcessor())
      std::cout << "radiation timestep at level " << level
		<< ":  estdt_rad = " << estdt_rad << std::endl;
    
    estdt = std::min(estdt, estdt_rad);
  }

  if (RadTests::do_thermal_wave_cgs) {
    ParmParse pp("radiation");
    Real cfl = -1.0;
    pp.query("thermal_wave_cfl", cfl);
    if (cfl > 0.0) {
      Real rhocv, Q;
      pp.get("thermal_wave_rhocv", rhocv);
      pp.get("thermal_wave_Eexp", Q);
      Q /= rhocv;
      
      Real a = (16.0 * sigma) / (3.0 * const_kappa_r[0]) / rhocv;
      Real p = kappa_r_exp_n[0] + 3.0;
      
      Real pfac = exp(lgamma(2.5 + 1.0 / p) - lgamma(1.0 + 1.0 / p) - lgamma(1.5));

      Real xi0  = pow((3.0 * p + 2.0) / (pow(2.0, p - 1.0) * p *
					 pow(M_PI, p)),
		      1.0 / (3.0 * p + 2.0)) *
	pow(pfac, p / (3.0 * p + 2.0));
      Real xf = xi0 * pow(a * pow(Q,p) * time, 1.0 / (3.0 * p + 2.0));
      Real vf = xf / (time * (3.0 * p + 2.0));
      
      const Real *dx = geom.CellSize();
      Real estdt_rad = cfl * dx[0] / vf;
      
      if (verbose && ParallelDescriptor::IOProcessor())
	std::cout << "radiation timestep at level " << level
		  << ":  estdt_rad = " << estdt_rad << std::endl;
      
      estdt = std::min(estdt, estdt_rad);
    }
  }
}

void Radiation::set_current_group(int igroup)
{
#ifdef NEUTRINO
  if (radiation_type == Photon) {
    current_group_number = igroup;
    current_group_name = "Photon";
  }
  else if (igroup < nNeutrinoGroups[0]) {
    current_group_number = igroup;
    current_group_name = "Electron";
  }
  else if (nNeutrinoSpecies >= 2 && igroup < nNeutrinoGroups[0]+nNeutrinoGroups[1]) {
    current_group_number = igroup - nNeutrinoGroups[0];
    current_group_name = "Anti Electron";
  }
  else if (nNeutrinoSpecies == 3 && igroup >= nNeutrinoGroups[0]+nNeutrinoGroups[1]) {
    current_group_number = igroup - (nNeutrinoGroups[0]+nNeutrinoGroups[1]);
    current_group_name = "Muon";
  }
  else {
    BoxLib::Abort("Something is wrong!  Maybe there are more than three neutrino flavors.");
  }
#else
  current_group_number = igroup;
#endif
}

void Radiation::computeTemp(MultiFab& State, int resetEint)
{
  Real relative = 0.0;
  Real absolute = 0.0;

  for (MFIter mfi(State); mfi.isValid(); ++mfi) {
    const Box& bx = mfi.validbox();

    if (do_real_eos == 0) {
      BL_FORT_PROC_CALL(RESET_INTERNAL_E,reset_internal_e)
	(BL_TO_FORTRAN(State[mfi]),
	 bx.loVect(), bx.hiVect(),verbose);

      FArrayBox temp(bx);
      temp.copy(State[mfi],Eint,0,1);

      BL_FORT_PROC_CALL(CA_COMPUTE_TEMP_GIVEN_CV, ca_compute_temp_given_cv)
	(bx.loVect(), bx.hiVect(), 
	 BL_TO_FORTRAN(temp), 
	 BL_TO_FORTRAN(State[mfi]),
	 &const_c_v[0], &c_v_exp_m[0], &c_v_exp_n[0]);

      State[mfi].copy(temp,0,Temp,1);
    }
    else {
      BL_FORT_PROC_CALL(RESET_EINT_COMPUTE_TEMP, reset_eint_compute_temp)
	(bx.loVect(),bx.hiVect(),BL_TO_FORTRAN(State[mfi]),
	 &resetEint, &relative, &absolute);
    }
  }
}


void Radiation::filter_prim(int level, MultiFab& State)
{
  Castro *castro = dynamic_cast<Castro*>(&parent->getLevel(level));
  const BoxArray& grids = castro->boxArray();
  const Geometry& geom = parent->Geom(level);

  const int*  domain_lo = geom.Domain().loVect();
  const int*  domain_hi = geom.Domain().hiVect();
  const Real* dx        = geom.CellSize();
  const Real* prob_lo   = geom.ProbLo();

  int ngrow = filter_prim_T;
  int ncomp = State.nComp();
  Real time = castro->get_state_data(Rad_Type).curTime();

  MultiFab mask(grids,1,ngrow);
  mask.setVal(-1.0,ngrow);
  mask.setVal( 0.0,0);
  mask.FillBoundary();
  if (geom.isAnyPeriodic()) {
    geom.FillPeriodicBoundary(mask, true);
  }

  BoxArray baf;
  if (level < parent->finestLevel()) {
    baf = parent->boxArray(level+1);
    baf.coarsen(parent->refRatio(level));
  }

  for (FillPatchIterator fpi(*castro,State,ngrow,time,State_Type,
			     0,ncomp); fpi.isValid(); ++fpi) {

    const Box& bx = fpi.validbox();

    FArrayBox &Sborder = fpi();

    //    RealBox gridloc = RealBox(bx,geom.CellSize(),geom.ProbLo());
    RealBox gridloc = RealBox(bx, dx, prob_lo);
    const Real* xlo = gridloc.lo();

    FArrayBox& mask_fab = mask[fpi];
    const Box& mask_box = mask_fab.box();

    if (level < parent->finestLevel()) {
      std::vector< std::pair<int,Box> > isects = baf.intersections(mask_box);

      for (int ii = 0; ii < isects.size(); ii++) {
	mask_fab.setVal(1.0, isects[ii].second, 0);
      }
    }

    BL_FORT_PROC_CALL(CA_FILT_PRIM, ca_filt_prim)
      (bx.loVect(), bx.hiVect(), 
       BL_TO_FORTRAN(Sborder),
       BL_TO_FORTRAN(State[fpi]),
       BL_TO_FORTRAN(mask_fab),
       &filter_prim_T, &filter_prim_S,
       domain_lo, domain_hi,
       dx, xlo, prob_lo, 
       &time, &level);
  }
}
