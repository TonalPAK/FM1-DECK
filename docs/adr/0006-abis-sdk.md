# ADR-0006: ABIs arm64-v8a + x86_64, minSdk 29, target/compile 36, NDK 28.2

Status: accepted.

The test phones are arm64 (Snapdragon 778G, Helio G81 Ultra). x86_64 is for the Android emulator and host tests.
Dropping armeabi-v7a/x86 halves the build of firmware modules and packages. 16 KB page alignment is on
(`-Wl,-z,max-page-size=16384`). Toolchain: AGP 9.4.1, Kotlin 2.3.21, Gradle 9.7.1, Oboe 1.10.0,
Compose BOM 2025.02.00, verified by building the skeleton on 2026-10-12.
