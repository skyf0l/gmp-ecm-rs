use std::{str::FromStr, time::Duration};

use clap::{command, Parser};
use gmp_ecm::{ecm_factor, EcmMethod, EcmParams, NTT};
use rug::Integer;
use update_informer::{registry, Check};

fn parse_integer(s: &str) -> Result<Integer, std::io::Error> {
    Integer::from_str(s)
        .map_err(|_| std::io::Error::new(std::io::ErrorKind::InvalidInput, "Invalid integer"))
}

#[derive(Parser, Debug, Clone)]
#[command(author, version)]
struct Args {
    /// Input number.
    #[clap(short, value_parser = parse_integer)]
    n: Integer,

    // Factoring method
    /// Perform P-1 instead of the default method (ECM).
    #[clap(long)]
    pm1: bool,
    /// Perform P+1 instead of the default method (ECM).
    #[clap(long)]
    pp1: bool,

    // Bounds
    /// Stage 1 bound (all primes 2 <= p <= B1 are processed in step 1).
    #[clap(long)]
    b1: f64,
    /// Stage 2 bound (all primes B1 <= p <= B2 are processed in step 2). [default: optimally computed from B1]
    #[clap(long, value_parser = parse_integer)]
    b2: Option<Integer>,
    /// Stage 2 lower bound (all primes B2min <= p <= B2 are processed in step 2). [default: B1]
    #[clap(long, value_parser = parse_integer)]
    b2_min: Option<Integer>,

    // Group and initial point parameters
    /// Initial x point. [default: generated from sigma for ECM, or at random for P-1 and P+1]
    #[clap(short, long, value_parser = parse_integer)]
    x0: Option<Integer>, // allow rational numbers
    /// Initial y point. [default: generated from sigma for ECM, or at random for P-1 and P+1]
    #[clap(short, long, value_parser = parse_integer)]
    y0: Option<Integer>, // allow rational numbers
    /// [ECM only] Curve parametrization.
    #[clap(long, value_parser = parse_integer, conflicts_with_all = &["pm1", "pp1"])]
    param: usize,
    /// [ECM only] Seed of the curve generator (can use --sigma i:s to specify --param i at the same). [default: random]
    #[clap(long, value_parser = parse_integer, conflicts_with_all = &["pm1", "pp1"])]
    sigma: Option<Integer>,
    /// [ECM only] Curve coefficient. [default: generated from sigma]
    #[clap(short, value_parser = parse_integer, conflicts_with_all = &["pm1", "pp1"])]
    a: Option<Integer>,
    // TODO: go

    // Stage 2 parameters
    /// Perform k blocks in step 2. (Increasing k decreases the memory usage of step 2, at the expense of more cpu time.)
    #[clap(short, value_parser = parse_integer)]
    k: Option<Integer>,
    // TODO: treefile
    /// [ECM only] Use x^n for Brent-Suyama's extension (-power 1 disables Brent-Suyama's extension). [default: auto]
    #[clap(long, value_parser = parse_integer, conflicts_with_all = &["pm1", "pp1", "dickson"])]
    power: Option<Integer>,
    /// [ECM only] Use degree-n Dickson's polynomial for Brent-Suyama's extension.
    #[clap(long, value_parser = parse_integer, conflicts_with_all = &["pm1", "pp1", "power"])]
    dickson: Option<Integer>,
    /// Use at most n megabytes of memory in stage 2.
    #[clap(long, conflicts_with = "no_ntt")]
    maxmem: usize,
    /// Always use NTT convolution routines in stage 2. [default: auto]
    #[clap(long, conflicts_with = "no_ntt")]
    ntt: bool,
    /// Never use NTT convolution routines in stage 2. [default: auto]
    #[clap(long, conflicts_with = "ntt")]
    no_ntt: bool,

    // Modular arithmetic options
    /// Use GMP's mpz_mod function (sub-quadratic for large inputs, but induces some overhead for small ones). [default: auto]
    #[clap(long)]
    mpzmod: bool,
    /// Use Montgomery's multiplication (quadratic version). Usually best method for small input. [default: auto]
    #[clap(long)]
    modmuln: bool,
    /// Use Montgomery's multiplication (sub-quadratic version). Theoretically optimal for large input. [default: auto]
    #[clap(long)]
    redc: bool,
    /// Disable special base-2 code (which is used when the input number is a large factor of 2^n+1 or 2^n-1, see -v). [default: auto]
    #[clap(long)]
    nobase2: bool,
    /// Disable special base-2 code in ecm stage 2 only (which is used when the input number is a large factor of 2^n+1 or 2^n-1, see -v). [default: auto]
    #[clap(long)]
    nobase2s2: bool,
    /// Force use of special base-2 code, input number must divide 2^n+1 if n > 0, or 2^|n|-1 if n < 0. [default: auto]
    #[clap(long, value_parser = parse_integer)]
    base2: Option<Integer>,

    // Loop mode
    /// Perform n runs on each input number and stop if a factor is found. [default: 1]
    #[clap(long, conflicts_with_all = &["sigma", "x0", "y0"])]
    c: usize,
    /// Stop processing a candidate if a factor is found. (looping mode)
    #[clap(long, conflicts_with_all = &["sigma", "x0", "y0"])]
    one: bool,
    // Output
    // TODO: quiet, verbose, timestamp, torsion, primetest, stage1time
}

fn main() {
    let pkg_name = env!("CARGO_PKG_NAME");
    let current_version = env!("CARGO_PKG_VERSION");
    let informer = update_informer::new(registry::Crates, pkg_name, current_version)
        .interval(Duration::from_secs(60 * 60));
    if let Ok(Some(new_version)) = informer.check_version() {
        eprintln!("A new release of {pkg_name} is available: v{current_version} -> {new_version}");
        eprintln!("You can update by running: cargo install {pkg_name}\n");
    }

    // Parse command line arguments
    let args = Args::parse();

    // Set static parameters
    let params = EcmParams {
        // Factoring method
        method: if args.pp1 {
            EcmMethod::Pp1
        } else {
            EcmMethod::Ecm
        },

        // Bounds
        b2: args.b2,
        b2_min: args.b2_min,

        // Stage 2 parameters
        ntt: if args.ntt {
            NTT::Enabled
        } else if args.no_ntt {
            NTT::Disabled
        } else {
            NTT::Auto
        },
    };
    let res = ecm_factor(&args.n, args.b1, &params);
    println!("Found factor: {:?}", res);
}
