module gravity_sources_module

  implicit none

  public

contains

#ifdef SELF_GRAVITY
  subroutine ca_gsrc(lo,hi,domlo,domhi,phi,phi_lo,phi_hi,grav,grav_lo,grav_hi, &
                     uold,uold_lo,uold_hi, &
                     source,src_lo,src_hi,vol,vol_lo,vol_hi, &
                     dx,dt,time,E_added,mom_added) bind(C, name="ca_gsrc")

    use meth_params_module, only : NVAR, URHO, UMX, UMZ, UEDEN, grav_source_type
    use bl_constants_module
    use math_module, only: cross_product
    use castro_util_module, only: position
#ifdef HYBRID_MOMENTUM
    use meth_params_module, only: UMR, UMP
    use hybrid_advection_module, only: add_hybrid_momentum_source
#endif
    use prob_params_module, only: center

    implicit none

    integer          :: lo(3), hi(3)
    integer          :: domlo(3), domhi(3)
    integer          :: phi_lo(3), phi_hi(3)
    integer          :: grav_lo(3), grav_hi(3)
    integer          :: uold_lo(3), uold_hi(3)
    integer          :: src_lo(3), src_hi(3)
    integer          :: vol_lo(3), vol_hi(3)

    double precision :: phi(phi_lo(1):phi_hi(1),phi_lo(2):phi_hi(2),phi_lo(3):phi_hi(3))
    double precision :: grav(grav_lo(1):grav_hi(1),grav_lo(2):grav_hi(2),grav_lo(3):grav_hi(3),3)
    double precision :: uold(uold_lo(1):uold_hi(1),uold_lo(2):uold_hi(2),uold_lo(3):uold_hi(3),NVAR)
    double precision :: source(src_lo(1):src_hi(1),src_lo(2):src_hi(2),src_lo(3):src_hi(3),NVAR)
    double precision :: vol(vol_lo(1):vol_hi(1),vol_lo(2):vol_hi(2),vol_lo(3):vol_hi(3))
    double precision :: dx(3), dt, time
    double precision :: E_added, mom_added(3)

    double precision :: rho, rhoInv
    double precision :: Sr(3), SrE
    double precision :: old_rhoeint, new_rhoeint, old_ke, new_ke, old_re, old_mom(3)
    double precision :: loc(3)
    integer          :: i, j, k

    double precision :: src(NVAR)

    ! Temporary array for seeing what the new state would be if the update were applied here.

    double precision :: snew(NVAR)

    ! Gravitational source options for how to add the work to (rho E):
    ! grav_source_type =
    ! 1: Original version ("does work")
    ! 2: Modification of type 1 that updates the momentum before constructing the energy corrector
    ! 3: Puts all gravitational work into KE, not (rho e)
    ! 4: Conservative energy formulation

    ! Add gravitational source terms
    do k = lo(3), hi(3)
       do j = lo(2), hi(2)
          do i = lo(1), hi(1)
             rho    = uold(i,j,k,URHO)
             rhoInv = ONE / rho

             loc = position(i,j,k) - center

             src = ZERO
             snew = uold(i,j,k,:)

             ! **** Start Diagnostics ****
             old_re = snew(UEDEN)
             old_ke = HALF * sum(snew(UMX:UMZ)**2) * rhoInv
             old_rhoeint = snew(UEDEN) - old_ke
             old_mom = snew(UMX:UMZ)
             ! ****   End Diagnostics ****

             Sr = rho * grav(i,j,k,:)

             src(UMX:UMZ) = Sr

             snew(UMX:UMZ) = snew(UMX:UMZ) + dt * src(UMX:UMZ)

#ifdef HYBRID_MOMENTUM
             call add_hybrid_momentum_source(loc, src(UMR:UMP), Sr)

             snew(UMR:UMP) = snew(UMR:UMP) + dt * src(UMR:UMP)
