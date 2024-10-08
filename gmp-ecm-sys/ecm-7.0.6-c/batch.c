/* batch.c - Implement batch mode for step 1 of ECM
 
Copyright 2011, 2012, 2016 Cyril Bouvier, Paul Zimmermann and David Cleaver.
 
This file is part of the ECM Library.

The ECM Library is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 3 of the License, or (at your
option) any later version.

The ECM Library is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
License for more details.

You should have received a copy of the GNU Lesser General Public License
along with the ECM Library; see the file COPYING.LIB.  If not, see
http://www.gnu.org/licenses/ or write to the Free Software Foundation, Inc.,
51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA. */


/* ECM stage 1 in batch mode, for initial point (x:z) with small coordinates,
   such that x and z fit into a mp_limb_t.
   For example we can start with (x=2:y=1) with the curve by^2 = x^3 + ax^2 + x
   with a = 4d-2 and b=16d+2, then we have to multiply by d=(a+2)/4 in the
   duplicates.
   With the change of variable x=b*X, y=b*Y, this curve becomes:
   Y^2 = X^3 + a/b*X^2 + 1/b^2*X.
*/

#include <stdlib.h>
#include "ecm-impl.h"
#include "getprime_r.h"

#define MAX_HEIGHT 32

#if ECM_UINT_MAX == 4294967295
/* On a 32-bit machine, with no access to a 64-bit type,
    the maximum value that can be returned by mpz_sizeinbase(s,2)
    is = (2^32-1).  Multiplying all primes up to the following
    will result in a product that has (2^32-1) bits. */
#define MAX_B1_BATCH 2977044736UL
#elif defined(_WIN32) && __GNU_MP_VERSION <= 6 && !defined(__MPIR_VERSION)
/* Due to a limitation in GMP on 64-bit Windows, should also
    affect 32-bit Windows, sufficient memory cannot be allocated
    for the batch product s when using primes larger than the following */
#define MAX_B1_BATCH 3124253146UL
#else
/* nth_prime(2^(MAX_HEIGHT-1))-1 */
#define MAX_B1_BATCH 50685770166ULL
#endif

/* If forbiddenres != NULL, forbiddenres = "m r_1 ... r_k -1" indicating that
   if p = r_i mod m, then p^2 should be considered instead of p. This has
   only a sense for CM curves. We assume r_1 < r_2 < ... < r_k.
   Typical example: "4 3 -1" for curves Y^2 = X^3 + a * X.
*/
void
compute_s (mpz_t s, ecm_uint B1, int *forbiddenres ATTRIBUTE_UNUSED)
{
  mpz_t acc[MAX_HEIGHT]; /* To accumulate products of prime powers */
  mpz_t ppz;
  unsigned int i, j;
  ecm_uint pi = 2, pp, maxpp, qi;
  prime_info_t prime_info;

  prime_info_init (prime_info);

  ASSERT_ALWAYS (B1 <= MAX_B1_BATCH);

  for (j = 0; j < MAX_HEIGHT; j++)
    mpz_init (acc[j]); /* sets acc[j] to 0 */
  mpz_init (ppz);

  i = 0;
  while (pi <= B1)
    {
      pp = qi = pi;
      maxpp = B1 / qi;
#ifdef HAVE_ADDLAWS
      if (forbiddenres != NULL && pi > 2)
        {
          /* non splitting primes can occur in even powers only */
          int rp = (int)(pi % forbiddenres[0]);
          for (j = 1; forbiddenres[j] >= 0; j++)
            if (rp >= forbiddenres[j])
              break;
          if (rp == forbiddenres[j])
            {
              /* printf("p=%lu is forbidden\n", pi); */
              if (qi <= maxpp)
                {
                  /* qi <= B1/qi => qi^2 <= B1, let it go */
                  qi *= qi;
                }
              else
                {
                  /* qi is too large, do not increment i */
                  pi = getprime_mt (prime_info);
                  continue;
                }
            }
        }
#endif
      while (pp <= maxpp)
          pp *= qi;

#if ECM_UINT_MAX == 4294967295
      mpz_set_ui (ppz, pp);
#else
      mpz_set_uint64 (ppz, pp);
#endif

      if ((i & 1) == 0)
        mpz_set (acc[0], ppz);
      else
        mpz_mul (acc[0], acc[0], ppz);

      j = 0;
      /* We have accumulated i+1 products so far. If bits 0..j of i are all
         set, then i+1 is a multiple of 2^(j+1). */
      while ((i & (1 << j)) != 0)
        {
          /* we use acc[MAX_HEIGHT-1] as 0-sentinel below, thus we need
             j+1 < MAX_HEIGHT-1 */
          ASSERT (j + 1 < MAX_HEIGHT - 1);
          if ((i & (1 << (j + 1))) == 0) /* i+1 is not multiple of 2^(j+2),
                                            thus add[j+1] is "empty" */
            mpz_swap (acc[j+1], acc[j]); /* avoid a copy with mpz_set */
          else
            mpz_mul (acc[j+1], acc[j+1], acc[j]); /* accumulate in acc[j+1] */
          mpz_set_ui (acc[j], 1);
          j++;
        }

      i++;
      pi = getprime_mt (prime_info);
    }

  for (mpz_set (s, acc[0]), j = 1; mpz_cmp_ui (acc[j], 0) != 0; j++)
    mpz_mul (s, s, acc[j]);

  prime_info_clear (prime_info); /* free the prime tables */
  
  for (i = 0; i < MAX_HEIGHT; i++)
    mpz_clear (acc[i]);
  mpz_clear (ppz);
}

