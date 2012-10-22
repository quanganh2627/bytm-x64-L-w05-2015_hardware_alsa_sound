/* AudioExternalRouteModemIA.cpp
 **
 ** Copyright 2012 Intel Corporation
 **
 ** Licensed under the Apache License, Version 2.0 (the "License");
 ** you may not use this file except in compliance with the License.
 ** You may obtain a copy of the License at
 **
 **      http://www.apache.org/licenses/LICENSE-2.0
 **
 ** Unless required by applicable law or agreed to in writing, software
 ** distributed under the License is distributed on an "AS IS" BASIS,
 ** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 ** See the License for the specific language governing permissions and
 ** limitations under the License.
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "AudioExternalRouteModemIA"

#include "AudioExternalRouteModemIA.h"

#define DEVICE_OUT_BUILTIN_ALL (AudioSystem::DEVICE_OUT_EARPIECE | \
                    AudioSystem::DEVICE_OUT_SPEAKER | \
                    AudioSystem::DEVICE_OUT_WIRED_HEADSET | \
                    AudioSystem::DEVICE_OUT_WIRED_HEADPHONE)

#define base    CAudioExternalRoute

namespace android_audio_legacy
{

const string strRouteName = "AudioExternalRouteModemIA";

CAudioExternalRouteModemIA::CAudioExternalRouteModemIA(uint32_t uiRouteId, CAudioPlatformState *platformState) :
    CAudioExternalRoute(uiRouteId, strRouteName, platformState)
{
}

bool CAudioExternalRouteModemIA::isApplicable(uint32_t devices, int mode, bool bIsOut)
{
    if (mode == AudioSystem::MODE_IN_CALL) {

        // Only condition to enable this route is to have a voice call active
        // whatever the stream direction
        return _pPlatformState->isModemAudioAvailable();
    }

    return false;
}

}       // namespace android