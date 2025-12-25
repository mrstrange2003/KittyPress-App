// native-lib.cpp (MODIFIED)
#include <jni.h>
#include <string>
#include <android/log.h>
#include "archive.h"
#include "compress.h"
#include "progress.h"
#include "kp_log.h"
#include <atomic>
#include <mutex>
#include <fstream>

// Forward: We'll store JVM pointer and callback refs
static JavaVM* gJvm = nullptr;
static jobject gProgressClassGlobal = nullptr;
static jmethodID gOnProgressMethod = nullptr;
static std::mutex gProgressMutex;

static std::atomic<uint64_t> g_totalBytes{0};
static std::atomic<uint64_t> g_processedBytes{0};

static void call_java_progress(int pct) {
    std::lock_guard<std::mutex> lock(gProgressMutex);
    if (!gJvm || !gProgressClassGlobal || !gOnProgressMethod) return;
    JNIEnv* env = nullptr;
    bool attached = false;
    jint getEnvRes = gJvm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (getEnvRes == JNI_EDETACHED) {
        if (gJvm->AttachCurrentThread(&env, nullptr) != 0) {
            KP_LOGE("AttachCurrentThread failed");
            return;
        }
        attached = true;
    } else if (getEnvRes == JNI_OK) {
        // already attached
    } else {
        KP_LOGE("GetEnv unexpected result %d", getEnvRes);
        return;
    }

    env->CallStaticVoidMethod((jclass)gProgressClassGlobal, gOnProgressMethod, (jint)pct);

    if (attached) gJvm->DetachCurrentThread();
}

// JNI_OnLoad to capture JavaVM*
jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    gJvm = vm;
    return JNI_VERSION_1_6;
}

// Implementation of progress.h functions
extern "C" void native_progress_reset() {
    g_totalBytes.store(0);
    g_processedBytes.store(0);
    call_java_progress(0);
}

extern "C" void native_progress_set_total(uint64_t totalBytes) {
    g_totalBytes.store(totalBytes);
    g_processedBytes.store(0);
    call_java_progress(0);
}

extern "C" void native_progress_add_processed(uint64_t bytes) {
    if (bytes == 0) return;
    uint64_t prev = g_processedBytes.fetch_add(bytes);
    uint64_t total = g_totalBytes.load();
    if (total == 0) return;
    uint64_t processed = prev + bytes;
    int pct = (int)((processed * 100) / total);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    call_java_progress(pct);
}

// Safe conversion: handles null jstring
static std::string toStr(JNIEnv* env, jstring js) {
    if (js == nullptr) return std::string();
    const char* c = env->GetStringUTFChars(js, nullptr);
    if (!c) return std::string();
    std::string s(c);
    env->ReleaseStringUTFChars(js, c);
    return s;
}