#if 0
/* this function is useful in debug mode to print non-normalized residues */
static void
mpresn_print (mpres_t x, mpmod_t n)
{
  mp_size_t m, xn;

  xn = SIZ(x);
  m = ABSIZ(x);
  MPN_NORMALIZE(PTR(x), m);
  SIZ(x) = xn >= 0 ? m : -m;
  gmp_printf ("%Zd\n", x);
  SIZ(x) = xn;
}
#endif

/* (x1:z1) <- 2(x1:z1)
   (x2:z2) <- (x1:z1) + (x2:z2)
   assume (x2:z2) - (x1:z1) = (2:1)
   Uses 4 modular multiplies and 4 modular squarings.
   Inputs are x1, z1, x2, z2, d, n.
   Use two auxiliary variables: t, w (it seems using one only is not possible
   if all mpresn_mul and mpresn_sqr calls don't overlap input and output).

   In the batch 1 mode, we pass d_prime such that the actual d is d_prime/beta.
   Since beta is a square, if d_prime is a square (on 64-bit machines),
   so is d.
   In mpresn_mul_1, we multiply by d_prime = beta*d and divide by beta.
*/
static void
dup_add_batch1 (mpres_t x1, mpres_t z1, mpres_t x2, mpres_t z2,
                mpres_t t, mpres_t w, mp_limb_t d_prime, mpmod_t n)
{
  /* active: x1 z1 x2 z2 */
  mpresn_addsub (w, z1, x1, z1, n); /* w = x1+z1, z1 = x1-z1 */
  /* active: w z1 x2 z2 */
  mpresn_addsub (x1, x2, x2, z2, n); /* x1 = x2+z2, x2 = x2-z2 */
  /* active: w z1 x1 x2 */

  mpresn_mul (z2, w, x2, n); /* w = (x1+z1)(x2-z2) */
  /* active: w z1 x1 z2 */
  mpresn_mul (x2, z1, x1, n); /* x2 = (x1-z1)(x2+z2) */
  /* active: w z1 x2 z2 */
  mpresn_sqr (t, z1, n);    /* t = (x1-z1)^2 */
  /* active: w t x2 z2 */
  mpresn_sqr (z1, w, n);    /* z1 = (x1+z1)^2 */
  /* active: z1 t x2 z2 */

  mpresn_mul (x1, z1, t, n); /* xdup = (x1+z1)^2 * (x1-z1)^2 */
  /* active: x1 z1 t x2 z2 */

  mpresn_sub (w, z1, t, n);   /* w = (x1+z1)^2 - (x1-z1)^2 */
  /* active: x1 w t x2 z2 */

  mpresn_mul_1 (z1, w, d_prime, n); /* z1 = d * ((x1+z1)^2 - (x1-z1)^2) */
  /* active: x1 z1 w t x2 z2 */

  mpresn_add (t, t, z1, n);  /* t = (x1-z1)^2 - d* ((x1+z1)^2 - (x1-z1)^2) */
  /* active: x1 w t x2 z2 */
  mpresn_mul (z1, w, t, n); /* zdup = w * [(x1-z1)^2 - d* ((x1+z1)^2 - (x1-z1)^2)] */
  /* active: x1 z1 x2 z2 */

  mpresn_addsub (w, z2, x2, z2, n);
  /* active: x1 z1 w z2 */

  mpresn_sqr (x2, w, n);
  /* active: x1 z1 x2 z2 */
  mpresn_sqr (w, z2, n);
  /* active: x1 z1 x2 w */
  mpresn_add (z2, w, w, n);
}