#endif

             if (grav_source_type == 1 .or. grav_source_type == 2) then

                ! Src = rho u dot g, evaluated with all quantities at t^n

                SrE = dot_product(uold(i,j,k,UMX:UMZ) * rhoInv, Sr)

             else if (grav_source_type .eq. 3) then

                new_ke = HALF * sum(snew(UMX:UMZ)**2) * rhoInv
                SrE = new_ke - old_ke

             else if (grav_source_type .eq. 4) then

                ! The conservative energy formulation does not strictly require
                ! any energy source-term here, because it depends only on the
                ! fluid motions from the hydrodynamical fluxes which we will only
                ! have when we get to the 'corrector' step. Nevertheless we add a
                ! predictor energy source term in the way that the other methods
                ! do, for consistency. We will fully subtract this predictor value
                ! during the corrector step, so that the final result is correct.
                ! Here we use the same approach as grav_source_type == 2.

                SrE = dot_product(uold(i,j,k,UMX:UMZ) * rhoInv, Sr)

             else
                call bl_error("Error:: gravity_sources_nd.F90 :: invalid grav_source_type")
             end if

             src(UEDEN) = SrE

             snew(UEDEN) = snew(UEDEN) + dt * SrE

             ! **** Start Diagnostics ****
             new_ke = HALF * sum(snew(UMX:UMZ)**2) * rhoInv
             new_rhoeint = snew(UEDEN) - new_ke
             E_added =  E_added + (snew(UEDEN) - old_re) * vol(i,j,k)
             mom_added = mom_added + (snew(UMX:UMZ) - old_mom) * vol(i,j,k)
             ! ****   End Diagnostics ****

             ! Add to the outgoing source array.

             source(i,j,k,:) = src

          enddo
       enddo
    enddo

  end subroutine ca_gsrc

  ! :::
  ! ::: ------------------------------------------------------------------
  ! :::

  subroutine ca_corrgsrc(lo,hi,domlo,domhi, &
                         pold,po_lo,po_hi, &
                         pnew,pn_lo,pn_hi, &
                         gold,go_lo,go_hi, &
                         gnew,gn_lo,gn_hi, &
                         uold,uo_lo,uo_hi, &
                         unew,un_lo,un_hi, &
                         source,sr_lo,sr_hi, &
                         flux1,f1_lo,f1_hi, &
                         flux2,f2_lo,f2_hi, &
                         flux3,f3_lo,f3_hi, &
                         dx,dt,time, &
                         vol,vol_lo,vol_hi, &
                         E_added,mom_added) bind(C, name="ca_corrgsrc")

    use mempool_module, only : bl_allocate, bl_deallocate
    use meth_params_module, only : NVAR, URHO, UMX, UMZ, UEDEN, &
                                   grav_source_type, gravity_type, get_g_from_phi
    use prob_params_module, only : dg, center
    use bl_constants_module
    use multifab_module
    use fundamental_constants_module, only: Gconst
    use castro_util_module, only : position
#ifdef HYBRID_MOMENTUM
    use meth_params_module, only: UMR, UMP
    use hybrid_advection_module, only : add_hybrid_momentum_source
