#include <filesystem>

#import <Foundation/Foundation.h>

// ~/Library for the current user. Copied out of the autoreleased NSURL before the pool drains.
std::filesystem::path MacLibraryDirectory()
{
  @autoreleasepool {
    NSArray<NSURL*>* urls = [[NSFileManager defaultManager] URLsForDirectory:NSLibraryDirectory
                                                                   inDomains:NSUserDomainMask];
    if (urls.count == 0)
      return std::filesystem::path(NSHomeDirectory().fileSystemRepresentation) / "Library";
    return urls.firstObject.path.fileSystemRepresentation;
  }
}
