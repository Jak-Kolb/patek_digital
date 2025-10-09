// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "ESP32DataIOS",
    platforms: [
        .iOS(.v16)
    ],
    products: [
        .executable(name: "ESP32DataIOSApp", targets: ["App"])
    ],
    targets: [
        .executableTarget(
            name: "App",
            path: "Sources/App"
        )
    ]
)