#endif

    implicit none

    integer          :: lo(3), hi(3)
    integer          :: domlo(3), domhi(3)

    integer          :: po_lo(3), po_hi(3)
    integer          :: pn_lo(3), pn_hi(3)
    integer          :: go_lo(3), go_hi(3)
    integer          :: gn_lo(3), gn_hi(3)
    integer          :: uo_lo(3), uo_hi(3)
    integer          :: un_lo(3), un_hi(3)
    integer          :: sr_lo(3), sr_hi(3)
    integer          :: f1_lo(3), f1_hi(3)
    integer          :: f2_lo(3), f2_hi(3)
    integer          :: f3_lo(3), f3_hi(3)
    integer          :: vol_lo(3), vol_hi(3)

    ! Old and new time gravitational potential

    double precision :: pold(po_lo(1):po_hi(1),po_lo(2):po_hi(2),po_lo(3):po_hi(3))
    double precision :: pnew(pn_lo(1):pn_hi(1),pn_lo(2):pn_hi(2),pn_lo(3):pn_hi(3))

    ! Old and new time gravitational acceleration

    double precision :: gold(go_lo(1):go_hi(1),go_lo(2):go_hi(2),go_lo(3):go_hi(3),3)
    double precision :: gnew(gn_lo(1):gn_hi(1),gn_lo(2):gn_hi(2),gn_lo(3):gn_hi(3),3)

    ! Old and new time state data

    double precision :: uold(uo_lo(1):uo_hi(1),uo_lo(2):uo_hi(2),uo_lo(3):uo_hi(3),NVAR)
    double precision :: unew(un_lo(1):un_hi(1),un_lo(2):un_hi(2),un_lo(3):un_hi(3),NVAR)

    ! The source term to send back

    double precision :: source(sr_lo(1):sr_hi(1),sr_lo(2):sr_hi(2),sr_lo(3):sr_hi(3),NVAR)

    ! Hydrodynamics fluxes

    double precision :: flux1(f1_lo(1):f1_hi(1),f1_lo(2):f1_hi(2),f1_lo(3):f1_hi(3),NVAR)
    double precision :: flux2(f2_lo(1):f2_hi(1),f2_lo(2):f2_hi(2),f2_lo(3):f2_hi(3),NVAR)
    double precision :: flux3(f3_lo(1):f3_hi(1),f3_lo(2):f3_hi(2),f3_lo(3):f3_hi(3),NVAR)

    double precision :: vol(vol_lo(1):vol_hi(1),vol_lo(2):vol_hi(2),vol_lo(3):vol_hi(3))
    double precision :: dx(3), dt, time
    double precision :: E_added, mom_added(3)

    integer          :: i, j, k

    double precision :: Sr_old(3), Sr_new(3), Srcorr(3)
    double precision :: vold(3), vnew(3)
    double precision :: SrE_old, SrE_new, SrEcorr
    double precision :: rhoo, rhooinv, rhon, rhoninv

    double precision :: old_ke, old_rhoeint, old_re
    double precision :: new_ke, new_rhoeint
    double precision :: old_mom(3), loc(3)

    double precision :: src(NVAR)

    ! Temporary array for seeing what the new state would be if the update were applied here.

    double precision :: snew(NVAR)

    double precision, pointer :: phi(:,:,:)
    double precision, pointer :: grav(:,:,:,:)
    double precision, pointer :: gravx(:,:,:)
    double precision, pointer :: gravy(:,:,:)
    double precision, pointer :: gravz(:,:,:)

    ! Gravitational source options for how to add the work to (rho E):
    ! grav_source_type =
    ! 1: Original version ("does work")
    ! 2: Modification of type 1 that updates the U before constructing SrEcorr
    ! 3: Puts all gravitational work into KE, not (rho e)
    ! 4: Conservative gravity approach (discussed in first white dwarf merger paper).

    if (grav_source_type .eq. 4) then

       call bl_allocate(phi,   lo(1)-1,hi(1)+1,lo(2)-1,hi(2)+1,lo(3)-1,hi(3)+1)
       call bl_allocate(grav,  lo(1)-1,hi(1)+1,lo(2)-1,hi(2)+1,lo(3)-1,hi(3)+1,1,3)
       call bl_allocate(gravx, lo(1),hi(1)+1,lo(2),hi(2),lo(3),hi(3))
       call bl_allocate(gravy, lo(1),hi(1),lo(2),hi(2)+1,lo(3),hi(3))
       call bl_allocate(gravz, lo(1),hi(1),lo(2),hi(2),lo(3),hi(3)+1)

       ! For our purposes, we want the time-level n+1/2 phi because we are
       ! using fluxes evaluated at that time. To second order we can
       ! average the new and old potentials.

       phi = ZERO
       grav = ZERO
       gravx = ZERO
       gravy = ZERO
       gravz = ZERO

       do k = lo(3)-1*dg(3), hi(3)+1*dg(3)
          do j = lo(2)-1*dg(2), hi(2)+1*dg(2)
             do i = lo(1)-1*dg(1), hi(1)+1*dg(1)
                phi(i,j,k) = HALF * (pnew(i,j,k) + pold(i,j,k))
                grav(i,j,k,:) = HALF * (gnew(i,j,k,:) + gold(i,j,k,:))
             enddo
          enddo
       enddo

       ! We need to perform the following hack to deal with the fact that
       ! the potential is defined on cell edges, not cell centers, for ghost
       ! zones. We redefine the boundary zone values as equal to the adjacent
       ! cell minus the original value. Then later when we do the adjacent zone
       ! minus the boundary zone, we'll get the boundary value, which is what we want.
       ! We don't need to reset this at the end because phi is a temporary array.
       ! Note that this is needed for Poisson gravity only; the other gravity methods
       ! generally define phi on cell centers even outside the domain.

       if (gravity_type == "PoissonGrav") then

          do k = lo(3)-1*dg(3), hi(3)+1*dg(3)
             do j = lo(2)-1*dg(2), hi(2)+1*dg(2)
                do i = lo(1)-1*dg(1), hi(1)+1*dg(1)
                   if (i .lt. domlo(1)) then
                      phi(i,j,k) = phi(i+1,j,k) - phi(i,j,k)
                   endif
                   if (i .gt. domhi(1)) then
                      phi(i,j,k) = phi(i-1,j,k) - phi(i,j,k)
                   endif
                   if (j .lt. domlo(2)) then
                      phi(i,j,k) = phi(i,j+1,k) - phi(i,j,k)
                   endif
                   if (j .gt. domhi(2)) then
                      phi(i,j,k) = phi(i,j-1,k) - phi(i,j,k)
                   endif
                   if (k .lt. domlo(3)) then
                      phi(i,j,k) = phi(i,j,k+1) - phi(i,j,k)
                   endif
                   if (k .gt. domhi(3)) then
                      phi(i,j,k) = phi(i,j,k-1) - phi(i,j,k)
                   endif
                enddo
             enddo
          enddo

       endif

       if (.not. (gravity_type == "PoissonGrav" .or. (gravity_type == "MonopoleGrav" .and. get_g_from_phi == 1) ) ) then

          ! Construct the time-averaged edge-centered gravity.

          do k = lo(3), hi(3)
             do j = lo(2), hi(2)
                do i = lo(1), hi(1)+1*dg(1)
                   gravx(i,j,k) = HALF * (grav(i,j,k,1) + grav(i-1,j,k,1))
                enddo
             enddo
          enddo

          do k = lo(3), hi(3)
             do j = lo(2), hi(2)+1*dg(2)
                do i = lo(1), hi(1)
                   gravy(i,j,k) = HALF * (grav(i,j,k,2) + grav(i,j-1,k,2))
                enddo
             enddo
          enddo

          do k = lo(3), hi(3)+1*dg(3)
             do j = lo(2), hi(2)
                do i = lo(1), hi(1)
                   gravz(i,j,k) = HALF * (grav(i,j,k,3) + grav(i,j,k-1,3))
                enddo
             enddo
          enddo

       endif

    endif

    do k = lo(3),hi(3)
       do j = lo(2),hi(2)
          do i = lo(1),hi(1)

             loc = position(i,j,k) - center

             rhoo    = uold(i,j,k,URHO)
             rhooinv = ONE / uold(i,j,k,URHO)

             rhon    = unew(i,j,k,URHO)
             rhoninv = ONE / unew(i,j,k,URHO)

             src = ZERO
             snew = unew(i,j,k,:)

             ! **** Start Diagnostics ****
             old_re = snew(UEDEN)
             old_ke = HALF * sum(snew(UMX:UMZ)**2) * rhoninv
             old_rhoeint = snew(UEDEN) - old_ke
             old_mom = snew(UMX:UMZ)
             ! ****   End Diagnostics ****

             ! Define old source terms

             vold = uold(i,j,k,UMX:UMZ) * rhooinv

             Sr_old = rhoo * gold(i,j,k,:)
             SrE_old = dot_product(vold, Sr_old)

             ! Define new source terms

             vnew = snew(UMX:UMZ) * rhoninv

             Sr_new = rhon * gnew(i,j,k,:)
             SrE_new = dot_product(vnew, Sr_new)

             ! Define corrections to source terms

             Srcorr = HALF * (Sr_new - Sr_old)

             ! Correct momenta

             src(UMX:UMZ) = Srcorr

             snew(UMX:UMZ) = snew(UMX:UMZ) + dt * src(UMX:UMZ)

