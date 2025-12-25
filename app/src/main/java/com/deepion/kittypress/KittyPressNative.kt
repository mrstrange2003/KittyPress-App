// KittyPressNative.kt (UNIFIED - Archive format for all cases)
package com.deepion.kittypress

object KittyPressNative {
    init {
        System.loadLibrary("kittypress")
    }

    // Archive compression: handles 1 file, multiple files, or folders
    // returns 0 on success, non-zero on error
    external fun compressNative(inputArray: Array<String>, outPath: String): Int

    // Archive extraction: handles 1 file, multiple files, or folders
    external fun decompressNative(archive: String, outDir: String): String?

    // registers native -> Java progress callback endpoint
    external fun registerProgressCallback()
}