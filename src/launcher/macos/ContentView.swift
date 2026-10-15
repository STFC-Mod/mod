//
//  ContentView.swift
//  STFC Community Mod Launcher
//
//  Created by tashcan on 3/22/24.
//

import Foundation
import SwiftUI

struct ContentView: View {
  var body: some View {
    GeometryReader { geo in
      ZStack {
        VStack(spacing: 10) {
          topContent
            .frame(height: geo.size.height / 1.5)
          bottomContent
            .frame(height: geo.size.height * (2 / 2) - 10)
        }
        ActionView().offset(y: geo.size.height - 150)
      }
    }
    .edgesIgnoringSafeArea(.all)
  }

  private var topContent: some View {
    GeometryReader { geo in
      ZStack {
        VStack(spacing: 5) {
          Color.lcarPink
          Color.lcarViolet
        }
        .cornerRadius(70, corners: .bottomLeft)
        .overlay(alignment: .topTrailing) {
          Color.black
            .cornerRadius(35, corners: .bottomLeft)
            .frame(width: geo.size.width - 100, height: geo.size.height - 20)
        }
        .overlay(alignment: .topLeading) {
          Color.black
            .frame(width: 100, height: 50)
        }
        .overlay(alignment: .bottomTrailing) {
          ZStack {
            Color.black
            HStack(spacing: 5) {
              Color.lcarOrange
                .frame(width: 20)
                .padding(.leading, 5)
              Color.lcarPink
                .frame(width: 50)
              Color.lcarPink
              Color.lcarPlum
                .frame(width: 20)
            }
          }
          .frame(width: 200, height: 20)
        }
        .overlay(alignment: .leading) {
          VStack(alignment: .trailing, spacing: 15) {
            Text("LCARS \(randomDigits(5))")
            Text("02-\(randomDigits(6))")
          }
          .font(.custom("HelveticaNeue-CondensedBold", size: 17))
          .foregroundColor(.black)
          .scaleEffect(x: 0.7, anchor: .trailing)
          .frame(width: 90)
        }
        .overlay(alignment: .topTrailing) {
          Text("LCARS ACCESS 333")
            .font(.custom("HelveticaNeue-CondensedBold", size: 35))
            .padding(.top, 45)
            .foregroundColor(.lcarOrange)
            .scaleEffect(x: 0.7, anchor: .trailing)
            .offset(x: -15, y: 0)
        }
        .overlay(alignment: .trailing) {
          Grid(alignment: .trailing) {
            ForEach(0..<7) { row in
              GridRow {
                ForEach(0..<8) { _ in
                  Text(randomDigits(Int.random(in: 1...6)))
                    .foregroundColor((row == 3 || row == 4) ? .lcarWhite : .lcarOrange)
                }
              }
            }
          }
          .font(.custom("HelveticaNeue-CondensedBold", size: 17))
          .padding(.top, 45)
          .offset(x: -15, y: 0)
        }
      }
    }
  }

  private var bottomContent: some View {
    GeometryReader { geo in
      ZStack {
        VStack(alignment: .leading, spacing: 5) {
          Color.lcarPlum
            .frame(height: 100)
            .overlay(alignment: .bottomLeading) {
              commonLabel(prefix: "03")
                .padding(.bottom, 5)
            }
          Color.lcarPlum
            .frame(height: 200)
            .overlay(alignment: .bottomLeading) {
              commonLabel(prefix: "04")
                .padding(.bottom, 5)
            }
          Color.lcarOrange
            .frame(height: 50)
            .overlay(alignment: .leading) {
              commonLabel(prefix: "05")
            }
          Color.lcarTan
            .overlay(alignment: .topLeading) {
              commonLabel(prefix: "06")
                .padding(.top, 5)
            }
        }
        .cornerRadius(70, corners: .topLeft)
        .overlay(alignment: .bottomTrailing) {
          Color.black
            .cornerRadius(35, corners: .topLeft)
            .frame(width: geo.size.width - 100, height: geo.size.height - 20)
        }
        .overlay(alignment: .bottomLeading) {
          Color.black
            .frame(width: 100, height: 50)
        }
        .overlay(alignment: .topTrailing) {
          ZStack {
            Color.black
            HStack(alignment: .top, spacing: 5) {
              Color.lcarLightOrange
                .frame(width: 20)
                .padding(.leading, 5)
              Color.lcarLightOrange
                .frame(width: 50, height: 10)
              Color.lcarPink
              Color.lcarOrange
                .frame(width: 20)
            }
          }
          .frame(width: 200, height: 20)
        }
        .overlay {
          Image("ufp-logo")
            .opacity(0.15)
            .offset(x: 50, y: -200)
        }
      }
    }
  }

