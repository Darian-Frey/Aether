# Bugs

Catalogue of bugs discovered during development. Per Maintenance Rule 8, bugs are logged here when found, not silently fixed. The author decides whether to fix immediately, defer, or leave alone.

This is the dual of [ATTACK_VECTORS.md](ATTACK_VECTORS.md): attack vectors are anticipated failure modes with detection methods; bugs are realised failures with status flags. A recurring bug pattern may warrant a new AV entry; an AV entry that escaped detection becomes a bug.

Status vocabulary: open | fixed | wontfix | deferred.
Severity vocabulary: low | medium | high.

Entry format:

```markdown
### BUG-001: {short title}
**Status:** open
**Found:** YYYY-MM-DD ({session/commit context})
**Location:** {path/to/file.ext:line, or "cross-cutting"}
**Severity:** {low | medium | high}
**Description.** {What's wrong and why it matters.}
**Reproduction.** {Minimum steps to trigger.}
**Notes.** {Related context, suggested fix, links to BUG/IMP/D/AV entries.}
```

## Open

*None. No code exists yet.*

## Fixed

*None.*

## Won't Fix

*None.*

## Deferred

*None.*
