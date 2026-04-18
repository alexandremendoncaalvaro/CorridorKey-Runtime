# Disabled GitHub Actions workflows

These workflows are parked here on purpose. GitHub Actions only reads
`.github/workflows/`, so nothing in this directory fires.

## Why they are disabled

The hosted-runner matrix in `ci.yml` and `release.yml` was failing in
sequence without producing actionable signal. The repository has no
required status checks configured in branch protection, so the failures
did not block merges — they only added noise. Keeping the files here
preserves their history and makes reactivation a one-liner.

## How to reactivate

```sh
git mv .github/workflows.disabled/ci.yml .github/workflows/ci.yml
git mv .github/workflows.disabled/release.yml .github/workflows/release.yml
```

## When to reactivate

Before flipping them back on, confirm the underlying stability issues are
addressed:

- Build reproduces green locally on Windows, macOS and Linux for the full
  release preset.
- A self-hosted Windows runner is available for CUDA/TensorRT-bound jobs,
  or the Windows job has been narrowed to a scope that hosted runners can
  finish reliably.
- Required status checks are enabled on `main` in branch protection so a
  green run actually gates merge.
