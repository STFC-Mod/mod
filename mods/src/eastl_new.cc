// The allocation hooks EASTL requires every program to define. Its containers free with operator delete[], so
// these must stay plain malloc: honouring `alignment` with an aligned allocator would mismatch that free.

#include <cstddef>
#include <cstdlib>

void* operator new[](size_t size, const char* /*name*/, int /*flags*/, unsigned /*debugFlags*/, const char* /*file*/,
                     int /*line*/)
{ return malloc(size); }

void* operator new[](size_t size, size_t /*alignment*/, size_t /*alignmentOffset*/, const char* /*name*/, int /*flags*/,
                     unsigned /*debugFlags*/, const char* /*file*/, int /*line*/)
{ return malloc(size); }
