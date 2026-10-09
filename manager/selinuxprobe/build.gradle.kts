plugins { alias(libs.plugins.agp.app) }

android {
    namespace = "com.sukisu.ultra.selinuxprobe"
    compileSdk = 36
    ndkVersion = libs.versions.ndk.get()
    defaultConfig {
        applicationId = "com.sukisu.ultra.selinuxprobe"
        minSdk = 26
        targetSdk = 36
        versionCode = 2
        versionName = "1.1-selinux-parity-validation"
        ndk { abiFilters += "arm64-v8a" }
    }
    buildTypes { release { isMinifyEnabled = false } }
    externalNativeBuild { cmake { path = file("src/main/cpp/CMakeLists.txt") } }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_21
        targetCompatibility = JavaVersion.VERSION_21
    }
}
