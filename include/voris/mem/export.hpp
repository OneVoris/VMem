#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
#if defined(VORIS_VMEM_SHARED)
#if defined(VORIS_VMEM_BUILD)
#define VORIS_VMEM_API __declspec(dllexport)
#else
#define VORIS_VMEM_API __declspec(dllimport)
#endif
#else
#define VORIS_VMEM_API
#endif
#define VORIS_VMEM_LOCAL
#elif defined(__GNUC__) || defined(__clang__)
#if defined(VORIS_VMEM_SHARED)
#define VORIS_VMEM_API __attribute__((visibility("default")))
#define VORIS_VMEM_LOCAL __attribute__((visibility("hidden")))
#else
#define VORIS_VMEM_API
#define VORIS_VMEM_LOCAL
#endif
#else
#define VORIS_VMEM_API
#define VORIS_VMEM_LOCAL
#endif
