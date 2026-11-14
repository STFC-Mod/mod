#pragma once

// Digit.PrimeServer.Models.StateContainer<T>, a managed struct. Layout matches T values of at most
// pointer alignment (the game uses it with int enums).
template <typename T> struct StateContainer {
  T     currentState;
  T     previousState;
  void* equalityComparer;

  T CurrentState() const
  { return currentState; }
  T PreviousState() const
  { return previousState; }
};
