# Changelog

Format follows [Keep a Changelog](https://keepachangelog.com). Entries reference stable IDs (`F-`, `D-`, `AV-`, `BUG-`, `IMP-`) where applicable.

## [Unreleased]

### Added
- Documentation scaffold: README, FEATURES, ROADMAP, ARCHITECTURE, DECISIONS, SPEC, ATTACK_VECTORS, BUGS, IMPROVEMENTS, CHANGELOG, CLAUDE (2026-08-30).
- Feature register F-001 … F-022 covering engine, rule authoring, initial state, dynamics, presentation, and session handling.
- Decision register D-001 … D-011 covering execution backend, rule IR, Lua scoping, backend selection, mutation model, reproducibility, technology stack, grid representation, project name, continuous-state provision, and the CPU reference oracle.
- Attack vector register AV-001 … AV-015 across resource limits, correctness, rule authoring, evolutionary dynamics, and numerical stability.
- Technical specification covering the cell and grid model, neighbourhoods, rule IR schema and validation, lookup-table layout and backend threshold, GLSL codegen contract, DSL grammar, Lua sandbox and budget, mutation semantics, RNG streams, session format, performance budgets, and rendering.
- `rule/ir`: the RuleIR type per SPEC §4 with Table, Expression and Kernel forms, full validation, a stable 64-bit hash, and enum name conversion (2026-09-11).
- `rule/neighbourhood`: canonical offset enumeration and counts per SPEC §3 (2026-09-11).
- `rule/table_layout`: exact table sizes and index arithmetic per SPEC §5, with count-vector ranking for multi-state outer-totalistic rules (2026-09-11).
- `rule/dsl`: parser for B/S, B/S/C and count-condition table blocks, emitting a Table or an Expression by the §5 threshold (2026-09-11).
- Catch2 test suite (`tests/`) wired into CTest; 42 cases covering the above (2026-09-11).
- CMake build fetching raylib 6.0 (4.3 backend), Dear ImGui 1.92.7 and rlImGui in-tree; `src/main.cpp` opens a window and verifies a compute dispatch, with `--gl-check` for a headless pass/fail (2026-09-11).
- `BUILD.md` with prerequisites, dependency pins and PRIME offload instructions for the NVIDIA GPU (2026-09-11).
- `LICENSE`: Apache-2.0 (2026-09-11).
- Source tree per README §Project structure, empty apart from `.gitkeep` placeholders, and a `.gitignore` (2026-09-11).

### Fixed
- BUG-001: SPEC §3 closed form for the 3D von Neumann count (2026-09-11).
- BUG-002: SPEC §5 multi-state outer-totalistic index encoding contradicted its size formula; resolved as dense lexicographic ranking (2026-09-11).

### Changed
- SPEC §7 gains notes on `and`/`or` precedence, `n(0)`, comments, and the unspecified `signature_literal` (2026-09-11).
- D-009 project name moved from Proposed to Accepted on author confirmation; GitHub repository created at `Darian-Frey/Aether` (2026-09-11).

### Notes
- Phase 0 complete. No engine code exists; `src/main.cpp` is a probe to be replaced by Phase 1.
- The project name is confirmed (D-009).
