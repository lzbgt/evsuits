/*
module: common
description: 
author: Bruce.Lu <lzbgt@icloud.com>
update: 2019/08/23
*/


#ifndef __COMMON_H__
#define __COMMON_H__


#include "utils.hpp"
#include "av_common.hpp"

#if defined(__cplusplus) && __cplusplus >= 201703L && defined(__has_include)
#if __has_include(<filesystem>)
#define GHC_USE_STD_FS
#include <filesystem>
namespace fs {
using namespace std::filesystem;
using ifstream = std::ifstream;
using ofstream = std::ofstream;
using fstream = std::fstream;
}
#endif
#endif
#ifndef GHC_USE_STD_FS
#include "ghc/fs_fwd.hpp"
namespace fs {
using namespace ghc::filesystem;
using ifstream = ghc::filesystem::ifstream;
using ofstream = ghc::filesystem::ofstream;
using fstream = ghc::filesystem::fstream;
} 
#endif

#endif
