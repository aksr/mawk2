
/********************************************
array.c
copyright 1991-1993.  Michael D. Brennan

This is a source file for mawk, an implementation of
the AWK programming language.

Mawk is distributed without warranty under the terms of
the GNU General Public License, version 2, 1991.
********************************************/

/* $Log: array.c,v $
 * Revision 1.7  1995/06/18  19:17:43  mike
 * Create a type Int which on most machines is an int, but on machines
 * with 16bit ints, i.e., the PC is a long.  This fixes implicit assumption
 * that int==long.
 *
 * Revision 1.6  1994/12/13  00:10:58  mike
 * delete A statement to delete all of A at once
 *
 * Revision 1.5  1994/02/21  00:07:49  mike
 * trivial change to macro
 *
 * Revision 1.4  1993/11/30  04:20:12  mike
 * different hashing for arrays
 *
 * Revision 1.3  1993/07/24  18:58:25  mike
 * use len test on find_by_sval
 *
 * Revision 1.2  1993/07/14  01:20:01  mike
 * code cleanup
 *
 * Revision 1.1.1.1  1993/07/03	 18:58:07  mike
 * move source to cvs
 *
 * Revision 5.5	 1993/04/10  18:38:40  mike
 * merge patch3
 *
 * Revision 5.4	 1993/01/01  21:30:48  mike
 * split new_STRING() into new_STRING and new_STRING0
 *
 * Revision 5.3.1.1  1993/01/20	 12:24:25  mike
 * patch3: safer double to int conversions
 *
 * Revision 5.3	 1992/11/28  23:48:42  mike
 * For internal conversion numeric->string, when testing
 * if integer, use longs instead of ints so 16 and 32 bit
 * systems behave the same
 *
 * Revision 5.2	 1992/04/07  17:17:31  brennan
 * patch 2
 * n = split(s,A,r)
 * delete A[i] if i not in 1..n
 * This is consistent with [ng]?awk
 *
 * Revision 5.1	 91/12/05  07:55:32  brennan
 * 1.1 pre-release
 *
*/

#include "mawk.h"
#include "symtype.h"
#include "memory.h"
#include "field.h"
#include "bi_vars.h"

/* convert doubles to string for array indexing */
#define	  NO_MOVE	2
#define	  NO_IVAL	(-1)

static ANODE *PROTO(find_by_sval, (ARRAY, STRING *, int)) ;
static ANODE *PROTO(find_by_index, (ARRAY, int, Int, int)) ;
static ANODE *PROTO(find_by_dval, (ARRAY, double, int)) ;
static void PROTO(load_array_ov, (ARRAY)) ;
static void PROTO(ilist_delete, (ARRAY, ANODE *)) ;


static unsigned  PROTO(ahash, (char*,unsigned)) ;

/* An array A is a pointer to an array of struct array,
   which is two hash tables in one.  One for strings
   and one for non-negative integers (this is much simplier
   and works just as well as previous versions)

   Each array is of size A_HASH_PRIME.

   When an index is deleted via	 delete A[i], the
   ANODE is not removed from the hash chain.  A[i].cp
   and A[i].sval are both freed and sval is set NULL.
   This method of deletion simplifies for( i in A ) loops.

*/


