#include "Castro.H"
#include "Castro_F.H"

#ifdef RADIATION
#include "Radiation.H"
#endif

void
Castro::construct_hydro_source(Real time, Real dt)
{

    if (verbose && ParallelDescriptor::IOProcessor())
        std::cout << "... Entering hydro advance" << std::endl << std::endl;

    hydro_source.setVal(0.0);

    // Set up the source terms to go into the hydro.

    sources_for_hydro.setVal(0.0);

    for (int n = 0; n < num_src; ++n)
	MultiFab::Add(sources_for_hydro, old_sources[n], 0, 0, NUM_STATE, NUM_GROW);

    sources_for_hydro.FillBoundary(geom.periodicity());

#ifndef SDC
    // Optionally we can predict the source terms to t + dt/2,
    // which is the time-level n+1/2 value, To do this we use a
    // lagged predictor estimate: dS/dt_n = (S_n - S_{n-1}) / dt, so
    // S_{n+1/2} = S_n + (dt / 2) * dS/dt_n.

    if (source_term_predictor == 1) {

      MultiFab& dSdt_new = get_new_data(Source_Type);

      dSdt_new.FillBoundary(geom.periodicity());

      MultiFab::Saxpy(sources_for_hydro, 0.5 * dt, dSdt_new, 0, 0, NUM_STATE, NUM_GROW);

    }
#else
    // If we're doing SDC, time-center the source term (using the
    // current iteration's old sources and the last iteration's new
    // sources).

    MultiFab& SDC_source = get_new_data(SDC_Source_Type);

    MultiFab::Add(sources_for_hydro, SDC_source, 0, 0, NUM_STATE, 0);

    sources_for_hydro.FillBoundary(geom.periodicity());
#ifdef REACTIONS
    // Make sure that we have valid data on the ghost zones of the reactions source.

    MultiFab& SDC_react_source = get_new_data(SDC_React_Type);
    SDC_react_source.FillBoundary(geom.periodicity());
#endif
#endif

    int finest_level = parent->finestLevel();

    const Real *dx = geom.CellSize();
    Real courno    = -1.0e+200;

    MultiFab& S_new = get_new_data(State_Type);

#ifdef RADIATION
    MultiFab& Er_new = get_new_data(Rad_Type);

    if (!Radiation::rad_hydro_combined) {
      BoxLib::Abort("Castro::construct_hydro_source -- we don't implement a mode where we have radiation, but it is not coupled to hydro");
    }

    FillPatchIterator fpi_rad(*this, Er_new, NUM_GROW, time, Rad_Type, 0, Radiation::nGroups);
    MultiFab& Erborder = fpi_rad.get_mf();

    MultiFab lamborder(grids, Radiation::nGroups, NUM_GROW);
    if (radiation->pure_hydro) {
      lamborder.setVal(0.0, NUM_GROW);
    }
    else {
      radiation->compute_limiter(level, grids, Sborder, Erborder, lamborder);
    }

    int nstep_fsp = -1;
#endif

    // note: the radiation consup currently does not fill these
    Real E_added_flux    = 0.;
    Real mass_added_flux = 0.;
    Real xmom_added_flux = 0.;
    Real ymom_added_flux = 0.;
    Real zmom_added_flux = 0.;
    Real mass_lost       = 0.;
    Real xmom_lost       = 0.;
    Real ymom_lost       = 0.;
    Real zmom_lost       = 0.;
    Real eden_lost       = 0.;
    Real xang_lost       = 0.;
    Real yang_lost       = 0.;
    Real zang_lost       = 0.;

    BL_PROFILE_VAR("Castro::advance_hydro_ca_umdrv()", CA_UMDRV);

#ifdef _OPENMP
#ifdef RADIATION
#pragma omp parallel
#else
#pragma omp parallel reduction(+:E_added_flux,mass_added_flux) \
		     reduction(+:xmom_added_flux,ymom_added_flux,zmom_added_flux) \
		     reduction(+:mass_lost,xmom_lost,ymom_lost,zmom_lost) \
		     reduction(+:eden_lost,xang_lost,yang_lost,zang_lost)
#endif
#endif
    {

      FArrayBox flux[BL_SPACEDIM];
#if (BL_SPACEDIM <= 2)
      FArrayBox pradial(Box::TheUnitBox(),1);
#endif
#ifdef RADIATION
      FArrayBox rad_flux[BL_SPACEDIM];
#endif
      FArrayBox q, qaux, src_q;

      int priv_nstep_fsp = -1;

      Real cflLoc = -1.0e+200;
      int is_finest_level = (level == finest_level) ? 1 : 0;
      const int*  domain_lo = geom.Domain().loVect();
      const int*  domain_hi = geom.Domain().hiVect();

      for (MFIter mfi(S_new,hydro_tile_size); mfi.isValid(); ++mfi)
      {
	  const Box& bx    = mfi.tilebox();
	  const Box& qbx = BoxLib::grow(bx, NUM_GROW);

	  const int* lo = bx.loVect();
	  const int* hi = bx.hiVect();

	  FArrayBox &statein  = Sborder[mfi];
	  FArrayBox &stateout = S_new[mfi];

	  FArrayBox &source_in  = sources_for_hydro[mfi];
	  FArrayBox &source_out = hydro_source[mfi];

#ifdef RADIATION
	  FArrayBox &Er = Erborder[mfi];
	  FArrayBox &lam = lamborder[mfi];
	  FArrayBox &Erout = Er_new[mfi];
#endif

	  FArrayBox& vol      = volume[mfi];

#ifdef RADIATION
	  q.resize(qbx, QRADVAR);
#else
	  q.resize(qbx, QVAR);
#endif
	  qaux.resize(qbx, NQAUX);
	  src_q.resize(qbx, QVAR);

	  // convert the conservative state to the primitive variable state.
	  // this fills both q and qaux.

	  ctoprim(ARLIM_3D(qbx.loVect()), ARLIM_3D(qbx.hiVect()),
		  statein.dataPtr(), ARLIM_3D(statein.loVect()), ARLIM_3D(statein.hiVect()),
#ifdef RADIATION
		  Er.dataPtr(), ARLIM_3D(Er.loVect()), ARLIM_3D(Er.hiVect()),
		  lam.dataPtr(), ARLIM_3D(lam.loVect()), ARLIM_3D(lam.hiVect()),
#endif
		  q.dataPtr(), ARLIM_3D(q.loVect()), ARLIM_3D(q.hiVect()),
		  qaux.dataPtr(), ARLIM_3D(qaux.loVect()), ARLIM_3D(qaux.hiVect()));

	  // convert the source terms expressed as sources to the conserved state to those
	  // expressed as sources for the primitive state.

	  srctoprim(ARLIM_3D(qbx.loVect()), ARLIM_3D(qbx.hiVect()),
		    q.dataPtr(), ARLIM_3D(q.loVect()), ARLIM_3D(q.hiVect()),
		    qaux.dataPtr(), ARLIM_3D(qaux.loVect()), ARLIM_3D(qaux.hiVect()),
		    source_in.dataPtr(), ARLIM_3D(source_in.loVect()), ARLIM_3D(source_in.hiVect()),
		    src_q.dataPtr(), ARLIM_3D(src_q.loVect()), ARLIM_3D(src_q.hiVect()));

#ifndef RADIATION

	  // Add in the reactions source term; only done in SDC.

#ifdef SDC
#ifdef REACTIONS
	  if (do_react)
	    src_q.plus(SDC_react_source[mfi],qbx,qbx,0,0,QVAR);
#endif
#endif
#endif
	  // Allocate fabs for fluxes
	  for (int i = 0; i < BL_SPACEDIM ; i++)  {
	    const Box& bxtmp = BoxLib::surroundingNodes(bx,i);
	    flux[i].resize(bxtmp,NUM_STATE);
#ifdef RADIATION
	    rad_flux[i].resize(bxtmp,Radiation::nGroups);
#endif
	  }

#if (BL_SPACEDIM <= 2)
	  if (!Geometry::IsCartesian()) {
	    pradial.resize(BoxLib::surroundingNodes(bx,0),1);
	  }
#endif

	  ca_umdrv
	    (&is_finest_level, &time,
	     lo, hi, domain_lo, domain_hi,
	     BL_TO_FORTRAN(statein), 
	     BL_TO_FORTRAN(stateout),
#ifdef RADIATION
	     BL_TO_FORTRAN(Er), 
	     BL_TO_FORTRAN(Erout),
#endif
	     BL_TO_FORTRAN(q),
	     BL_TO_FORTRAN(qaux),
	     BL_TO_FORTRAN(src_q),
	     BL_TO_FORTRAN(source_out),
	     dx, &dt,
	     D_DECL(BL_TO_FORTRAN(flux[0]),
		    BL_TO_FORTRAN(flux[1]),
		    BL_TO_FORTRAN(flux[2])),
#ifdef RADIATION
	     D_DECL(BL_TO_FORTRAN(rad_flux[0]),
		    BL_TO_FORTRAN(rad_flux[1]),
		    BL_TO_FORTRAN(rad_flux[2])),
#endif
#if (BL_SPACEDIM < 3)
	     BL_TO_FORTRAN(pradial),
#endif
	     D_DECL(BL_TO_FORTRAN(area[0][mfi]),
		    BL_TO_FORTRAN(area[1][mfi]),
		    BL_TO_FORTRAN(area[2][mfi])),
#if (BL_SPACEDIM < 3)
	     BL_TO_FORTRAN(dLogArea[0][mfi]),
#endif
	     BL_TO_FORTRAN(volume[mfi]),
	     &cflLoc, verbose,
#ifdef RADIATION
	     &priv_nstep_fsp,
#endif
	     mass_added_flux,
	     xmom_added_flux,
	     ymom_added_flux,
	     zmom_added_flux,
	     E_added_flux,
	     mass_lost, xmom_lost, ymom_lost, zmom_lost,
	     eden_lost, xang_lost, yang_lost, zang_lost);

	  // Store the fluxes from this advance.
	  // For normal integration we want to add the fluxes from this advance
	  // since we may be subcycling the timestep. But for SDC integration
	  // we want to copy the fluxes since we expect that there will not be
	  // subcycling and we only want the last iteration's fluxes.

	  for (int i = 0; i < BL_SPACEDIM ; i++) {
#ifndef SDC
	    fluxes    [i][mfi].plus(    flux[i],mfi.nodaltilebox(i),0,0,NUM_STATE);
#ifdef RADIATION
	    rad_fluxes[i][mfi].plus(rad_flux[i],mfi.nodaltilebox(i),0,0,Radiation::nGroups);
#endif
#else
	    fluxes    [i][mfi].copy(    flux[i],mfi.nodaltilebox(i),0,mfi.nodaltilebox(i),0,NUM_STATE);
#ifdef RADIATION
	    rad_fluxes[i][mfi].copy(rad_flux[i],mfi.nodaltilebox(i),0,mfi.nodaltilebox(i),0,Radiation::nGroups);
#endif	    
#endif
	  }

#if (BL_SPACEDIM <= 2)
	  if (!Geometry::IsCartesian()) {
#ifndef SDC
	    P_radial[mfi].plus(pradial,mfi.nodaltilebox(0),0,0,1);
#else
	    P_radial[mfi].copy(pradial,mfi.nodaltilebox(0),0,mfi.nodaltilebox(0),0,1);
#endif
	  }
#endif
      } // MFIter loop

#ifdef _OPENMP
#pragma omp critical (hydro_courno)
#endif
      {
	courno = std::max(courno,cflLoc);
#ifdef RADIATION
	nstep_fsp = std::max(nstep_fsp, priv_nstep_fsp);
#endif
      }
    }  // end of omp parallel region

    BL_PROFILE_VAR_STOP(CA_UMDRV);

#ifdef RADIATION
    if (radiation->verbose>=1) {
#ifdef BL_LAZY
      Lazy::QueueReduction( [=] () mutable {
#endif
	  ParallelDescriptor::ReduceIntMax(nstep_fsp, ParallelDescriptor::IOProcessorNumber());
	  if (ParallelDescriptor::IOProcessor() && nstep_fsp > 0) {
	    std::cout << "Radiation f-space advection on level " << level
		      << " takes as many as " << nstep_fsp;
	    if (nstep_fsp == 1) {
	      std::cout<< " substep.\n";
	    }
	    else {
	      std::cout<< " substeps.\n";
	    }
	  }
#ifdef BL_LAZY
	});
#endif
    }
#else
    // Flush Fortran output

    if (verbose)
      flush_output();

    if (track_grid_losses)
    {
	material_lost_through_boundary_temp[0] += mass_lost;
	material_lost_through_boundary_temp[1] += xmom_lost;
	material_lost_through_boundary_temp[2] += ymom_lost;
	material_lost_through_boundary_temp[3] += zmom_lost;
	material_lost_through_boundary_temp[4] += eden_lost;
	material_lost_through_boundary_temp[5] += xang_lost;
	material_lost_through_boundary_temp[6] += yang_lost;
	material_lost_through_boundary_temp[7] += zang_lost;
    }

    if (print_energy_diagnostics)
    {
	Real foo[5] = {E_added_flux, xmom_added_flux, ymom_added_flux,
		       zmom_added_flux, mass_added_flux};

#ifdef BL_LAZY
	Lazy::QueueReduction( [=] () mutable {
#endif
	    ParallelDescriptor::ReduceRealSum(foo, 5, ParallelDescriptor::IOProcessorNumber());

	    if (ParallelDescriptor::IOProcessor())
	      {
	        E_added_flux    = foo[0];
	        xmom_added_flux = foo[1];
	        ymom_added_flux = foo[2];
	        zmom_added_flux = foo[3];
	        mass_added_flux = foo[4];
		
		std::cout << "mass added from fluxes                      : " <<
		  mass_added_flux << std::endl;
		std::cout << "xmom added from fluxes                      : " <<
		  xmom_added_flux << std::endl;
		std::cout << "ymom added from fluxes                      : " <<
		  ymom_added_flux << std::endl;
		std::cout << "zmom added from fluxes                      : " <<
		  zmom_added_flux << std::endl;
		std::cout << "(rho E) added from fluxes                   : " <<
		  E_added_flux << std::endl;
	      }
#ifdef BL_LAZY
	});
#endif
      }
#endif

    if (courno > 1.0) {
	std::cout << "WARNING -- EFFECTIVE CFL AT THIS LEVEL " << level << " IS " << courno << '\n';
	if (hard_cfl_limit == 1)
	  BoxLib::Abort("CFL is too high at this level -- go back to a checkpoint and restart with lower cfl number");
    }

    if (verbose && ParallelDescriptor::IOProcessor())
        std::cout << std::endl << "... Leaving hydro advance" << std::endl << std::endl;

}
