import java.io.File
import java.util.Properties

plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.compose.compiler)
}

// ccache when installed: speeds up native rebuilds, never required.
fun findCcache(): String? {
    val exe = if (org.gradle.internal.os.OperatingSystem.current().isWindows) "ccache.exe" else "ccache"
    System.getenv("PATH")?.split(File.pathSeparator)?.forEach { dir ->
        val f = File(dir, exe)
        if (f.isFile) return f.absolutePath
    }
    return null
}
val ccachePath: String? = findCcache()

fun gitFwbuildInfo(): String {
    return try {
        val rootDir = rootProject.projectDir
        val tree = providers.exec {
            workingDir = rootDir
            commandLine("git", "rev-parse", "--short=12", "HEAD:fwbuild")
            isIgnoreExitValue = true
        }.standardOutput.asText.get().trim()

        val status = providers.exec {
            workingDir = rootDir
            commandLine("git", "status", "--porcelain", "fwbuild")
            isIgnoreExitValue = true
        }.standardOutput.asText.get().trim()

        val dirty = if (status.isNotEmpty()) "-dirty" else ""
        if (tree.isNotEmpty()) "fwbuild=$tree$dirty" else "fwbuild=unknown"
    } catch (_: Exception) {
        "fwbuild=unknown"
    }
}

val fm1Abis: List<String> = (findProperty("fm1deckAbis") as String?)
    ?.split(",")?.map { it.trim() }?.filter { it.isNotEmpty() }
    ?: listOf("arm64-v8a", "x86_64")
// Built-in firmware modules need build/fwgen/<id>/gen (tools/fwgen). Default true from P1.
val fm1BuiltinFw: Boolean = (findProperty("fm1deckBuiltinFw") as String?)?.toBooleanStrictOrNull() ?: true

// Release signing from keystore.properties (gitignored); absent -> unsigned release (CI signs separately).
val keystoreProps = Properties().apply {
    val f = rootProject.file("keystore.properties")
    if (f.isFile) f.inputStream().use { load(it) }
}

android {
    namespace = "io.github.fm1deck"
    compileSdk = 36
    ndkVersion = "28.2.13676358"

    defaultConfig {
        applicationId = "io.github.fm1deck"
        minSdk = 29
        targetSdk = 36
        versionCode = 2
        versionName = "0.2.0"

        ndk { abiFilters += fm1Abis }
        externalNativeBuild {
            cmake {
                cppFlags += "-std=c++17"
                arguments += "-DANDROID_STL=c++_shared"
                arguments += "-DFM1DECK_BUILTIN_FIRMWARE=${if (fm1BuiltinFw) "ON" else "OFF"}"
                arguments += "-DFM1DECK_FW_BUILD_INFO=${gitFwbuildInfo()}"
                if (providers.gradleProperty("fm1deckDspProfile").isPresent) arguments += "-DFM1DECK_DSP_PROFILE=ON"
                ccachePath?.let {
                    arguments += "-DCMAKE_C_COMPILER_LAUNCHER=$it"
                    arguments += "-DCMAKE_CXX_COMPILER_LAUNCHER=$it"
                }
            }
        }
        testOptions {
            unitTests.isReturnDefaultValues = true
        }
    }

    signingConfigs {
        if (keystoreProps.getProperty("storeFile") != null) {
            create("release") {
                storeFile = rootProject.file(keystoreProps.getProperty("storeFile"))
                storePassword = keystoreProps.getProperty("storePassword")
                keyAlias = keystoreProps.getProperty("keyAlias")
                keyPassword = keystoreProps.getProperty("keyPassword")
            }
        }
    }

    buildTypes {
        debug {
            // The native DSP is optimised in debug too (it runs in the audio callback); see CMakeLists.txt.
        }
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            signingConfigs.findByName("release")?.let { signingConfig = it }
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }
    buildFeatures {
        compose = true
        prefab = true
        buildConfig = true
    }
    packaging {
        // .so stay uncompressed and page-aligned in the APK (mapped directly, 16 KB-page ready).
        jniLibs { useLegacyPackaging = false }
    }
}

dependencies {
    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.compose.ui)
    implementation(libs.androidx.compose.ui.graphics)
    implementation(libs.androidx.compose.ui.tooling.preview)
    implementation(libs.androidx.compose.material3)
    implementation(libs.androidx.activity.compose)
    implementation(libs.androidx.core.ktx)
    implementation(libs.oboe)

    testImplementation(libs.junit)
    testImplementation(libs.org.json)
}

// ---------------------------------------------------------------------------------------------------------------
// Host unit tests: AGP's testDebugUnitTest is disabled; `gradlew test` runs every compiled
// *Test class through JUnitCore. Classes are discovered at execution time (configuration-cache safe).
abstract class HostTestClassArgs : CommandLineArgumentProvider {
    @get:InputFiles
    @get:PathSensitive(PathSensitivity.RELATIVE)
    abstract val classDirs: ConfigurableFileCollection

    override fun asArguments(): Iterable<String> = classDirs.files.filter { it.isDirectory }.flatMap { root ->
        root.walkTopDown()
            .filter { it.isFile && it.name.endsWith("Test.class") && '$' !in it.name }
            .map { it.relativeTo(root).path.removeSuffix(".class").replace(File.separatorChar, '.') }
            .toList()
    }.sorted()
}

val hostTestJavaClasses = layout.buildDirectory.dir(
    "intermediates/javac/debugUnitTest/compileDebugUnitTestJavaWithJavac/classes")
val hostTestKotlinClasses = layout.buildDirectory.dir(
    "intermediates/built_in_kotlinc/debugUnitTest/compileDebugUnitTestKotlin/classes")

tasks.configureEach {
    if (name == "testDebugUnitTest" || name == "testReleaseUnitTest") enabled = false
}

val hostAarJars by tasks.registering(Sync::class) {
    into(layout.buildDirectory.dir("host-aar-runtime"))
    configurations.getByName("debugUnitTestRuntimeClasspath").filter { it.extension == "aar" }.forEach { aar ->
        from(zipTree(aar)) {
            include("classes.jar")
            rename { "${aar.nameWithoutExtension}.jar" }
        }
    }
}

val fm1HostUnitTest by tasks.registering(JavaExec::class) {
    description = "Runs FM1 DECK host-side unit tests with JUnitCore (all *Test classes)."
    group = "verification"
    dependsOn("compileDebugUnitTestJavaWithJavac", "compileDebugUnitTestKotlin", hostAarJars)
    classpath = files(hostTestJavaClasses, hostTestKotlinClasses) +
        configurations.getByName("debugUnitTestRuntimeClasspath") +
        fileTree(layout.buildDirectory.dir("host-aar-runtime")) { include("*.jar") }
    mainClass.set("org.junit.runner.JUnitCore")
    argumentProviders.add(objects.newInstance(HostTestClassArgs::class.java).apply {
        classDirs.from(hostTestJavaClasses, hostTestKotlinClasses)
    })
}

tasks.matching { it.name == "test" }.configureEach { dependsOn(fm1HostUnitTest) }
