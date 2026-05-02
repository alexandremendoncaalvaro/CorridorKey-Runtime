#pragma once

/**
 * @file api_export.hpp
 * @brief Macros for managing symbol visibility and library export/import.
 */

#ifdef CORRIDORKEY_STATIC_DEFINE
#define CORRIDORKEY_API
#elif defined(_WIN32) || defined(__CYGWIN__)
#ifdef CORRIDORKEY_EXPORTS
#define CORRIDORKEY_API __declspec(dllexport)
#else
#define CORRIDORKEY_API __declspec(dllimport)
#endif
#else
#if __GNUC__ >= 4
#define CORRIDORKEY_API __attribute__((visibility("default")))
#else
#define CORRIDORKEY_API
#endif
#endif