#ifdef HYBRID_MOMENTUM
             call add_hybrid_momentum_source(loc, src(UMR:UMP), Srcorr)

             snew(UMR:UMP) = snew(UMR:UMP) + dt * src(UMR:UMP)
#endif

             ! Correct energy

             if (grav_source_type .eq. 1) then

                ! If grav_source_type == 1, then we calculated SrEcorr before updating the velocities.

                SrEcorr = HALF * (SrE_new - SrE_old)

             else if (grav_source_type .eq. 2) then

                ! For this source type, we first update the momenta
                ! before we calculate the energy source term.

                vnew = snew(UMX:UMZ) * rhoninv
                SrE_new = dot_product(vnew, Sr_new)

                SrEcorr = HALF * (SrE_new - SrE_old)

             else if (grav_source_type .eq. 3) then

                ! Instead of calculating the energy source term explicitly,
                ! we simply update the kinetic energy.

                new_ke = HALF * sum(snew(UMX:UMZ)**2) * rhoninv
                SrEcorr = new_ke - old_ke

             else if (grav_source_type .eq. 4) then

                ! First, subtract the predictor step we applied earlier.

                SrEcorr = - SrE_old

                ! The change in the gas energy is equal in magnitude to, and opposite in sign to,
                ! the change in the gravitational potential energy, rho * phi.
                ! This must be true for the total energy, rho * E_gas + rho * phi, to be conserved.
                ! Consider as an example the zone interface i+1/2 in between zones i and i + 1.
                ! There is an amount of mass drho_{i+1/2} leaving the zone. From this zone's perspective
                ! it starts with a potential phi_i and leaves the zone with potential phi_{i+1/2} =
                ! (1/2) * (phi_{i-1}+phi_{i}). Therefore the new rotational energy is equal to the mass
                ! change multiplied by the difference between these two potentials.
                ! This is a generalization of the cell-centered approach implemented in
                ! the other source options, which effectively are equal to
                ! SrEcorr = - drho(i,j,k) * phi(i,j,k),
                ! where drho(i,j,k) = HALF * (unew(i,j,k,URHO) - uold(i,j,k,URHO)).

                ! Note that in the hydrodynamics step, the fluxes used here were already
                ! multiplied by dA and dt, so dividing by the cell volume is enough to
                ! get the density change (flux * dt * dA / dV). We then divide by dt
                ! so that we get the source term and not the actual update, which will
                ! be applied later by multiplying by dt.

                if (gravity_type == "PoissonGrav" .or. (gravity_type == "MonopoleGrav" .and. get_g_from_phi == 1) ) then

                   SrEcorr = SrEcorr - (HALF / dt) * ( flux1(i        ,j,k,URHO)  * (phi(i,j,k) - phi(i-1,j,k)) - &
                                                       flux1(i+1*dg(1),j,k,URHO)  * (phi(i,j,k) - phi(i+1,j,k)) + &
                                                       flux2(i,j        ,k,URHO)  * (phi(i,j,k) - phi(i,j-1,k)) - &
                                                       flux2(i,j+1*dg(2),k,URHO)  * (phi(i,j,k) - phi(i,j+1,k)) + &
                                                       flux3(i,j,k        ,URHO)  * (phi(i,j,k) - phi(i,j,k-1)) - &
                                                       flux3(i,j,k+1*dg(3),URHO)  * (phi(i,j,k) - phi(i,j,k+1)) ) / vol(i,j,k)

                else

                   ! However, at present phi is usually only actually filled for Poisson gravity.
                   ! Here's an alternate version that only requires the use of the
                   ! gravitational acceleration. It relies on the concept that, to second order,
                   ! g_{i+1/2} = -( phi_{i+1} - phi_{i} ) / dx.

                   SrEcorr = SrEcorr + (HALF / dt) * ( flux1(i        ,j,k,URHO) * gravx(i  ,j,k) * dx(1) + &
                                                       flux1(i+1*dg(1),j,k,URHO) * gravx(i+1,j,k) * dx(1) + &
                                                       flux2(i,j        ,k,URHO) * gravy(i,j  ,k) * dx(2) + &
                                                       flux2(i,j+1*dg(2),k,URHO) * gravy(i,j+1,k) * dx(2) + &
                                                       flux3(i,j,k        ,URHO) * gravz(i,j,k  ) * dx(3) + &
                                                       flux3(i,j,k+1*dg(3),URHO) * gravz(i,j,k+1) * dx(3) ) / vol(i,j,k)

                endif

             else
                call bl_error("Error:: gravity_sources_nd.F90 :: invalid grav_source_type")
             end if

             src(UEDEN) = SrEcorr

             snew(UEDEN) = snew(UEDEN) + dt * SrEcorr

             ! **** Start Diagnostics ****
             new_ke = HALF * sum(snew(UMX:UMZ)**2) * rhoninv
             new_rhoeint = snew(UEDEN) - new_ke
             E_added =  E_added + (snew(UEDEN) - old_re) * vol(i,j,k)
             mom_added = mom_added + (snew(UMX:UMZ) - old_mom) * vol(i,j,k)
             ! ****   End Diagnostics ****

             ! Add to the outgoing source array.

             source(i,j,k,:) = src

          enddo
       enddo
    enddo

    if (grav_source_type .eq. 4) then
       call bl_deallocate(phi)
       call bl_deallocate(grav)
       call bl_deallocate(gravx)
       call bl_deallocate(gravy)
       call bl_deallocate(gravz)
    endif

  end subroutine ca_corrgsrc

