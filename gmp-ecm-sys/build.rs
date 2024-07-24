// This code is partially taken from: <https://gitlab.com/tspiteri/gmp-mpfr-sys/-/blob/master/build.rs>
// A big thank to the gmp-mpfr-sys project for the inspiration.
// TODO: A lot of work is still needed to make this code clean and efficient.

// Copyright © 2017–2024 Trevor Spiteri

// Copying and distribution of this file, with or without
// modification, are permitted in any medium without royalty provided
// the copyright notice and this notice are preserved. This file is
// offered as-is, without any warranty.

// Notes:
//
//  1. Configure GMP with --enable-fat so that built file is portable.
//
//  2. Configure MPFR with --enable-thread-safe --disable-decimal-float --disable-float128.
//
//  3. Configure GMP, MPFR and MPC with: --disable-shared --with-pic
//
//  4. Add symlinks to work around relative path issues in MPFR and MPC.
//     In MPFR: ln -s ../ecm-build
//     In MPC: ln -s ../mpfr-src ../mpfr-build ../ecm-build .
//
//  5. Unset CC and CFLAGS before building MPFR, so that both MPFR and MPC are
//     left to their default behavior and obtain them from ecm.h.
//
//  6. Use relative paths for configure otherwise msys/mingw might be
//     confused with drives and such.
#![cfg_attr(feature = "fail-on-warnings", deny(warnings))]
#![allow(unused_variables)]
#![allow(unused_assignments)]
#![allow(dead_code)]

use std::cmp::Ordering;
use std::env;
use std::ffi::{OsStr, OsString};
use std::fs;
use std::fs::File;
use std::io::{BufRead, BufReader, BufWriter, Result as IoResult, Write};
#[cfg(unix)]
use std::os::unix::fs as unix_fs;
#[cfg(windows)]
use std::os::windows::fs as windows_fs;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::str;

const ECM_DIR: &str = "ecm-7.0.6-c";
const ECM_VER: (i32, i32, i32) = (7, 0, 6);

#[derive(Clone, Copy, PartialEq)]
enum Target {
    Mingw,
    Msvc,
    Other,
}

struct Environment {
    rustc: OsString,
    c_compiler: OsString,
    target: Target,
    cross_target: Option<String>,
    c_no_tests: bool,
    src_dir: PathBuf,
    out_dir: PathBuf,
    lib_dir: PathBuf,
    include_dir: PathBuf,
    build_dir: PathBuf,
    cache_dir: Option<PathBuf>,
    jobs: OsString,
    version_prefix: String,
    version_patch: Option<u64>,
    use_system_libs: bool,
}