static void
dup_add_batch2 (mpres_t x1, mpres_t z1, mpres_t x2, mpres_t z2,
                mpres_t t, mpres_t w, mpres_t d, mpmod_t n)
{
  /* active: x1 z1 x2 z2 */
  mpresn_addsub (w, z1, x1, z1, n); /* w = x1+z1, z1 = x1-z1 */
  /* active: w z1 x2 z2 */
  mpresn_addsub (x1, x2, x2, z2, n); /* x1 = x2+z2, x2 = x2-z2 */
  /* active: w z1 x1 x2 */

  mpresn_mul (z2, w, x2, n); /* w = (x1+z1)(x2-z2) */
  /* active: w z1 x1 z2 */
  mpresn_mul (x2, z1, x1, n); /* x2 = (x1-z1)(x2+z2) */
  /* active: w z1 x2 z2 */
  mpresn_sqr (t, z1, n);    /* t = (x1-z1)^2 */
  /* active: w t x2 z2 */
  mpresn_sqr (z1, w, n);    /* z1 = (x1+z1)^2 */
  /* active: z1 t x2 z2 */

  mpresn_mul (x1, z1, t, n); /* xdup = (x1+z1)^2 * (x1-z1)^2 */
  /* active: x1 z1 t x2 z2 */

  mpresn_sub (w, z1, t, n);   /* w = (x1+z1)^2 - (x1-z1)^2 */
  /* active: x1 w t x2 z2 */

  mpresn_mul (z1, w, d, n); /* z1 = d * ((x1+z1)^2 - (x1-z1)^2) */
  /* active: x1 z1 w t x2 z2 */

  mpresn_add (t, t, z1, n);  /* t = (x1-z1)^2 - d* ((x1+z1)^2 - (x1-z1)^2) */
  /* active: x1 w t x2 z2 */
  mpresn_mul (z1, w, t, n); /* zdup = w * [(x1-z1)^2 - d* ((x1+z1)^2 - (x1-z1)^2)] */
  /* active: x1 z1 x2 z2 */

  mpresn_addsub (w, z2, x2, z2, n);
  /* active: x1 z1 w z2 */

  mpresn_sqr (x2, w, n);
  /* active: x1 z1 x2 z2 */
  mpresn_sqr (w, z2, n);
  /* active: x1 z1 x2 w */
  mpresn_add (z2, w, w, n);
}


