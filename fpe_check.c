
/* This code attempts to figure out what the default
   floating point exception handling does.
*/

/* $Log: fpe_check.c,v $
 * Revision 1.4  1995/01/09  01:22:28  mike
 * check sig handler ret type to make fpe_check.c more robust
 *
 * Revision 1.3  1994/12/18  20:54:00  mike
 * check NetBSD mathlib defines
 *
 * Revision 1.2  1994/12/14  14:37:26  mike
 * add messages to user
 *
 * Revision 1.1  1994/10/10  00:14:50  mike
 * initial ci
 *
*/

#include <setjmp.h>
#include <signal.h>
#include <math.h>

/* Sets up NetBSD 1.0A for ieee floating point */
#if defined(_LIB_VERSION_TYPE) && defined(_LIB_VERSION) && defined(_IEEE_)
_LIB_VERSION_TYPE _LIB_VERSION = _IEEE_;
#endif

void message(s)
   char *s ;
{
   printf("\t%s\n", s) ;
}

jmp_buf jbuff ;
int may_be_safe_to_look_at_why = 0 ;
int why_v ;

RETSIGTYPE fpe_catch() ;

double
div_by(x,y)
   double x ;
   double y ;
{
   return x/y ;
}

double overflow(x)
   double x ;
{
   double y ;

   do
   {
      y = x ;
      x *= x ;
   } while( y != x ) ;
   return x ;
}

   
void check_fpe_traps()
{
   int traps = 0 ;

   if (setjmp(jbuff) == 0)
   {
      div_by(44.0, 0.0) ;
      message("division by zero does not generate an exception") ;
   }
   else
   {
      traps = 1 ;
      message("division by zero generates an exception") ;
      signal(SIGFPE, fpe_catch) ; /* set again if sysV */
   }

   if ( setjmp(jbuff) == 0 )
   {
      overflow(1000.0) ;
      message("overflow does not generate an exception") ;
   }
   else
   {
      traps |= 2 ;
      message("overflow generates an exception") ;
      signal(SIGFPE, fpe_catch) ; 
   }

   if ( traps == 0 )
   {
      double maybe_nan = log(-8.0) ;

      if ( maybe_nan == maybe_nan )
      {
	 /* evidently not a nan */
	 traps |= 4 ;
	 message("math library does not support ieee754") ;
      }
      else
      {
	 message("math library supports ieee754") ;
      }
   }

   exit(traps) ;
}


/*
Only get here if we think we have Berkeley type signals so we can
look at a second argument to fpe_catch() to get the reason for
an exception
*/
void
get_fpe_codes()  
{
   int divz ;
   int ovf ;

   may_be_safe_to_look_at_why = 1 ;

   if( setjmp(jbuff) == 0 ) div_by(1000.0, 0.0) ;
   else  
   {
      divz = why_v ;
      signal(SIGFPE, fpe_catch) ;
   }

   if( setjmp(jbuff) == 0 ) overflow(1000.0) ;
   else  
   {
      ovf = why_v ;
      signal(SIGFPE, fpe_catch) ;
   }


   /* make some guesses if sane values */
   if ( divz>0 && ovf>0 && divz != ovf )
   {
      printf("X FPE_ZERODIVIDE %d\n", divz) ;
      printf("X FPE_OVERFLOW %d\n", ovf) ;
      exit(0) ;
   }
   else exit(1) ;
}

int
main(argc)
   int argc ;
{

   signal(SIGFPE, fpe_catch) ;
   if (argc > 1)  get_fpe_codes() ;
   else  check_fpe_traps() ;

   /* not reached */
   return 0 ;
} 

/* put this down here in attempt to defeat ambitious compiler that
   may have seen a prototype without 2nd argument */
   
RETSIGTYPE fpe_catch(signal, why)
   int signal ;
   int why ;
{
   if ( may_be_safe_to_look_at_why ) why_v = why ;
   longjmp(jbuff,1) ;
}
