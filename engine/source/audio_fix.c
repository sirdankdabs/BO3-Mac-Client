// Output device hot-switching for the game's SDL2 CoreAudio backend.
//
// The game's WASAPI emulation plays through an SDL audio device. When SDL opens
// the default device it pins its AudioQueue to that device's UID
// (kAudioQueueProperty_CurrentDevice) and watches the device's "is alive"
// property. Switching outputs (e.g. speakers -> AirPods) therefore keeps the
// game on the old device, and if that device goes away SDL marks the stream
// dead and audio stays silent until restart.
//
// For output queues opened on the current default output device we leave the
// queue unpinned, so CoreAudio routes it to whatever the default output is,
// and we skip SDL's "is alive" listener for that device. Input (microphone)
// queues are never touched: following the default input would move voice chat
// onto the AirPods microphone and force them into the low-quality headset mode.
#include "dyld-interposing.h"
#include "t7_runtime.h"
#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TRACKED 16

static pthread_mutex_t audio_lock = PTHREAD_MUTEX_INITIALIZER;
static AudioQueueRef output_queues[MAX_TRACKED];
static AudioObjectID unpinned_devices[MAX_TRACKED];

static bool audio_fix_enabled(void) {
    const char *disabled = getenv("BO3MACFIX_DISABLE_AUDIO_FIX");
    return !(disabled && !strcmp(disabled, "1"));
}

static void track_queue(AudioQueueRef queue, bool add) {
    for (int i = 0; i < MAX_TRACKED; ++i) {
        if (add ? !output_queues[i] : output_queues[i] == queue) { output_queues[i] = add ? queue : NULL; return; }
    }
}

static void add_unpinned_device(AudioObjectID device) {
    for (int i = 0; i < MAX_TRACKED; ++i) {
        if (unpinned_devices[i] == device) return;
        if (!unpinned_devices[i]) { unpinned_devices[i] = device; return; }
    }
}

static bool contains_queue(AudioQueueRef queue) {
    for (int i = 0; i < MAX_TRACKED; ++i) if (output_queues[i] == queue) return true;
    return false;
}

static bool contains_device(AudioObjectID device) {
    for (int i = 0; i < MAX_TRACKED; ++i) if (device && unpinned_devices[i] == device) return true;
    return false;
}

static AudioObjectID default_output_device(void) {
    AudioObjectPropertyAddress address = {
        kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    AudioObjectID device = kAudioObjectUnknown;
    UInt32 size = sizeof(device);
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, NULL, &size, &device) != noErr)
        return kAudioObjectUnknown;
    return device;
}

static bool is_default_output_uid(CFStringRef uid, AudioObjectID *device_out) {
    AudioObjectID device = default_output_device();
    if (!uid || device == kAudioObjectUnknown) return false;
    AudioObjectPropertyAddress address = {
        kAudioDevicePropertyDeviceUID, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    CFStringRef default_uid = NULL;
    UInt32 size = sizeof(default_uid);
    if (AudioObjectGetPropertyData(device, &address, 0, NULL, &size, &default_uid) != noErr || !default_uid)
        return false;
    bool same = CFStringCompare(uid, default_uid, 0) == kCFCompareEqualTo;
    CFRelease(default_uid);
    if (same && device_out) *device_out = device;
    return same;
}

static OSStatus AudioQueueNewOutput_hook(const AudioStreamBasicDescription *format, AudioQueueOutputCallback callback,
                                         void *user_data, CFRunLoopRef run_loop, CFStringRef run_loop_mode,
                                         UInt32 flags, AudioQueueRef *queue) {
    OSStatus status = AudioQueueNewOutput(format, callback, user_data, run_loop, run_loop_mode, flags, queue);
    if (status == noErr && queue && *queue) {
        pthread_mutex_lock(&audio_lock);
        track_queue(*queue, true);
        pthread_mutex_unlock(&audio_lock);
    }
    return status;
}
DYLD_INTERPOSE(AudioQueueNewOutput_hook, AudioQueueNewOutput);

static OSStatus AudioQueueDispose_hook(AudioQueueRef queue, Boolean immediate) {
    pthread_mutex_lock(&audio_lock);
    track_queue(queue, false);
    pthread_mutex_unlock(&audio_lock);
    return AudioQueueDispose(queue, immediate);
}
DYLD_INTERPOSE(AudioQueueDispose_hook, AudioQueueDispose);

static OSStatus AudioQueueSetProperty_hook(AudioQueueRef queue, AudioQueuePropertyID property,
                                           const void *data, UInt32 size) {
    if (property == kAudioQueueProperty_CurrentDevice && data && size == sizeof(CFStringRef) && audio_fix_enabled()) {
        pthread_mutex_lock(&audio_lock);
        bool output = contains_queue(queue);
        pthread_mutex_unlock(&audio_lock);
        AudioObjectID device = kAudioObjectUnknown;
        if (output && is_default_output_uid(*(const CFStringRef *)data, &device)) {
            // A queue with no explicit device plays on the system default output.
            pthread_mutex_lock(&audio_lock);
            add_unpinned_device(device);
            pthread_mutex_unlock(&audio_lock);
            t7_log("[BO3MacFix] Audio: output follows the system default device.\n");
            return noErr;
        }
    }
    return AudioQueueSetProperty(queue, property, data, size);
}
DYLD_INTERPOSE(AudioQueueSetProperty_hook, AudioQueueSetProperty);

static bool skip_alive_listener(AudioObjectID object, const AudioObjectPropertyAddress *address) {
    if (!address || address->mSelector != kAudioDevicePropertyDeviceIsAlive) return false;
    pthread_mutex_lock(&audio_lock);
    bool unpinned = contains_device(object);
    pthread_mutex_unlock(&audio_lock);
    return unpinned;
}

static OSStatus AudioObjectAddPropertyListener_hook(AudioObjectID object, const AudioObjectPropertyAddress *address,
                                                    AudioObjectPropertyListenerProc listener, void *client_data) {
    if (skip_alive_listener(object, address)) return noErr;
    return AudioObjectAddPropertyListener(object, address, listener, client_data);
}
DYLD_INTERPOSE(AudioObjectAddPropertyListener_hook, AudioObjectAddPropertyListener);

static OSStatus AudioObjectRemovePropertyListener_hook(AudioObjectID object, const AudioObjectPropertyAddress *address,
                                                       AudioObjectPropertyListenerProc listener, void *client_data) {
    if (skip_alive_listener(object, address)) return noErr;
    return AudioObjectRemovePropertyListener(object, address, listener, client_data);
}
DYLD_INTERPOSE(AudioObjectRemovePropertyListener_hook, AudioObjectRemovePropertyListener);
