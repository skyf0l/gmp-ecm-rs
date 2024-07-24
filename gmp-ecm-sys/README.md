# GMP-ECM-sys

[![Crate.io](https://img.shields.io/crates/v/gmp-ecm-sys.svg)](https://crates.io/crates/gmp-ecm-sys)

Rust low-level bindings for [GMP-ECM](https://gitlab.inria.fr/zimmerma/ecm).

## Caching the built C libraries

Building the C libraries can take some time. In order to save
compilation time, the built libraries are cached in the user’s cache
directory as follows:

- on GNU/Linux: inside `$XDG_CACHE_HOME/gmp-ecm-sys` or
  `$HOME/.cache/gmp-ecm-sys`
- on macOS: inside `$HOME/Library/Caches/gmp-ecm-sys`
- on Windows: inside `{FOLDERID_LocalAppData}\gmp-ecm-sys`

To use a different directory, you can set the environment variable
`GMP_ECM_SYS_CACHE` to the desired cache directory. Setting the
`GMP_ECM_SYS_CACHE` variable to an empty string or to a single
underscore (`"_"`) will disable caching.

## License

The `gmp-ecm-sys` crate is free software: you can redistribute it
and/or modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation, either version 3
of the License, or (at your option) any later version. See the full
text of the [GNU LGPL](https://www.gnu.org/licenses/lgpl-3.0.en.html)
and [GNU GPL](https://www.gnu.org/licenses/gpl-3.0.html) for details.
