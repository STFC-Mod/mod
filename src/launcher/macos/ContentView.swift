// The launcher window: the LCARS-styled frame, with the action buttons along its bottom.

import SwiftUI

struct ContentView: View {
  var body: some View {
    GeometryReader { geo in
      ZStack {
        VStack(spacing: 10) {
          topContent
            .frame(height: geo.size.height / 1.5)
          bottomContent
            .frame(height: geo.size.height - 10)
        }
        ActionView().offset(y: geo.size.height - 150)
      }
    }
    .ignoresSafeArea()
  }

  private var topContent: some View {
    GeometryReader { geo in
      ZStack {
        VStack(spacing: 5) {
          Color.lcarPink
          Color.lcarViolet
        }
        .clipShape(UnevenRoundedRectangle(bottomLeadingRadius: 70, style: .circular))
        .overlay(alignment: .topTrailing) {
          Color.black
            .clipShape(UnevenRoundedRectangle(bottomLeadingRadius: 35, style: .circular))
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
          .font(.lcars(size: 17))
          .foregroundStyle(.black)
          .scaleEffect(x: 0.7, anchor: .trailing)
          .frame(width: 90)
        }
        .overlay(alignment: .topTrailing) {
          Text("LCARS ACCESS 333")
            .font(.lcars(size: 35))
            .padding(.top, 45)
            .foregroundStyle(Color.lcarOrange)
            .scaleEffect(x: 0.7, anchor: .trailing)
            .offset(x: -15, y: 0)
        }
        .overlay(alignment: .trailing) {
          Grid(alignment: .trailing) {
            ForEach(0..<7) { row in
              GridRow {
                ForEach(0..<8) { _ in
                  Text(randomDigits(Int.random(in: 1...6)))
                    .foregroundStyle(row == 3 || row == 4 ? Color.lcarWhite : Color.lcarOrange)
                }
              }
            }
          }
          .font(.lcars(size: 17))
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
        .clipShape(UnevenRoundedRectangle(topLeadingRadius: 70, style: .circular))
        .overlay(alignment: .bottomTrailing) {
          Color.black
            .clipShape(UnevenRoundedRectangle(topLeadingRadius: 35, style: .circular))
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
        .font(.lcars(size: 17))
        .foregroundStyle(.black)
    }
    .frame(width: 90)
    .scaleEffect(x: 0.7, anchor: .trailing)
  }
}

// Filler digits for the LCARS labels. Views call it from body, so the digits change whenever they redraw.
func randomDigits(_ count: Int) -> String {
  (0..<count).map { _ in String(Int.random(in: 0...9)) }.joined()
}

extension Font {
  static func lcars(size: CGFloat) -> Font {
    .custom("HelveticaNeue-CondensedBold", size: size)
  }
}

// The LCARS palette, from Assets.xcassets.
extension Color {
  static let lcarLightOrange = Color("lcarLightOrange")
  static let lcarOrange = Color("lcarOrange")
  static let lcarPink = Color("lcarPink")
  static let lcarPlum = Color("lcarPlum")
  static let lcarTan = Color("lcarTan")
  static let lcarViolet = Color("lcarViolet")
  static let lcarWhite = Color("lcarWhite")
}
