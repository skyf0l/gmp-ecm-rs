/* ecm-ecm.h - private header file for GMP-ECM.

Copyright 2001, 2002, 2003, 2004, 2005, 2006, 2007, 2008, 2009, 2010, 2011,
2012 Paul Zimmermann, Alexander Kruppa and Cyril Bouvier.

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

#ifndef _ECM_ECM_H
#define _ECM_ECM_H 1

#include "config.h"

#include <assert.h>
#define ASSERT_ALWAYS(expr)   assert (expr)
#ifdef WANT_ASSERT
#define ASSERT(expr)   assert (expr)
#else
#define ASSERT(expr)   do {} while (0)
#endif

#include "ecm.h"

/* Structure for candidate usage.  This is much more powerful than using a
   simple mpz_t to hold the candidate.  This structure also houses the 
   expression (in raw form), and will modify the expression as factors 
   are found (if in looping modes).  Also, since we are warehousing all
   of the data associated with the candidate, we also store whether the
   candidate is PRP here (so testing will cease), along with the length
   of the candidate.  As each factor is found, the candidate will also
   have the factor removed from it */
typedef struct
{
#if defined (CANDI_DEBUG)
  unsigned long magic;	/* used for debugging purposes while writing this code */
#endif
  char *cpExpr;		/* if non-NULL, then this is a "simpler" expression than the 
			   decimal output of n */
  mpz_t n;		/* the cofactor candidate currently being used to find factors from */
  unsigned ndigits;	/* the number of digits (decimal) in n */
  unsigned nexprlen;	/* strlen of expression, 0 if there is NO expression */
  int isPrp;		/* usually 0, but turns 1 if factor found, and the cofactor is PRP, 
			   OR if the original candidate was PRP and the user asked to prp check */
} mpcandi_t;

typedef struct
{
  int  Valid;           /* Is ONLY set to 1 if there is a proper -go <integer> switch.  Otherwise is 0
                           and if 0, then PM1, PP1 and ECM all ignore it */
  char *cpOrigExpr;	/* if non-NULL, then this is a "simpler" expression than the 
			   decimal output of n */
  mpcandi_t Candi;      /* The value after expression checked */
  int containsN;        /* 0 for simple number or expression.  1 if the expression "contains" N as
                           that expression will have to be built for each candidate */
} mpgocandi_t;

/* auxi.c */
unsigned int nb_digits  (const mpz_t);
int read_number (mpcandi_t*, FILE*, int);
int process_newfactor (mpz_t, int, mpcandi_t*, int, int, int, unsigned int*, 
                       int*, mpz_t, FILE*, int, int);

/* Various logging levels */
/* OUTPUT_ALWAYS means print always, regardless of verbose value */
#define OUTPUT_ALWAYS 0
/* OUTPUT_NORMAL means print during normal program execution */
#define OUTPUT_NORMAL 1
/* OUTPUT_VERBOSE means print if the user requested more verbosity */
#define OUTPUT_VERBOSE 2
/* OUTPUT_RESVERBOSE is for printing residues (after stage 1 etc) */
#define OUTPUT_RESVERBOSE 3
/* OUTPUT_DEVVERBOSE is for printing internal parameters (for developers) */
#define OUTPUT_DEVVERBOSE 4
/* OUTPUT_TRACE is for printing trace data, produces lots of output */
#define OUTPUT_TRACE 5
/* OUTPUT_ERROR is for printing error messages */
#define OUTPUT_ERROR -1

#define MAX_NUMBER_PRINT_LEN 1000

#define NTT_SIZE_THRESHOLD 30

/* auxlib.c */
int  test_verbose (int);
void set_verbose (int);

/* Return codes */
/* Bit coded values: 1: error (for example out of memory)
   2: proper factor found, 4: factor is prime, 
   8: cofactor is prime or 1 */
#define ECM_EXIT_ERROR 1
#define ECM_COMP_FAC_COMP_COFAC 2
#define ECM_PRIME_FAC_COMP_COFAC (2+4)
#define ECM_INPUT_NUMBER_FOUND 8
#define ECM_COMP_FAC_PRIME_COFAC (2+8)
#define ECM_PRIME_FAC_PRIME_COFAC (2+4+8)

