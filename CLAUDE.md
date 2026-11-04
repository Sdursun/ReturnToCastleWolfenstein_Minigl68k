# Project memory

This project's memory lives in the project folder, in [memory/](memory/), so it
follows the project to other computers through OneDrive. Use it instead of the
per-user memory directory under `~/.claude/projects/`, and do not write project
memories there. `memory/` is in `.gitignore`: it does not go to GitHub, and a
plain `git clone` will not have it.

- One fact per file: `memory/<short-kebab-case-slug>.md`, with the usual
  frontmatter (`name`, `description`, `metadata.type`).
- Add a one-line pointer to each new file in `memory/MEMORY.md`.
- Keep machine-specific facts (paths, IP addresses) marked as such, because the
  next session may run on another computer.
- OneDrive syncs this folder: do not put tokens or passwords in `memory/`.

@memory/MEMORY.md

# Shared experience notes

Porting experience shared between the user's projects lives in
`../Deneyimler/` (next to this project, on OneDrive).

- Write what this project learns to `../Deneyimler/rtcw_<topic>.md`, in Turkish,
  in the style of the existing files. Say on which machine and on what date
  something was verified, and mark what was not tested.
- Other projects' files there (for example `openmohaa_*.md`) are read-only: each
  project's agent writes only its own. Read them before starting related work.
- `memory/` holds this project's working state; `../Deneyimler/` holds lessons
  another port could use.
