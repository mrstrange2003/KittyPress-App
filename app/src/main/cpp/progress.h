#pragma once
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// Called from C++ code to set total number of bytes for upcoming operation.
void native_progress_reset();
void native_progress_set_total(uint64_t totalBytes);
void native_progress_add_processed(uint64_t bytes);

#ifdef __cplusplus
}
#endif
