/* Simple expression parser for GMP-ECM.

Copyright 2003, 2004, 2005, 2006, 2007, 2008, 2012 Jim Fougeron, Paul Zimmermann and Alexander Kruppa.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or (at your
option) any later version.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING.  If not, see
http://www.gnu.org/licenses/ or write to the Free Software Foundation, Inc.,
51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ecm-ecm.h"

#ifdef HAVE_STRINGS_H
# include <strings.h> /* for strncasecmp */
#endif

#ifdef HAVE_CTYPE_H
# include <ctype.h>
#endif


/*****************************************************************
 *   Syntax for this VERY simple recursive expression parser:	 *
 *                                                               *
 *   ( or [ or { along with ) or ] or } are valid for grouping   *
 *   Normal "simple" operators:  + - * /  (. can be used for *)  *
 *   Module:                     n%m    345%11                   *
 *   Unary minus is supported:   -n     -500                     *
 *   Exponentation:              n^m    2^500                    *
 *   Simple factorial:           n!     53!  == 1*2*3*4...*52*53 *
 *   Multi-factorial:            n!m    14!3 == 14.11.8.5.2      *
 *   Simple Primorial:           n#     11# == 2*3*5*7*11        *
 *   Reduced Primorial:          n#m    17#5 == 5.7.11.13.17     *
 *                                                               *
 * Supported functions:	(case insensitive)			 *
 *   Phi(n,x)			Phi(3,5) == 1+x+x^2 = 31	 *
 *   GCD(m,n)			GCD(120, 28) = 4		 *
 *   U(p,q,n)							 *
 *   primU(p,q,n)						 *
 *   TODO: PhiL(k,n), PhiM(k,n)  				 *
 *      only for bases 2,3,5,6,7,10,11 (times a square)		 *
 * Note for developers: 					 *
 * 	First k-1 arguments are passed as an mpz_t array	 *
 *                                                               *
 * NOTE Lines ending in a \ character are "joined"               *
 * NOTE C++ // single line comments (rest of line is a comment)  *
 *                                                               *
 ****************************************************************/

/* value only used by the expression parser */
static mpz_t t, mpOne;
static char *expr_str;

static void eval_power (mpz_t n, const mpz_t arg, char op);
static void eval_product (mpz_t n, const mpz_t arg, char op);
static void eval_sum (mpz_t n, const mpz_t arg, char op);
static int  eval_Phi (mpz_t *params, mpz_t n);
static int  eval_PhiL (mpz_t *params, mpz_t n);
static int  eval_PhiM (mpz_t *params, mpz_t n);
// static int  eval_gcd (mpz_t *params, mpz_t n);
static int  eval_U (mpz_t *params, mpz_t n);
static int  eval_primU (mpz_t *params, mpz_t n);
static int  eval_2 (int bInFuncParams);
static int  aurif (mpz_t output, mpz_t n, mpz_t base, int sign);

#if 0 /* strncasecmp is a required function in configure.in */
#if defined (_MSC_VER) || defined (__MINGW32__)
#define strncasecmp strnicmp
#endif
#endif