fn main() {
    let rustc = cargo_env("RUSTC");

    let cc = env::var_os("CC");
    let cc_cache_dir = cc.as_ref().map(|cc| {
        let mut dir = OsString::from("CC-");
        dir.push(cc);
        dir
    });
    let c_compiler = cc.unwrap_or_else(|| "gcc".into());

    let cflags = env::var_os("CFLAGS");
    let cflags_cache_dir = cflags.as_ref().map(|cflags| {
        #[allow(deprecated)]
        {
            use std::hash::{Hash, Hasher, SipHasher};
            let mut hasher = SipHasher::new();
            cflags.hash(&mut hasher);
            let hash = hasher.finish();
            OsString::from(format!("CFLAGS-{hash:016X}"))
        }
    });

    let host = cargo_env("HOST")
        .into_string()
        .expect("env var HOST having sensible characters");
    let raw_target = cargo_env("TARGET")
        .into_string()
        .expect("env var TARGET having sensible characters");
    let force_cross = there_is_env("CARGO_FEATURE_FORCE_CROSS");
    if !force_cross && !compilation_target_allowed(&host, &raw_target) {
        panic!(
            "Cross compilation from {host} to {raw_target} not supported! \
             Use the `force-cross` feature to cross compile anyway."
        );
    }

    let target = if raw_target.contains("-windows-msvc") {
        Target::Msvc
    } else if raw_target.contains("-windows-gnu") {
        Target::Mingw
    } else {
        Target::Other
    };
    let cross_target = if host == raw_target {
        None
    } else {
        Some(raw_target)
    };

    let c_no_tests = there_is_env("CARGO_FEATURE_C_NO_TESTS");

    let src_dir = PathBuf::from(cargo_env("CARGO_MANIFEST_DIR"));
    let out_dir = PathBuf::from(cargo_env("OUT_DIR"));

    let (version_prefix, version_patch) = get_version();

    println!("cargo:rerun-if-env-changed=GMP_ECM_SYS_CACHE");
    let cache_dir = match env::var_os("GMP_ECM_SYS_CACHE") {
        Some(ref c) if c.is_empty() || c == "_" => None,
        Some(c) => Some(PathBuf::from(c)),
        None => system_cache_dir().map(|c| c.join("gmp-ecm-sys")),
    };
    let cache_target = cross_target.as_ref().unwrap_or(&host);
    let cache_dir = cache_dir
        .map(|cache| cache.join(&version_prefix))
        .map(|cache| cache.join(cache_target))
        .map(|cache| match cc_cache_dir {
            Some(dir) => cache.join(dir),
            None => cache,
        })
        .map(|cache| match cflags_cache_dir {
            Some(dir) => cache.join(dir),
            None => cache,
        });

    let use_system_libs = there_is_env("CARGO_FEATURE_USE_SYSTEM_LIBS");
    if use_system_libs {
        match target {
            Target::Msvc => panic!("the use-system-libs feature is not supported on this target"),
            Target::Mingw => mingw_pkg_config_libdir_or_panic(),
            _ => {}
        }
    }
    let env = Environment {
        rustc,
        c_compiler,
        target,
        cross_target,
        c_no_tests,
        src_dir,
        out_dir: out_dir.clone(),
        lib_dir: out_dir.join("lib"),
        include_dir: out_dir.join("include"),
        build_dir: out_dir.join("build"),
        cache_dir,
        jobs: cargo_env("NUM_JOBS"),
        version_prefix,
        version_patch,
        use_system_libs,
    };

    // make sure we have target directories
    create_dir_or_panic(&env.lib_dir);
    create_dir_or_panic(&env.include_dir);

    if env.use_system_libs {
        check_system_libs(&env);
    } else {
        compile_libs(&env);
    }

    // println!("cargo:rustc-link-lib=gmp");
}

fn check_system_libs(env: &Environment) {
    let build_dir_existed = env.build_dir.exists();
    let try_dir = env.build_dir.join("system_libs");
    remove_dir_or_panic(&try_dir);
    create_dir_or_panic(&try_dir);
    println!("$ cd {try_dir:?}");

    println!("$ #Check for system GMP");
    create_file_or_panic(&try_dir.join("system_ecm.c"), SYSTEM_ECM_C);

    let mut cmd = Command::new(&env.c_compiler);
    cmd.current_dir(&try_dir)
        .args(["-fPIC", "system_ecm.c", "-lecm", "-o", "system_ecm.exe"]);
    execute(cmd);

    cmd = Command::new(try_dir.join("system_ecm.exe"));
    cmd.current_dir(&try_dir);
    execute(cmd);
    process_ecm_header(
        env,
        &try_dir.join("system_ecm.out"),
        Some(&env.out_dir.join("ecm_h.rs")),
    )
    .unwrap_or_else(|e| panic!("{}", e));

    if !there_is_env("CARGO_FEATURE_C_NO_DELETE") {
        if build_dir_existed {
            let _ = remove_dir(&try_dir);
        } else {
            remove_dir_or_panic(&env.build_dir);
        }
    }

    write_link_info(env);
}

fn compile_libs(env: &Environment) {
    let ecm_ah = (env.lib_dir.join("libecm.a"), env.include_dir.join("ecm.h"));

    if need_compile(env, &ecm_ah) {
        check_for_msvc(env);
        remove_dir_or_panic(&env.build_dir);
        create_dir_or_panic(&env.build_dir);

        let src_dst = env.build_dir.join("ecm-src");
        copy_dir(&env.src_dir.join(ECM_DIR), &src_dst);

        let mut cmd = Command::new("autoreconf");
        cmd.current_dir(&src_dst).args(["-i"]);
        execute(cmd);

        let (ref a, ref h) = ecm_ah;
        build_ecm(env, a, h);

        if !there_is_env("CARGO_FEATURE_C_NO_DELETE") {
            remove_dir_or_panic(&env.build_dir);
        }
        if save_cache(env, &ecm_ah) {
            clear_cache_redundancies(env);
        }
    }
    process_ecm_header(env, &ecm_ah.1, Some(&env.out_dir.join("ecm_h.rs")))
        .unwrap_or_else(|e| panic!("{}", e));
    write_link_info(env);
}

