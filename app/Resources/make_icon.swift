import AppKit
// Renders the 1024x1024 app icon: dark rounded square, orange glow, "III".
let size = 1024.0
let image = NSImage(size: NSSize(width: size, height: size))
image.lockFocus()
let rect = NSRect(x: 0, y: 0, width: size, height: size).insetBy(dx: 100, dy: 100)
let shape = NSBezierPath(roundedRect: rect, xRadius: 185, yRadius: 185)
NSGradient(colors: [NSColor(calibratedRed: 0.09, green: 0.09, blue: 0.11, alpha: 1),
                    NSColor(calibratedRed: 0.17, green: 0.16, blue: 0.19, alpha: 1)])!.draw(in: shape, angle: 90)
NSGraphicsContext.current?.saveGraphicsState()
shape.addClip()
let glow = NSGradient(colors: [NSColor(calibratedRed: 1.0, green: 0.45, blue: 0.1, alpha: 0.55), .clear])!
glow.draw(fromCenter: NSPoint(x: size / 2, y: 330), radius: 0, toCenter: NSPoint(x: size / 2, y: 330), radius: 420, options: [])
NSGraphicsContext.current?.restoreGraphicsState()
NSColor(white: 1, alpha: 0.08).setStroke()
shape.lineWidth = 6
shape.stroke()
let paragraph = NSMutableParagraphStyle(); paragraph.alignment = .center
let title = NSAttributedString(string: "III", attributes: [
    .font: NSFont.systemFont(ofSize: 380, weight: .black), .foregroundColor: NSColor.white, .paragraphStyle: paragraph,
    .kern: 18])
title.draw(in: NSRect(x: 100, y: 330, width: 824, height: 460))
let subtitle = NSAttributedString(string: "MAC CLIENT", attributes: [
    .font: NSFont.systemFont(ofSize: 76, weight: .semibold),
    .foregroundColor: NSColor(calibratedRed: 1.0, green: 0.6, blue: 0.25, alpha: 1), .paragraphStyle: paragraph, .kern: 14])
subtitle.draw(in: NSRect(x: 100, y: 215, width: 824, height: 110))
image.unlockFocus()
let rep = NSBitmapImageRep(data: image.tiffRepresentation!)!
try! rep.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: CommandLine.arguments[1]))
