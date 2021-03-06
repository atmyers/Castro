subroutine ca_ext_src(lo,hi, &
                      old_state,old_state_l1,old_state_l2,old_state_h1,old_state_h2,&
                      new_state,new_state_l1,new_state_l2,new_state_h1,new_state_h2,&
                      src,src_l1,src_l2,src_h1,src_h2,problo,dx,time,dt)

  use bl_types, only: dp_t
  use bl_constants_module, only: THIRD
  use meth_params_module, only : NVAR, UEDEN, UEINT, URHO, UTEMP, UFS
  use network, only: network_species_index

  implicit none
  integer         , intent(in   ) :: lo(2),hi(2)
  integer         , intent(in   ) :: old_state_l1,old_state_l2,old_state_h1,old_state_h2
  integer         , intent(in   ) :: new_state_l1,new_state_l2,new_state_h1,new_state_h2
  integer         , intent(in   ) :: src_l1,src_l2,src_h1,src_h2
  real(kind=dp_t) , intent(in   ) :: old_state(old_state_l1:old_state_h1,old_state_l2:old_state_h2,NVAR)
  real(kind=dp_t) , intent(in   ) :: new_state(new_state_l1:new_state_h1,new_state_l2:new_state_h2,NVAR)
  real(kind=dp_t) , intent(  out) :: src(    src_l1:  src_h1,  src_l2:src_h2  ,NVAR)
  real(kind=dp_t) , intent(in   ) :: problo(2),dx(2),time,dt

  integer          :: i,j
  double precision :: x,y

  real(kind=dp_t) :: rho, temp, T6, T613, X_CNO, X_1, g14, eps_CNO

  integer, save :: ih1, ic12, in14, io16

  logical, save :: firstCall = .true.

  if (firstCall) then
     ih1 =  network_species_index("hydrogen-1")
     ic12 = network_species_index("carbon-12")
     in14 = network_species_index("nitrogen-14")
     io16 = network_species_index("oxygen-16")

     firstCall = .false.
  endif

  src(lo(1):hi(1),lo(2):hi(2),:) = 0.d0


  do j=lo(2),hi(2)
     !y = problo(2) + dx(2)*(float(j) + 0.5d0)

     do i=lo(1),hi(1)
        !x = problo(1) + dx(1)*(float(i) + 0.5d0)

        rho = new_state(i,j,URHO)

        T6 = new_state(i,j,UTEMP)/1.0d6
        T613 = T6**THIRD

        ! CNO abundance
        X_CNO = (new_state(i,j,UFS-1+ic12) + &
                 new_state(i,j,UFS-1+in14) + &
                 new_state(i,j,UFS-1+io16))/rho

        ! H abundance
        X_1 = new_state(i,j,UFS-1+ih1)/rho

        ! CNO heating from Kippenhahn & Weigert, Eq. 18.65                                                                            
        g14 = 1.0_dp_t + 2.7d-3*T613 - 7.78d-3*T613**2 - 1.49d-4*T6
        eps_CNO = 8.67e27_dp_t * g14 * X_CNO * X_1 * rho * exp(-152.28_dp_t/T613) / T613**2

        ! source terms
        src(i,j,UEDEN) = rho*eps_CNO
        src(i,j,UEINT) = rho*eps_CNO

     end do
  end do
  
end subroutine ca_ext_src
