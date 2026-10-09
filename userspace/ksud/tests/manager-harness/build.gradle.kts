plugins {
    kotlin("jvm") version "2.3.10"
    application
}
repositories { mavenCentral() }
dependencies {
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.10.2")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-test:1.10.2")
}
kotlin { jvmToolchain(21) }
val production = file("../../../../manager/app/src/main/java/com/sukisu/ultra")
val copyProductionSettingsSources by tasks.registering(Sync::class) {
    from(production.resolve("ui/viewmodel/SettingsViewModel.kt"))
    from(production.resolve("ui/viewmodel/SettingsUiState.kt"))
    from(production.resolve("data/repository/SettingsRepository.kt"))
    from(production.resolve("ui/util/PatchedImageExport.kt"))
    into(layout.buildDirectory.dir("generated/production"))
}
kotlin.sourceSets["main"].kotlin.srcDir(layout.buildDirectory.dir("generated/production"))
tasks.named("compileKotlin") { dependsOn(copyProductionSettingsSources) }
application { mainClass.set("regression.RegressionKt") }
tasks.named<JavaExec>("run") { args(production.resolve("../../../../res/values/strings.xml").absolutePath) }