static ANODE *
find_by_sval(A, sval, cflag)
   ARRAY A ;
   STRING *sval ;
   int cflag ;			 /* create if on */
{
   char *s = sval->str ;
   unsigned len = sval->len ;
   unsigned h = ahash(s,len) % A_HASH_PRIME ;
   register ANODE *p = A[h].link ;
   ANODE *q = 0 ;		 /* holds first deleted ANODE */

   while (1)			/* q not set in this loop */
   {
      STRING *sv ;

      if (!p)  goto not_there ;
      else if (sv = p->sval)
      {
	 if (len == sv->len && strcmp(s, sv->str) == 0)  
	    return p ;
	 else  p = p->link ;
      }
      else
      {
	 q = p ; p = p->link ; break ;
      }
   }

   /* q is now set */
   while (p)	
   {
      STRING *sv = p->sval ;

      if (sv && sv->len == len && strcmp(s, sv->str) == 0)  
      {
	return p ;
      }
      else  p = p->link ;
   }

 not_there:
   if (cflag)
   {
      if (q)  p = q ;		      /* reuse the deleted node q */
      else
      {
	 p = ZMALLOC(ANODE) ;
	 p->link = A[h].link ; A[h].link = p ;
      }

      p->ival = NO_IVAL ;
      p->sval = sval ;
      sval->ref_cnt++ ;
      p->cp = ZMALLOC(CELL) ;
      p->cp->type = C_NOINIT ;
   }
   return p ;
}


/* find an array by (long) integer ival.
   Caller has already computed the hash value index.
   (This allows fast insertion for split())
*/

static ANODE *
find_by_index(A, index, ival, flag)
   ARRAY A ;
   int index ;
   Int ival ;
   int flag ;
{
   register ANODE *p = A[index].ilink ;
   ANODE *q = 0 ;		 /* trails p */

   while (p)
   {
      if (p->ival == ival)	/* found */
      {
	 if (!q || flag == NO_MOVE)  return p ;
	 else  /* delete to put at front */
	 {
	    q->ilink = p->ilink ;
	    goto found ;
	 }
      }
      else  
      { 
	 q = p ; p = p->ilink ; 
      }
   }
   /* not there, still need to look by sval */

   {				/* convert to string */
      char xbuff[16] ;
      STRING *sval ;
      char *s = xbuff + 14 ;
      long x = ival ;

      xbuff[15] = 0 ;

      do { *s-- = x%10 + '0' ; x /= 10 ; } while(x) ;
      sval = new_STRING(s + 1) ;
      p = find_by_sval(A, sval, flag) ;
      free_STRING(sval) ;
   }


   if (!p)  return (ANODE *) 0 ;

   p->ival = ival ;

 found:			/* put p at front */
   p->ilink = A[index].ilink ; A[index].ilink = p ;
   return p ;
}


static ANODE *
find_by_dval(A, d, flag)
   ARRAY A ;
   double d;
   int flag ;
{
   Int lval ;
   ANODE *p ;
   char xbuff[260] ;
   STRING *sval ;


   lval = d_to_I(d) ;
   if ((double) lval == d)	/* integer valued */
   {
      if (lval >= 0)
      {
	 return
	    find_by_index(A, (int) (lval % A_HASH_PRIME), lval, flag) ;
      }
      else  sprintf(xbuff, INT_FMT, lval) ;
   }
   else	 sprintf(xbuff, string(CONVFMT)->str, d) ;

   sval = new_STRING(xbuff) ;
   p = find_by_sval(A, sval, flag) ;
   free_STRING(sval) ;
   return p ;
}



CELL *
array_find(A, cp, create_flag)
   ARRAY A ;
   CELL *cp ;
   int create_flag ;
{
   ANODE *ap ;

   switch (cp->type)
   {
      case C_DOUBLE:
	 ap = find_by_dval(A, cp->dval, create_flag) ;
	 break ;

      case C_NOINIT:
	 ap = find_by_sval(A, &null_str, create_flag) ;
	 break ;

      default:
	 ap = find_by_sval(A, string(cp), create_flag) ;
	 break ;
   }

   return ap ? ap->cp : (CELL *) 0 ;
}


