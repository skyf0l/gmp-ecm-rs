/* This file sets tuning parameters for GMP-ECM.
   According to the preprocessor macros obtained with gcc -dM -E -xc /dev/null
   or gcc -march=native -dM -E -xc /dev/null
   or gcc -mtune=native -dM -E -xc /dev/null, it includes one file or the
   other one. */

#if defined (PARAMS00)
#define ECM_TUNE_CASE "generic/params00.h"
#include "generic/params00.h"

#elif defined (PARAMS11)
#define ECM_TUNE_CASE "generic/params11.h"
#include "generic/params11.h"

#elif defined (PARAMS22)
#define ECM_TUNE_CASE "generic/params22.h"
#include "generic/params22.h"

#elif defined (PARAMS33)
#define ECM_TUNE_CASE "generic/params33.h"
#include "generic/params33.h"

#elif defined(__x86_64)
#define ECM_TUNE_CASE "x86_64/params.h"
#include "x86_64/params.h"

#elif defined (__tune_pentiumpro__) || defined (__tune_i686__) || defined (__i386) /* we consider all other 386's here */
#define ECM_TUNE_CASE "x86/params.h"
#include "x86/params.h"

#elif defined (__ia64) || defined (__itanium__) || defined (__tune_ia64__)
/* Threshold for IA64 */
#define ECM_TUNE_CASE "ia64/params.h"
#include "ia64/params.h"

#elif defined (__arm__) /* Threshold for ARM */
#define ECM_TUNE_CASE "arm/params.h"
#include "arm/params.h"

#elif defined (__PPC64__) /* Threshold for 64-bit PowerPC, test it before
                             32-bit PowerPC since _ARCH_PPC is also defined
                             on 64-bit PowerPC */
#define ECM_TUNE_CASE "powerpc64/params.h"
#include "powerpc64/params.h"

#elif defined (_ARCH_PPC) /* Threshold for 32-bit PowerPC */
#define ECM_TUNE_CASE "powerpc32/params.h"
#include "powerpc32/params.h"

#elif defined (__sparc_v9__) /* Threshold for 64-bits Sparc */
#define ECM_TUNE_CASE "sparc64/params.h"
#include "sparc64/params.h"

#elif defined (__hppa__) /* Threshold for HPPA */
#define ECM_TUNE_CASE "hppa/params.h"
#include "hppa/params.h"

#elif defined (__mips__) /* MIPS */
#define ECM_TUNE_CASE "mips/params.h"
#include "mips/params.h"

#else
#define ECM_TUNE_CASE "default"
#endif

/****************************************************************
 * Default values of Threshold.                                 *
 * Must be included in any case: it checks, for every constant, *
 * if it has been defined, and it sets it to a default value if *
 * it was not previously defined.                               *
 ****************************************************************/
#include "generic/params.h"
