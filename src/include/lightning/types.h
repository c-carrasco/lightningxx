// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2024 Carlos Carrasco
// ----------------------------------------------------------------------------
#ifndef __LIGHTNING_TYPES_H__
#define __LIGHTNING_TYPES_H__
#include <cxxlog/logger.h>
#include <cxxlog/transport.h>


namespace lightning {

using Logger = cxxlog::Logger<cxxlog::transport::OutputStream>;

using LogLevel = cxxlog::Severity;

}

#endif