/***************************************/
/* Main expression evaluation function */
/* This is the function that the app   */
/* calls to read the expression line   */
/***************************************/
int eval (mpcandi_t *n, FILE *fd, int primetest)
{
  int ret;
  int nMaxSize = 2000, nCurSize = 0;
  int c;
  char *expr = (char *) malloc (nMaxSize + 1);

  ASSERT_ALWAYS (expr != NULL);
JoinLinesLoop:
  c = fgetc (fd);
  if (0)
    {
ChompLine:
      do
        c = fgetc (fd);
      while (c != EOF && !IS_NEWLINE(c));
      if (IS_NEWLINE(c))
        goto JoinLinesLoop;
    }
  
  while (c != EOF && !IS_NEWLINE(c) && c != ';')
    {
      if (c == '/')
	{
	  /* This might be a C++ // comment or it might be a / division operator.  
	     Check it out, and if it is a comment, then "eat it" */
	  int peek_c = fgetc (fd);
	  if (peek_c == '/')
	    /* Got a C++ single line comment, so Chomp the line */
	    goto ChompLine;

	  /* Put the char back on the file, then allow the code to add the '/' char to the buffer */
	  ungetc (peek_c, fd);
        }

      /* strip space and tabs out here, and then we DON'T have to mess with them in the rest of the parser */
      if (!isspace (c) && c != '"' && c != '\'')
	expr[nCurSize++] = (char) c;

      if (nCurSize == nMaxSize)
      {
	char *cp;
	nMaxSize += nMaxSize / 2;
	cp = (char *) realloc (expr, nMaxSize + 1);
        ASSERT_ALWAYS (cp != NULL);
	expr = cp;
      }
      c = fgetc (fd);
    }
  expr[nCurSize] = 0;
  if (!nCurSize)
    ret = 0;
  else
    {
      if (c == ';')
	ungetc (c, fd);
      mpz_init (t);
      expr_str = expr;
      ret = eval_2 (0);
      if (ret)
	{
	  char *s;
	  char *cpTmpExpr = expr;
	  s = mpz_get_str (NULL, 10, t);
	  if (!strcmp(s, cpTmpExpr))
	    cpTmpExpr = NULL;
	  ret = mpcandi_t_add_candidate (n, t, cpTmpExpr, primetest);
	  free (s); /* size strlen (s) + 1 */
	}
      mpz_clear(t);
    }
#if defined (DEBUG_EVALUATOR)
  gmp_fprintf (stderr, "eval(\"%s\") = %Zd\n", expr, n->n);
#endif
  free(expr);
  return ret;
}

int eval_str (mpcandi_t *n, char *cp, int primetest, char **EndChar)
{
  int ret;
  int nMaxSize=2000, nCurSize=0;
  char *c;
  char *expr = (char *) malloc(nMaxSize+1);

  ASSERT_ALWAYS (expr != NULL);
  c = cp;
JoinLinesLoop:
  if (*c == '#')
    {
      do
        ++c;
      while (*c && !IS_NEWLINE(*c));
      if (IS_NEWLINE(*c))
        goto JoinLinesLoop;
    }
  
  while (*c && !IS_NEWLINE(*c) && *c != ';')
    {
      /* strip space and tabs out here, and then we DON'T have to mess with them in the rest of the parser */
      if (!isspace((int) *c) && *c != '"' && *c != '\'')
	expr[nCurSize++] = *c;
      if (nCurSize == nMaxSize)
      {
	char *cp;
	nMaxSize += nMaxSize / 2;
	cp = (char *) realloc (expr, nMaxSize + 1);
        ASSERT_ALWAYS (cp != NULL);
	expr = cp;
      }
      ++c;
    }
  expr[nCurSize] = 0;
  if (!nCurSize)
    ret = 0;
  else
    {
      if (*c != ';')
	++c;
      mpz_init(t);
      expr_str = expr;
      ret = eval_2(0);
      if (ret)
	{
	  char *s;
	  char *cpTmpExpr = expr;
	  s = mpz_get_str (NULL, 10, t);
	  if (!strcmp(s, cpTmpExpr))
	    cpTmpExpr = NULL;
	  ret = mpcandi_t_add_candidate(n, t, cpTmpExpr, primetest);
	  free (s); /* size strlen (s) + 1 */
	}
      mpz_clear(t);
    }
#if defined (DEBUG_EVALUATOR)
  gmp_fprintf (stderr, "eval_str(\"%s\") = %Zd\n", expr, n->n);
#endif
  free(expr);
  if (EndChar && *EndChar)
    *EndChar = c;
  return ret;
}

/**
 * Evaluate "power" style operations.
 * Arguments
 *      n: output also possibly second argument
 *      arg: first argument to operation
 *      op: operation to perform, no-op if not a power argument
 */
