# ADR-0007: Add Adobe Host Plugins

**Status:** accepted
**Date:** 2026-05-22
**Deciders:** Runtime maintainers

## Context

CorridorKey needs native plugin surfaces for Adobe After Effects and Adobe
Premiere. The current architecture treats user-facing integrations as thin
Interface-layer clients over the App and Core contracts, with the existing OFX
plugin serving OpenFX hosts through `src/plugins/ofx`.

Adding Adobe host support expands the product surface beyond the validated OFX
hosts. The decision needs to preserve the existing Library First and Interface
Segregation rules so Adobe-specific work does not fork inference, model
selection, diagnostics, or runtime policy away from the shared runtime.

## Decision

We will add first-class Adobe host plugins for After Effects and Premiere as
Interface-layer adapters over the existing App and Core runtime contracts.

The Adobe plugins will keep host-specific entrypoints, parameter mapping,
timeline/frame exchange, logging, and packaging concerns in their plugin layer.
Shared CorridorKey behavior remains in App/Core contracts and reusable common
infrastructure rather than being duplicated inside either Adobe host plugin.

## Consequences

After Effects and Premiere users get native host integrations instead of relying
on non-Adobe entrypoints.

The Adobe plugins can reuse the same runtime behavior, model policy,
diagnostics, and performance constraints that already bind the CLI, OFX plugin,
and GUI surfaces.

The Interface layer grows to include Adobe host-specific code, which requires an
architecture update when the implementation adds new directories or public
build targets.

Testing, packaging, installer validation, and release certification expand to
cover two additional host integrations.

Any render-hot-path work shared with OFX remains subject to the existing
benchmark gate for latency and runtime regressions.

## Alternatives Considered

* Keep only the current OFX, CLI, and GUI surfaces - rejected because the stated
  product need is native plugins for After Effects and Premiere.
* Put CorridorKey runtime behavior directly inside each Adobe plugin - rejected
  because it would fork shared App/Core behavior and violate the Library First
  architecture.
* Route Adobe users through the existing OFX plugin - rejected because the
  requirement is Adobe host plugins, not another OpenFX host path.