fn get_version() -> (String, Option<u64>) {
    let version = cargo_env("CARGO_PKG_VERSION")
        .into_string()
        .unwrap_or_else(|e| panic!("version not in utf-8: {e:?}"));
    let last_dot = version
        .rfind('.')
        .unwrap_or_else(|| panic!("version has no dots: {version}"));
    if last_dot == 0 {
        panic!("version starts with dot: {version}");
    }
    match version[last_dot + 1..].parse::<u64>() {
        Ok(patch) => {
            let mut v = version;
            v.truncate(last_dot);
            (v, Some(patch))
        }
        Err(_) => (version, None),
    }
}

fn need_compile(env: &Environment, ecm_ah: &(PathBuf, PathBuf)) -> bool {
    let ecm_fine = ecm_ah.0.is_file() && ecm_ah.1.is_file();
    if ecm_fine {
        if should_save_cache(env) && save_cache(env, ecm_ah) {
            clear_cache_redundancies(env);
        }
        return false;
    } else if load_cache(env, ecm_ah) {
        // if loading cache works, we're done
        return false;
    }
    !ecm_fine
}

fn save_cache(env: &Environment, ecm_ah: &(PathBuf, PathBuf)) -> bool {
    let cache_dir = match env.cache_dir {
        Some(ref s) => s,
        None => return false,
    };
    let version_dir = match env.version_patch {
        None => cache_dir.join(&env.version_prefix),
        Some(patch) => cache_dir.join(format!("{}.{}", env.version_prefix, patch)),
    };
    let mut ok = create_dir(&version_dir).is_ok();
    let dir = if env.c_no_tests {
        let no_tests_dir = version_dir.join("c-no-tests");
        ok = ok && create_dir(&no_tests_dir).is_ok();
        no_tests_dir
    } else {
        version_dir
    };
    let (ref a, ref h) = *ecm_ah;
    ok = ok && copy_file(a, &dir.join("libecm.a")).is_ok();
    ok = ok && copy_file(h, &dir.join("ecm.h")).is_ok();
    ok
}

fn clear_cache_redundancies(env: &Environment) {
    let Some(cache_dir) = &env.cache_dir else {
        return;
    };
    let cache_dirs = cache_directories(env, cache_dir)
        .into_iter()
        .rev()
        .filter(|x| match env.version_patch {
            None => x.1.is_none(),
            Some(patch) => x.1.map(|p| p <= patch).unwrap_or(false),
        });
    for (version_dir, version_patch) in cache_dirs {
        let no_tests_dir = version_dir.join("c-no-tests");

        // do not clear newly saved cache
        if version_patch == env.version_patch {
            // but if we tested and c-no-tests directory doesn't have more libs, remove it
            if !env.c_no_tests {
                let _ = remove_dir(&no_tests_dir);
            }

            continue;
        }

        // Do not clear cache with more libraries than newly saved cache.

        // // First check c-no-tests subdirectory for more libs.
        // if (!mpc && no_tests_dir.join("libmpc.a").is_file())
        //     || (!mpfr && no_tests_dir.join("libmpfr.a").is_file())
        // {
        //     continue;
        // }
        // Remove c-no-tests subdirectory as it does not have more libs.
        let _ = remove_dir(&no_tests_dir);

        if env.c_no_tests && !version_dir.join("libecm.a").is_file() {
            // We did not test, so version_dir must not contain any libs at all.
            let _ = remove_dir(&version_dir);
        }
    }
}

fn cache_directories(env: &Environment, base: &Path) -> Vec<(PathBuf, Option<u64>)> {
    let Ok(dir) = fs::read_dir(base) else {
        return Vec::new();
    };
    let mut vec = Vec::new();
    for entry in dir {
        let Ok(e) = entry else { continue };
        let path = e.path();
        if !path.is_dir() {
            continue;
        }
        let patch = {
            let Some(file_name) = path.file_name() else {
                continue;
            };
            let Some(path_str) = file_name.to_str() else {
                continue;
            };
            if path_str == env.version_prefix {
                None
            } else if !path_str.starts_with(&env.version_prefix)
                || !path_str[env.version_prefix.len()..].starts_with('.')
            {
                continue;
            } else {
                match path_str[env.version_prefix.len() + 1..].parse::<u64>() {
                    Ok(patch) => Some(patch),
                    Err(_) => continue,
                }
            }
        };
        vec.push((path, patch));
    }
    vec.sort_by_key(|k| k.1);
    vec
}

