//
//  SSLDummy.swift
//  SSL
//
//  Created by Sasha Weiss on 1/4/21.
//

import Foundation

/// This is an empty, dummy struct to convince Xcode to build the `SSL_*` targets.
///
/// Since `SSL_*` are just targets to merge an externally-built `libssl.a` and
/// `libcrypto.a` into a single lib that the libHttpClient targets can depend on,
/// they have no actual source of its own and consequently Xcode was skipping steps.
/// 
/// See `<srcroot>/Utilies/XcodeBuildScripts/openssl.bash` and the "Build Phases"
/// of the `SSL_*` targets for more information.
struct SSLDummy {}
