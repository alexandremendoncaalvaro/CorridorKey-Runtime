# Security Policy

## Reporting a Vulnerability

If you discover a security vulnerability in CorridorKey Runtime, please report
it responsibly. **Do not open a public GitHub issue.**

### How to Report

Send an email to **alexandre.alvaro@gmail.com** with:

1. A description of the vulnerability
2. Steps to reproduce the issue
3. The potential impact
4. Any suggested fix (optional)

### What to Expect

- **Acknowledgement** within 72 hours of your report
- **Status update** within 14 days with an assessment and remediation plan
- **Credit** in the fix commit and release notes (unless you prefer anonymity)

### Scope

This policy covers the CorridorKey Runtime codebase, including:

- The inference engine and all execution providers
- File I/O modules (EXR, PNG, video)
- CLI and GUI applications
- OFX plugin integration
- Build system and dependency configuration

### Out of Scope

- Vulnerabilities in upstream dependencies (ONNX Runtime, FFmpeg, etc.) should
  be reported to those projects directly
- Issues in third-party model files

## Supported Versions

Security fixes are applied to the latest release only. We do not maintain
long-term support branches.
