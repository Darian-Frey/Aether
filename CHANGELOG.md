# Changelog

Format follows [Keep a Changelog](https://keepachangelog.com). Entries reference stable IDs (`F-`, `D-`, `AV-`, `BUG-`, `IMP-`) where applicable.

## [Unreleased]

### Added
- Documentation scaffold: README, FEATURES, ROADMAP, ARCHITECTURE, DECISIONS, SPEC, ATTACK_VECTORS, BUGS, IMPROVEMENTS, CHANGELOG, CLAUDE (2026-08-30).
- Feature register F-001 … F-022 covering engine, rule authoring, initial state, dynamics, presentation, and session handling.
- Decision register D-001 … D-011 covering execution backend, rule IR, Lua scoping, backend selection, mutation model, reproducibility, technology stack, grid representation, project name, continuous-state provision, and the CPU reference oracle.
- Attack vector register AV-001 … AV-015 across resource limits, correctness, rule authoring, evolutionary dynamics, and numerical stability.
- Technical specification covering the cell and grid model, neighbourhoods, rule IR schema and validation, lookup-table layout and backend threshold, GLSL codegen contract, DSL grammar, Lua sandbox and budget, mutation semantics, RNG streams, session format, performance budgets, and rendering.

- `LICENSE`: Apache-2.0 (2026-09-11).
- Source tree per README §Project structure, empty apart from `.gitkeep` placeholders, and a `.gitignore` (2026-09-11).

### Changed
- D-009 project name moved from Proposed to Accepted on author confirmation; GitHub repository created at `Darian-Frey/Aether` (2026-09-11).

### Notes
- No code exists. `BUILD.md` is deferred until the first successful build per the standard's creation order.
- The project name is confirmed (D-009).
