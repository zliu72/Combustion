module kernels_module
  use chemistry_module, only : nspecies, molecular_weight
  use derivative_stencil_module, only : stencil_ng, first_deriv_8, M8, D8 
  use variables_module
  implicit none

  private

  public :: hypterm_3d, narrow_diffterm_3d, chemterm_3d, comp_courno_3d

contains

  subroutine hypterm_3d (lo,hi,dx,cons,clo,chi,q,qlo,qhi,rhs,rlo,rhi)

    integer,         intent(in ) :: lo(3),hi(3),clo(3),chi(3),qlo(3),qhi(3),rlo(3),rhi(3)
    double precision,intent(in ) :: dx(3)
    double precision,intent(in):: cons(clo(1):chi(1),clo(2):chi(2),clo(3):chi(3),ncons)
    double precision,intent(in)::    q(qlo(1):qhi(1),qlo(2):qhi(2),qlo(3):qhi(3),nprim)
    double precision           ::  rhs(rlo(1):rhi(1),rlo(2):rhi(2),rlo(3):rhi(3),ncons)

    integer          :: i,j,k,n
    double precision :: dxinv(3)
    double precision, allocatable :: tmpx(:), tmpy(:,:),tmpz(:,:,:)

    integer :: jj
    integer, parameter :: jblocksize=8

    do i=1,3
       dxinv(i) = 1.0d0 / dx(i)
    end do

    allocate(tmpz(lo(1)  :hi(1)  ,lo(2)  :hi(2)  ,lo(3)-4:hi(3)+4))
    allocate(tmpx(lo(1)-4:hi(1)+4))
    allocate(tmpy(lo(1)  :hi(1)  ,lo(2)-4:hi(2)+4))
    
    ! ------- BEGIN x-direction -------

    do k=lo(3),hi(3)
       do j=lo(2),hi(2)

          do i=lo(1),hi(1)
             rhs(i,j,k,irho) = rhs(i,j,k,irho) - dxinv(1) * &
                  first_deriv_8( cons(i-4:i+4,j,k,imx) ) 
          end do

          do i=lo(1)-4,hi(1)+4
             tmpx(i) = cons(i,j,k,imx)*q(i,j,k,qu)+q(i,j,k,qpres)
          end do
          do i=lo(1),hi(1)
             rhs(i,j,k,imx) = rhs(i,j,k,imx) - dxinv(1) * first_deriv_8(tmpx(i-4:i+4))
          end do

          do i=lo(1)-4,hi(1)+4
             tmpx(i) = cons(i,j,k,imy)*q(i,j,k,qu)
          end do
          do i=lo(1),hi(1)
             rhs(i,j,k,imy) = rhs(i,j,k,imy) - dxinv(1) * first_deriv_8(tmpx(i-4:i+4))
          end do

          do i=lo(1)-4,hi(1)+4
             tmpx(i) = cons(i,j,k,imz)*q(i,j,k,qu)
          end do
          do i=lo(1),hi(1)
             rhs(i,j,k,imz) = rhs(i,j,k,imz) - dxinv(1) * first_deriv_8(tmpx(i-4:i+4))
          end do

          do i=lo(1)-4,hi(1)+4
             tmpx(i) = (cons(i,j,k,iene)+q(i,j,k,qpres))*q(i,j,k,qu)
          end do
          do i=lo(1),hi(1)
             rhs(i,j,k,iene) = rhs(i,j,k,iene) - dxinv(1) * first_deriv_8(tmpx(i-4:i+4))
          end do

       end do
    end do

    do n = iry1, iry1+nspecies-1
       do k=lo(3),hi(3)
          do j=lo(2),hi(2)
    
             do i=lo(1)-4,hi(1)+4
                tmpx(i) = cons(i,j,k,n)*q(i,j,k,qu)
             end do
             do i=lo(1),hi(1)
                rhs(i,j,k,n) = rhs(i,j,k,n) - dxinv(1) * first_deriv_8(tmpx(i-4:i+4))
             end do

          enddo
       enddo
    enddo

    ! ------- END x-direction -------

    ! ------- BEGIN y-direction -------

    do k=lo(3),hi(3)
       do j=lo(2),hi(2)
          do i=lo(1),hi(1)
             rhs(i,j,k,irho)=rhs(i,j,k,irho) - dxinv(2) * &
                  first_deriv_8( cons(i,j-4:j+4,k,imy) )
          enddo
       enddo

       do j=lo(2)-4,hi(2)+4
          do i=lo(1),hi(1)
             tmpy(i,j) = cons(i,j,k,imx)*q(i,j,k,qv)
          end do
       end do
       do j=lo(2),hi(2)
          do i=lo(1),hi(1)
             rhs(i,j,k,imx)=rhs(i,j,k,imx) - dxinv(2) * first_deriv_8(tmpy(i,j-4:j+4))
          enddo
       enddo

       do j=lo(2)-4,hi(2)+4
          do i=lo(1),hi(1)
             tmpy(i,j) = cons(i,j,k,imy)*q(i,j,k,qv)+q(i,j,k,qpres)
          end do
       end do
       do j=lo(2),hi(2)
          do i=lo(1),hi(1)
             rhs(i,j,k,imy)=rhs(i,j,k,imy) - dxinv(2) * first_deriv_8(tmpy(i,j-4:j+4))
          enddo
       enddo

       do j=lo(2)-4,hi(2)+4
          do i=lo(1),hi(1)
             tmpy(i,j) = cons(i,j,k,imz)*q(i,j,k,qv)
          end do
       end do
       do j=lo(2),hi(2)
          do i=lo(1),hi(1)
             rhs(i,j,k,imz)=rhs(i,j,k,imz) - dxinv(2) * first_deriv_8(tmpy(i,j-4:j+4))
          enddo
       enddo

       do j=lo(2)-4,hi(2)+4
          do i=lo(1),hi(1)
             tmpy(i,j) = (cons(i,j,k,iene)+q(i,j,k,qpres))*q(i,j,k,qv)
          end do
       end do
       do j=lo(2),hi(2)
          do i=lo(1),hi(1)
             rhs(i,j,k,iene)=rhs(i,j,k,iene) - dxinv(2) * first_deriv_8(tmpy(i,j-4:j+4))
          enddo
       enddo
    enddo

    do n = iry1, iry1+nspecies-1
       do k=lo(3),hi(3)
          do j=lo(2)-4,hi(2)+4
             do i=lo(1),hi(1)
                tmpy(i,j) = cons(i,j,k,n)*q(i,j,k,qv)
             end do
          end do
          do j=lo(2),hi(2)
             do i=lo(1),hi(1)
                rhs(i,j,k,n) = rhs(i,j,k,n) - dxinv(2) * first_deriv_8(tmpy(i,j-4:j+4))
             end do
          enddo
       enddo
    enddo

    ! ------- END y-direction -------

    ! ------- BEGIN z-direction -------

    do k=lo(3),hi(3)
       do j=lo(2),hi(2)
          do i=lo(1),hi(1)
             rhs(i,j,k,irho)=rhs(i,j,k,irho) - dxinv(3) * &
                  first_deriv_8( cons(i,j,k-4:k+4,imz) )
          end do
       end do
    end do

    do k=lo(3)-4,hi(3)+4
       do j=lo(2),hi(2)
          do i=lo(1),hi(1)
             tmpz(i,j,k) = cons(i,j,k,imx) * q(i,j,k,qw)
          end do
       end do
    end do

    do k=lo(3),hi(3)
       do j=lo(2),hi(2)
          do i=lo(1),hi(1)
             rhs(i,j,k,imx)=rhs(i,j,k,imx) - dxinv(3) * first_deriv_8(tmpz(i,j,k-4:k+4))
          end do
       end do
    end do

    do k=lo(3)-4,hi(3)+4
       do j=lo(2),hi(2)
          do i=lo(1),hi(1)
             tmpz(i,j,k) = cons(i,j,k,imy) * q(i,j,k,qw)
          end do
       end do
    end do

    do k=lo(3),hi(3)
       do j=lo(2),hi(2)
          do i=lo(1),hi(1)
             rhs(i,j,k,imy)=rhs(i,j,k,imy) - dxinv(3) * first_deriv_8(tmpz(i,j,k-4:k+4))
          end do
       end do
    end do

    do k=lo(3)-4,hi(3)+4
       do j=lo(2),hi(2)
          do i=lo(1),hi(1)
             tmpz(i,j,k) = cons(i,j,k,imz)*q(i,j,k,qw) + q(i,j,k,qpres)
          end do
       end do
    end do

    do k=lo(3),hi(3)
       do j=lo(2),hi(2)
          do i=lo(1),hi(1)
             rhs(i,j,k,imz)=rhs(i,j,k,imz) - dxinv(3) * first_deriv_8(tmpz(i,j,k-4:k+4))
          end do
       end do
    end do

    do k=lo(3)-4,hi(3)+4
       do j=lo(2),hi(2)
          do i=lo(1),hi(1)
             tmpz(i,j,k) = (cons(i,j,k,iene)+q(i,j,k,qpres))*q(i,j,k,qw)
          end do
       end do
    end do

    do k=lo(3),hi(3)
       do j=lo(2),hi(2)
          do i=lo(1),hi(1)
             rhs(i,j,k,iene)=rhs(i,j,k,iene) - dxinv(3) * first_deriv_8(tmpz(i,j,k-4:k+4))
          end do
       end do
    end do

    do n = iry1, iry1+nspecies-1
       do k=lo(3)-4,hi(3)+4
          do j=lo(2),hi(2)
             do i=lo(1),hi(1)
                tmpz(i,j,k) = cons(i,j,k,n)*q(i,j,k,qw)
             end do
          end do
       end do

       do jj=lo(2),hi(2),jblocksize
       do k=lo(3),hi(3)
