---
title: Releases
description: How to obtain and install Endo.
---

# Releases

## Current Status

Endo is currently in active development and available as a **build-from-source** project.
Pre-built binaries and package manager installs are planned for a future stable release.

!!! info "Early Adopters Welcome"
    Endo is fully functional for daily use but the language and APIs are still evolving.
    Building from source ensures you get the latest features and fixes.

## Build from Source

The recommended way to obtain Endo today is to build from the Git repository:

```bash
git clone https://github.com/christianparpart/endo.git
cd endo
cmake --preset clang-release
cmake --build --preset clang-release
sudo cmake --install build/clang-release
```

See the [Getting Started](getting-started.md) guide for detailed prerequisites and build
instructions.

## GitHub Repository

The source code, issue tracker, and development activity are hosted on GitHub:

[**github.com/christianparpart/endo**](https://github.com/christianparpart/endo)

## Package Managers

!!! warning "Coming Soon"
    Package manager support is on the roadmap but not yet available.

Planned package manager support includes:

| Package Manager | Platform | Status |
|----------------|----------|--------|
| Homebrew | macOS, Linux | Planned |
| Scoop | Windows | Planned |
| AUR | Arch Linux | Planned |
| APT / PPA | Debian, Ubuntu | Planned |
| RPM / COPR | Fedora, RHEL | Planned |
| Nix | NixOS, any | Planned |

If you would like to help package Endo for your preferred distribution, contributions are
welcome. See [Contributing](contributing.md) for details.

## Versioning

Endo follows [Semantic Versioning](https://semver.org/). Until the 1.0 release, the API
and language syntax may change between minor versions. After 1.0, backward compatibility
will be maintained within major versions.
