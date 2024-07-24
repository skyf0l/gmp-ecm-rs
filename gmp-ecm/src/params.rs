use std::mem::MaybeUninit;

use gmp_mpfr_sys::gmp;
use rug::Integer;

/// Factorization method.
#[derive(Debug, Clone, Copy)]
pub enum EcmMethod {
    /// Elliptic Curve Method
    Ecm,
    /// P-1 method
    Pm1,
    /// P+1 method
    Pp1,
}

/// Usage of the Number-Theoretic Transform code for polynomial arithmetic in stage 2
#[derive(Debug, Clone, Copy)]
pub enum NTT {
    /// Disable NTT
    Disabled,
    /// Enable NTT only for small inputs
    Auto,
    /// Enable NTT
    Enabled,
}

/// ECM parameters.
#[derive(Debug, Clone)]
pub struct EcmParams {
    /// Factorization method, default is ecm
    pub method: EcmMethod,

    // Bounds
    /// Stage 2 bound (all primes B1 <= p <= B2 are processed in step 2)
    pub b2: Option<Integer>,
    /// Stage 2 lower bound (all primes B2min <= p <= B2 are processed in step 2)
    pub b2_min: Option<Integer>,

    // Stage 2 parameters
    /// Usage of the Number-Theoretic Transform code for polynomial arithmetic in stage 2
    pub ntt: NTT,
}

impl Default for EcmParams {
    fn default() -> Self {
        Self {
            method: EcmMethod::Ecm,
            b2: None,
            b2_min: None,
            ntt: NTT::Auto,
        }
    }
}

pub(crate) struct RawEcmParams(gmp_ecm_sys::__ecm_param_struct);

impl RawEcmParams {
    pub fn as_mut_ptr(&mut self) -> *mut gmp_ecm_sys::__ecm_param_struct {
        &mut self.0
    }
}

impl From<&EcmParams> for RawEcmParams {
    fn from(params: &EcmParams) -> Self {
        let mut raw = unsafe {
            let mut raw = MaybeUninit::uninit();
            gmp_ecm_sys::ecm_init(raw.as_mut_ptr());
            raw.assume_init()
        };

        raw.method = match params.method {
            EcmMethod::Ecm => 0,
            EcmMethod::Pm1 => 1,
            EcmMethod::Pp1 => 2,
        };
        if let Some(b2) = &params.b2 {
            unsafe { gmp::mpz_set(raw.B2.as_mut_ptr() as *mut gmp::mpz_t, b2.as_raw()) };
        }
        if let Some(b2_min) = &params.b2_min {
            unsafe { gmp::mpz_set(raw.B2min.as_mut_ptr() as *mut gmp::mpz_t, b2_min.as_raw()) };
        }
        raw.use_ntt = match params.ntt {
            NTT::Disabled => 0,
            NTT::Auto => 1,
            NTT::Enabled => 2,
        };
        raw.verbose = 1;

        Self(raw)
    }
}

impl Drop for RawEcmParams {
    fn drop(&mut self) {
        unsafe {
            gmp_ecm_sys::ecm_clear(&mut self.0);
        }
    }
}