fn load_cache(env: &Environment, ecm_ah: &(PathBuf, PathBuf)) -> bool {
    let Some(cache_dir) = &env.cache_dir else {
        return false;
    };
    let env_version_patch = env.version_patch;
    let cache_dirs = cache_directories(env, cache_dir)
        .into_iter()
        .rev()
        .filter(|x| match env_version_patch {
            None => x.1.is_none(),
            Some(patch) => x.1.map(|p| p >= patch).unwrap_or(false),
        })
        .collect::<Vec<_>>();
    let suffixes: &[Option<&str>] = if env.c_no_tests {
        &[None, Some("c-no-tests")]
    } else {
        // we need tests, so do not try to load from c-no-tests
        &[None]
    };
    for suffix in suffixes {
        for (version_dir, _) in &cache_dirs {
            let joined;
            let dir = if let Some(suffix) = suffix {
                joined = version_dir.join(suffix);
                &joined
            } else {
                version_dir
            };
            let mut ok = true;
            let (ref a, ref h) = *ecm_ah;
            ok = ok && copy_file(&dir.join("libecm.a"), a).is_ok();
            let header = dir.join("ecm.h");
            ok = ok && process_ecm_header(env, &header, None).is_ok();
            ok = ok && copy_file(&header, h).is_ok();

            if ok {
                return true;
            }
        }
    }
    false
}

fn should_save_cache(env: &Environment) -> bool {
    let Some(cache_dir) = &env.cache_dir else {
        return false;
    };
    let cache_dirs = cache_directories(env, cache_dir)
        .into_iter()
        .rev()
        .filter(|x| match env.version_patch {
            None => x.1.is_none(),
            Some(patch) => x.1.map(|p| p >= patch).unwrap_or(false),
        })
        .collect::<Vec<_>>();
    let suffixes: &[Option<&str>] = if env.c_no_tests {
        &[None, Some("c-no-tests")]
    } else {
        // we need tests, so do not try to load from c-no-tests
        &[None]
    };
    for suffix in suffixes {
        for (version_dir, _) in &cache_dirs {
            let joined;
            let dir = if let Some(suffix) = suffix {
                joined = version_dir.join(suffix);
                &joined
            } else {
                version_dir
            };
            let mut ok = true;
            ok = ok && dir.join("libecm.a").is_file();
            ok = ok && dir.join("ecm.h").is_file();
            if ok {
                return false;
            }
        }
    }
    true
}

fn get_actual_cross_target(cross_target: &str) -> &str {
    match cross_target {
        "x86_64-pc-windows-gnu" => "x86_64-w64-mingw32",
        _ => cross_target,
    }
}

fn build_ecm(env: &Environment, lib: &Path, header: &Path) {
    let build_dir = env.build_dir.join("ecm-build");
    create_dir_or_panic(&build_dir);
    println!("$ cd {build_dir:?}");
    let mut conf = String::from("../ecm-src/configure --enable-fat --disable-shared --with-pic");
    if let Some(cross_target) = env.cross_target.as_ref() {
        conf.push_str(" --host ");
        conf.push_str(get_actual_cross_target(cross_target));
    }

    configure(&build_dir, &OsString::from(conf));
    make_and_check(env, &build_dir);
    let build_lib = build_dir.join(".libs").join("libecm.a");
    copy_file_or_panic(&build_lib, lib);
    let build_header = build_dir.join("ecm.h");
    copy_file_or_panic(&build_header, header);
}

fn compatible_version(
    env: &Environment,
    major: i32,
    minor: i32,
    patchlevel: i32,
    expected: (i32, i32, i32),
) -> bool {
    (major == expected.0 && minor >= expected.1)
        && (minor > expected.1 || patchlevel >= expected.2 || env.use_system_libs)
}

