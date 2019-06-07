// swift-tools-version:5.1

import PackageDescription
import Foundation

let depPath = URL(fileURLWithPath: #file)
    .deletingLastPathComponent()
    .appendingPathComponent("dependencies.list")
let dependencies = try! String(contentsOf: depPath, encoding: .utf8)
let versionLine = dependencies[dependencies.range(of: #"VERSION=[0-9]+\.[0-9]+\.[0-9]+"#, options: .regularExpression)!]
let versionStr = versionLine.replacingOccurrences(of: "VERSION=", with: "")
let versionPieces = versionStr.split(separator: ".")

let package = Package(
    name: "RealmCore",
    products: [
        .library(
            name: "RealmCore",
            targets: ["RealmCore"]),
    ],
    targets: [
        .target(
            name: "RealmCore",
            path: "src",
            exclude: [
                "realm/tools",
                "realm/parser",
                "realm/metrics",
                "realm/exec",
                "win32",
                "external"
            ],
            publicHeadersPath: ".",
            cxxSettings: [
                .headerSearchPath("src"),
                .define("REALM_NO_CONFIG"),
                .define("REALM_INSTALL_LIBEXECDIR", to: ""),
                .define("REALM_ENABLE_ASSERTIONS", to: "1"),
                .define("REALM_ENABLE_ENCRYPTION", to: "1"),

                .define("REALM_VERSION_MAJOR", to: String(versionPieces[0])),
                .define("REALM_VERSION_MINOR", to: String(versionPieces[1])),
                .define("REALM_VERSION_PATCH", to: String(versionPieces[2])),
                .define("REALM_VERSION_EXTRA", to: "\"\""),
                .define("REALM_VERSION_STRING", to: "\"\(versionStr)\""),
            ]),
    ],
    cxxLanguageStandard: .cxx14
)
