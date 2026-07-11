# Security Policy

## Supported Versions

ChronoStore is currently pre-1.0. Security fixes are applied to the latest
revision of the `main` branch; older commits and persistent-format versions do
not receive backports.

## Reporting A Vulnerability

Do not open a public issue for a suspected vulnerability. Use the repository's
[private security advisory form](https://github.com/peprick/chronostore/security/advisories/new)
and include:

- the affected commit or version;
- operating system and compiler details;
- a minimal reproduction or malformed input sample;
- expected impact and required filesystem access;
- whether the issue affects confidentiality, integrity, availability, or
  durability.

Please remove credentials and private telemetry. Acknowledgement and disclosure
timing will depend on severity and reproducibility.

## Security Boundaries

CRC32C detects accidental corruption; it is not authentication. ChronoStore
does not encrypt data, authenticate users, sandbox file access, or defend a
database directory from a malicious process with write permission. Applications
embedding ChronoStore remain responsible for access control, secret handling,
backups, and operating-system hardening.
