! This function is designed to permit you to update
! the sponge parameters as a time-dependent process.
! By default it does nothing, meaning that the probin
! parameters are used.

subroutine update_sponge_params(time) bind(C, name="update_sponge_params")

  use sponge_module

  implicit none
  
  double precision, intent(in) :: time
  
end subroutine update_sponge_params