// Safe array conversion
static std::vector<std::string> toStrArray(JNIEnv* env, jobjectArray arr) {
    std::vector<std::string> out;
    if (arr == nullptr) return out;

    jsize len = env->GetArrayLength(arr);
    out.reserve(static_cast<size_t>(len));

    for (jsize i = 0; i < len; ++i) {
        jstring js = (jstring) env->GetObjectArrayElement(arr, i);
        if (js == nullptr) {
            KP_LOGE("toStrArray: null string at index %d", i);
            out.emplace_back();
            continue;
        }
        out.push_back(toStr(env, js));
        env->DeleteLocalRef(js);
    }

    return out;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_deepion_kittypress_KittyPressNative_registerProgressCallback(JNIEnv* env, jobject thiz) {
std::lock_guard<std::mutex> lock(gProgressMutex);
jclass cls = env->FindClass("com/deepion/kittypress/NativeProgress");
if (!cls) {
KP_LOGE("registerProgressCallback: FindClass failed");
return;
}
if (gProgressClassGlobal) {
env->DeleteGlobalRef(gProgressClassGlobal);
gProgressClassGlobal = nullptr;
gOnProgressMethod = nullptr;
}
gProgressClassGlobal = env->NewGlobalRef(cls);
gOnProgressMethod = env->GetStaticMethodID((jclass)gProgressClassGlobal, "onNativeProgress", "(I)V");
if (!gOnProgressMethod) {
KP_LOGE("registerProgressCallback: GetStaticMethodID failed");
} else {
KP_LOGI("registerProgressCallback: registered");
}
}

// EXISTING: Multi-file archive compression
extern "C" JNIEXPORT jint JNICALL
        Java_com_deepion_kittypress_KittyPressNative_compressNative(
        JNIEnv* env, jobject, jobjectArray inputArray, jstring outPath) {
try {
auto inputs = toStrArray(env, inputArray);
std::string out = toStr(env, outPath);

KP_LOGI("Compressing to: %s", out.c_str());
for (size_t i = 0; i < inputs.size(); ++i) {
KP_LOGI("  input[%zu] = %s", i, inputs[i].c_str());
}

native_progress_reset();
createArchive(inputs, out);
call_java_progress(100);
return 0;
} catch (const std::exception& e) {
KP_LOGE("Error: %s", e.what());
return 1;
}
}

// NEW: Single-file streaming compression (input URI → output URI, direct streaming)
extern "C" JNIEXPORT jint JNICALL
        Java_com_deepion_kittypress_KittyPressNative_compressSingleFileStreamNative(
        JNIEnv* env, jobject, jstring inputPath, jstring outputPath) {
try {
std::string inPath = toStr(env, inputPath);
std::string outPath = toStr(env, outputPath);

KP_LOGI("Single-file streaming compress: %s -> %s", inPath.c_str(), outPath.c_str());

// Open input file
std::ifstream in(inPath, std::ios::binary);
if (!in) {
KP_LOGE("Cannot open input file: %s", inPath.c_str());
return 1;
}

// Get file size
in.seekg(0, std::ios::end);
uint64_t fileSize = in.tellg();
in.seekg(0, std::ios::beg);

// Open output file
std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
if (!out) {
KP_LOGE("Cannot open output file: %s", outPath.c_str());
return 1;
}

// Set progress total to file size
native_progress_set_total(fileSize);

// Get extension
std::string ext;
size_t dotPos = inPath.rfind('.');
if (dotPos != std::string::npos && dotPos < inPath.size() - 1) {
ext = inPath.substr(dotPos + 1);
}

// Compress stream to stream
uint64_t outDataSize = 0;
compressStreamToStream(in, out, fileSize, ext, outDataSize);

in.close();
out.close();

call_java_progress(100);
KP_LOGI("Single-file compress complete: %llu -> %llu bytes", fileSize, outDataSize);
return 0;

} catch (const std::exception& e) {
KP_LOGE("Single-file compress error: %s", e.what());
return 1;
}
}

// NEW: Single-file streaming decompression (input URI → output URI, direct streaming)
extern "C" JNIEXPORT jint JNICALL
        Java_com_deepion_kittypress_KittyPressNative_decompressSingleFileStreamNative(
        JNIEnv* env, jobject, jstring inputPath, jstring outputPath) {
try {
std::string inPath = toStr(env, inputPath);
std::string outPath = toStr(env, outputPath);

KP_LOGI("Single-file streaming decompress: %s -> %s", inPath.c_str(), outPath.c_str());

// Open input file
std::ifstream in(inPath, std::ios::binary);
if (!in) {
KP_LOGE("Cannot open input file: %s", inPath.c_str());
return 1;
}

// Get file size for progress
in.seekg(0, std::ios::end);
uint64_t fileSize = in.tellg();
in.seekg(0, std::ios::beg);

// Set progress total to compressed file size
native_progress_set_total(fileSize);

KP_LOGI("Input file size: %llu bytes", fileSize);

// For single-file streaming, the entire file IS the KP05 payload
// (no archive wrapper), so we read and decompress directly
try {
decompressFromStream(in, fileSize, outPath);
} catch (const std::exception& e) {
KP_LOGE("Decompression failed: %s", e.what());
throw;
}

in.close();

call_java_progress(100);
KP_LOGI("Single-file decompress complete");
return 0;

} catch (const std::exception& e) {
KP_LOGE("Single-file decompress error: %s", e.what());
return 1;
}
}

// EXISTING: Multi-file archive extraction (handles 1 or multiple files)
extern "C" JNIEXPORT jstring JNICALL
        Java_com_deepion_kittypress_KittyPressNative_decompressNative(
        JNIEnv* env, jobject, jstring archivePath, jstring outputFolder) {
try {
std::string in = toStr(env, archivePath);
std::string out = toStr(env, outputFolder);

KP_LOGI("Decompressing archive: %s -> %s", in.c_str(), out.c_str());

native_progress_reset();
std::string extractedName = extractArchive(in, out);
call_java_progress(100);
return env->NewStringUTF(extractedName.c_str());

} catch (const std::exception& e) {
KP_LOGE("Error: %s", e.what());
return nullptr;
}
}