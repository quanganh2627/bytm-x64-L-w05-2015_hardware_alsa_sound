/*
**
** Copyright 2007, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
*
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS, 
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#include <cutils/properties.h>
#include <string.h>
#include <unistd.h>

#define LOG_TAG "AudioHardwareInterface"
#include <utils/Log.h>
#include <utils/String8.h>

#include "AudioHardwareALSA.h"

#if 0
#ifdef WITH_A2DP
#include "A2dpAudioInterface.h"
#endif

#ifdef ENABLE_AUDIO_DUMP
#include "AudioDumpInterface.h"
#endif

#endif


// change to 1 to log routing calls
#define LOG_ROUTING_CALLS 1

namespace android_audio_legacy {

#if LOG_ROUTING_CALLS
static const char* routingModeStrings[] =
{
    "OUT OF RANGE",
    "INVALID",
    "CURRENT",
    "NORMAL",
    "RINGTONE",
    "IN_CALL",
    "IN_COMMUNICATION"
};

static const char* displayMode(int mode)
{
    if ((mode < AudioSystem::MODE_INVALID) || (mode >= AudioSystem::NUM_MODES))
        return routingModeStrings[0];
    return routingModeStrings[mode+3];
}
#endif

AudioHardwareInterface* AudioHardwareInterface::create()
{
    return NULL;
}

AudioStreamOut::~AudioStreamOut()
{
}

// default implementation is unsupported
status_t AudioStreamOut::getNextWriteTimestamp(int64_t __UNUSED *timestamp)
{
    return INVALID_OPERATION;
}

AudioStreamIn::~AudioStreamIn() {}

AudioHardwareBase::AudioHardwareBase()
{
    mMode = 0;
    mFmRxMode = AudioSystem::MODE_FM_OFF;
    mPrevFmRxMode = AudioSystem::MODE_FM_OFF;
}

status_t AudioHardwareBase::setMode(int mode)
{
#if LOG_ROUTING_CALLS
    ALOGD("setMode(%s)", displayMode(mode));
#endif
    if ((mode < 0) || (mode >= AudioSystem::NUM_MODES))
        return BAD_VALUE;
    if (mMode == mode)
        return ALREADY_EXISTS;
    mMode = mode;
    return NO_ERROR;
}

status_t AudioHardwareBase::setFmRxMode(int mode)
{
#if LOG_ROUTING_CALLS
    ALOGD("setFmRxMode(%s)", (mode == AudioSystem::MODE_FM_ON) ? "FM ON" : "FM OFF");
#endif
    if ((mode < 0) || (mode >= AudioSystem::MODE_FM_NUM))
        return BAD_VALUE;
    if (mFmRxMode == mode)
        return ALREADY_EXISTS;
    mPrevFmRxMode = mFmRxMode;
    mFmRxMode = mode;
    return NO_ERROR;
}

// default implementation
status_t AudioHardwareBase::setParameters(const String8 __UNUSED &keyValuePairs)
{
    return NO_ERROR;
}

// default implementation
String8 AudioHardwareBase::getParameters(const String8& keys)
{
    AudioParameter param = AudioParameter(keys);
    return param.toString();
}

// default implementation
size_t AudioHardwareBase::getInputBufferSize(uint32_t sampleRate, int format, int channelCount)
{
    if (sampleRate != 8000) {
        ALOGW("getInputBufferSize bad sampling rate: %d", sampleRate);
        return 0;
    }
    if (format != AudioSystem::PCM_16_BIT) {
        ALOGW("getInputBufferSize bad format: %d", format);
        return 0;
    }
    if (channelCount != 1) {
        ALOGW("getInputBufferSize bad channel count: %d", channelCount);
        return 0;
    }

    return 320;
}

// default implementation is unsupported
status_t AudioHardwareBase::getMasterVolume(float __UNUSED *volume)
{
    return INVALID_OPERATION;
}

status_t AudioHardwareBase::dumpState(int fd, const Vector<String16>& args)
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;
    snprintf(buffer, sizeof(buffer), "AudioHardwareBase::dumpState\n");
    result.append(buffer);
    snprintf(buffer, sizeof(buffer), "\tmMode: %d\n", mMode);
    result.append(buffer);
    ::write(fd, result.string(), result.size());
    dump(fd, args);  // Dump the state of the concrete child.
    return NO_ERROR;
}

}; // namespace android