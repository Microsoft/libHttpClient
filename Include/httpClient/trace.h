// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#pragma once

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

    // Configuration
    // All macros used for configuration are always defined. Clients can define
    // their own values for these macros to configure tracing
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
    // HC_TRACE_TO_TRACELOGGING [0,1]
    //     controls if trace will output using the TraceLoggingProvider
    //     only has an effect when building trace.cpp

    // these defines are not meant to be used by clients, the TraceLevel enum should
    // be used instead
#define HC_PRIVATE_TRACE_LEVEL_OFF 0
#define HC_PRIVATE_TRACE_LEVEL_ERROR 1
#define HC_PRIVATE_TRACE_LEVEL_WARNING 2
#define HC_PRIVATE_TRACE_LEVEL_IMPORTANT 3
#define HC_PRIVATE_TRACE_LEVEL_INFORMATION 4
#define HC_PRIVATE_TRACE_LEVEL_VERBOSE 5

#ifndef HC_TRACE_BUILD_LEVEL
#define HC_TRACE_BUILD_LEVEL HC_PRIVATE_TRACE_LEVEL_VERBOSE
#endif

#ifndef HC_TRACE_TO_DEBUGGER
#define HC_TRACE_TO_DEBUGGER 1
#endif

#ifndef HC_TRACE_TO_CLIENT
#define HC_TRACE_TO_CLIENT 1
#endif

    // level enabled macros, can be used to easily check if a given trace level will
    // be included in the build (for example to avoid compiling helper code for a
    // trace)
    //
    // HC_TRACE_ENABLE
    // HC_TRACE_ERROR_ENABLE
    // HC_TRACE_WARNING_ENABLE
    // HC_TRACE_IMPORTANT_ENABLE
    // HC_TRACE_INFORMATION_ENABLE
    // HC_TRACE_VERBOSE_ENABLE
    //
    // these should not be manually modified, they are automatically set based on
    // the values of HC_TRACE_ENABLE and HC_TRACE_BUILD_LEVEL
#if HC_TRACE_BUILD_LEVEL > HC_PRIVATE_TRACE_LEVEL_OFF
#define HC_TRACE_ENABLE 1
#else
#define HC_TRACE_ENABLE 0
#endif

#if HC_TRACE_ENABLE && HC_TRACE_BUILD_LEVEL >= HC_PRIVATE_TRACE_LEVEL_ERROR
#define HC_TRACE_ERROR_ENABLE 1
#else
#define HC_TRACE_ERROR_ENABLE 0
#endif


#if HC_TRACE_ENABLE && HC_TRACE_BUILD_LEVEL >= HC_PRIVATE_TRACE_LEVEL_WARNING
#define HC_TRACE_WARNING_ENABLE 1
#else
#define HC_TRACE_WARNING_ENABLE 0
#endif


#if HC_TRACE_ENABLE && HC_TRACE_BUILD_LEVEL >= HC_PRIVATE_TRACE_LEVEL_IMPORTANT
#define HC_TRACE_IMPORTANT_ENABLE 1
#else
#define HC_TRACE_IMPORTANT_ENABLE 0
#endif


#if HC_TRACE_ENABLE && HC_TRACE_BUILD_LEVEL >= HC_PRIVATE_TRACE_LEVEL_INFORMATION
#define HC_TRACE_INFORMATION_ENABLE 1
#else
#define HC_TRACE_INFORMATION_ENABLE 0
#endif


#if HC_TRACE_ENABLE && HC_TRACE_BUILD_LEVEL >= HC_PRIVATE_TRACE_LEVEL_VERBOSE
#define HC_TRACE_VERBOSE_ENABLE 1
#else
#define HC_TRACE_VERBOSE_ENABLE 0
#endif

#if defined(__cplusplus)
#if HC_TRACE_ENABLE
#pragma detect_mismatch("HCTrace", "on")
#else
#pragma detect_mismatch("HCTrace", "off")
#endif
#endif

    enum HCTraceLevel
    {
        HC_TRACELEVEL_OFF = HC_PRIVATE_TRACE_LEVEL_OFF,
        HC_TRACELEVEL_ERROR = HC_PRIVATE_TRACE_LEVEL_ERROR,
        HC_TRACELEVEL_WARNING = HC_PRIVATE_TRACE_LEVEL_WARNING,
        HC_TRACELEVEL_IMPORTANT = HC_PRIVATE_TRACE_LEVEL_IMPORTANT,
        HC_TRACELEVEL_INFORMATION = HC_PRIVATE_TRACE_LEVEL_INFORMATION,
        HC_TRACELEVEL_VERBOSE = HC_PRIVATE_TRACE_LEVEL_VERBOSE,
    };

    struct HCTraceArea
    {
        char const* const Name;
        enum HCTraceLevel Verbosity;
    };

    typedef void (HCTraceCallback)(
        char const* areaName,
        enum HCTraceLevel level,
        unsigned int threadId,
        uint64_t timestamp,
        char const* message
        );

    void HCTraceSetClientCallback(HCTraceCallback* callback);

