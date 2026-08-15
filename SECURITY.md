# Security

## Reporting

Report vulnerabilities privately via GitHub's **Report a vulnerability**
(Security tab) on this repository. You should hear back within a week.
Please do not open public issues for security reports.

## Supported versions

Only the latest 0.x release receives fixes.

## What the threat model already admits

moth's push channel is documented honestly, limitations included:

- **Serial pushes are unauthenticated by design** — holding the cable is
  the pairing (docs/DECISIONS.md, ADR-010).
- **WiFi pushes are HMAC-paired** (PBKDF2-derived key, nonce-carrying
  frames). Two known limitations are accepted for v0.1 and recorded in
  ADR-010: a captured valid push can be **replayed** while the same pairing
  phrase is in use, and the receiver allocates for a frame **before**
  authenticating it, so a LAN peer can force transient allocations. Both
  close via challenge-response in a later release.

A report that one of these documented limitations exists is expected; a
report of a way around the authentication itself is exactly what we want to
hear about.