fn process_ecm_header(
    env: &Environment,
    header: &Path,
    out_file: Option<&Path>,
) -> Result<(), String> {
    let mut major = None;
    let mut minor = None;
    let mut patchlevel = None;
    let mut limb_bits = None;
    let mut nail_bits = None;
    let mut long_long_limb = None;
    let mut cc = None;
    let mut cflags = None;
    let mut reader = open(header);
    let mut buf = String::new();
    while read_line(&mut reader, &mut buf, header) > 0 {
        let s = "#define ECM_VERSION ";
        if let Some(start) = buf.find(s) {
            let version = buf[(start + s.len())..].trim();
            let version = version[1..version.len() - 1].split('.').collect::<Vec<_>>();
            major = version.first().and_then(|v| v.parse::<i32>().ok());
            minor = version.get(1).and_then(|v| v.parse::<i32>().ok());
            patchlevel = version.get(2).and_then(|v| v.parse::<i32>().ok());
        }

        let s = "#define __GNU_MP_VERSION ";
        if let Some(start) = buf.find(s) {
            major = buf[(start + s.len())..].trim().parse::<i32>().ok();
        }
        let s = "#define __GNU_MP_VERSION_MINOR ";
        if let Some(start) = buf.find(s) {
            minor = buf[(start + s.len())..].trim().parse::<i32>().ok();
        }
        let s = "#define __GNU_MP_VERSION_PATCHLEVEL ";
        if let Some(start) = buf.find(s) {
            patchlevel = buf[(start + s.len())..].trim().parse::<i32>().ok();
        }
        if buf.contains("#undef _LONG_LONG_LIMB") {
            long_long_limb = Some(false);
        }
        if buf.contains("#define _LONG_LONG_LIMB 1") {
            long_long_limb = Some(true);
        }
        let s = "#define ECM_LIMB_BITS ";
        if let Some(start) = buf.find(s) {
            limb_bits = buf[(start + s.len())..].trim().parse::<i32>().ok();
        }
        let s = "#define ECM_NAIL_BITS ";
        if let Some(start) = buf.find(s) {
            nail_bits = buf[(start + s.len())..].trim().parse::<i32>().ok();
        }
        let s = "#define __ECM_CC ";
        if let Some(start) = buf.find(s) {
            cc = Some(
                buf[(start + s.len())..]
                    .trim()
                    .trim_matches('"')
                    .to_string(),
            );
        }
        let s = "#define __ECM_CFLAGS ";
        if let Some(start) = buf.find(s) {
            cflags = Some(
                buf[(start + s.len())..]
                    .trim()
                    .trim_matches('"')
                    .to_string(),
            );
        }
        buf.clear();
    }
    drop(reader);

    let major = major.expect("Cannot determine __GNU_MP_VERSION");
    let minor = minor.expect("Cannot determine __GNU_MP_VERSION_MINOR");
    let patchlevel = patchlevel.expect("Cannot determine __GNU_MP_VERSION_PATCHLEVEL");
    if !compatible_version(env, major, minor, patchlevel, ECM_VER) {
        return Err(format!(
            "This version of gmp-ecm-sys supports ECM {}.{}.{}, but {}.{}.{} was found",
            ECM_VER.0, ECM_VER.1, ECM_VER.2, major, minor, patchlevel
        ));
    }

    // let limb_bits = limb_bits.expect("Cannot determine ECM_LIMB_BITS");
    // println!("cargo:limb_bits={limb_bits}");

    // println!("cargo:rustc-check-cfg=cfg(nails)");
    // let nail_bits = nail_bits.expect("Cannot determine ECM_NAIL_BITS");
    // if nail_bits > 0 {
    //     println!("cargo:rustc-cfg=nails");
    // }

    // println!("cargo:rustc-check-cfg=cfg(long_long_limb)");
    // let long_long_limb = long_long_limb.expect("Cannot determine _LONG_LONG_LIMB");
    // let long_long_limb = if long_long_limb {
    //     println!("cargo:rustc-cfg=long_long_limb");
    //     "libc::c_ulonglong"
    // } else {
    //     "c_ulong"
    // };
    // let cc = cc.expect("Cannot determine __ECM_CC");
    // let cflags = cflags.expect("Cannot determine __ECM_CFLAGS");

    let content = format!(
        concat!(
            "const ECM_VERSION: c_int = {};\n",
            "const ECM_VERSION_MINOR: c_int = {};\n",
            "const ECM_VERSION_PATCHLEVEL: c_int = {};\n",
            // "const ECM_LIMB_BITS: c_int = {};\n",
            // "const ECM_NAIL_BITS: c_int = {};\n",
            // "type ECM_LIMB_T = {};\n",
            // "const ECM_CC: *const c_char = b\"{}\\0\".as_ptr().cast();\n",
            // "const ECM_CFLAGS: *const c_char = b\"{}\\0\".as_ptr().cast();\n"
        ),
        major,
        minor,
        patchlevel //, limb_bits, nail_bits, long_long_limb, cc, cflags
    );
    if let Some(out_file) = out_file {
        let mut rs = create(out_file);
        write_flush(&mut rs, &content, out_file);
    }
    Ok(())
}

