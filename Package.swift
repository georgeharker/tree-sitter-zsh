// swift-tools-version:5.3
import PackageDescription

let package = Package(
    name: "TreeSitterZsh",
    products: [
        .library(name: "TreeSitterZsh", targets: ["TreeSitterZsh"])
    ],
    dependencies: [
        .package(
            name: "SwiftTreeSitter",
            url: "https://github.com/tree-sitter/swift-tree-sitter",
            from: "0.25.0"
        )
    ],
    targets: [
        .target(
            name: "TreeSitterZsh",
            dependencies: [],
            path: ".",
            sources: [
                "src/parser.c",
                "src/scanner.c",
            ],
            resources: [
                .copy("queries")
            ],
            publicHeadersPath: "bindings/swift",
            cSettings: [.headerSearchPath("src")]
        ),
        .testTarget(
            name: "TreeSitterZshTests",
            dependencies: [
                "SwiftTreeSitter",
                "TreeSitterZsh",
            ],
            path: "bindings/swift/TreeSitterZshTests"
        ),
    ],
    cLanguageStandard: .c11
)
