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
#[command(author, version, about)]
struct Args {
    /// Input number
    #[clap(short, value_parser = parse_integer)]
    n: Integer,

    // Factoring method
    /// Perform P-1 instead of the default method (ECM)
    #[clap(long)]
    pm1: bool,
    /// Perform P+1 instead of the default method (ECM)
    #[clap(long)]
    pp1: bool,

    // Bounds
    /// Stage 1 bound (all primes 2 <= p <= B1 are processed in step 1)
    #[clap(long)]
    b1: f64,
    /// Stage 2 bound (all primes B1 <= p <= B2 are processed in step 2) [default: optimally computed from B1]
    #[clap(long, value_parser = parse_integer)]
    b2: Option<Integer>,
    /// Stage 2 lower bound (all primes B2min <= p <= B2 are processed in step 2) [default: B1]
    #[clap(long, value_parser = parse_integer)]
    b2_min: Option<Integer>,

    // Stage 2 parameters
    /// Always use NTT convolution routines in stage 2 [default: auto]
    #[clap(long, conflicts_with = "no_ntt")]
    ntt: bool,
    /// Never use NTT convolution routines in stage 2 [default: auto]
    #[clap(long, conflicts_with = "ntt")]
    no_ntt: bool,
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
        method: if args.pp1 {
            EcmMethod::Pp1
        } else {
            EcmMethod::Ecm
        },
        b2: args.b2,
        b2_min: args.b2_min,
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