fn write_link_info(env: &Environment) {
    let out_str = env.out_dir.to_str().unwrap_or_else(|| {
        panic!(
            "Path contains unsupported characters, can only make {}",
            env.out_dir.display()
        )
    });
    let lib_str = env.lib_dir.to_str().unwrap_or_else(|| {
        panic!(
            "Path contains unsupported characters, can only make {}",
            env.lib_dir.display()
        )
    });
    let include_str = env.include_dir.to_str().unwrap_or_else(|| {
        panic!(
            "Path contains unsupported characters, can only make {}",
            env.include_dir.display()
        )
    });
    println!("cargo:out_dir={out_str}");
    println!("cargo:lib_dir={lib_str}");
    println!("cargo:include_dir={include_str}");
    println!("cargo:rustc-link-search=native={lib_str}");

    let target_env = env::var("CARGO_CFG_TARGET_ENV").unwrap_or_default();
    if target_env == "musl" && env.use_system_libs {
        println!("cargo:rustc-link-search=/usr/lib");
    }

    let target_features = env::var("CARGO_CFG_TARGET_FEATURE").unwrap_or_default();
    let using_static_musl = target_env == "musl" && target_features.contains("crt-static");
    let use_static = using_static_musl || !env.use_system_libs;
    let maybe_static = if use_static { "static=" } else { "" };
    println!("cargo:rustc-link-lib={maybe_static}ecm");
}

impl Environment {
    #[allow(dead_code)]
    fn check_feature(&self, name: &str, contents: &str, nightly_features: Option<&str>) {
        let try_dir = self.out_dir.join(format!("try_{name}"));
        let filename = format!("try_{name}.rs");
        create_dir_or_panic(&try_dir);
        println!("$ cd {try_dir:?}");

        enum Iteration {
            Stable,
            Unstable,
        }
        for i in &[Iteration::Stable, Iteration::Unstable] {
            let s;
            let file_contents = match *i {
                Iteration::Stable => contents,
                Iteration::Unstable => match nightly_features {
                    Some(features) => {
                        s = format!("#![feature({features})]\n{contents}");
                        &s
                    }
                    None => continue,
                },
            };
            create_file_or_panic(&try_dir.join(&filename), file_contents);
            let mut cmd = Command::new(&self.rustc);
            cmd.current_dir(&try_dir)
                .stdout(Stdio::null())
                .stderr(Stdio::null())
                .args([&*filename, "--emit=dep-info,metadata"]);
            println!("$ {cmd:?} >& /dev/null");
            let status = cmd
                .status()
                .unwrap_or_else(|_| panic!("Unable to execute: {cmd:?}"));
            println!("cargo:rustc-check-cfg=cfg({name})");
            println!("cargo:rustc-check-cfg=cfg(nightly_{name})");
            if status.success() {
                println!("cargo:rustc-cfg={name}");
                if let Iteration::Unstable = *i {
                    println!("cargo:rustc-cfg=nightly_{name}");
                }
                break;
            }
        }

        remove_dir_or_panic(&try_dir);
    }
}

fn cargo_env(name: &str) -> OsString {
    env::var_os(name)
        .unwrap_or_else(|| panic!("environment variable not found: {name}, please use cargo"))
}

fn there_is_env(name: &str) -> bool {
    env::var_os(name).is_some()
}

fn check_for_msvc(env: &Environment) {
    if env.target == Target::Msvc {
        panic!("Windows MSVC target is not supported (linking would fail)");
    }
}

#[allow(dead_code)]
fn rustc_later_eq(major: i32, minor: i32) -> bool {
    let rustc = cargo_env("RUSTC");
    let output = Command::new(rustc)
        .arg("--version")
        .output()
        .expect("unable to run rustc --version");
    let version = String::from_utf8(output.stdout).expect("unrecognized rustc version");
    if !version.starts_with("rustc ") {
        panic!("unrecognized rustc version: {version}");
    }
    let remain = &version[6..];
    let dot = remain.find('.').expect("unrecognized rustc version");
    let ver_major = remain[0..dot]
        .parse::<i32>()
        .expect("unrecognized rustc version");
    match ver_major.cmp(&major) {
        Ordering::Less => false,
        Ordering::Greater => true,
        Ordering::Equal => {
            let remain = &remain[dot + 1..];
            let dot = remain.find('.').expect("unrecognized rustc version");
            let ver_minor = remain[0..dot]
                .parse::<i32>()
                .expect("unrecognized rustc version");
            ver_minor >= minor
        }
    }
}

