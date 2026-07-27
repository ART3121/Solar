# Solar user manual

Solar is a lightweight open hardware design platform and EDA workflow
orchestrator for Linux. This manual describes the stable 0.5.0 command surface
and project manifest format 2.

## Start here

1. [Install Solar and external tools](installation.md).
2. [Build your first Verilog project](getting-started.md).
3. Use the [command reference](command-reference.md).
4. Consult [troubleshooting](troubleshooting.md) when a flow fails.

Brazilian Portuguese readers can begin with
[Primeiros passos com o Solar](pt-BR/getting-started.md).

## Project configuration

- [Project format and supported TOML subset](project-format.md)
- [Profiles and effective configuration](profiles.md)
- [Format-1 to format-2 migration](migration-v1-to-v2.md)

## Backend and advanced flows

- [YANC/SAPHO projects](yanc.md)
- [Compiler backend contract](compiler-backends.md)
- [Backend lifecycle](backend-contract.md)

## Contributor documentation

- [Architecture](architecture.md)
- [Testing](testing.md)
- [Roadmap](roadmap.md)
- [Release process](release-process.md)
- [Solar 0.4.5 public release audit](release-audit-0.4.5.md)
- [Solar 0.5.0 release notes](release-notes-0.5.0.md)

The Pages site follows the latest stable branch. Every release archive contains
the documentation that matches its binary.
