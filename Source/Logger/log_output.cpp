﻿// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "log.h"
#include <iomanip>

NAMESPACE_XBOX_LIBHCBEGIN

log_output::log_output(log_output_level_setting setting, log_level level):
    m_levelSetting(setting),
    m_logLevel(level)
{
}

void log_output::add_log(_In_ const log_entry& entry)
{
    std::string msg = format_log(entry);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        write(msg);
    }
}

void log_output::write(_In_ const std::string& msg)
{
    UNREFERENCED_PARAMETER(msg);
}

std::string 
log_output::format_log(_In_ const log_entry& entry)
{
    std::stringstream stream;
    std::time_t t = std::time(nullptr);
    std::tm tm_snapshot;
#if (defined(WIN32) || defined(_WIN32) || defined(__WIN32__))
    localtime_s(&tm_snapshot, &t);
#else
    localtime_r(&t, &tm_snapshot); // POSIX  
#endif

    // format : "<time> [<thread id>] <level> <category> - <msg>"
#if !http_A 
    stream << std::put_time(&tm_snapshot, "%c") << " [" << std::this_thread::get_id() << "] ";
#endif
    stream << entry.level_to_string() << " " << entry.category() << " - ";
    stream << entry.msg_stream().str() << std::endl;

    return stream.str();

}

NAMESPACE_XBOX_LIBHCEND
