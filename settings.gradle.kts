pluginManagement {
    repositories {
        google()          // REQUIRED for Android Gradle Plugin
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()          // REQUIRED for AndroidX, Material, etc.
        mavenCentral()
    }
}

rootProject.name = "KittyPress"
include(":app")
