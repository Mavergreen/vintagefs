# Build ingredients

Everything baked into the shipped plug-in, and how a change to it reaches a release. This repo is its
own upstream: the plug-in and libvimg are Mavergreen code, versioned `YYYYMMDD.N`.

| Ingredient | Pinned in | On a bump |
|---|---|---|
| clang22, the compiler and linker | `components/clang22/version`, `components/clang22/linux-sha256` | ✅ Renovate (`github-releases`); the PR says how to move `linux-sha256`. A new release |
| MacOSX10.9 SDK, stevedore, packaging helpers | `Mavergreen/shipyard@v1` | ✅ Renovate's github-actions manager; `@v1` moves without a pin change |

No upstream release notes: vintagefs is its own upstream.

Apple's clang (`/usr/bin/clang`) builds the host unit tests only when a developer asks for sanitizers on a Mac
(`VF_SANITIZE=1 sh tests/c/run.sh`): clang22 ships no ASan or UBSan runtime for any host. Nothing it builds is
shipped, and CI does not use it. It goes away when clang22 carries compiler-rt's sanitizer runtimes.

## Declared state

- upstream: UPSTREAM_VERSION
- clang22: components/clang22/version

## Conformance deviations

- scheme: vintagefs is its own upstream (no upstream release to repackage), so it versions itself by date as YYYYMMDD.N with no -mavericks.N axis
