import Foundation
import SwiftUI

/// Display configuration for the emulator output.
struct DisplaySettings: Codable, Hashable {
    var filter: DisplayFilter
    var aspectRatio: AspectRatio
    var windowMode: WindowMode
    var zoom: Int
    var colorMode: ColorMode

    static let `default` = DisplaySettings(
        filter: .sharp,
        aspectRatio: .ratio4to3,
        windowMode: .windowed,
        zoom: 2,
        colorMode: .color
    )
}

enum DisplayFilter: String, Codable, CaseIterable {
    case sharp      = "sharp"
    case crt        = "crt"
    case crtDeluxe  = "crtDeluxe"
    case lcdGrid    = "lcdGrid"
    case unfiltered = "unfiltered"

    var displayName: String {
        switch self {
        case .sharp:      return "Sharp (Default)"
        case .crt:        return "CRT Simulation"
        case .crtDeluxe:  return "CRT Deluxe"
        case .lcdGrid:    return "LCD Grid"
        case .unfiltered: return "Unfiltered (Soft)"
        }
    }

    /// MAME BGFX chain name (must match a .json in bgfx/chains/).
    var mameBGFXChain: String {
        switch self {
        case .sharp:      return "default"
        case .crt:        return "crt-geom"
        case .crtDeluxe:  return "crt-geom-deluxe"
        case .lcdGrid:    return "lcd-grid"
        case .unfiltered: return "unfiltered"
        }
    }
}

enum AspectRatio: String, Codable, CaseIterable {
    case pixel1to1 = "1:1"
    case ratio4to3 = "4:3"
    case stretch   = "stretch"

    var displayName: String {
        switch self {
        case .pixel1to1: return "1:1 Pixel Perfect"
        case .ratio4to3: return "4:3 (Original)"
        case .stretch:   return "Stretch to Fill"
        }
    }
}

enum WindowMode: String, Codable, CaseIterable {
    case windowed          = "windowed"
    case fullscreen        = "fullscreen"
    case fullscreenWindow  = "fullscreenWindow"

    var displayName: String {
        switch self {
        case .windowed:         return "Windowed"
        case .fullscreen:       return "Fullscreen"
        case .fullscreenWindow: return "Fullscreen Window"
        }
    }
}

enum ColorMode: String, Codable, CaseIterable {
    case color  = "color"
    case green  = "green"
    case amber  = "amber"
    case white  = "white"

    var displayName: String {
        switch self {
        case .color: return "Color"
        case .green: return "Green Phosphor"
        case .amber: return "Amber Phosphor"
        case .white: return "White Phosphor"
        }
    }
}
