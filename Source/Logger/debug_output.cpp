﻿// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "debug_output.h"


NAMESPACE_XBOX_LIBHCBEGIN

void debug_output::write(_In_ const std::string& msg)
{
    OutputDebugStringA(msg.c_str());
}

NAMESPACE_XBOX_LIBHCEND
