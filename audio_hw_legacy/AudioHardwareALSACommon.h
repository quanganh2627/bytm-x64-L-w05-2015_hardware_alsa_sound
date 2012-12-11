/* AudioHardwareALSACommon.h
 **
 ** Copyright 2012 Intel Corporation
 **
 ** Licensed under the Apache License, Version 2.0 (the "License");
 ** you may not use this file except in compliance with the License.
 ** You may obtain a copy of the License at
 **
 **     http://www.apache.org/licenses/LICENSE-2.0
 **
 ** Unless required by applicable law or agreed to in writing, software
 ** distributed under the License is distributed on an "AS IS" BASIS,
 ** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 ** See the License for the specific language governing permissions and
 ** limitations under the License.
 */

#ifndef ANDROID_AUDIO_HARDWARE_ALSA_COMMON_H
#define ANDROID_AUDIO_HARDWARE_ALSA_COMMON_H

#include <hardware_legacy/AudioHardwareBase.h>

#include <alsa/asoundlib.h>

#include <hardware/hardware.h>
#include <vpc_hardware.h>

#define NULL_IN_DEVICE 0

using namespace android;

namespace android_audio_legacy
{

/**
 * The id of ALSA module
 */
#define ALSA_HARDWARE_MODULE_ID "alsa"
#define ALSA_HARDWARE_NAME      "alsa"

struct alsa_device_t;

struct alsa_handle_t {
    alsa_device_t *     module;
    uint32_t            devices;
    uint32_t            curDev;
    int                 curMode;
    snd_pcm_t *         handle;
    snd_pcm_format_t    format;
    uint32_t            channels;
    uint32_t            sampleRate;
    unsigned int        latency;         // Delay in usec
    unsigned int        wait_timeoutMs;  // Timeout for snd_pcm_wait function
    unsigned int        bufferSize;      // Size of sample buffer
    uint32_t            periodTime;
    void *              modPrivate;
    bool                openFlag;        // If handle is opened then openFlag = 1
    int                 curFmRxMode;
    bool                isOutput;
};

struct alsa_device_t {
    hw_device_t common;

    // Methods
    status_t (*init)(alsa_device_t *, uint32_t defaultInputSampleRate, uint32_t defaultOutputSampleRate);
    status_t (*open)(alsa_handle_t *, uint32_t, int, int);
    status_t (*standby)(alsa_handle_t *);
    status_t (*close)(alsa_handle_t *);
    status_t (*volume)(alsa_handle_t *, uint32_t, float);
    status_t (*initStream)(alsa_handle_t *, uint32_t, int, int);
    int      (*wait_pcm)(alsa_handle_t *);
};

/**
 * The id of acoustics module
 */
#define ACOUSTICS_HARDWARE_MODULE_ID    "acoustics"
#define ACOUSTICS_HARDWARE_NAME         "acoustics"

struct acoustic_device_t {
    hw_device_t common;

    // Required methods...
    status_t (*use_handle)(acoustic_device_t *, alsa_handle_t *);
    status_t (*cleanup)(acoustic_device_t *);

    status_t (*set_params)(acoustic_device_t *, AudioSystem::audio_in_acoustics, void *);

    // Optional methods...
    ssize_t (*read)(acoustic_device_t *, void *, size_t);
    ssize_t (*write)(acoustic_device_t *, const void *, size_t);
    status_t (*recover)(acoustic_device_t *, int);

    void *              modPrivate;
};

// ----------------------------------------------------------------------------

};        // namespace android
#endif    // ANDROID_AUDIO_HARDWARE_ALSA_COMMON_H
