#ifndef __EV_FS_H__
#define __EV_FS_H__

// #if defined(__cplusplus) && __cplusplus >= 201703L && defined(__has_include)
// #if __has_include(<filesystem>)
// #define GHC_USE_STD_FS
// #include <filesystem>
// namespace fs = std::filesystem;
// #endif
// #endif
// #ifndef GHC_USE_STD_FS
#include <ghc/filesystem.hpp>
namespace fs = ghc::filesystem;
// #endif

#endif