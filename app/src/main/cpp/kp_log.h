#pragma once

#include <android/log.h>

#ifdef NDEBUG
// -------- RELEASE BUILD --------
#define KP_LOGI(...)
#define KP_LOGE(...)
#else
// -------- DEBUG BUILD --------
#define KP_LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "KittyPress", __VA_ARGS__)
#define KP_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "KittyPress", __VA_ARGS__)
#endif