#else

  subroutine ca_gsrc(lo,hi,domlo,domhi,uold,uold_lo,uold_hi, &
                     source,src_lo,src_hi,dx,dt,time) bind(C, name="ca_gsrc")

    use prob_params_module, only : dim
    use meth_params_module, only : NVAR, URHO, UMX, UMZ, UEDEN, grav_source_type, const_grav
    use bl_constants_module
    use math_module, only: cross_product
#ifdef HYBRID_MOMENTUM
    use meth_params_module, only: UMR, UMP
    use hybrid_advection_module, only: add_hybrid_momentum_source
#endif

    implicit none

    integer          :: lo(3), hi(3)
    integer          :: domlo(3), domhi(3)
    integer          :: uold_lo(3), uold_hi(3)
    integer          :: src_lo(3), src_hi(3)

    double precision :: uold(uold_lo(1):uold_hi(1),uold_lo(2):uold_hi(2),uold_lo(3):uold_hi(3),NVAR)
    double precision :: source(src_lo(1):src_hi(1),src_lo(2):src_hi(2),src_lo(3):src_hi(3),NVAR)
    double precision :: dx(3), dt, time

    double precision :: rho, rhoInv
    double precision :: Sr(3), SrE
    double precision :: old_rhoeint, new_rhoeint, old_ke, new_ke, old_re, old_mom(3)
    integer          :: i, j, k

    double precision :: src(NVAR)

    ! Temporary array for seeing what the new state would be if the update were applied here.

    double precision :: snew(NVAR)

    ! Gravitational source options for how to add the work to (rho E):
    ! grav_source_type =
    ! 1: Original version ("does work")
    ! 2: Modification of type 1 that updates the momentum before constructing the energy corrector
    ! 3: Puts all gravitational work into KE, not (rho e)
    ! 4: Conservative energy formulation

    ! Add gravitational source terms
    do k = lo(3), hi(3)
       do j = lo(2), hi(2)
          do i = lo(1), hi(1)
             rho    = uold(i,j,k,URHO)
             rhoInv = ONE / rho

             src = ZERO
             snew = uold(i,j,k,:)

             ! **** Start Diagnostics ****
             old_re = snew(UEDEN)
             old_ke = HALF * sum(snew(UMX:UMZ)**2) * rhoInv
             old_rhoeint = snew(UEDEN) - old_ke
             old_mom = snew(UMX:UMZ)
             ! ****   End Diagnostics ****

             Sr(:)   = 0.d0
             Sr(dim) = rho * const_grav

             src(UMX:UMZ) = Sr

             snew(UMX:UMZ) = snew(UMX:UMZ) + dt * src(UMX:UMZ)