  private func commonLabel(prefix: String) -> some View {
    HStack {
      Spacer()
      Text("\(prefix)-\(randomDigits(6))")
        .font(.custom("HelveticaNeue-CondensedBold", size: 17))
        .foregroundColor(.black)
    }
    .frame(width: 90)
    .scaleEffect(x: 0.7, anchor: .trailing)
  }

  private func randomDigits(_ count: Int) -> String {
    (1...count)
      .map { _ in "\(Int.random(in: 0...9))" }
      .joined()
  }
}

extension Color {
  static let lcarLightOrange = Color("lcarLightOrange")
  static let lcarOrange = Color("lcarOrange")
  static let lcarPink = Color("lcarPink")
  static let lcarPlum = Color("lcarPlum")
  static let lcarTan = Color("lcarTan")
  static let lcarViolet = Color("lcarViolet")
  static let lcarWhite = Color("lcarWhite")
}

extension View {
  func cornerRadius(_ radius: CGFloat, corners: RectCorner) -> some View {
    clipShape(RoundedCornersShape(radius: radius, corners: corners))
  }
}

// draws shape with specified rounded corners applying corner radius
struct RoundedCornersShape: SwiftUI.Shape {

  var radius: CGFloat = .zero
  var corners: RectCorner = .allCorners

  func path(in rect: CGRect) -> SwiftUI.Path {
    var path = SwiftUI.Path()

    let p1 = CGPoint(x: rect.minX, y: corners.contains(.topLeft) ? rect.minY + radius : rect.minY)
    let p2 = CGPoint(x: corners.contains(.topLeft) ? rect.minX + radius : rect.minX, y: rect.minY)

    let p3 = CGPoint(x: corners.contains(.topRight) ? rect.maxX - radius : rect.maxX, y: rect.minY)
    let p4 = CGPoint(x: rect.maxX, y: corners.contains(.topRight) ? rect.minY + radius : rect.minY)

    let p5 = CGPoint(
      x: rect.maxX, y: corners.contains(.bottomRight) ? rect.maxY - radius : rect.maxY)
    let p6 = CGPoint(
      x: corners.contains(.bottomRight) ? rect.maxX - radius : rect.maxX, y: rect.maxY)

    let p7 = CGPoint(
      x: corners.contains(.bottomLeft) ? rect.minX + radius : rect.minX, y: rect.maxY)
    let p8 = CGPoint(
      x: rect.minX, y: corners.contains(.bottomLeft) ? rect.maxY - radius : rect.maxY)

    path.move(to: p1)
    path.addArc(
      tangent1End: CGPoint(x: rect.minX, y: rect.minY),
      tangent2End: p2,
      radius: radius)
    path.addLine(to: p3)
    path.addArc(
      tangent1End: CGPoint(x: rect.maxX, y: rect.minY),
      tangent2End: p4,
      radius: radius)
    path.addLine(to: p5)
    path.addArc(
      tangent1End: CGPoint(x: rect.maxX, y: rect.maxY),
      tangent2End: p6,
      radius: radius)
    path.addLine(to: p7)
    path.addArc(
      tangent1End: CGPoint(x: rect.minX, y: rect.maxY),
      tangent2End: p8,
      radius: radius)
    path.closeSubpath()

    return path
  }
}

struct RectCorner: OptionSet {
  let rawValue: Int

  static let topLeft = RectCorner(rawValue: 1 << 0)
  static let topRight = RectCorner(rawValue: 1 << 1)
  static let bottomRight = RectCorner(rawValue: 1 << 2)
  static let bottomLeft = RectCorner(rawValue: 1 << 3)

  static let allCorners: RectCorner = [.topLeft, topRight, .bottomLeft, .bottomRight]
}
