package com.deepion.kittypress

object NativeProgress {

    @Volatile
    var handler: ((Int) -> Unit)? = null

    /**
     * Register a progress handler.
     * Also tells the native side to register its callback.
     */
    fun setProgressCallback(cb: (Int) -> Unit) {
        handler = cb
        KittyPressNative.registerProgressCallback()
    }

    /**
     * Called from C++ via JNI.
     */
    @JvmStatic
    fun onNativeProgress(pct: Int) {
        handler?.invoke(pct)
    }
}