!          do j=lo(2),hi(2)
          do j=jj,min(jj+jblocksize-1,hi(2))
             do i=lo(1),hi(1)
                rhs(i,j,k,n) = rhs(i,j,k,n) - dxinv(3) * first_deriv_8(tmpz(i,j,k-4:k+4))
             end do
          enddo
       enddo
       end do ! jj
    enddo

    deallocate(tmpx,tmpy)
    deallocate(tmpz)

  end subroutine hypterm_3d


  subroutine narrow_diffterm_3d (lo,hi,dx,q,qlo,qhi,rhs,rlo,rhi,mu,xi,lam,dxy)

    integer,         intent(in):: lo(3),hi(3),qlo(3),qhi(3),rlo(3),rhi(3)
    double precision,intent(in):: dx(3)
    double precision,intent(in):: q  (qlo(1):qhi(1),qlo(2):qhi(2),qlo(3):qhi(3),nprim)
    double precision,intent(in):: mu (qlo(1):qhi(1),qlo(2):qhi(2),qlo(3):qhi(3))
    double precision,intent(in):: xi (qlo(1):qhi(1),qlo(2):qhi(2),qlo(3):qhi(3))
    double precision,intent(in):: lam(qlo(1):qhi(1),qlo(2):qhi(2),qlo(3):qhi(3))
    double precision,intent(in):: dxy(qlo(1):qhi(1),qlo(2):qhi(2),qlo(3):qhi(3),nspecies)
    double precision           :: rhs(rlo(1):rhi(1),rlo(2):rhi(2),rlo(3):rhi(3),ncons)

    double precision, allocatable, dimension(:,:,:) :: ux,uy,uz,vx,vy,vz,wx,wy,wz
    double precision, allocatable :: tmpx(:), tmpy(:,:),tmpz(:,:,:)
    double precision, allocatable, dimension(:,:,:) :: vsp,vsm, dpe
    double precision, allocatable, dimension(:,:,:,:) :: Hg, dpy, dxe
    ! dxy: diffusion coefficient of X in equation for Y
    ! dpy: diffusion coefficient of p in equation for Y
    ! dxe: diffusion coefficient of X in equation for energy
    ! dpe: diffusion coefficient of p in equation for energy

    double precision :: dxinv(3), dx2inv(3)
    double precision :: tauxx(lo(1):hi(1)),tauyy(lo(1):hi(1)),tauzz(lo(1):hi(1)),divu(lo(1):hi(1))
    integer          :: i,j,k,n, qxn, qyn, qhn
    integer :: dlo(3), dhi(3)

    double precision :: mmtmp(8,qlo(1):qhi(1)), Yhalf, hhalf
    double precision, allocatable, dimension(:,:,:,:) :: M8p
    double precision, allocatable, dimension(:,:,:) :: Hry

    integer :: jj 
    integer, parameter :: jblocksize=8

    do i = 1,3
       dxinv(i) = 1.0d0 / dx(i)
       dx2inv(i) = dxinv(i)**2
    end do

    dlo = lo - stencil_ng
    dhi = hi + stencil_ng

    allocate(ux( lo(1): hi(1),dlo(2):dhi(2),dlo(3):dhi(3)))
    allocate(vx( lo(1): hi(1),dlo(2):dhi(2),dlo(3):dhi(3)))
    allocate(wx( lo(1): hi(1),dlo(2):dhi(2),dlo(3):dhi(3)))

    allocate(uy(dlo(1):dhi(1), lo(2): hi(2),dlo(3):dhi(3)))
    allocate(vy(dlo(1):dhi(1), lo(2): hi(2),dlo(3):dhi(3)))
    allocate(wy(dlo(1):dhi(1), lo(2): hi(2),dlo(3):dhi(3)))

    allocate(uz(dlo(1):dhi(1),dlo(2):dhi(2), lo(3): hi(3)))
    allocate(vz(dlo(1):dhi(1),dlo(2):dhi(2), lo(3): hi(3)))
    allocate(wz(dlo(1):dhi(1),dlo(2):dhi(2), lo(3): hi(3)))

    allocate(vsp(dlo(1):dhi(1),dlo(2):dhi(2),dlo(3):dhi(3)))
    allocate(vsm(dlo(1):dhi(1),dlo(2):dhi(2),dlo(3):dhi(3)))

    allocate(tmpz( lo(1): hi(1), lo(2): hi(2),dlo(3):dhi(3)))
    allocate(tmpx(dlo(1):dhi(1)))
    allocate(tmpy( lo(1): hi(1),dlo(2):dhi(2)))

    do k=dlo(3),dhi(3)
       do j=dlo(2),dhi(2)
          do i=dlo(1),dhi(1)
             vsp(i,j,k) = xi(i,j,k) + FourThirds*mu(i,j,k)
             vsm(i,j,k) = xi(i,j,k) -  TwoThirds*mu(i,j,k)
          enddo
       enddo
    enddo

    do k=dlo(3),dhi(3)
       do j=dlo(2),dhi(2)
          do i=lo(1),hi(1)
             ux(i,j,k) = dxinv(1)*first_deriv_8(q(i-4:i+4,j,k,qu))
             vx(i,j,k) = dxinv(1)*first_deriv_8(q(i-4:i+4,j,k,qv))
             wx(i,j,k) = dxinv(1)*first_deriv_8(q(i-4:i+4,j,k,qw))
          enddo
       enddo
    enddo

    do k=dlo(3),dhi(3)
       do j=lo(2),hi(2)   

          do i=dlo(1),dhi(1)
             uy(i,j,k) = dxinv(2)*first_deriv_8(q(i,j-4:j+4,k,qu))
             vy(i,j,k) = dxinv(2)*first_deriv_8(q(i,j-4:j+4,k,qv))
             wy(i,j,k) = dxinv(2)*first_deriv_8(q(i,j-4:j+4,k,qw))
          enddo

       enddo
    enddo

    do k=lo(3),hi(3)
       do j=dlo(2),dhi(2)    
          do i=dlo(1),dhi(1)
             uz(i,j,k) = dxinv(3)*first_deriv_8(q(i,j,k-4:k+4,qu))
             vz(i,j,k) = dxinv(3)*first_deriv_8(q(i,j,k-4:k+4,qv))
             wz(i,j,k) = dxinv(3)*first_deriv_8(q(i,j,k-4:k+4,qw))
          end do      
       end do
    end do

    !----- mx -----

    !----- mx : d()/dx -----
    do k=lo(3),hi(3)
       do j=lo(2),hi(2)

          do i=lo(1)-4,hi(1)+4
             tmpx(i) = vsm(i,j,k)*(vy(i,j,k)+wz(i,j,k))
          end do

          do i=lo(1),hi(1)
             rhs(i,j,k,imx) = rhs(i,j,k,imx) + dxinv(1) * first_deriv_8(tmpx(i-4:i+4)) 
          end do

       end do
    end do

    !----- mx : d()/dy -----
    do k=lo(3),hi(3)

       do j=lo(2)-4,hi(2)+4
          do i=lo(1),hi(1)
             tmpy(i,j) = mu(i,j,k)*vx(i,j,k)
          end do
       end do

       do j=lo(2),hi(2)
          do i=lo(1),hi(1)
             rhs(i,j,k,imx) = rhs(i,j,k,imx) + dxinv(2) * first_deriv_8(tmpy(i,j-4:j+4)) 
          end do
       end do

    end do

    !----- mx : d()/dz -----
    do k=lo(3)-4,hi(3)+4
       do j=lo(2),hi(2)
          do i=lo(1),hi(1)
             tmpz(i,j,k) = mu(i,j,k)*wx(i,j,k)
          end do
       end do
    end do
    do k=lo(3),hi(3)
       do j=lo(2),hi(2)
          do i=lo(1),hi(1)
             rhs(i,j,k,imx) = rhs(i,j,k,imx) + dxinv(3) * first_deriv_8(tmpz(i,j,k-4:k+4)) 
          end do
       end do
    end do

    !----- my -----

    do k=lo(3),hi(3)
       do j=lo(2),hi(2)

          do i=lo(1)-4,hi(1)+4
             tmpx(i) = mu(i,j,k)*uy(i,j,k)
          end do

          do i=lo(1),hi(1)
             rhs(i,j,k,imy) = rhs(i,j,k,imy) + dxinv(1) * first_deriv_8(tmpx(i-4:i+4)) 
          end do

       end do
    end do

    do k=lo(3),hi(3)

       do j=lo(2)-4,hi(2)+4
          do i=lo(1),hi(1)
             tmpy(i,j) = vsm(i,j,k)*(ux(i,j,k)+wz(i,j,k))
          end do
       end do

       do j=lo(2),hi(2)
          do i=lo(1),hi(1)
             rhs(i,j,k,imy) = rhs(i,j,k,imy) + dxinv(2) * first_deriv_8(tmpy(i,j-4:j+4)) 
          end do
       end do

    end do

    do k=lo(3)-4,hi(3)+4
       do j=lo(2),hi(2)
          do i=lo(1),hi(1)
             tmpz(i,j,k) = mu(i,j,k)*wy(i,j,k)
          end do
       end do
    end do
    do k=lo(3),hi(3)
       do j=lo(2),hi(2)
          do i=lo(1),hi(1)
             rhs(i,j,k,imy) = rhs(i,j,k,imy) + dxinv(3) * first_deriv_8(tmpz(i,j,k-4:k+4)) 
          end do
       end do
    end do

    !----- mz -----

    do k=lo(3),hi(3)
       do j=lo(2),hi(2)

          do i=lo(1)-4,hi(1)+4
             tmpx(i) = mu(i,j,k)*uz(i,j,k)
          end do

          do i=lo(1),hi(1)
             rhs(i,j,k,imz) = rhs(i,j,k,imz) + dxinv(1) * first_deriv_8(tmpx(i-4:i+4))
          end do

       end do
    end do

    do k=lo(3),hi(3)

       do j=lo(2)-4,hi(2)+4
          do i=lo(1),hi(1)
             tmpy(i,j) = mu(i,j,k)*vz(i,j,k)
          end do
       end do

       do j=lo(2),hi(2)
          do i=lo(1),hi(1)
             rhs(i,j,k,imz) = rhs(i,j,k,imz) + dxinv(2) * first_deriv_8(tmpy(i,j-4:j+4))
          end do
       end do

    end do

    do k=lo(3)-4,hi(3)+4
       do j=lo(2),hi(2)
          do i=lo(1),hi(1)
             tmpz(i,j,k) = vsm(i,j,k)*(ux(i,j,k)+vy(i,j,k))
          end do
       end do
    end do
    do k=lo(3),hi(3)
       do j=lo(2),hi(2)
          do i=lo(1),hi(1)
             rhs(i,j,k,imz) = rhs(i,j,k,imz) + dxinv(3) * first_deriv_8(tmpz(i,j,k-4:k+4))
          end do
       end do
    end do

    !----- energy -----

    do k=lo(3),hi(3)
       do j=lo(2),hi(2)
          do i=lo(1),hi(1)

             divu(i) = (ux(i,j,k)+vy(i,j,k)+wz(i,j,k))*vsm(i,j,k)
             tauxx(i) = 2.d0*mu(i,j,k)*ux(i,j,k) + divu(i)
             tauyy(i) = 2.d0*mu(i,j,k)*vy(i,j,k) + divu(i)
             tauzz(i) = 2.d0*mu(i,j,k)*wz(i,j,k) + divu(i)
             
             ! change in internal energy
             rhs(i,j,k,iene) = rhs(i,j,k,iene) + &
                  tauxx(i)*ux(i,j,k) + tauyy(i)*vy(i,j,k) + tauzz(i)*wz(i,j,k) &
                  + mu(i,j,k)*((uy(i,j,k)+vx(i,j,k))**2 &
                  &          + (wx(i,j,k)+uz(i,j,k))**2 &
                  &          + (vz(i,j,k)+wy(i,j,k))**2 )

          end do
       end do
    end do

    deallocate(tmpx,tmpy)

    deallocate(ux,uy,uz,vx,vy,vz,wx,wy,wz,tmpz)

    allocate(dpy(dlo(1):dhi(1),dlo(2):dhi(2),dlo(3):dhi(3),nspecies))
    allocate(dxe(dlo(1):dhi(1),dlo(2):dhi(2),dlo(3):dhi(3),nspecies))
    allocate(dpe(dlo(1):dhi(1),dlo(2):dhi(2),dlo(3):dhi(3)))

    allocate(Hg(lo(1):hi(1)+1,lo(2):hi(2)+1,lo(3):hi(3)+1,2:ncons))

    allocate(M8p(8,lo(1):hi(1)+1,lo(2):hi(2)+1,lo(3):hi(3)+1))
    allocate(Hry(  lo(1):hi(1)+1,lo(2):hi(2)+1,lo(3):hi(3)+1))

    dpe = 0.d0

    do n=1,nspecies
       qxn = qx1+n-1
       qyn = qy1+n-1
       qhn = qh1+n-1
       do k=dlo(3),dhi(3)
          do j=dlo(2),dhi(2)
             do i=dlo(1),dhi(1)
                dpy(i,j,k,n) = dxy(i,j,k,n)/q(i,j,k,qpres)*(q(i,j,k,qxn)-q(i,j,k,qyn))
                dxe(i,j,k,n) = dxy(i,j,k,n)*q(i,j,k,qhn)
                dpe(i,j,k) = dpe(i,j,k) + dpy(i,j,k,n)*q(i,j,k,qhn)
             end do
          end do
       end do
    end do

    ! ------- BEGIN x-direction -------

    do k=lo(3),hi(3)
       do j=lo(2),hi(2)
          do i=lo(1),hi(1)+1
             mmtmp(1:8,i) = matmul(vsp(i-4:i+3,j,k), M8)
             Hg(i,j,k,imx) = dot_product(mmtmp(1:8,i), q(i-4:i+3,j,k,qu))
          end do
       end do
    end do

    do k=lo(3),hi(3)
       do j=lo(2),hi(2)
          do i=lo(1),hi(1)+1
             mmtmp(1:8,i) = matmul(mu(i-4:i+3,j,k), M8)
             Hg(i,j,k,imy) = dot_product(mmtmp(1:8,i), q(i-4:i+3,j,k,qv))
             Hg(i,j,k,imz) = dot_product(mmtmp(1:8,i), q(i-4:i+3,j,k,qw))
          end do
       end do
    end do

    do k=lo(3),hi(3)
       do j=lo(2),hi(2)
          do i=lo(1),hi(1)+1
             mmtmp(1:8,i) = matmul(lam(i-4:i+3,j,k), M8)
             Hg(i,j,k,iene) = dot_product(mmtmp(1:8,i), q(i-4:i+3,j,k,qtemp))
          end do
       end do
    end do

    do k=lo(3),hi(3)
       do j=lo(2),hi(2)
          do i=lo(1),hi(1)+1
             mmtmp(1:8,i) = matmul(M8, q(i-4:i+3,j,k,qpres))
             Hg(i,j,k,iene) = Hg(i,j,k,iene) + dot_product(dpe(i-4:i+3,j,k), mmtmp(1:8,i))
          end do
          do i=lo(1),hi(1)+1
             M8p(:,i,j,k) = mmtmp(:,i)             
          end do
       end do
    end do

    do n = 1, nspecies
       qxn = qx1+n-1

       do k=lo(3),hi(3)
          do j=lo(2),hi(2)
             do i=lo(1),hi(1)+1 
                Hg(i,j,k,iry1+n-1) = dot_product(dpy(i-4:i+3,j,k,n), M8p(:,i,j,k))
             end do
          end do
       end do

       do k=lo(3),hi(3)
          do j=lo(2),hi(2)    
             do i=lo(1),hi(1)+1
                mmtmp(1:8,i) = matmul(M8, q(i-4:i+3,j,k,qxn))
                Hg(i,j,k,iene) = Hg(i,j,k,iene) + dot_product(dxe(i-4:i+3,j,k,n), mmtmp(1:8,i))
                Hg(i,j,k,iry1+n-1) = Hg(i,j,k,iry1+n-1) &
                     + dot_product(dxy(i-4:i+3,j,k,n), mmtmp(1:8,i))
             end do
          end do
       end do
    end do

    ! correction
    
    Hry = 0.d0

    do n = 1, nspecies
       do k=lo(3),hi(3)
          do j=lo(2),hi(2)
             do i=lo(1),hi(1)+1
                Hry(i,j,k) = Hry(i,j,k) + Hg(i,j,k,iry1+n-1)
             end do
          end do
       end do
    end do
    
    do n = 1, nspecies
       qyn = qy1+n-1
       qhn = qh1+n-1
       do k=lo(3),hi(3)
          do j=lo(2),hi(2)
             do i=lo(1),hi(1)+1
                Yhalf = 0.5d0*(q(i-1,j,k,qyn) + q(i,j,k,qyn))
                hhalf = 0.5d0*(q(i-1,j,k,qhn) + q(i,j,k,qhn))
                Hg(i,j,k,iry1+n-1) = Hg(i,j,k,iry1+n-1)- (Yhalf*Hry(i,j,k))
                Hg(i,j,k,iene) = Hg(i,j,k,iene) - (Yhalf*Hry(i,j,k))*hhalf
             end do
          end do
       end do
    end do

    ! add x-direction rhs
    do n=2,ncons
       do k=lo(3),hi(3)
          do j=lo(2),hi(2)
             do i=lo(1),hi(1)
                rhs(i,j,k,n) = rhs(i,j,k,n) + (Hg(i+1,j,k,n) - Hg(i,j,k,n)) * dx2inv(1)
             end do
          end do
       end do
    end do

    ! ------- END x-direction -------

    ! ------- BEGIN y-direction -------

    do k=lo(3),hi(3)
       do j=lo(2),hi(2)+1
          do i=lo(1),hi(1)             
             mmtmp(1:8,i) = matmul(mu(i,j-4:j+3,k), M8)
             Hg(i,j,k,imx) = dot_product(mmtmp(1:8,i), q(i,j-4:j+3,k,qu))
             Hg(i,j,k,imz) = dot_product(mmtmp(1:8,i), q(i,j-4:j+3,k,qw))
          end do
       end do
    end do

    do k=lo(3),hi(3)
       do j=lo(2),hi(2)+1
          do i=lo(1),hi(1)
             mmtmp(1:8,i) = matmul(vsp(i,j-4:j+3,k), M8)
             Hg(i,j,k,imy) = dot_product(mmtmp(1:8,i), q(i,j-4:j+3,k,qv))
          end do
       end do
    end do

    do k=lo(3),hi(3)
       do j=lo(2),hi(2)+1
          do i=lo(1),hi(1)
             mmtmp(1:8,i) = matmul(lam(i,j-4:j+3,k), M8)
             Hg(i,j,k,iene) = dot_product(mmtmp(1:8,i), q(i,j-4:j+3,k,qtemp))
          end do
       end do
    end do

    do k=lo(3),hi(3)
       do j=lo(2),hi(2)+1
          do i=lo(1),hi(1)
             mmtmp(1:8,i) = matmul(M8, q(i,j-4:j+3,k,qpres))
             Hg(i,j,k,iene) = Hg(i,j,k,iene) + dot_product(dpe(i,j-4:j+3,k), mmtmp(1:8,i))
          end do
          do i=lo(1),hi(1)
             M8p(:,i,j,k) = mmtmp(:,i)
          end do
       end do
    end do

    do n = 1, nspecies
       qxn = qx1+n-1

       do k=lo(3),hi(3)
          do j=lo(2),hi(2)+1
             do i=lo(1),hi(1) 
                Hg(i,j,k,iry1+n-1) = dot_product(dpy(i,j-4:j+3,k,n), M8p(:,i,j,k))
             end do
          end do
       end do
       
       do k=lo(3),hi(3)
          do j=lo(2),hi(2)+1
             do i=lo(1),hi(1)
                mmtmp(1:8,i) = matmul(M8, q(i,j-4:j+3,k,qxn))
                Hg(i,j,k,iene) = Hg(i,j,k,iene) + dot_product(dxe(i,j-4:j+3,k,n), mmtmp(1:8,i))
                Hg(i,j,k,iry1+n-1) = Hg(i,j,k,iry1+n-1) &
                     + dot_product(dxy(i,j-4:j+3,k,n), mmtmp(1:8,i))
             end do
          end do
       end do

    end do
       
    ! correction

    Hry = 0.d0

    do n = 1, nspecies
       do k=lo(3),hi(3)
          do j=lo(2),hi(2)+1
             do i=lo(1),hi(1)
                Hry(i,j,k) = Hry(i,j,k) + Hg(i,j,k,iry1+n-1)
             end do
          end do
       end do
    end do

    do n = 1, nspecies
       qyn = qy1+n-1
       qhn = qh1+n-1
       do k=lo(3),hi(3)
          do j=lo(2),hi(2)+1
             do i=lo(1),hi(1)
                Yhalf = 0.5d0*(q(i,j-1,k,qyn) + q(i,j,k,qyn))
                hhalf = 0.5d0*(q(i,j-1,k,qhn) + q(i,j,k,qhn))
                Hg(i,j,k,iry1+n-1) = Hg(i,j,k,iry1+n-1)- (Yhalf*Hry(i,j,k))
                Hg(i,j,k,iene) = Hg(i,j,k,iene) - (Yhalf*Hry(i,j,k))*hhalf
             end do
          end do
       end do
    end do

    ! add y-direction rhs
    do n=2,ncons
       do k=lo(3),hi(3)
          do j=lo(2),hi(2)
             do i=lo(1),hi(1)
                rhs(i,j,k,n) = rhs(i,j,k,n) + (Hg(i,j+1,k,n) - Hg(i,j,k,n)) * dx2inv(2)
             end do
          end do
       end do
    end do

    ! ------- END y-direction -------

    ! ------- BEGIN z-direction -------

    do k=lo(3),hi(3)+1
       do j=lo(2),hi(2)
          do i=lo(1),hi(1)
             mmtmp(1:8,i) = matmul(mu(i,j,k-4:k+3), M8)
             Hg(i,j,k,imx) = dot_product(mmtmp(1:8,i), q(i,j,k-4:k+3,qu))
             Hg(i,j,k,imy) = dot_product(mmtmp(1:8,i), q(i,j,k-4:k+3,qv))
          end do
       end do
    end do

    do k=lo(3),hi(3)+1
       do j=lo(2),hi(2)
          do i=lo(1),hi(1)
             mmtmp(1:8,i) = matmul(vsp(i,j,k-4:k+3), M8)
             Hg(i,j,k,imz) = dot_product(mmtmp(1:8,i), q(i,j,k-4:k+3,qw))
          end do
       end do
    end do

    do k=lo(3),hi(3)+1
       do j=lo(2),hi(2)
          do i=lo(1),hi(1)
             mmtmp(1:8,i) = matmul(lam(i,j,k-4:k+3), M8)
             Hg(i,j,k,iene) = dot_product(mmtmp(1:8,i), q(i,j,k-4:k+3,qtemp))
          end do
       end do
    end do

    do k=lo(3),hi(3)+1
       do j=lo(2),hi(2)
          do i=lo(1),hi(1)
             mmtmp(1:8,i) = matmul(M8, q(i,j,k-4:k+3,qpres))
             Hg(i,j,k,iene) = Hg(i,j,k,iene) + dot_product(dpe(i,j,k-4:k+3), mmtmp(1:8,i))
          end do
          do i=lo(1),hi(1)          
             M8p(:,i,j,k) = mmtmp(:,i) 
          end do
       end do
    end do

    do n = 1, nspecies
       qxn = qx1+n-1

       do jj=lo(2),hi(2),jblocksize
       do k=lo(3),hi(3)+1