void
array_delete(A, cp)
ARRAY A ; CELL *cp ;
{
   ANODE *ap ;

   switch (cp->type)
   {
      case C_DOUBLE:
	 ap = find_by_dval(A, cp->dval, NO_CREATE) ;
	 /* cut the ilink */
	 if (ap && ap->ival >= 0)	/* must be at front */
	    A[(int) (ap->ival % A_HASH_PRIME)].ilink = ap->ilink ;
	 break ;

      case C_NOINIT:
	 ap = find_by_sval(A, &null_str, NO_CREATE) ;
	 break ;

      default:
	 ap = find_by_sval(A, string(cp), NO_CREATE) ;
	 if (ap && ap->ival >= 0)  ilist_delete(A, ap) ;
	 break ;
   }


   /* delete -- leave the empty ANODE so for(i in A)
     works */
   if (ap)
   {
      free_STRING(ap->sval) ; ap->sval = (STRING *) 0 ;
      cell_destroy(ap->cp)  ; ZFREE(ap->cp) ;
   }
}

/* load into an array from the split_ov_list */

static void
load_array_ov(A)
   ARRAY A ;
{
   register SPLIT_OV *p ;
   register CELL *cp ;
   SPLIT_OV *q ;

   int cnt = MAX_SPLIT + 1 ;
   int index = (MAX_SPLIT + 1) % A_HASH_PRIME ;

   p = split_ov_list ; split_ov_list = (SPLIT_OV*) 0 ;
#ifdef	DEBUG
   if (!p)  bozo("array_ov") ;
#endif

   while (1)
   {
      cp = find_by_index(A, index, (long) cnt, NO_MOVE)->cp ;
      cell_destroy(cp) ;
      cp->type = C_MBSTRN ;
      cp->ptr = (PTR) p->sval ;

      q = p ; p = p->link ; ZFREE(q) ;
      if (!p)  break ;

      cnt++ ;
      if (++index == A_HASH_PRIME)  index = 0 ;
   }
}

/* delete an ANODE from the ilist that is known
   to be there */

static void
ilist_delete(A, d)
   ARRAY A ;
   ANODE *d ;
{
   int index = d->ival % A_HASH_PRIME ;
   register ANODE *p = A[index].ilink ;
   register ANODE *q = (ANODE *) 0 ;

   while ( p != d ) 
   { 
      q = p ; p = p->ilink ; 
   }
   if (q)  q->ilink = p->ilink ;
   else	 A[index].ilink = p->ilink ;
}


/* this is called by bi_split()
   to load strings into an array
*/

void
load_array(A, cnt)
   ARRAY A ;
   register int cnt ;
{
   register CELL *cp ;
   register int index ;

   {
      /* clear A , leaving only A[1]..A[cnt] (if exist) */
      int i ;
      ANODE *p ;

      for (i = 0; i < A_HASH_PRIME; i++)
      {
	 p = A[i].link ;
	 while (p)
	 {
	    if (p->sval && (p->ival <= 0 || p->ival > cnt))
	    {
	       if (p->ival >= 0)  ilist_delete(A, p) ;

	       free_STRING(p->sval) ;
	       p->sval = (STRING *) 0 ;
	       cell_destroy(p->cp) ;
	       ZFREE(p->cp) ;
	    }
	    p = p->link ;
	 }
      }
   }
   if (cnt > MAX_SPLIT)
   {
      load_array_ov(A) ;
      cnt = MAX_SPLIT ;
   }

   index = cnt % A_HASH_PRIME ;

   while (cnt)
   {
      cp = find_by_index(A, index, (long) cnt, NO_MOVE)->cp ;
      cell_destroy(cp) ;
      cp->type = C_MBSTRN ;
      cp->ptr = (PTR) split_buff[--cnt] ;

      if (--index < 0)	index = A_HASH_PRIME - 1 ;
   }
}


/* cat together cnt elements on the eval stack to form
   an array index using SUBSEP */

