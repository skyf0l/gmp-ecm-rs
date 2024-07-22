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

/// ECM parameters.
#[derive(Debug, Clone)]
pub struct EcmParams {
    /// Factorization method, default is ecm
    pub method: EcmMethod,
    /// lower bound for stage 2 (default is B1)
    pub b2_min: Option<Integer>,
    /// Step 2 bound (chosen automatically if < 0)
    pub b2: Option<Integer>,
}

impl Default for EcmParams {
    fn default() -> Self {
        Self {
            method: EcmMethod::Ecm,
            b2_min: None,
            b2: None,
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
        if let Some(b2_min) = &params.b2_min {
            unsafe { gmp::mpz_set(raw.B2min.as_mut_ptr() as *mut gmp::mpz_t, b2_min.as_raw()) };
        }
        if let Some(b2) = &params.b2 {
            unsafe { gmp::mpz_set(raw.B2.as_mut_ptr() as *mut gmp::mpz_t, b2.as_raw()) };
        }
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