!          do j=lo(2),hi(2)
          do j=jj,min(jj+jblocksize-1,hi(2))
             do i=lo(1),hi(1)
                Hg(i,j,k,iry1+n-1) = dot_product(dpy(i,j,k-4:k+3,n), M8p(:,i,j,k))
             end do
          end do
       end do
       enddo

       do jj=lo(2),hi(2),jblocksize
       do k=lo(3),hi(3)+1
!          do j=lo(2),hi(2)
          do j=jj,min(jj+jblocksize-1,hi(2))
             do i=lo(1),hi(1)
                mmtmp(1:8,i) = matmul(M8, q(i,j,k-4:k+3,qxn))
                Hg(i,j,k,iene) = Hg(i,j,k,iene) + dot_product(dxe(i,j,k-4:k+3,n), mmtmp(1:8,i))
                Hg(i,j,k,iry1+n-1) = Hg(i,j,k,iry1+n-1) &
                     + dot_product(dxy(i,j,k-4:k+3,n), mmtmp(1:8,i))
             end do
          end do
       end do
       enddo
    end do

    ! correction

    Hry = 0.d0

    do n = 1, nspecies
       do k=lo(3),hi(3)+1
          do j=lo(2),hi(2)
             do i=lo(1),hi(1)
                Hry(i,j,k) = Hry(i,j,k) + Hg(i,j,k,iry1+n-1)
             end do
          end do
       end do
    end do

    do n = 1, nspecies
       qyn = qy1+n-1
       qhn = qh1+n-1
       do k=lo(3),hi(3)+1
          do j=lo(2),hi(2)
             do i=lo(1),hi(1)
                Yhalf = 0.5d0*(q(i,j,k-1,qyn) + q(i,j,k,qyn))
                hhalf = 0.5d0*(q(i,j,k-1,qhn) + q(i,j,k,qhn))
                Hg(i,j,k,iry1+n-1) = Hg(i,j,k,iry1+n-1)- (Yhalf*Hry(i,j,k))
                Hg(i,j,k,iene) = Hg(i,j,k,iene) - (Yhalf*Hry(i,j,k))*hhalf
             end do
          end do
       end do
    end do
    
    ! add z-direction rhs
    do n=2,ncons
       do k=lo(3),hi(3)
          do j=lo(2),hi(2)
             do i=lo(1),hi(1)
                rhs(i,j,k,n) = rhs(i,j,k,n) + (Hg(i,j,k+1,n) - Hg(i,j,k,n)) * dx2inv(3)
             end do
          end do
       end do
    end do

    ! ------- END z-direction -------
    
    ! add kinetic energy
    do k=lo(3),hi(3)
       do j=lo(2),hi(2)
          do i=lo(1),hi(1)
             rhs(i,j,k,iene) = rhs(i,j,k,iene) &
                  + rhs(i,j,k,imx)*q(i,j,k,qu) &
                  + rhs(i,j,k,imy)*q(i,j,k,qv) &
                  + rhs(i,j,k,imz)*q(i,j,k,qw)
          end do
       end do
    end do

    deallocate(Hg,dpy,dxe,dpe,vsp,vsm,M8p,Hry)

  end subroutine narrow_diffterm_3d


  subroutine chemterm_3d(lo,hi,q,qlo,qhi,up,uplo,uphi)
    integer,         intent(in):: lo(3),hi(3),qlo(3),qhi(3),uplo(3),uphi(3)
    double precision,intent(in):: q ( qlo(1): qhi(1), qlo(2): qhi(2), qlo(3): qhi(3),nprim)
    double precision           :: up(uplo(1):uphi(1),uplo(2):uphi(2),uplo(3):uphi(3),ncons)

    integer :: iwrk, i,j,k
    double precision :: Yt(nspecies), wdot(nspecies), rwrk

    do k=lo(3),hi(3)
       do j=lo(2),hi(2)
          do i=lo(1),hi(1)

             Yt = q(i,j,k,qy1:qy1+nspecies-1)
             call ckwyr(q(i,j,k,qrho), q(i,j,k,qtemp), Yt, iwrk, rwrk, wdot)
             up(i,j,k,iry1:) = wdot * molecular_weight
             
          end do
       end do
    end do

  end subroutine chemterm_3d

  subroutine comp_courno_3d(lo,hi,dx,Q,qlo,qhi,courno)
    integer, intent(in) :: lo(3), hi(3), qlo(3), qhi(3)
    double precision, intent(in) :: dx(3)
    double precision, intent(in) :: q(qlo(1):qhi(1),qlo(2):qhi(2),qlo(3):qhi(3),nprim)
    double precision, intent(inout) :: courno

    integer :: i,j,k, iwrk
    double precision :: dxinv(3), c, rwrk, Cv, Cp
    double precision :: Tt, X(nspecies), gamma
    double precision :: courx, coury, courz

    double precision, parameter :: Ru = 8.31451d7

    do i=1,3
       dxinv(i) = 1.0d0 / dx(i)
    end do

    do k=lo(3),hi(3)
       do j=lo(2),hi(2)
          do i=lo(1),hi(1)

             Tt = q(i,j,k,qtemp)
             X  = q(i,j,k,qx1:qx1+nspecies-1)
             call ckcvbl(Tt, X, iwrk, rwrk, Cv)
             Cp = Cv + Ru
             gamma = Cp / Cv
             c = sqrt(gamma*q(i,j,k,qpres)/q(i,j,k,qrho))

             courx = (c+abs(q(i,j,k,qu))) * dxinv(1)
             coury = (c+abs(q(i,j,k,qv))) * dxinv(2)
             courz = (c+abs(q(i,j,k,qw))) * dxinv(3)
             
             courno = max( courx, coury, courz , courno )

          end do
       end do
    end do

  end subroutine comp_courno_3d

end module kernels_module
