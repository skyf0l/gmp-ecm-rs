#![doc = include_str!("../README.md")]
#![deny(rust_2018_idioms)]
#![warn(missing_docs)]

use gmp_ecm_sys::__mpz_struct;
use rug::Integer;
use std::ffi::CStr;

mod params;
pub use params::*;

/// Returns the version of the ECM library.
pub fn ecm_version() -> &'static str {
    unsafe { CStr::from_ptr(gmp_ecm_sys::ecm_version()).to_str().unwrap() }
}

/// Returns one factor of N using the Elliptic Curve Method.
pub fn ecm_factor(n: &Integer, b1: f64, params: &EcmParams) -> Integer {
    let mut n = n.clone();

    unsafe {
        let mut params = RawEcmParams::from(params);
        println!("ECM params: {}", ecm_version());
        let mut factor = Integer::ZERO;

        // Args: factor, n, b1, params
        gmp_ecm_sys::ecm_factor(
            factor.as_raw_mut() as *mut __mpz_struct,
            n.as_raw_mut() as *mut __mpz_struct,
            b1,
            params.as_mut_ptr(),
        );
        factor
    }
}
