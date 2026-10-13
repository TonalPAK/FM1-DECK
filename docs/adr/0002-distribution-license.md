# ADR-0002: GPL-3.0-only, distributed via GitHub Releases + Obtainium, no Google Play

Status: accepted (2026-10-12).

**Context.** All three firmwares are GPL-3.0-only; the app runs them and distributes them with itself.
Google Play policy forbids downloading executable code outside Play (which breaks "drop in a firmware, it updates").

**Decision.** The app is GPL-3.0-only. The APK and firmware packages are published on GitHub Releases; APK updates
go through Obtainium. About links the sources of every component at their exact commits; every package carries its
LICENSE and SOURCE.md. F-Droid maybe later (firmware import is flagged as an anti-feature there).

**Consequences.** Packages can be imported freely; signing the APK and the packages is the project's responsibility.
