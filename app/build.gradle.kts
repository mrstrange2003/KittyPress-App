plugins {
    id("com.android.application")
    kotlin("android")
}


android {
    namespace = "com.deepion.kittypress"
    compileSdk = 34


    defaultConfig {
        applicationId = "com.deepion.kittypress"
        minSdk = 26
        targetSdk = 34
        versionCode = 1
        versionName = "1.0"


        externalNativeBuild {
            cmake {
                cppFlags += "-std=c++17"
            }
        }
    }


    buildTypes {
        getByName("release") {
            isMinifyEnabled = false
        }
    }


    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }


    packaging {
        resources.excludes += setOf("META-INF/DEPENDENCIES", "META-INF/LICENSE")
    }


    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }


    kotlinOptions {
        jvmTarget = "17"
    }
}


dependencies {
    implementation("androidx.appcompat:appcompat:1.6.1")
    implementation("com.google.android.material:material:1.9.0")
    implementation("androidx.core:core-ktx:1.10.1")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.7.1")
    implementation("androidx.documentfile:documentfile:1.0.1")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.7.0")

}