void
eval_power (mpz_t n, const mpz_t arg, char op)
{
#if defined (DEBUG_EVALUATOR)
  if ('^'==op || '!'==op || '@'==op || '#'==op || '$'==op)
    {
      gmp_fprintf (stderr, "eval power %Zd%c%Zd\n", arg, op, n);
    }
#endif

  // arg must be positive for everything except power
  if (mpz_sgn(arg) < 0) {
    if ('!'==op || '#'==op) {
      gmp_fprintf (stderr, "\nError - negative argument [%Zd%c]\n", arg, op);
      exit (EXIT_FAILURE);
    }
    if ('@'==op || '$'==op) {
      gmp_fprintf (stderr, "\nError - negative argument [%Zd%c%Zd]\n", arg, op, n);
      exit (EXIT_FAILURE);
    }
  }

  // second arg must be positive for everything with two args
  if ('^'==op || '@'==op || '$'==op) {
    if (mpz_sgn(n) < 0) {
      gmp_fprintf (stderr, "\nError - negative argument [%Zd%c%Zd]\n", arg, op, n);
      exit (EXIT_FAILURE);
    }
  }

  if ('^'==op)
    mpz_pow_ui(n, arg, mpz_get_ui(n));
  else if ('!'==op)	/* simple factorial  (syntax arg!    example: 7! == 1*2*3*4*5*6*7) */
    mpz_fac_ui(n, mpz_get_ui(arg));
  else if ('@'==op)	/* Multi factorial   (syntax arg!n
                           Example: 14!3 == 14*11*8*5*2
                           Note: 14!3 is substituted into 14@3 by the parser */
    {
      /* gmp_printf ("Multi factorial  %Zd!%Zd\n", arg, n); */
      ASSERT_ALWAYS (mpz_cmp(arg, n) >= 0); // arg >= n
      mpz_t p, dec;
      mpz_init_set (p, arg);
      mpz_init_set (dec, n);
      mpz_set_ui (n, 1);
      while (mpz_cmp_ui(p, 1) > 0)
	{
	  /* Could use factor-tree, not worth the extra code. */
	  mpz_mul (n, n, p);
	  mpz_sub (p, p, dec);
	}
      mpz_clear(p);
      mpz_clear(dec);
    }
  else if ('#'==op)  /* simple primorial (syntax  arg#   example:  11# == 2*3*5*7*11 */
    {
      mpz_primorial_ui(n, mpz_get_ui(arg));
    }
  else if ('$'==op)  /* reduced primorial (syntax  arg#n   example:  13#5 == (5*7*11*13) */
    {
      /* gmp_printf ("Reduced-primorial  %Zd#%Zd\n", arg, n); */
      ASSERT_ALWAYS (mpz_cmp(arg, n) >= 0); // arg >= n
      mpz_t p;
      mpz_init_set(p, n);
      mpz_set_ui (n, 1);
      mpz_sub_ui(p, p, 1);
      mpz_nextprime(p, p);
      for (; mpz_cmp(p, arg) <= 0; mpz_nextprime(p, p))
	  /* Could use factor-tree, not worth the extra code. */
	  mpz_mul (n, n, p);
      mpz_clear(p);
    }
}

/**
 * Evaluate "product" style operations.
 * Arguments
 *      n: output also possibly second argument
 *      arg: first argument to operation
 *      op: operation to perform, no-op if not a power argument
 */
void
eval_product (mpz_t n, const mpz_t arg, char op)
{
#if defined (DEBUG_EVALUATOR)
  if ('*'==op || '.'==op || '/'==op || '%'==op)
    {
      gmp_fprintf (stderr, "eval_product %Zd%c%Zd\n", arg, op, n);
    }
#endif
  if ('*' == op || '.' == op)
    mpz_mul (n, arg, n);
  else if ('/' == op)
    {
      mpz_t r;
      mpz_init (r);
      mpz_tdiv_qr (n, r, arg, n);
      if (mpz_cmp_ui (r, 0) != 0)
        {
          fprintf (stderr, "Parsing Error: inexact division\n");
          exit (EXIT_FAILURE);
        }
      mpz_clear (r);
    }
  else if ('%' == op)
    mpz_tdiv_r (n, arg, n);
}

/**
 * Evaluate "sum" style operations.
 * Arguments
 *      n: output also possibly second argument
 *      arg: first argument to operation
 *      op: operation to perform, no-op if not a power argument
 */
void
eval_sum (mpz_t n, const mpz_t arg, char op)
{
#if defined (DEBUG_EVALUATOR)
  if ('+'==op || '-'==op)
    {
      gmp_fprintf (stderr, "eval_sum %Zd%c%Zd\n", arg, op, n);
    }
#endif

  if ('+' == op)
    mpz_add(n, arg, n);
  else if ('-' == op)
    mpz_sub(n, arg, n);
}

