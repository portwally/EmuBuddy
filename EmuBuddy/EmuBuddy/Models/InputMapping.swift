import Foundation
import Combine

/// Input configuration for keyboard, joystick, and game controllers.
struct InputMapping: Codable, Hashable {
    var openAppleKey: String       // Key code name (e.g., "Option")
    var closedAppleKey: String     // Key code name (e.g., "Command")
    var joystickSource: JoystickSource
    var numpadAsJoystick: Bool
    var gamepadMappingID: UUID?    // Reference to a saved GamepadMapping

    static let `default` = InputMapping(
        openAppleKey: "Option",
        closedAppleKey: "Command",
        joystickSource: .keyboard,
        numpadAsJoystick: false,
        gamepadMappingID: nil
    )
}

enum JoystickSource: String, Codable, CaseIterable {
    case keyboard = "keyboard"
    case gamepad  = "gamepad"
    case mouse    = "mouse"

    var displayName: String {
        switch self {
        case .keyboard: return "Keyboard (Arrow Keys)"
        case .gamepad:  return "Game Controller"
        case .mouse:    return "Mouse / Trackpad"
        }
    }
}
