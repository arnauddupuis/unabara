# Flathub Submission Workflow

## Prerequisites

### 1. App ID
Current ID: `org.unabara.unabara`. To use this, you need to prove ownership of `unabara.org`. Alternative: use `io.github.arnauddupuis.unabara` (no domain ownership needed).

### 2. Required files

- **AppStream metainfo file** — `org.unabara.unabara.metainfo.xml` installed to `/app/share/metainfo/`. Describes the app for the Flathub store (name, summary, description, screenshots, release notes, categories).
- **Desktop file** — `.desktop` file installed to `/app/share/applications/` with the same app ID.
- **App icon** — SVG or at least 128x128 PNG, installed to `/app/share/icons/hicolor/`.
- **Flatpak manifest** — YAML or JSON file named `org.unabara.unabara.yml`. Since Unabara is Qt6, use the KDE runtime:
  ```yaml
  app-id: org.unabara.unabara
  runtime: org.kde.Platform
  runtime-version: '6.8'
  sdk: org.kde.Sdk
  command: unabara
  ```

### 3. Test the build locally

Build with `org.flatpak.Builder` and run the linter before submitting:
```bash
flatpak run --command=flatpak-builder-lint org.flatpak.Builder manifest org.unabara.unabara.yml
flatpak run --command=flatpak-builder-lint org.flatpak.Builder repo repo
```

## Submission process

1. Fork `https://github.com/flathub/flathub` (with **all branches**, not just master)
2. Create a new branch named after the app ID
3. Add the manifest file
4. Open a pull request
5. Wait for review (reviewers are volunteers)
6. Address any feedback
7. Once approved, a reviewer comments `bot, build` to trigger a test build
8. After merge, the app appears on Flathub

## Things to watch out for

- Flathub requires building from source (not bundling a pre-built binary)
- The existing CI already builds a `.flatpak` — reuse that manifest as a starting point
- Keep permissions minimal (no `--filesystem=host` unless truly needed)
- Flathub has a **Generative AI policy** — check it (commit messages mention Claude Code)
- After initial submission, updates never go through this process again — they happen via the app's own Flathub repo

## References

- Flathub Submission Documentation: https://docs.flathub.org/docs/for-app-authors/submission
- Flathub App Submission Wiki: https://github.com/flathub/flathub/wiki/App-Submission
- Flatpak Manifests Documentation: https://docs.flatpak.org/en/latest/manifests.html
- KDE Flatpak Packaging Guide: https://develop.kde.org/docs/packaging/flatpak/packaging/
- Flatpak Requirements & Conventions: https://docs.flatpak.org/en/latest/conventions.html