int eval_Phi (mpz_t* params, mpz_t n)
{
  /* params[0]=exp, n=base */
  int factors[200];
  unsigned dwFactors=0, dw;
  unsigned long B;
  unsigned long p;
  mpz_t D, T, org_n;
  
  /* deal with trivial cases first */
  if (mpz_cmp_ui (params[0], 0) == 0)
  {
    mpz_set_ui (n, 1);
    return 1;
  }
  if (mpz_cmp_ui (params[0], 0) < 0)
    return 0;
  if (mpz_cmp_ui (params[0], 1) == 0)
    {
	    mpz_sub_ui (n, n, 1);
      return 1;
    }
  if (mpz_cmp_ui (params[0], 2) == 0)
    {
	    mpz_add_ui (n, n, 1);
      return 1;
    }
  if (mpz_cmp_ui (n, 0) < 0) 
  /* Convert to positive base; this is always valid when exp>=3 */
    {
      mpz_neg (n, n);
      if (mpz_congruent_ui_p (params[0], 1, 2))
        {
          mpz_mul_ui(params[0], params[0], 2);
        }
      else if (mpz_congruent_ui_p (params[0], 2, 4))
        {
          mpz_divexact_ui(params[0], params[0], 2);
        }
    }
  if (mpz_cmp_ui (n, 1) == 0)
    {
      /* return value is p if params[0] is prime power p^k, or 1 otherwise */
      int maxpower=mpz_sizeinbase(params[0], 2)+1;
      mpz_init (T);
      for (int power=maxpower; power>=1; --power)
        {
          if ( mpz_root (T, params[0], power) ) break;
        }
      int isPrime = mpz_probab_prime_p (T, PROBAB_PRIME_TESTS);
      mpz_set (n, isPrime ? T : mpOne);
      mpz_clear(T);
      return 1;
    }


  /* Ok, do the real h_primative work, since we are not one of the trivial case */

  if (mpz_fits_ulong_p (params[0]) == 0)
    return 0;

  B = mpz_get_ui (params[0]);

  /* Obtain the factors of B */
  for (p = 2; p <= B; p += 1 + (p>2))
    {
      if (B % p == 0)
	{
	  /* Add the factor one time */
	  factors[dwFactors++] = p;
	  /* but be sure to totally remove it */
	  do { B /= p; } while (B % p == 0);
        }
     }
  B = mpz_get_si (params[0]);

  mpz_init_set (org_n, n);
  mpz_set_ui (n, 1);
  mpz_init_set_ui (D, 1);
  mpz_init (T);

  for(dw=0;(dw<(1U<<dwFactors)); dw++)
    {
      /* for all Mobius terms */
      int iPower=B;
      int iMobius=0;
      unsigned dwIndex=0;
      unsigned dwMask=1;

      while(dwIndex < dwFactors)
	{
	  if(dw&dwMask)
	    {
	      /* printf ("iMobius = %d iPower = %d, dwIndex = %d ", iMobius, iPower, dwIndex); */
	      iMobius++;
	      iPower/=factors[dwIndex];
	      /* printf ("Then iPower = %d\n", iPower);  */
	    }
	  dwMask<<=1;
	  ++dwIndex;
	}
      // gmp_fprintf (stderr, "Taking %Zd^%d-1\n", org_n, iPower);
      mpz_pow_ui(T, org_n, iPower);
      mpz_sub_ui(T, T, 1);
    
      if(iMobius&1)
      {
	// gmp_fprintf (stderr, "Muling D=D*T  %Zd*%Zd\n", D, T);
	mpz_mul(D, D, T);
      }
      else
      {
	// gmp_fprintf (stderr, "Muling n=n*T  %Zd*%Zd\n", n, T);
	mpz_mul(n, n, T); 
      }
  }
  mpz_divexact(n, n, D);
  mpz_clear(T);
  mpz_clear(org_n);
  mpz_clear(D);

  return 1;
}