#ifdef HYBRID_MOMENTUM
             call add_hybrid_momentum_source(loc, src(UMR:UMP), Sr)
 
             snew(UMR:UMP) = snew(UMR:UMP) + dt * src(UMR:UMP)
#endif 

             if (grav_source_type == 1 .or. grav_source_type == 2) then

                ! Src = rho u dot g, evaluated with all quantities at t^n

                SrE = dot_product(uold(i,j,k,UMX:UMZ) * rhoInv, Sr)

             else if (grav_source_type .eq. 3) then

                new_ke = HALF * sum(snew(UMX:UMZ)**2) * rhoInv
                SrE = new_ke - old_ke

             else if (grav_source_type .eq. 4) then

                ! The conservative energy formulation does not strictly require
                ! any energy source-term here, because it depends only on the
                ! fluid motions from the hydrodynamical fluxes which we will only
                ! have when we get to the 'corrector' step. Nevertheless we add a
                ! predictor energy source term in the way that the other methods
                ! do, for consistency. We will fully subtract this predictor value
                ! during the corrector step, so that the final result is correct.
                ! Here we use the same approach as grav_source_type == 2.

                SrE = dot_product(uold(i,j,k,UMX:UMZ) * rhoInv, Sr)

             else
                call bl_error("Error:: gravity_sources_nd.F90 :: invalid grav_source_type")
             end if

             src(UEDEN) = SrE

             ! **** Start Diagnostics ****
             new_ke = HALF * sum(snew(UMX:UMZ)**2) * rhoInv
             new_rhoeint = snew(UEDEN) - new_ke
             ! ****   End Diagnostics ****

             ! Add to the outgoing source array.

             source(i,j,k,:) = src

          enddo
       enddo
    enddo

  end subroutine ca_gsrc

  ! :::
  ! ::: ------------------------------------------------------------------
  ! :::

  subroutine ca_corrgsrc(lo,hi,domlo,domhi, &
                         uold,uo_lo,uo_hi, &
                         unew,un_lo,un_hi, &
                         source,sr_lo,sr_hi, &
                         flux1,f1_lo,f1_hi, &
                         flux2,f2_lo,f2_hi, &
                         flux3,f3_lo,f3_hi, &
                         dx,dt,time, &
                         vol,vol_lo,vol_hi) bind(C, name="ca_corrgsrc")

    use mempool_module, only : bl_allocate, bl_deallocate
    use meth_params_module, only : NVAR, URHO, UMX, UMZ, UEDEN, &
                                   grav_source_type, const_grav
    use prob_params_module, only : dg, dim
    use bl_constants_module
    use multifab_module