fn mingw_pkg_config_libdir_or_panic() {
    let mut cmd = Command::new("pkg-config");
    cmd.args(["--libs-only-L", "--keep-system-libs", "ecm"]);
    let output = execute_stdout(cmd);
    if output.len() < 2 || &output[0..2] != b"-L" {
        panic!("expected pkg-config output to begin with \"-L\"");
    }
    let libdir = str::from_utf8(&output[2..]).expect("output from pkg-config not utf-8");
    println!("cargo:rustc-link-search=native={libdir}");
}

fn remove_dir(dir: &Path) -> IoResult<()> {
    if !dir.exists() {
        return Ok(());
    }
    assert!(dir.is_dir(), "Not a directory: {dir:?}");
    println!("$ rm -r {dir:?}");
    fs::remove_dir_all(dir)
}

fn remove_dir_or_panic(dir: &Path) {
    remove_dir(dir).unwrap_or_else(|_| panic!("Unable to remove directory: {dir:?}"));
}

fn create_dir(dir: &Path) -> IoResult<()> {
    println!("$ mkdir -p {dir:?}");
    fs::create_dir_all(dir)
}

fn create_dir_or_panic(dir: &Path) {
    create_dir(dir).unwrap_or_else(|_| panic!("Unable to create directory: {dir:?}"));
}

fn create_file_or_panic(filename: &Path, contents: &str) {
    println!("$ printf '%s' {:?}... > {filename:?}", &contents[0..10]);
    let mut file =
        File::create(filename).unwrap_or_else(|_| panic!("Unable to create file: {filename:?}"));
    file.write_all(contents.as_bytes())
        .unwrap_or_else(|_| panic!("Unable to write to file: {filename:?}"));
}

fn copy_file(src: &Path, dst: &Path) -> IoResult<u64> {
    println!("$ cp {src:?} {dst:?}");
    fs::copy(src, dst)
}

fn copy_file_or_panic(src: &Path, dst: &Path) {
    copy_file(src, dst).unwrap_or_else(|_| {
        panic!("Unable to copy {src:?} -> {dst:?}");
    });
}

fn configure(build_dir: &Path, conf_line: &OsStr) {
    let mut conf = Command::new("sh");
    conf.current_dir(build_dir).arg("-c").arg(conf_line);
    execute(conf);
}

fn make_and_check(env: &Environment, build_dir: &Path) {
    let mut make = Command::new("make");
    make.current_dir(build_dir).arg("-j").arg(&env.jobs);
    execute(make);
    if !env.c_no_tests && env.cross_target.is_none() {
        let mut make_check = Command::new("make");
        make_check
            .current_dir(build_dir)
            .arg("-j")
            .arg(&env.jobs)
            .arg("check");
        execute(make_check);
    }
}

#[cfg(unix)]
fn link_dir(src: &Path, dst: &Path) {
    println!("$ ln -s {src:?} {dst:?}");
    unix_fs::symlink(src, dst).unwrap_or_else(|_| {
        panic!("Unable to symlink {src:?} -> {dst:?}");
    });
}

#[cfg(windows)]
fn link_dir(src: &Path, dst: &Path) {
    println!("$ ln -s {src:?} {dst:?}");
    if windows_fs::symlink_dir(src, dst).is_ok() {
        return;
    }
    println!("symlink_dir: failed to create symbolic link, copying instead");
    let mut c = Command::new("cp");
    c.arg("-R").arg(src).arg(dst);
    execute(c);
}

fn copy_dir(src: &Path, dst: &Path) {
    println!("$ cp -R {src:?} {dst:?}");
    let mut c = Command::new("cp");
    c.arg("-R").arg(src).arg(dst);
    execute(c);
}

fn mv(src: &str, dst_dir: &Path) {
    let mut c = Command::new("mv");
    c.arg(src).arg(".").current_dir(dst_dir);
    execute(c);
}

fn execute(mut command: Command) {
    println!("$ {command:?}");
    let status = command
        .status()
        .unwrap_or_else(|_| panic!("Unable to execute: {command:?}"));
    if !status.success() {
        if let Some(code) = status.code() {
            panic!("Program failed with code {code}: {command:?}");
        } else {
            panic!("Program failed: {command:?}");
        }
    }
}

fn execute_stdout(mut command: Command) -> Vec<u8> {
    println!("$ {command:?}");
    let output = command
        .output()
        .unwrap_or_else(|_| panic!("Unable to execute: {command:?}"));
    if !output.status.success() {
        if let Some(code) = output.status.code() {
            panic!("Program failed with code {code}: {command:?}");
        } else {
            panic!("Program failed: {command:?}");
        }
    }
    output.stdout
}