int aurif (mpz_t output, mpz_t n, mpz_t base, int sign) // Evaluate Aurifeullian polynomials
{
  int b,k=mpz_get_ui(n);
  mpz_t orig_base;
  mpz_t C,D,l,m;
  // Find a proper base
  mpz_init_set(orig_base,base);
  mpz_inits(C,D,l,m,NULL);
  for(b=2;b<=11;b++)
    {
      mpz_set(base,orig_base);
      mpz_mul_ui(base,base,b);
      if(mpz_perfect_square_p(base)) break;
    }
  if(b==12) // not found
    {
      gmp_fprintf (stderr, "Error: base %Zd not supported for Aurifeullian factorization yet\n", orig_base);
      return 0;
    }
  if(k%((b==5)?b:(2*b))!=0)
    {
      gmp_fprintf (stderr, "Error: exponent %Zd does not make sense for base %Zd\n", n, orig_base);
      return 0;
    }
  k/=((b==5)?b:(2*b));
  if(k%2==0)
    {
      gmp_fprintf (stderr, "Error: exponent %Zd does not make sense for base %Zd\n", n, orig_base);
      return 0;
    }
  mpz_set(base,orig_base);
  mpz_pow_ui(m, base, k); 
  mpz_mul_ui(l, m, b); 
  mpz_sqrt(l, l);
  switch(b)
    {
    case 2:
    case 3:
      mpz_add_ui(C, m, 1);
      mpz_set_ui(D, 1);
      break;
    case 5:
    case 6:
      mpz_add_ui(C, m, 3);
      mpz_mul(C, C, m);
      mpz_add_ui(C, C, 1);
      mpz_add_ui(D, m, 1);
      break;
    case 7:
      mpz_add_ui(C, m, 1);
      mpz_pow_ui(C, C, 3);
      mpz_add_ui(D, m, 1);
      mpz_mul(D, D, m);
      mpz_add_ui(D, D, 1);
      break;
    case 10:
      mpz_add_ui(C, m, 5);
      mpz_mul(C, C, m);
      mpz_add_ui(C, C, 7);
      mpz_mul(C, C, m);
      mpz_add_ui(C, C, 5);
      mpz_mul(C, C, m);
      mpz_add_ui(C, C, 1);
      mpz_add_ui(D, m, 2);
      mpz_mul(D, D, m);
      mpz_add_ui(D, D, 2);
      mpz_mul(D, D, m);
      mpz_add_ui(D, D, 1);
      break;
    case 11:
      mpz_add_ui(C, m, 5);
      mpz_mul(C, C, m);
      mpz_sub_ui(C, C, 1);
      mpz_mul(C, C, m);
      mpz_sub_ui(C, C, 1);
      mpz_mul(C, C, m);
      mpz_add_ui(C, C, 5);
      mpz_mul(C, C, m);
      mpz_add_ui(C, C, 1);
      mpz_add_ui(D, m, 1);
      mpz_mul(D, D, m);
      mpz_sub_ui(D, D, 1);
      mpz_mul(D, D, m);
      mpz_add_ui(D, D, 1);
      mpz_mul(D, D, m);
      mpz_add_ui(D, D, 1);
      break;
    default: // not supposed to arrive here
      break;
    }
  mpz_set(output, C);
  (sign>0 ? mpz_addmul : mpz_submul)(output, D, l);
  // gmp_fprintf (stderr, "Calculated base=%Zd, exp=%Zd, C=%Zd, D=%Zd, output=%Zd\n",base,n,C,D,output);
  mpz_clears(orig_base,C,D,l,m,NULL);
  return 1;
}
int eval_PhiL (mpz_t *params, mpz_t n)
{
  mpz_t aur;
  int err1,err2;
  mpz_init(aur);
  err1=aurif(aur,params[0],n,-1);
  err2=eval_Phi(params,n); // n now holds Phi(params[0],n) 
  mpz_gcd(n,n,aur);
  mpz_clear(aur);
  return err1*err2;
}
int eval_PhiM (mpz_t *params, mpz_t n)
{
  mpz_t aur;
  int err1,err2;
  mpz_init(aur);
  err1=aurif(aur,params[0],n,1);
  err2=eval_Phi(params,n); // n now holds Phi(params[0],n) 
  mpz_gcd(n,n,aur);
  mpz_clear(aur);
  return err1*err2;
}

int eval_gcd (mpz_t *params, mpz_t n)
{
  mpz_gcd(n, n, params[0]);
  return 1;
}