#ifdef HYBRID_MOMENTUM
    use meth_params_module, only: UMR, UMP
    use hybrid_advection_module, only : add_hybrid_momentum_source
#endif

    implicit none

    integer          :: lo(3), hi(3)
    integer          :: domlo(3), domhi(3)

    integer          :: uo_lo(3), uo_hi(3)
    integer          :: un_lo(3), un_hi(3)
    integer          :: sr_lo(3), sr_hi(3)
    integer          :: f1_lo(3), f1_hi(3)
    integer          :: f2_lo(3), f2_hi(3)
    integer          :: f3_lo(3), f3_hi(3)
    integer          :: vol_lo(3), vol_hi(3)


    ! Old and new time state data

    double precision :: uold(uo_lo(1):uo_hi(1),uo_lo(2):uo_hi(2),uo_lo(3):uo_hi(3),NVAR)
    double precision :: unew(un_lo(1):un_hi(1),un_lo(2):un_hi(2),un_lo(3):un_hi(3),NVAR)

    ! The source term to send back

    double precision :: source(sr_lo(1):sr_hi(1),sr_lo(2):sr_hi(2),sr_lo(3):sr_hi(3),NVAR)

    ! Hydrodynamics fluxes

    double precision :: flux1(f1_lo(1):f1_hi(1),f1_lo(2):f1_hi(2),f1_lo(3):f1_hi(3),NVAR)
    double precision :: flux2(f2_lo(1):f2_hi(1),f2_lo(2):f2_hi(2),f2_lo(3):f2_hi(3),NVAR)
    double precision :: flux3(f3_lo(1):f3_hi(1),f3_lo(2):f3_hi(2),f3_lo(3):f3_hi(3),NVAR)

    double precision :: vol(vol_lo(1):vol_hi(1),vol_lo(2):vol_hi(2),vol_lo(3):vol_hi(3))
    double precision :: dx(3), dt, time

    integer          :: i, j, k

    double precision :: Sr_old(3), Sr_new(3), Srcorr(3)
    double precision :: vold(3), vnew(3)
    double precision :: SrE_old, SrE_new, SrEcorr
    double precision :: rhoo, rhooinv, rhon, rhoninv

    double precision :: old_ke, old_rhoeint, old_re
    double precision :: new_ke, new_rhoeint
    double precision :: old_mom(3)

    double precision :: src(NVAR)

    ! Temporary array for seeing what the new state would be if the update were applied here.

    double precision :: snew(NVAR)

    ! Gravitational source options for how to add the work to (rho E):
    ! grav_source_type =
    ! 1: Original version ("does work")
    ! 2: Modification of type 1 that updates the U before constructing SrEcorr
    ! 3: Puts all gravitational work into KE, not (rho e)
    ! 4: Conservative gravity approach (discussed in first white dwarf merger paper).

    do k = lo(3),hi(3)
       do j = lo(2),hi(2)
          do i = lo(1),hi(1)

             rhoo    = uold(i,j,k,URHO)
             rhooinv = ONE / uold(i,j,k,URHO)

             rhon    = unew(i,j,k,URHO)
             rhoninv = ONE / unew(i,j,k,URHO)

             src = ZERO
             snew = unew(i,j,k,:)

             ! **** Start Diagnostics ****
             old_re = snew(UEDEN)
             old_ke = HALF * sum(snew(UMX:UMZ)**2) * rhoninv
             old_rhoeint = snew(UEDEN) - old_ke
             old_mom = snew(UMX:UMZ)
             ! ****   End Diagnostics ****

             ! Define old source terms

             vold = uold(i,j,k,UMX:UMZ) * rhooinv

             Sr_old(1:3) = 0.d0
             Sr_old(dim) = rhoo * const_grav
             SrE_old     = dot_product(vold, Sr_old)

             ! Define new source terms

             vnew = snew(UMX:UMZ) * rhoninv

             Sr_new(1:3) = 0.d0
             Sr_new(dim) = rhon * const_grav
             SrE_new     = dot_product(vnew, Sr_new)

             ! Define corrections to source terms

             Srcorr = HALF * (Sr_new - Sr_old)

             ! Correct momenta

             src(UMX:UMZ) = Srcorr

             snew(UMX:UMZ) = snew(UMX:UMZ) + dt * src(UMX:UMZ)

