#pragma once

#include <cstdint>

using IActionData = void*;

// Digit.PrimeServer.Models.ActionType is a struct around a flag value, not an enum, so it crosses the
// managed boundary as a 4-byte struct.
struct ActionType {
  int32_t flagValue;

  friend bool operator==(ActionType, ActionType) = default;

  static const ActionType Invalid;
  static const ActionType Upgrade;
  static const ActionType Purchase;
  static const ActionType Manage;
  static const ActionType Harvest;
  static const ActionType Repair;
  static const ActionType View;
  static const ActionType Recall;
  static const ActionType Promote;
  static const ActionType Demote;
  static const ActionType Move;
  static const ActionType Attack;
  static const ActionType Assign;
  static const ActionType Add;
  static const ActionType Remove;
  static const ActionType Claim;
  static const ActionType Chat;
  static const ActionType Help;
  static const ActionType Invite;
  static const ActionType Report;
  static const ActionType Copy;
  static const ActionType Share;
};

inline constexpr ActionType ActionType::Invalid {-1};
inline constexpr ActionType ActionType::Upgrade {1433481724};
inline constexpr ActionType ActionType::Purchase{1807968545};
inline constexpr ActionType ActionType::Manage  {-1997567611};
inline constexpr ActionType ActionType::Harvest {-1933286583};
inline constexpr ActionType ActionType::Repair  {-1850668115};
inline constexpr ActionType ActionType::View    {2666181};
inline constexpr ActionType ActionType::Recall  {-1851055311};
inline constexpr ActionType ActionType::Promote {1355442080};
inline constexpr ActionType ActionType::Demote  {2043415476};
inline constexpr ActionType ActionType::Move    {2404337};
inline constexpr ActionType ActionType::Attack  {1971575400};
inline constexpr ActionType ActionType::Assign  {1970629903};
inline constexpr ActionType ActionType::Add     {65665};
inline constexpr ActionType ActionType::Remove  {-1850743644};
inline constexpr ActionType ActionType::Claim   {65189916};
inline constexpr ActionType ActionType::Chat    {2099064};
inline constexpr ActionType ActionType::Help    {2245473};
inline constexpr ActionType ActionType::Invite  {-2099832023};
inline constexpr ActionType ActionType::Report  {-1850654380};
inline constexpr ActionType ActionType::Copy    {2106261};
inline constexpr ActionType ActionType::Share   {79847359};


enum ActionBehaviour {
  Default             = 1,
  Instant             = 2,
  Speedup             = 4,
  Collect             = 8,
  AskHelp             = 16,
  RequestConfirmation = 32,
};