int eval_U (mpz_t *params, mpz_t n)
/* params[0]=P, params[1]=Q */
{
  unsigned long N;
  mpz_t U1,U0,org_n,D,T; /* At each step U1 holds U(k), and U0 holds U(k-1) */
  long k,l;
  
  if (mpz_cmp_si (n, 0) < 0)
    return 0;
  if (mpz_cmp_ui (n, 1) == 0)
    {
      mpz_set_ui (n, 1);
      return 1;
    }
  if (mpz_cmp_ui (n, 0) == 0)
    {
      mpz_set_ui (n, 0);
      return 1;
    }
  if (mpz_fits_ulong_p (n) == 0)
    return 0;

  N = mpz_get_ui (n);
  if (mpz_cmp_ui (params[0], 0) == 0)
    {
      if( N%2==0 )
        {
          mpz_set_ui (n, 0);
	}
      else
        {
	  mpz_neg (params[1], params[1]);
	  mpz_pow_ui (n, params[1], (N-1)/2);
	  mpz_neg (params[1], params[1]);
	}
      return 1;
    }


  mpz_init_set (org_n, n);
  mpz_init_set_ui (U1, 1);
  mpz_init_set_ui (U0, 0);
  mpz_init (D);
  mpz_init (T);
  mpz_mul (D, params[0], params[0]);
  mpz_submul_ui (D, params[1], 4);
  k=1;

  for(l=mpz_sizeinbase(org_n,2)-2;l>=0;l--)
    {
       mpz_mul (U0, U0, U0);
       mpz_mul (U1, U1, U1);
       mpz_mul (U0, U0, params[1]);
       mpz_sub (U0, U1, U0); // U(2k-1)=U(k)^2-QU(k-1)^2
       mpz_pow_ui (T, params[1], k);
       mpz_mul (U1, U1, D);
       mpz_addmul_ui (U1, T, 2);
       mpz_addmul (U1, params[1], U0); // U(2k+1)=DU(k)^2+2Q^k+QU(2k-1)
       if (mpz_tstbit (org_n, l) )
         {
	    k=2*k+1;
	    mpz_mul (U0,U0,params[1]); // U0 is 2k, U1 is 2k+1
	    mpz_add (U0,U1,U0);
	    mpz_divexact (U0,U0,params[0]);
	 }
       else
         {
 	    k=2*k;
	    mpz_addmul (U1,U0,params[1]); // U0 is 2k-1, U1 is 2k
	    mpz_divexact (U1,U1,params[0]);
         }
	 /* gmp_fprintf (stderr, "%d %Zd %Zd\n",k,U0,U1); */
    }
  mpz_set(n, U1);

  mpz_clear(U0);
  mpz_clear(U1);
  mpz_clear(org_n);
  mpz_clear(D);
  mpz_clear(T);

  return 1;
}

int eval_primU (mpz_t* params, mpz_t n)
{
  int factors[200];
  unsigned dwFactors=0, dw;
  unsigned long N;
  unsigned long p;
  mpz_t D, T;
  
  if (mpz_cmp_ui (n, 0) <= 0)
    return 0;
  if (mpz_cmp_ui (n, 1) == 0)
    {
	mpz_set_ui (n, 1);
      return 1;
    }

  /* Ignore the special cases where P^2=0,Q or 4Q*/
  if (mpz_cmp_ui (params[0], 0) == 0)
    {
      return 0;
    }
  mpz_init(D);
  mpz_mul(D, params[0], params[0]);
  if (mpz_cmp (D, params[1]) == 0)
    {
      return 0;
    }
  mpz_submul_ui(D, params[1], 4);
  if (mpz_cmp_ui (D, 0) == 0)
    {
      return 0;
    }  


  if (mpz_fits_ulong_p (n) == 0)
    return 0;

  N = mpz_get_ui (n);

   /* Obtain the factors of N */
  for (p = 2; p <= N; p++)
    {
      if (N % p == 0)
	{
	  /* Add the factor one time */
	  factors[dwFactors++] = p;
	  /* but be sure to totally remove it */
	  do { N /= p; } while (N % p == 0);
        }
     }
  
  
  N = mpz_get_ui (n);

  mpz_set_ui (n, 1);
  mpz_set_ui (D, 1);
  mpz_init (T);
	      
  for(dw=0;(dw<(1U<<dwFactors)); dw++)
    {
      /* for all Mobius terms */
      int iPower=N;
      int iMobius=0;
      unsigned dwIndex=0;
      unsigned dwMask=1;
		  
      while(dwIndex < dwFactors)
	{
	  if(dw&dwMask)
	    {
	      /* printf ("iMobius = %d iPower = %d, dwIndex = %d ", iMobius, iPower, dwIndex); */
	      iMobius++;
	      iPower/=factors[dwIndex];
	      /* printf ("Then iPower = %d\n", iPower);  */
	    }
	  dwMask<<=1;
	  ++dwIndex;
	}
      // gmp_fprintf (stderr, "Taking U(%Zd,%Zd,%d)\n", P,Q,iPower);
      mpz_set_ui(T,iPower);
      if(eval_U(params, T)==0)
      {
        return 0;
      }
    
      if(iMobius&1)
      {
	// gmp_fprintf (stderr, "Muling D=D*T  %Zd*%Zd\n", D, T);
	mpz_mul(D, D, T);
      }
      else
      {
	// gmp_fprintf (stderr, "Muling n=n*T  %Zd*%Zd\n", n, T);
	mpz_mul(n, n, T); 
      }
  }
  mpz_divexact(n, n, D);
  mpz_clear(T);
  mpz_clear(D);

  return 1;
}

