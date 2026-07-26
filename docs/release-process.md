# Release process

Solar releases are built from an existing protected semantic-version tag. The
workflow publishes only after its clean build, tests, packaging, ABI ceiling,
installer rollback, quick-start, and uninstall gates pass.

## Maintainer checklist

1. Confirm `main` is green and the release audit has no blocker.
2. Match the version in CMake, `SOLAR_VERSION`, changelog, migration notes, and
   installer.
3. Run GCC, Clang, sanitizer, real available-tool, package, and installation
   matrices. Record skips separately from passes.
4. Create and push an annotated tag such as `v0.4.6`.
5. Let the release workflow rebuild from that tag and publish a release containing
   `solar-linux-x86_64.tar.gz`, `install.sh`, and `SHA256SUMS`.
6. Download the published assets, verify checksums, install into a clean prefix,
   exercise the quick start, and uninstall.
7. Never replace an asset after publication; corrections receive a new patch
   version.

## Remote repository settings

For `ART3121/solar`, maintainers enable:

- public visibility, protected `main`, required CI, and protected `v*` tags;
- GitHub Pages from `/docs` on `main`;
- Discussions with Announcements, Q&A, Ideas, and Show and tell;
- private vulnerability reporting;
- release immutability.

These are explicit administrative actions. A local build or tag does not prove
that any remote setting or release was created.