/* getprime.c */
double getprime (void);
void getprime_clear (void);
#define WANT_FREE_PRIME_TABLE(p) (p < 0.0)
#define FREE_PRIME_TABLE -1.0

/* resume.c */
int  read_resumefile_line (int *, mpz_t, mpz_t, mpcandi_t *, 
			   mpz_t, mpz_t,
			   mpz_t, mpz_t, int *, int *,
                           double *, char *, char *, char *, char *, FILE *);
int write_resumefile (char *, int, ecm_params params,
		      mpcandi_t *, const mpz_t, const mpz_t, const mpz_t,
		      const char *);
int write_s_in_file (char *, mpz_t);
int read_s_from_file (mpz_t, char *, double); 

/* main.c */
int kbnc_z (double *k, unsigned long *b, unsigned long *n, signed long *c,
            mpz_t z);
int kbnc_str (double *k, unsigned long *b, unsigned long *n, signed long *c,
              char *z, mpz_t num);

/* eval.c */
int eval (mpcandi_t *n, FILE *fd, int bPrp);
int eval_str (mpcandi_t *n, char *cp, int primetest, char **EndChar); /* EndChar can be NULL */
void init_expr (void);
void free_expr (void);

/* candi.c */
void mpcandi_t_init (mpcandi_t *n);  /* damn, a C++ class sure would have been nice :(  */
void mpcandi_t_free (mpcandi_t *n);
int  mpcandi_t_copy (mpcandi_t *to, mpcandi_t *from);
int  mpcandi_t_add_candidate (mpcandi_t *n, mpz_t c, const char *cpExpr, int bPrp);
int  mpcandi_t_addfoundfactor (mpcandi_t *n, mpz_t f, int displaywarning);
int  mpcandi_t_addfoundfactor_d (mpcandi_t *n, double f);
/* candi.c   Group Order candidate functions.  */
void mpgocandi_t_init(mpgocandi_t *go);
void mpgocandi_t_free(mpgocandi_t *go);
int  mpgocandi_fixup_with_N(mpgocandi_t *go, mpcandi_t *n);

/* random.c */
#undef get_random_ul
unsigned long get_random_ul (void);
#undef pp1_random_seed
void pp1_random_seed  (mpz_t, mpz_t, gmp_randstate_t);
#undef pm1_random_seed
void pm1_random_seed  (mpz_t, mpz_t, gmp_randstate_t);

/* memusage.c */
long PeakMemusage (void);

/* default number of probable prime tests */
#define PROBAB_PRIME_TESTS 1

/* maximal stage 1 bound = 2^53 - 1, the next prime being 2^53 + 5 */
#define MAX_B1 9007199254740991.0

/* The checksum for savefile is the product of all mandatory fields, modulo
   the greatest prime below 2^32 */
#define CHKSUMMOD 4294967291U

#define ABS(x) ((x) >= 0 ? (x) : -(x))

#ifdef HAVE_APRCL
#include "aprtcle/mpz_aprcl.h"
#define ECM_FAC_PRIME APRTCLE_PRIME
#define ECM_FAC_PRP APRTCLE_PRP
#else
#define mpz_aprtcle(x,y) mpz_probab_prime_p(x,PROBAB_PRIME_TESTS)
#define ECM_FAC_PRIME 2 /* mpz_probab_prime_p and aprcl returns 2 when
                           a number is definitely prime */
#define ECM_FAC_PRP 1   /* mpz_probab_prime_p and aprcl returns 1 when
                           a number is a probable prime */
#endif

/* Cutoff value: Show APRCL progress only if n has more digits than cutoff */
#define APRCL_CUTOFF 400   /* for more than APRCL_CUTOFF digits, print
                              progress */
/* Cutoff value: Use APRCL if n has fewer digits than cutoff2
                 Use mpz_probab_prime_p if n has more digits than cutoff2 */
#define APRCL_CUTOFF2 500 /* for more than APRCL_CUTOFF2 digits, perform
                              a pseudo-primality test */

#endif /* _ECM_ECM_H */