/* A simple partial-recursive decent parser */
int eval_2 (int bInFuncParams)
{
  mpz_t n_stack[5];
  mpz_t n;
  mpz_t param_stack[5];
  int i,j;
  int num_base;
  char op_stack[5];
  char op;
  char negate;
  typedef int (*fptr)(mpz_t *,mpz_t);
  const int num_of_funcs=6;
  const char *func_names[]={"Phi","PhiL","PhiM","U","primU","gcd"};
  const int func_num_params[]={2,2,2,3,3,2};
  const fptr func_ptrs[]={eval_Phi,eval_PhiL,eval_PhiM,eval_U,eval_primU,eval_gcd};
  char *paren_position;
  char tentative_func_name[20];
  int func_id;
  for (i=0;i<5;i++)
    {
      op_stack[i]=0;
      mpz_init(n_stack[i]); 
      mpz_init(param_stack[i]); 
    }
  mpz_init(n);
  op = 0;
  negate = 0;

  for (;;)
    {
      if ('-'==(*expr_str))
	{
	  expr_str++;
	  negate=1;
	}
      else
	negate=0;
      if ('('==(*expr_str) || '['==(*expr_str) || '{'==(*expr_str))    /* Number is subexpression */
	{
	  expr_str++;
	  eval_2 (bInFuncParams);
	  mpz_set(n, t);
	}
      else            /* Number is decimal value */
	{
	  for (i=0;isdigit((int) expr_str[i]);i++)
	    ;
	  if (!i)         /* No digits found */
	    {
	      /* check for a valid "function" */
	      paren_position=strchr(&expr_str[i], '(');
	      if (NULL==paren_position)
	        {
		  /* No parentheses found */
		  fprintf (stderr, "\nError - invalid number [%c]\n", expr_str[i]);
		  exit (EXIT_FAILURE);
		}
	      strncpy(tentative_func_name,&expr_str[i],paren_position-&expr_str[i]);
	      tentative_func_name[paren_position-&expr_str[i]]='\0';
	      for (func_id=0;func_id<num_of_funcs;func_id++)
	        {
		  if (!strcasecmp (tentative_func_name, func_names[func_id]))
		    break;
		}
	      if(func_id==num_of_funcs)	/* No matching function name found */
	        {
		  fprintf (stderr, "Error, Unknown function %s()\n", tentative_func_name);
		  exit (EXIT_FAILURE);
		}
	      /* Now we can actually process existing functions */
	      expr_str=paren_position+1;
	      for(j=0;j<func_num_params[func_id]-1;j++)
	        {
		  /* eval the first parameters.  NOTE we pass a 1 since we ARE in parameter mode, 
		     and this causes the ',' character to act as the end of expression */		
		  if(eval_2 (1) != 2)
		    {
		      fprintf (stderr, "Error, Function %s() requires %d parameters\n", func_names[func_id], func_num_params[func_id]);
                      exit (EXIT_FAILURE);
		    }
		  mpz_set(param_stack[j], t);
		}
	      /* Now eval the last parameter NOTE we pass a 0 since we are NOT expecting a ','
		 character to end the expression, but are expecting a ) character to end the function */	
	      if (eval_2 (0))
		{
		  mpz_set(n, t);
		  if( (func_ptrs[func_id])(param_stack, n) == 0 )
		    {
		      fprintf (stderr, "\nParsing Error -  Invalid "
                               "parameter passed to the %s function\n", func_names[func_id]);
		      exit (EXIT_FAILURE);
		    }
		}
	      goto MONADIC_SUFFIX_LOOP;
	    }
	  /* Now check for a hex number.  If so, handle it as such */
	  num_base=10;  /* assume base 10 */
	  if (i == 1 && !strncasecmp(expr_str, "0x", 2))
	    {
		num_base = 16;	/* Kick up to hex */
		expr_str += 2;	/* skip the 0x string of the number */
		for (i=0;isxdigit((int) expr_str[i]);i++)
	          ;
	    }
	  op=expr_str[i];
	  expr_str[i]=0;
	  mpz_set_str(n,expr_str,num_base);
	  expr_str+=i;
	  *expr_str=op;
      }
      if (negate) 
	mpz_neg(n,n);

/* This label is needed for "normal" primorials and factorials, since they are evaluated Monadic suffix 
   expressions.  Most of this parser assumes Dyadic operators  with the only exceptino being the
   Monadic prefix operator of "unary minus" which is handled by simply ignoring it (but remembering),
   and then fixing the expression up when completed. */
/* This is ALSO where functions should be sent.  A function should "act" like a stand alone number.
   We should NOT start processing, and expecting a number, but we should expect an operator first */
MONADIC_SUFFIX_LOOP:
        op=*expr_str++;
	    
      if (0==op || ')'==op || ']'==op || '}'==op || (','==op&&bInFuncParams))
	{
	  eval_power (n, n_stack[2], op_stack[2]);
	  eval_product (n, n_stack[1], op_stack[1]);
	  eval_sum (n, n_stack[0], op_stack[0]);
	  mpz_set(t, n);
	  mpz_clear(n);
	  for (i=0;i<5;i++)
            {
              op_stack[i]=0;
              mpz_clear(n_stack[i]);
              mpz_clear(param_stack[i]);
            }
	  /* Hurray! a valid expression (or sub-expression) was parsed! */
	  return ','==op?2:1;
	}
      else
	{
	  if ('^' == op)
	    {
	      eval_power (n, n_stack[2], op_stack[2]);
	      mpz_set(n_stack[2], n);
	      op_stack[2]='^';
	    }
	  else if ('!' == op)
	    {
	      if (!isdigit((int) *expr_str))
		{
		  /* If the next char is not a digit, then this is a simple factorial, and not a "multi" factorial */
		  mpz_set(n_stack[2], n);
		  op_stack[2]='!';
		  goto MONADIC_SUFFIX_LOOP;
		}
	      eval_power (n, n_stack[2], op_stack[2]);
	      mpz_set(n_stack[2], n);
	      op_stack[2]='@';
	    }
	  else if ('#' == op)
	    {
	      if (!isdigit((int) *expr_str))
		{
  		  /* If the next char is not a digit, then this is a simple primorial, and not a "reduced" primorial */
		  mpz_set(n_stack[2], n);
		  op_stack[2]='#';
		  goto MONADIC_SUFFIX_LOOP;
		}
	      eval_power (n, n_stack[2], op_stack[2]);
	      mpz_set(n_stack[2], n);
	      op_stack[2]='$';
	    }
	  else
	    {
	      if ('.'==op || '*'==op || '/'==op || '%'==op)
		{
		  eval_power (n, n_stack[2], op_stack[2]);
		  op_stack[2]=0;
		  eval_product (n, n_stack[1], op_stack[1]);
		  mpz_set(n_stack[1], n);
		  op_stack[1]=op;
		}
	      else
		{
		  if ('+'==op || '-'==op)
		    {
		      eval_power (n, n_stack[2], op_stack[2]);
		      op_stack[2]=0;
		      eval_product (n, n_stack[1], op_stack[1]);
		      op_stack[1]=0;
		      eval_sum (n, n_stack[0], op_stack[0]);
		      mpz_set(n_stack[0], n);
		      op_stack[0]=op;
		    }
		  else    /* Error - invalid operator */
		    {
		      fprintf (stderr, "\nError - unknown operator: '%c'\n", op);
                      exit (EXIT_FAILURE);
		     }
		}
	    }
	}
    }
}

void init_expr(void)
{
  mpz_init_set_ui(mpOne, 1);
}

void free_expr(void)
{
  mpz_clear(mpOne);
}
