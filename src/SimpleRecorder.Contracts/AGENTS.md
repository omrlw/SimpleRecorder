# Contracts module guide

## Own this layer
This project is the canonical home for:
- enums
- DTOs
- service interfaces
- stable public models shared across layers

## Hard boundaries
Do not add WinUI references.
Do not add Win32, COM, or native ABI details.
Do not put business logic here beyond small value semantics that belong to the models themselves.

## Design rules
- Name types clearly and conservatively.
- Prefer stable, explicit models over clever abstractions.
- Evolve interfaces with compatibility in mind because other layers depend on them as the contract source of truth.
