# Releasing an APK

## 1. A signing key (once)

```bash
keytool -genkeypair -v -keystore fm1deck-release.jks -alias fm1deck -keyalg RSA -keysize 4096 -validity 10000
```

Keep the `.jks` file and its passwords **outside the repository** and backed up: every update must be signed with
the same key, or Android refuses to install it over the previous version.

Create `keystore.properties` in the project root (it is gitignored):

```properties
storeFile=../keys/fm1deck-release.jks
storePassword=…
keyAlias=fm1deck
keyPassword=…
```

## 2. Version

In `app/build.gradle.kts` raise `versionCode` (an integer, +1 every release) and set `versionName` (e.g. `0.2.0`).
Add the release to [CHANGELOG.md](../CHANGELOG.md).

## 3. Build

```bash
git submodule update --init --recursive
python3 tools/fwgen/fwgen.py all
./gradlew test assembleRelease -Pfm1deckAbis=arm64-v8a
```

The APK is `app/build/outputs/apk/release/app-release.apk`. Install it on a phone and check: each firmware boots
and plays, settings open, Link connects.

## 4. Publish on GitHub

1. Tag: `git tag v0.2.0 && git push origin v0.2.0`.
2. GitHub → Releases → *Draft a new release* → the tag → title `FM1 DECK 0.2.0` → paste the CHANGELOG section →
   attach `FM1-DECK-0.2.0-arm64.apk` (the renamed APK) → Publish.
3. GPL-3.0: the release must point to the corresponding source — the tag does that; the firmwares' sources are the
   submodule commits in that tag.

## 4a. Automated release (CI)

`.github/workflows/release.yml` does steps 3–4 on GitHub: pushing a tag `v*` builds the signed arm64 APK, names it
`FM1-DECK-<version>-arm64.apk` (+ `.sha256`) and publishes a Release whose body is the CHANGELOG section
`## <version> …`. The job fails if the tag does not match `versionName` or the CHANGELOG has no such section.

Repository secrets (Settings → Secrets and variables → Actions):

| Secret | Value |
|---|---|
| `KEYSTORE_BASE64` | `base64 -w0 fm1deck-release.jks` (macOS: `base64 -i fm1deck-release.jks`; Windows: `certutil -encodehex -f fm1deck-release.jks out.txt 0x40000001`) |
| `KEYSTORE_PASSWORD` | store password |
| `KEY_ALIAS` | `fm1deck` |
| `KEY_PASSWORD` | key password |

Then: bump the version + CHANGELOG, commit, `git tag v0.2.0 && git push origin v0.2.0`.

## 5. Making the repository public

Before switching the repository to public, check that it contains no keys, `keystore.properties`, `local.properties`
or personal files (`git ls-files` / the GitHub file view), and that README's screenshots are current.
