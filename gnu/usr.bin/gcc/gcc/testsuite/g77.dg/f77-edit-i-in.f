C Test Fortran 77 I edit descriptor for input
C      (ANSI X3.9-1978 Section 13.5.9.1)
C
C Origin: David Billinghurst <David.Billinghurst@riotinto.com>
C
C { dg-do run }

      integer i,j
      character*10 buf

      write(buf,'(A)') '1  -1'

      read(buf,'(I1)') i
      if ( i.ne.1 ) call abort()

      read(buf,'(X,I1)') i
      if ( i.ne.0 ) call abort()

      read(buf,'(X,I1,X,I2)') i,j
      if ( i.ne.0 .and. j.ne.-1 ) call abort()

      end
