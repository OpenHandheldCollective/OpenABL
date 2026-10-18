// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026-present Open Handheld Collective
//   https://github.com/OpenHandheldCollective

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

val abl = file("../../VERSION").readText().trim()
val (major, minor) = abl.split(".").map { it.toInt() }

android {
    namespace = "org.openhandheld.openabl.installer"
    compileSdk = 36

    defaultConfig {
        applicationId = "org.openhandheld.openabl.installer"
        minSdk = 30
        targetSdk = 35
        versionCode = major * 1000 + minor
        versionName = abl
    }

    // A release is signed only when the key is given in the environment.
    val keystore = providers.environmentVariable("OPENABL_KEYSTORE").orNull
    signingConfigs {
        if (keystore != null) {
            create("release") {
                storeFile = file(keystore)
                storePassword = providers.environmentVariable("OPENABL_KEYSTORE_PASSWORD").get()
                keyAlias = providers.environmentVariable("OPENABL_KEY_ALIAS").get()
                keyPassword = providers.environmentVariable("OPENABL_KEY_PASSWORD").get()
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            if (keystore != null) signingConfig = signingConfigs.getByName("release")
        }
    }

    buildFeatures {
        compose = true
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlin {
        compilerOptions.jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17)
    }

    testOptions {
        unitTests.isReturnDefaultValues = true
    }
}

// Images of the release in ../../VERSION, or those in -PimagesDir=<folder>.
val fetchImages by tasks.registering(Exec::class) {
    val dir = providers.gradleProperty("imagesDir").orNull
    val args = listOf("sh", file("../fetch-images.sh").path) + listOfNotNull(dir)
    commandLine(args)
    inputs.files("../fetch-images.sh", "../release.sha256", "../known-images.txt", "../../VERSION")
    inputs.property("imagesDir", dir ?: "")
    outputs.dir("src/main/assets")
}

tasks.named("preBuild") { dependsOn(fetchImages) }

dependencies {
    implementation(platform("androidx.compose:compose-bom:2025.05.01"))
    implementation("androidx.activity:activity-compose:1.10.1")
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.foundation:foundation")

    testImplementation("junit:junit:4.13.2")
    testImplementation("org.json:json:20240303")
}