fn open(name: &Path) -> BufReader<File> {
    let file = File::open(name).unwrap_or_else(|_| panic!("Cannot open file: {name:?}"));
    BufReader::new(file)
}

fn create(name: &Path) -> BufWriter<File> {
    let file = File::create(name).unwrap_or_else(|_| panic!("Cannot create file: {name:?}"));
    BufWriter::new(file)
}

fn read_line(reader: &mut BufReader<File>, buf: &mut String, name: &Path) -> usize {
    reader
        .read_line(buf)
        .unwrap_or_else(|_| panic!("Cannot read from: {name:?}"))
}

fn write_flush(writer: &mut BufWriter<File>, buf: &str, name: &Path) {
    writer
        .write_all(buf.as_bytes())
        .unwrap_or_else(|_| panic!("Cannot write to: {name:?}"));
    writer
        .flush()
        .unwrap_or_else(|_| panic!("Cannot write to: {name:?}"));
}

fn system_cache_dir() -> Option<PathBuf> {
    #[cfg(target_os = "windows")]
    {
        use core::mem::MaybeUninit;
        use core::slice;
        use std::os::windows::ffi::OsStringExt;
        use windows_sys::Win32::Foundation::S_OK;
        use windows_sys::Win32::Globalization;
        use windows_sys::Win32::System::Com;
        use windows_sys::Win32::UI::Shell::{self, FOLDERID_LocalAppData};

        unsafe {
            let mut path = MaybeUninit::uninit();
            if Shell::SHGetKnownFolderPath(&FOLDERID_LocalAppData, 0, 0, path.as_mut_ptr()) != S_OK
            {
                return None;
            }
            let path = path.assume_init();
            let slice = slice::from_raw_parts(path, Globalization::lstrlenW(path) as usize);
            let string = OsString::from_wide(slice);
            Com::CoTaskMemFree(path.cast());
            Some(string.into())
        }
    }
    #[cfg(any(target_os = "macos", target_os = "ios"))]
    {
        env::var_os("HOME")
            .filter(|x| !x.is_empty())
            .map(|x| AsRef::<Path>::as_ref(&x).join("Library").join("Caches"))
    }
    #[cfg(not(any(target_os = "windows", target_os = "macos", target_os = "ios")))]
    {
        env::var_os("XDG_CACHE_HOME")
            .filter(|x| !x.is_empty())
            .map(PathBuf::from)
            .or_else(|| {
                env::var_os("HOME")
                    .filter(|x| !x.is_empty())
                    .map(|x| AsRef::<Path>::as_ref(&x).join(".cache"))
            })
    }
}

fn compilation_target_allowed(host: &str, target: &str) -> bool {
    if host == target {
        return true;
    }

    // Allow cross-compilation from x86_64 to i686, as it is a simple
    // -m32 switch in GMP compilation; unless MinGW is in use, where
    // cross compilation from 64-bit to 32-bit has issues.
    let (machine_x86_64, machine_i686) = ("x86_64", "i686");
    if host.starts_with(machine_x86_64)
        && target.starts_with(machine_i686)
        && host[machine_x86_64.len()..] == target[machine_i686.len()..]
        && !target.contains("-windows-gnu")
    {
        return true;
    }

    false
}

// prints part of the header
const SYSTEM_ECM_C: &str = r##"/* system_ecm.c */
#include <ecm.h>
#include <stdio.h>

#define STRINGIFY(x) #x
#define DEFINE_STR(x) ("#define " #x " " STRINGIFY(x) "\n")

int main(void) {
    FILE *f = fopen("system_ecm.out", "w");

#ifdef _LONG_LONG_LIMB
    fputs(DEFINE_STR(_LONG_LONG_LIMB), f);
#else
    fputs("#undef _LONG_LONG_LIMB\n", f);
#endif

    fputs(DEFINE_STR(ECM_VERSION), f);
    // fputs(DEFINE_STR(__GNU_MP_VERSION), f);
    // fputs(DEFINE_STR(__GNU_MP_VERSION_MINOR), f);
    // fputs(DEFINE_STR(__GNU_MP_VERSION_PATCHLEVEL), f);
    // fputs(DEFINE_STR(ECM_LIMB_BITS), f);
    // fputs(DEFINE_STR(ECM_NAIL_BITS), f);
    // fputs(DEFINE_STR(__ECM_CC), f);
    // fputs(DEFINE_STR(__ECM_CFLAGS), f);

    fclose(f);

    return 0;
}
"##;
