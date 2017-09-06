// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#pragma once

#include <stdint.h>

#include <httpClient/types.h>

#if defined(__cplusplus)
extern "C" {
#endif

// these defines are not meant to be used by clients at runtime, the
// HCTraceLevel enum should be used instead
#define HC_PRIVATE_TRACE_LEVEL_OFF 0
#define HC_PRIVATE_TRACE_LEVEL_ERROR 1
#define HC_PRIVATE_TRACE_LEVEL_WARNING 2
#define HC_PRIVATE_TRACE_LEVEL_IMPORTANT 3
#define HC_PRIVATE_TRACE_LEVEL_INFORMATION 4
#define HC_PRIVATE_TRACE_LEVEL_VERBOSE 5

//------------------------------------------------------------------------------
// trace level enum
//------------------------------------------------------------------------------
enum HCTraceLevel
{
    HC_TRACELEVEL_OFF = HC_PRIVATE_TRACE_LEVEL_OFF,
    HC_TRACELEVEL_ERROR = HC_PRIVATE_TRACE_LEVEL_ERROR,
    HC_TRACELEVEL_WARNING = HC_PRIVATE_TRACE_LEVEL_WARNING,
    HC_TRACELEVEL_IMPORTANT = HC_PRIVATE_TRACE_LEVEL_IMPORTANT,
    HC_TRACELEVEL_INFORMATION = HC_PRIVATE_TRACE_LEVEL_INFORMATION,
    HC_TRACELEVEL_VERBOSE = HC_PRIVATE_TRACE_LEVEL_VERBOSE,
};

//------------------------------------------------------------------------------
// Configuration
//------------------------------------------------------------------------------
// All macros used for configuration are always defined. Clients can define
// their own values for these macros to control tracing behaviour
//
// HC_TRACE_BUILD_LEVEL [trace level (0-5)]
//     controls the maximum level of verbosity that will be built in the
//     executable. To control verbosity at runtime see TraceArea. Set to 0 to 
//     completely disable tracing
//
// HC_TRACE_TO_DEBUGGER [0,1]
//     controls if trace will output using OutputDebugString
//     only has an effect when building trace.cpp
//
// HC_TRACE_TO_CLIENT [0,1]
//     controls if trace will output using the TraceLoggingProvider
//     only has an effect when building trace.cpp

#ifndef HC_TRACE_BUILD_LEVEL
#define HC_TRACE_BUILD_LEVEL HC_PRIVATE_TRACE_LEVEL_VERBOSE
#endif

#ifndef HC_TRACE_TO_DEBUGGER
#define HC_TRACE_TO_DEBUGGER 1
#endif

#ifndef HC_TRACE_TO_CLIENT
#define HC_TRACE_TO_CLIENT 1
#endif

//------------------------------------------------------------------------------
// level enabled macros
//------------------------------------------------------------------------------
// these macros can be used to easily check if a given trace level will be
// included in the build (for example to avoid compiling helper code for a
// trace).
// These macros are always defined
//
// HC_TRACE_ENABLE [0,1]
// HC_TRACE_ERROR_ENABLE [0,1]
// HC_TRACE_WARNING_ENABLE [0,1]
// HC_TRACE_IMPORTANT_ENABLE [0,1]
// HC_TRACE_INFORMATION_ENABLE [0,1]
// HC_TRACE_VERBOSE_ENABLE [0,1]
//
// these should not be manually modified, they are automatically set based on
// the values of HC_TRACE_BUILD_LEVEL

#if HC_TRACE_BUILD_LEVEL > HC_PRIVATE_TRACE_LEVEL_OFF
#define HC_TRACE_ENABLE 1
#else
#define HC_TRACE_ENABLE 0
#endif

#if HC_TRACE_BUILD_LEVEL >= HC_PRIVATE_TRACE_LEVEL_ERROR
#define HC_TRACE_ERROR_ENABLE 1
#else
#define HC_TRACE_ERROR_ENABLE 0
#endif


#if HC_TRACE_BUILD_LEVEL >= HC_PRIVATE_TRACE_LEVEL_WARNING
#define HC_TRACE_WARNING_ENABLE 1
#else
#define HC_TRACE_WARNING_ENABLE 0
#endif


#if HC_TRACE_BUILD_LEVEL >= HC_PRIVATE_TRACE_LEVEL_IMPORTANT
#define HC_TRACE_IMPORTANT_ENABLE 1
#else
#define HC_TRACE_IMPORTANT_ENABLE 0
#endif


#if HC_TRACE_BUILD_LEVEL >= HC_PRIVATE_TRACE_LEVEL_INFORMATION
#define HC_TRACE_INFORMATION_ENABLE 1
#else
#define HC_TRACE_INFORMATION_ENABLE 0
#endif


#if HC_TRACE_BUILD_LEVEL >= HC_PRIVATE_TRACE_LEVEL_VERBOSE
#define HC_TRACE_VERBOSE_ENABLE 1
#else
#define HC_TRACE_VERBOSE_ENABLE 0
#endif

//------------------------------------------------------------------------------
// Client callback
//------------------------------------------------------------------------------
// Register callback for tracing so that the client can merge tracing into their
// own logs
// These macros are always defined but will compile to nothing if trace is
// disabled

typedef void (HCTraceCallback)(
    CHAR_T const* areaName,
    enum HCTraceLevel level,
    unsigned int threadId,
    uint64_t timestamp,
    CHAR_T const* message
);

void HCTraceSetClientCallback(HCTraceCallback* callback);

//------------------------------------------------------------------------------
// Trace area macros
//------------------------------------------------------------------------------
// these macros are used to set up areas for tracing
// These macros are always defined but will compile to nothing if trace is
// disabled

#if HC_TRACE_ENABLE
// Defines an area for tracing:
// name is used as a prefix for all traces in the area
// verbosity sets the maximum verbosity that will be traced within this area
// vebosity can be tweaked at runtime by calling HC_TRACE_SET_VERBOSITY(area,
// level)
// since this defines a global variable, it should be only used from a .cpp file
// and each area should be defined only once
#define HC_DEFINE_TRACE_AREA(area, verbosity) \
    struct HCTraceImplArea HC_PRIVATE_TRACE_AREA_NAME(area) = { _T(#area), (verbosity) }

// Declares a trace area. Since DEFINE_TRACE_AREA can only be used once and in a
// .cpp, this allows to trace in an already defined area from another file
#define HC_DECLARE_TRACE_AREA(area) \
    extern struct HCTraceImplArea HC_PRIVATE_TRACE_AREA_NAME(area)

// Access to the verbosity property of the area for runtime control
#define HC_TRACE_SET_VERBOSITY(area, level) \
    HCTraceImplSetAreaVerbosity(&HC_PRIVATE_TRACE_AREA_NAME(area), level)

#define HC_TRACE_GET_VERBOSITY(area) \
    HCTraceImplGetAreaVerbosity(&HC_PRIVATE_TRACE_AREA_NAME(area))
#else
#define HC_DEFINE_TRACE_AREA(name, verbosity)
#define HC_DECLARE_TRACE_AREA(name)
#define HC_TRACE_SET_VERBOSITY(area, level)
#define HC_TRACE_GET_VERBOSITY(area) HC_TRACELEVEL_OFF
#endif

//------------------------------------------------------------------------------
// Trace macros
//------------------------------------------------------------------------------
// these are the macros to be used to log
// These macros are always defined but will compile to nothing if
// HC_TRACE_BUILD_LEVEL is not high enough

#if HC_TRACE_ENABLE
#define HC_TRACE_MESSAGE(area, level, format, ...) \
    HCTraceImplMessage(&HC_PRIVATE_TRACE_AREA_NAME(area), (level), _T(format), ##__VA_ARGS__)

#ifdef __cplusplus
#define HC_TRACE_SCOPE(area, level) \
        auto tsh = HCTraceImplScopeHelper{ &HC_PRIVATE_TRACE_AREA_NAME(area), level, HC_FUNCTION }
#else
#define HC_TRACE_SCOPE(area, level)
#endif
#else
#define HC_TRACE_MESSAGE(area, level, ...)
#define HC_TRACE_MESSAGE_WITH_LOCATION(area, level, format, ...)
#define HC_TRACE_SCOPE(area, level)
#endif

#if HC_TRACE_ERROR_ENABLE
#define HC_TRACE_ERROR(area, msg, ...)  \
    HC_TRACE_MESSAGE(area, HC_TRACELEVEL_ERROR, msg, ##__VA_ARGS__)

#define HC_TRACE_ERROR_HR(area, failedHr, msg)  \
    HC_TRACE_ERROR(area, "%hs (hr=0x%08x)", msg, failedHr)
#else
#define HC_TRACE_ERROR(area, msg, ...)
#define HC_TRACE_ERROR_HR(area, failedHr, msg)
#endif

#if HC_TRACE_WARNING_ENABLE
#define HC_TRACE_WARNING(area, msg, ...) \
    HC_TRACE_MESSAGE(area, HC_TRACELEVEL_WARNING, msg, ##__VA_ARGS__)

#define HC_TRACE_WARNING_HR(area, failedHr, msg)  \
    HC_TRACE_WARNING(area, "%hs (hr=0x%08x)", msg, failedHr)
#else
#define HC_TRACE_WARNING(area, msg, ...)
#define HC_TRACE_WARNING_HR(area, failedHr, msg)
#endif

#if HC_TRACE_IMPORTANT_ENABLE
#define HC_TRACE_IMPORTANT(area, msg, ...) \
    HC_TRACE_MESSAGE(area, HC_TRACELEVEL_IMPORTANT, msg, ##__VA_ARGS__)

#define HC_TRACE_SCOPE_IMPORTANT(area) \
    HC_TRACE_SCOPE(area, HC_TRACELEVEL_IMPORTANT)
#else
#define HC_TRACE_IMPORTANT(area, msg, ...)
#define HC_TRACE_SCOPE_IMPORTANT(area)
#endif

#if HC_TRACE_INFORMATION_ENABLE
#define HC_TRACE_INFORMATION(area, msg, ...) \
    HC_TRACE_MESSAGE(area, HC_TRACELEVEL_INFORMATION, msg, ##__VA_ARGS__)

#define HC_TRACE_SCOPE_INFORMATION(area) \
    HC_TRACE_SCOPE(area, HC_TRACELEVEL_INFORMATION)
#else
#define HC_TRACE_INFORMATION(area, msg, ...)
#define HC_TRACE_SCOPE_INFORMATION(area)
#endif

#if HC_TRACE_VERBOSE_ENABLE
#define HC_TRACE_VERBOSE(area, msg, ...) \
        HC_TRACE_MESSAGE(area, HC_TRACELEVEL_VERBOSE, msg, ##__VA_ARGS__)

#define HC_TRACE_SCOPE_VERBOSE(area) \
    HC_TRACE_SCOPE(area, HC_TRACELEVEL_VERBOSE)
#else
#define HC_TRACE_VERBOSE(area, msg, ...)
#define HC_TRACE_SCOPE_VERBOSE(area)
#endif

//------------------------------------------------------------------------------
// Implementation
//------------------------------------------------------------------------------
// these symbols are always built even if trace is disabled, but note that no
// trace areas will be defined so they should never be used directly
// the reason why they are always built is to avoid issues with different
// compilation units setting different values for HC_TRACE_BUILD_LEVEL
// the linker will discard everything if tracing is disabled because there will
// be no references

// DO NOT USE THESE SYMBOLS DIRECTLY

#define HC_PRIVATE_TRACE_AREA_NAME(area) g_trace##area

#if HC_CHAR_T_IS_WIDE
#define HC_FUNCTION __FUNCTIONW__
#else
#define HC_FUNCTION __FUNCTION__
#endif

struct HCTraceImplArea
{
    CHAR_T const* const Name;
    enum HCTraceLevel Verbosity;
};

inline
void HCTraceImplSetAreaVerbosity(HCTraceImplArea* area, HCTraceLevel verbosity)
{
    area->Verbosity = verbosity;
}

inline
HCTraceLevel HCTraceImplGetAreaVerbosity(HCTraceImplArea* area)
{
    return area->Verbosity;
}

void HCTraceImplMessage(
    struct HCTraceImplArea const* area,
    enum HCTraceLevel level,
    _Printf_format_string_ CHAR_T const* format,
    ...
);

#if defined(__cplusplus)
class HCTraceImplScopeHelper
{
public:
    HCTraceImplScopeHelper(HCTraceImplArea const* area, HCTraceLevel level, CHAR_T const* scope);
    ~HCTraceImplScopeHelper();

private:
    HCTraceImplArea const* m_area;
    HCTraceLevel const m_level;
    CHAR_T const* const m_scope;
    unsigned long long const m_id;
};
#endif

#if defined(__cplusplus)
} // end extern "C"
#endif // defined(__cplusplus)
