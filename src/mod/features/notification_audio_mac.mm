// macOS playback for the notification cues.

#include "features/notification_audio.h"

#import <AppKit/AppKit.h>

bool PlayNotificationBuffer(const uint8_t* data, size_t size)
{
  @autoreleasepool {
    // Kept so a new cue can stop the previous one, and so the sound is not released while it plays.
    static NSSound* current_sound = nil;

    NSData* sound_data = [NSData dataWithBytesNoCopy:const_cast<uint8_t*>(data) length:size freeWhenDone:NO];
    NSSound* sound      = [[NSSound alloc] initWithData:sound_data];
    if (!sound)
      return false;

    [current_sound stop];
#if __has_feature(objc_arc)
    current_sound = sound;
#else
    [current_sound release];
    current_sound = sound;
#endif
    return [current_sound play];
  }
}