#if HC_TRACE_ENABLE
    void HCTraceMessage(
        struct HCTraceArea const* area,
        enum HCTraceLevel level,
        _Printf_format_string_ char const* format,
        ...
    );

    void HCTraceGlobalInit();
    void HCTraceGlobalCleanup();

#define HC_PRIVATE_TRACE_AREA_NAME(area) g_trace##area

    // Defines an area for tracing:
    // name is used as a prefix for all traces in the area
    // verbosity sets the maximum verbosity that will be traced within this area
    // vebosity can be tweaked at runtime by changing the value of
    // g_trace<NAME>.Verbosity from the debugger (it is not intended to be changed
    // from code)
    // Since this defines a global variable, it should be only used from a .cpp file
    // and each area should be defined only once
#define HC_DEFINE_TRACE_AREA(area, verbosity) \
    struct HCTraceArea HC_PRIVATE_TRACE_AREA_NAME(area) = { #area, (verbosity) }

    // Declares a trace area. Since DEFINE_TRACE_AREA can only be used once and in a
    // .cpp, this allows to trace in an already defined area from another file
#define HC_DECLARE_TRACE_AREA(area) \
    extern struct HCTraceArea HC_PRIVATE_TRACE_AREA_NAME(area)

    // Initialize, cleanup and register callback for tracing
#define HC_TRACE_GLOBAL_INIT() HCTraceGlobalInit()
#define HC_TRACE_GLOBAL_CLEANUP() HCTraceGlobalCleanup()

#define HC_TRACE_MESSAGE(area, level, ...) \
    HCTraceMessage(&HC_PRIVATE_TRACE_AREA_NAME(area), (level), ##__VA_ARGS__)

#define HC_TRACE_MESSAGE_WITH_LOCATION(area, level, format, ...) \
    HC_TRACE_MESSAGE(HC_PRIVATE_TRACE_AREA_NAME(area), (level), format "\n    %s(%d): %s()" , ##__VA_ARGS__,  __FILE__, __LINE__, __FUNCTION__)
#else
#define HC_DEFINE_TRACE_AREA(name, verbosity)
#define HC_DECLARE_TRACE_AREA(name)

#define HC_TRACE_GLOBAL_INIT()
#define HC_TRACE_GLOBAL_CLEANUP()

#define HC_TRACE_MESSAGE(area, level, ...)
#define HC_TRACE_MESSAGE_WITH_LOCATION(area, level, format, ...)
#endif

#if defined(__cplusplus)
    class HCTraceScopeHelper
    {
    public:
        HCTraceScopeHelper(HCTraceArea const* area, HCTraceLevel level, char const* scope);
        ~HCTraceScopeHelper();

    private:
        HCTraceArea const* m_area;
        HCTraceLevel const m_level;
        char const* const m_scope;
        unsigned long long const m_id;
    };

#if HC_TRACE_ENABLE
#define HC_TRACE_SCOPE(area, level) \
        auto tsh = HCTraceScopeHelper{ HC_PRIVATE_TRACE_AREA_NAME(area), level, __FUNCTION__ }
#else
#define HC_TRACE_SCOPE(area, level)
#endif
#else
#define HC_TRACE_SCOPE(area, level)
#endif

#if HC_TRACE_ERROR_ENABLE
#define HC_TRACE_ERROR_WITH_LOCATION(area, msg, ...)  \
    HC_TRACE_MESSAGE_WITH_LOCATION(area, HC_TRACELEVEL_ERROR, msg, ##__VA_ARGS__)

#define HC_TRACE_ERROR(area, msg, ...)  \
    HC_TRACE_MESSAGE(area, HC_TRACELEVEL_ERROR, msg, ##__VA_ARGS__)

#define HC_TRACE_ERROR_HR(area, failedHr, msg)  \
    HC_TRACE_ERROR(area, "%hs (hr=0x%08x)", msg, failedHr)
#else
#define HC_TRACE_ERROR_WITH_LOCATION(area, msg, ...)
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

#if defined(__cplusplus)
} // end extern "C"
#endif // defined(__cplusplus)