/* Input: x is initial point
          A is curve parameter in Montgomery's form:
          g*y^2*z = x^3 + a*x^2*z + x*z^2
          n is the number to factor
          B1 is the stage 1 bound
   Output: If a factor is found, it is returned in x.
           Otherwise, x contains the x-coordinate of the point computed
           in stage 1 (with z coordinate normalized to 1).
           B1done is set to B1 if stage 1 completed normally,
           or to the largest prime processed if interrupted, but never
           to a smaller value than B1done was upon function entry.
   Return value: ECM_FACTOR_FOUND_STEP1 if a factor, otherwise 
           ECM_NO_FACTOR_FOUND
*/
/*
For now we don't take into account go stop_asap and chkfilename
*/
int
ecm_stage1_batch (mpz_t f, mpres_t x, mpres_t A, mpmod_t n, double B1,
                  double *B1done, int batch, mpz_t s)
{
  mp_limb_t d_1 = 0;
  mpz_t d_2;

  mpres_t x1, z1, x2, z2;
  ecm_uint i;
  mpres_t t, u;
  int ret = ECM_NO_FACTOR_FOUND;

  mpres_init (x1, n);
  mpres_init (z1, n);
  mpres_init (x2, n);
  mpres_init (z2, n);
  mpres_init (t, n);
  mpres_init (u, n);
  if (batch == ECM_PARAM_BATCH_2)
    mpres_init (d_2, n);

  /* initialize P */
  mpres_set (x1, x, n);
  mpres_set_ui (z1, 1, n); /* P1 <- 1P */

  /* Compute d=(A+2)/4 from A and d'=B*d thus d' = 2^(GMP_NUMB_BITS-2)*(A+2) */
  if (batch == ECM_PARAM_BATCH_SQUARE || batch == ECM_PARAM_BATCH_32BITS_D)
    {
      mpres_get_z (u, A, n);
      mpz_add_ui (u, u, 2);
      mpz_mul_2exp (u, u, GMP_NUMB_BITS - 2);
      mpres_set_z_for_gcd (u, u, n); /* reduces u mod n */
      if (mpz_size (u) > 1)
        {
          mpres_get_z (u, A, n);
          outputf (OUTPUT_ERROR, "Error, with -param %d, sigma should be < 2^32\n", batch);
          return ECM_USER_ERROR;
        }
      d_1 = mpz_getlimbn (u, 0);
    }
  else
    {
      /* b = (A0+2)*B/4, where B=2^(k*GMP_NUMB_BITS)
         for MODMULN or REDC, B=2^GMP_NUMB_BITS for batch1,
         and B=1 otherwise */
      mpres_add_ui (d_2, A, 2, n);
      mpres_div_2exp (d_2, d_2, 2, n); 
    }

  /* Compute 2P : no need to duplicate P, the coordinates are simple. */
  mpres_set_ui (x2, 9, n);
  /* here d = d_1 / GMP_NUMB_BITS */
  if (batch == ECM_PARAM_BATCH_SQUARE || batch == ECM_PARAM_BATCH_32BITS_D)
    {
      /* warning: mpres_set_ui takes an unsigned long which has only 32 bits
         on Windows, while d_1 might have 64 bits */
      ASSERT_ALWAYS (mpz_size (u) == 1 && mpz_getlimbn (u, 0) == d_1);
      mpres_set_z (z2, u, n);
      mpres_div_2exp (z2, z2, GMP_NUMB_BITS, n);
    }
  else
      mpres_set (z2, d_2, n);
 
  mpres_mul_2exp (z2, z2, 6, n);
  mpres_add_ui (z2, z2, 8, n); /* P2 <- 2P = (9 : : 64d+8) */

  /* invariant: if j represents the upper bits of s,
     then P1 = j*P and P2=(j+1)*P */

  mpresn_pad (x1, n);
  mpresn_pad (z1, n);
  mpresn_pad (x2, n);
  mpresn_pad (z2, n);

  /* now perform the double-and-add ladder */
  if (batch == ECM_PARAM_BATCH_SQUARE || batch == ECM_PARAM_BATCH_32BITS_D)
    {
      for (i = mpz_sizeinbase (s, 2) - 1; i-- > 0;)
        {
          if (ecm_tstbit (s, i) == 0) /* (j,j+1) -> (2j,2j+1) */
            /* P2 <- P1+P2    P1 <- 2*P1 */
            dup_add_batch1 (x1, z1, x2, z2, t, u, d_1, n);
          else /* (j,j+1) -> (2j+1,2j+2) */
              /* P1 <- P1+P2     P2 <- 2*P2 */
            dup_add_batch1 (x2, z2, x1, z1, t, u, d_1, n);
        }
    }
  else /* batch = ECM_PARAM_BATCH_2 */
    {
      mpresn_pad (d_2, n);
      for (i = mpz_sizeinbase (s, 2) - 1; i-- > 0;)
        {
          if (ecm_tstbit (s, i) == 0) /* (j,j+1) -> (2j,2j+1) */
            /* P2 <- P1+P2    P1 <- 2*P1 */
            dup_add_batch2 (x1, z1, x2, z2, t, u, d_2, n);
          else /* (j,j+1) -> (2j+1,2j+2) */
              /* P1 <- P1+P2     P2 <- 2*P2 */
            dup_add_batch2 (x2, z2, x1, z1, t, u, d_2, n);
        }
    }

  *B1done=B1;

  mpresn_unpad (x1);
  mpresn_unpad (z1);

  if (!mpres_invert (u, z1, n)) /* Factor found? */
    {
      mpres_gcd (f, z1, n);
      ret = ECM_FACTOR_FOUND_STEP1;
    }
  mpres_mul (x, x1, u, n);

  mpz_clear (x1);
  mpz_clear (z1);
  mpz_clear (x2);
  mpz_clear (z2);
  mpz_clear (t);
  mpz_clear (u);
  if (batch == ECM_PARAM_BATCH_2)
      mpz_clear (d_2);

  return ret;
}
