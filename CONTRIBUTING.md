# Contributing

Keep changes small, buildable, and aligned with the product direction in `README.md` and the agent rules in `AGENTS.md`.

## Rules
- Preserve the five-project architecture.
- Keep native/media details out of `Presentation`.
- Keep managed native interop under `Infrastructure/Native`.
- Keep H.264 as the active product codec.
- Do not add camera recording, screenshot capture, watermarks, or artificial recording limits.
- Update nearby docs when behavior, ABI, architecture, or product direction changes.

## Validation
See `docs/DEVELOPMENT.md` for setup, build, run, and verification commands.

## Commit Style
Keep one concern per change. Conventional Commit prefixes such as `feat:`, `fix:`, `docs:`, `refactor:`, and `chore:` are preferred.
