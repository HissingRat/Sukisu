@file:Suppress("UnstableApiUsage")

enableFeaturePreview("TYPESAFE_PROJECT_ACCESSORS")

pluginManagement {
    repositories {
        google()
        mavenCentral()
    }
}

dependencyResolutionManagement {
    repositories {
        google()
        mavenCentral()
        maven("https://jitpack.io")
    }
}

rootProject.name = "KernelSU"
include(":app")

// Explicitly opt in to the unprivileged device-validation app.
if (providers.gradleProperty("SUKISU_BUILD_SELINUX_PROBE").orNull == "true") {
    include(":selinuxprobe")
}