#ifdef HYBRID_MOMENTUM
             call add_hybrid_momentum_source(loc, src(UMR:UMP), Srcorr)
 
             snew(UMR:UMP) = snew(UMR:UMP) + dt * src(UMR:UMP)
#endif

             ! Correct energy

             if (grav_source_type .eq. 1) then

                ! If grav_source_type == 1, then we calculated SrEcorr before updating the velocities.

                SrEcorr = HALF * (SrE_new - SrE_old)

             else if (grav_source_type .eq. 2) then

                ! For this source type, we first update the momenta
                ! before we calculate the energy source term.

                vnew = snew(UMX:UMZ) * rhoninv
                SrE_new = dot_product(vnew, Sr_new)

                SrEcorr = HALF * (SrE_new - SrE_old)

             else if (grav_source_type .eq. 3) then

                ! Instead of calculating the energy source term explicitly,
                ! we simply update the kinetic energy.

                new_ke = HALF * sum(snew(UMX:UMZ)**2) * rhoninv
                SrEcorr = new_ke - old_ke

             else if (grav_source_type .eq. 4) then

                ! First, subtract the predictor step we applied earlier.

                SrEcorr = - SrE_old

                ! The change in the gas energy is equal in magnitude to, and opposite in sign to,
                ! the change in the gravitational potential energy, rho * phi.
                ! This must be true for the total energy, rho * E_gas + rho * phi, to be conserved.
                ! Consider as an example the zone interface i+1/2 in between zones i and i + 1.
                ! There is an amount of mass drho_{i+1/2} leaving the zone. From this zone's perspective
                ! it starts with a potential phi_i and leaves the zone with potential phi_{i+1/2} =
                ! (1/2) * (phi_{i-1}+phi_{i}). Therefore the new rotational energy is equal to the mass
                ! change multiplied by the difference between these two potentials.
                ! This is a generalization of the cell-centered approach implemented in
                ! the other source options, which effectively are equal to
                ! SrEcorr = - drho(i,j,k) * phi(i,j,k),
                ! where drho(i,j,k) = HALF * (unew(i,j,k,URHO) - uold(i,j,k,URHO)).

                ! Note that in the hydrodynamics step, the fluxes used here were already
                ! multiplied by dA and dt, so dividing by the cell volume is enough to
                ! get the density change (flux * dt * dA / dV). We then divide by dt
                ! so that we get the source term and not the actual update, which will
                ! be applied later by multiplying by dt.

                ! However, at present phi is usually only actually filled for Poisson gravity.
                ! Here's an alternate version that only requires the use of the
                ! gravitational acceleration. It relies on the concept that, to second order,
                ! g_{i+1/2} = -( phi_{i+1} - phi_{i} ) / dx.

                if (dim .eq. 1) then
                   SrEcorr = SrEcorr + (HALF / dt) * ( flux1(i        ,j,k,URHO) * const_grav * dx(1) + &
                                                       flux1(i+1*dg(1),j,k,URHO) * const_grav * dx(1) ) / vol(i,j,k)
                else if (dim .eq. 2) then
                   SrEcorr = SrEcorr + (HALF / dt) * ( flux2(i,j        ,k,URHO) * const_grav * dx(2) + &
                                                       flux2(i,j+1*dg(2),k,URHO) * const_grav * dx(2) ) / vol(i,j,k) 
                else if (dim .eq. 3) then
                   SrEcorr = SrEcorr + (HALF / dt) * ( flux3(i,j,k        ,URHO) * const_grav * dx(3) + &
                                                       flux3(i,j,k+1*dg(3),URHO) * const_grav * dx(3) ) / vol(i,j,k)
                end if

             else
                call bl_error("Error:: gravity_sources_nd.F90 :: invalid grav_source_type")
             end if

             src(UEDEN) = SrEcorr

             ! **** Start Diagnostics ****
             new_ke = HALF * sum(snew(UMX:UMZ)**2) * rhoninv
             new_rhoeint = snew(UEDEN) - new_ke
             ! ****   End Diagnostics ****

             ! Add to the outgoing source array.

             source(i,j,k,:) = src

          enddo
       enddo
    enddo

  end subroutine ca_corrgsrc
#endif

end module gravity_sources_module
