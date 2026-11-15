#ifdef __APPLE__

#include "patches/notification_audio_platform.h"

#import <AppKit/AppKit.h>

bool notification_audio_platform_play(const uint8_t* data, size_t size)
{
  @autoreleasepool {
    static NSSound* current_sound = nil;

    NSData* sound_data = [NSData dataWithBytesNoCopy:const_cast<uint8_t*>(data) length:size freeWhenDone:NO];
    NSSound* sound      = [[NSSound alloc] initWithData:sound_data];
    if (!sound) return false;

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

#endif