CELL *
array_cat(sp, cnt)
   register CELL *sp ;
   int cnt ;
{
   register CELL *p ;		 /* walks the stack */
   CELL subsep ;		 /* a copy of SUBSEP */
   unsigned subsep_len ;
   char *subsep_str ;
   unsigned total_len ;		 /* length of cat'ed expression */
   CELL *top ;			 /* sp at entry */
   char *t ;			 /* target ptr when catting */
   STRING *sval ;		 /* build new STRING here */

   /* get a copy of subsep, we may need to cast */
   cellcpy(&subsep, SUBSEP) ;
   if (subsep.type < C_STRING)	cast1_to_s(&subsep) ;
   subsep_len = string(&subsep)->len ;
   subsep_str = string(&subsep)->str ;
   total_len = --cnt * subsep_len ;

   top = sp ;
   sp -= cnt ;
   for (p = sp; p <= top; p++)
   {
      if (p->type < C_STRING)  cast1_to_s(p) ;
      total_len += string(p)->len ;
   }

   sval = new_STRING0(total_len) ;
   t = sval->str ;

   /* put the pieces together */
   for (p = sp; p < top; p++)
   {
      memcpy(t, string(p)->str, string(p)->len) ;
      memcpy(t += string(p)->len, subsep_str, subsep_len) ;
      t += subsep_len ;
   }
   /* p == top */
   memcpy(t, string(p)->str, string(p)->len) ;

   /* done, now cleanup */
   free_STRING(string(&subsep)) ;
   while ( p >= sp )  { free_STRING(string(p)) ; p-- ; }
   sp->type = C_STRING ;
   sp->ptr = (PTR) sval ;
   return sp ;
}


/* delete all elements of an array
   blow the array structure away if its a function call local
*/

void
array_free(A, local_flag)
   ARRAY A ;
   int local_flag ;
{
   register ANODE *p ;
   register int i ;
   ANODE *q ;

   for (i = 0; i < A_HASH_PRIME; i++)
   {
      p = A[i].link ;
      /*  clear the links in case doing an "delete A" */
      A[i].link = A[i].ilink = (ANODE*) 0 ;
      while (p)
      {				/* check its not a deleted node */
	 if (p->sval)
	 {
	    free_STRING(p->sval) ;
	    cell_destroy(p->cp) ;
	    ZFREE(p->cp) ;
	 }

	 q = p ; p = p->link ;
	 ZFREE(q) ;
      }
   }

   if (local_flag) zfree(A, sizeof(A[0]) * A_HASH_PRIME) ;
}


int
inc_aloop_state(ap)
   ALOOP_STATE *ap ;
{
   register ANODE *p ;
   register int i ;
   ARRAY A = ap->A ;
   CELL *cp ;

   i = ap->index ;
   if (i == -1)			/* just starting */
   {
      i = 0 ;
      p = A[0].link ;
   }
   else	 p = ap->ptr->link ;

   while (1)
   {
      if (!p)
      {
	 if (++i == A_HASH_PRIME)  return 0 ;
	 else p = A[i].link ; continue ; 
      }

      if (p->sval)		/* found one */
      {
	 cp = ap->var ;
	 cell_destroy(cp) ;
	 cp->type = C_STRING ;
	 cp->ptr = (PTR) p->sval ;
	 p->sval->ref_cnt++ ;

	 /* save the state */
	 ap->index = i ;
	 ap->ptr = p ;
	 return 1 ;
      }
      else			/* its deleted */
	 p = p->link ;
   }
}

/* hash on 5 chars on the front and 5 chars 
   and the length
*/

static unsigned
ahash(s, len)
   char *s ;
   unsigned len ;
{

#define  BIG_PRIME  0x6cc31c19  

   unsigned h = 0 ;
   unsigned char *p = (unsigned char *) s ;

   if ( len > 10 )
   {
      int cnt = 5 ;
      unsigned char *q = p + len - 1 ;

      while ( cnt )
      {
	 cnt--  ;
	 h = (h<<2) + *p++ ;
	 h = (h<<2) + *q-- ;
      }
   }
   else
   {
      unsigned k ;

      while ( k = *p )
      {
	 p++ ;
	 h = (h<<2) + k ;
      }
   }

   return (h+len)*BIG_PRIME ;
}


