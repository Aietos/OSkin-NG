#pragma once

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

// Add new modules here before using them
#include <unordered_set>
#include <shared_mutex>
#include <ranges>
#include <charconv>
#include <cwctype>
#include <filesystem>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>

#include <fstream>
#define RAPIDJSON_SCHEMA_USE_INTERNALREGEX 0
#define RAPIDJSON_SCHEMA_USE_STDREGEX 1
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/error/en.h>
#include <rapidjson/filereadstream.h>
#include <rapidjson/schema.h>
#include <random>

namespace logger = SKSE::log;
namespace fs = std::filesystem;

using namespace std::literals;

#define DLLEXPORT __declspec(dllexport)
