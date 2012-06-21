/* ALSAStreamOps.cpp
 **
 ** Copyright 2008-2009 Wind River Systems
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

#include <errno.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>

#define LOG_TAG "AudioHardwareALSA"
//#define LOG_NDEBUG 0
#include <utils/Log.h>
#include <utils/String8.h>

#include <cutils/properties.h>
#include <media/AudioRecord.h>
#include <hardware_legacy/power.h>

#include "AudioHardwareALSA.h"
#include "AudioRoute.h"

#define DEVICE_OUT_BLUETOOTH_SCO_ALL (AudioSystem::DEVICE_OUT_BLUETOOTH_SCO | AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_HEADSET | AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_CARKIT)

#define DEFAULT_SAMPLE_RATE     44100
#define DEFAULT_BUFFER_SIZE     (DEFAULT_SAMPLE_RATE/ 5)

namespace android_audio_legacy
{

// ----------------------------------------------------------------------------

static const char* heasetPmDownDelaySysFile = "//sys//devices//ipc//msic_audio//Medfield Headset//pmdown_time";
static const char* speakerPmDownDelaySysFile = "//sys//devices//ipc//msic_audio//Medfield Speaker//pmdown_time";
static const char* voicePmDownDelaySysFile = "//sys//devices//ipc//msic_audio//Medfield Voice//pmdown_time";

ALSAStreamOps::ALSAStreamOps(AudioHardwareALSA *parent, alsa_handle_t *handle, const char* pcLockTag) :
    mParent(parent),
    mHandle(handle),
    mStandby(false),
    mDevices(0),
    isResetted(false),
    mAudioRoute(NULL),
    mPowerLock(false),
    mPowerLockTag(pcLockTag)
{
}

ALSAStreamOps::~ALSAStreamOps()
{
    AutoW lock(mParent->mLock);

    close();
}

// use emulated popcount optimization
// http://www.df.lth.se/~john_e/gems/gem002d.html
static inline uint32_t popCount(uint32_t u)
{
    u = ((u&0x55555555) + ((u>>1)&0x55555555));
    u = ((u&0x33333333) + ((u>>2)&0x33333333));
    u = ((u&0x0f0f0f0f) + ((u>>4)&0x0f0f0f0f));
    u = ((u&0x00ff00ff) + ((u>>8)&0x00ff00ff));
    u = ( u&0x0000ffff) + (u>>16);
    return u;
}

acoustic_device_t *ALSAStreamOps::acoustics()
{
    return mParent->getAcousticHwDevice();
}
vpc_device_t *ALSAStreamOps::vpc()
{
   return mParent->getVpcHwDevice();
}

ALSAMixer *ALSAStreamOps::mixer()
{
    return mParent->mMixer;
}

status_t ALSAStreamOps::set(int      *format,
                            uint32_t *channels,
                            uint32_t *rate)
{
    bool bad_channels = false;
    bool bad_rate = false;
    bool bad_format = false;

    LOGD("set IN : format:%d channels:0x%x rate:%d", *format, *channels, *rate);

    if (channels) {
        if ( (*channels != 0) && ( mHandle->channels != popCount(*channels) ) ) {
            bad_channels = true;
        }

        // change channel value in those cases :
        //  - the channel is not set on entry or
        //  - the channel is not correctly set according to ALSA
        if ( (bad_channels) || (*channels == 0) )
        {
            *channels = 0;
            if (mHandle->devices & AudioSystem::DEVICE_OUT_ALL) {
                switch(mHandle->channels) {
                case 4:
                    *channels |= AudioSystem::CHANNEL_OUT_BACK_LEFT;
                    *channels |= AudioSystem::CHANNEL_OUT_BACK_RIGHT;
                    // Fall through...
                default:
                case 2:
                    *channels |= AudioSystem::CHANNEL_OUT_FRONT_RIGHT;
                    // Fall through...
                case 1:
                    *channels |= AudioSystem::CHANNEL_OUT_FRONT_LEFT;
                    break;
                }
            } else {
                switch(mHandle->channels) {
                default:
                case 2:
                    *channels |= AudioSystem::CHANNEL_IN_RIGHT;
                    // Fall through...
                case 1:
                    *channels |= AudioSystem::CHANNEL_IN_LEFT;
                    break;
                }
            }
            LOGD("set : change channels to 0x%x",  *channels);
        }
    }

    if (rate) {
        if ( (*rate != 0) && (mHandle->sampleRate != *rate) ) {
            bad_rate = false;
        }

        if ( (bad_rate) || (*rate == 0) ) {
            *rate = mHandle->sampleRate;
            LOGD("set : change rate to %d",  *rate);
        }
    }

    snd_pcm_format_t iformat = mHandle->format;

    int iActualFormat = AudioSystem::FORMAT_DEFAULT;

    switch(mHandle->format) {
    case SND_PCM_FORMAT_S16_LE:
        iActualFormat = AudioSystem::PCM_16_BIT;
        break;
    case SND_PCM_FORMAT_S8:
        iActualFormat = AudioSystem::PCM_8_BIT;
        break;
    default:
        LOGE("Unexpected Sample Format: %d", mHandle->format);
        return UNKNOWN_ERROR;
    }

    if (format) {
        if ( (*format != 0) && (iActualFormat != *format) ) {
            bad_format = true;
        }

        if ( (bad_format) || (*format == 0) ) {
            *format = iActualFormat;
            LOGD("set : change format to %d",  *format);
        }
    }

    return (bad_channels || bad_rate || bad_format) ? BAD_VALUE : NO_ERROR;
}

status_t ALSAStreamOps::setParameters(const String8& keyValuePairs)
{
    return NO_ERROR;
}

String8 ALSAStreamOps::getParameters(const String8& keys)
{
    AudioParameter param = AudioParameter(keys);
    String8 value;
    String8 key = String8(AudioParameter::keyRouting);

    if (param.get(key, value) == NO_ERROR) {
        param.addInt(key, (int)mHandle->curDev);
    }

    LOGV("getParameters() %s", param.toString().string());
    return param.toString();
}

uint32_t ALSAStreamOps::sampleRate() const
{
    return mHandle->sampleRate;
}

//
// Return the number of bytes (not frames)
//
size_t ALSAStreamOps::bufferSize() const
{
    snd_pcm_uframes_t bufferSize = mHandle->bufferSize;
    snd_pcm_uframes_t periodSize;

    size_t bytes;

    if(mHandle->handle) {
        snd_pcm_get_params(mHandle->handle, &bufferSize, &periodSize);

        bytes = static_cast<size_t>(snd_pcm_frames_to_bytes(mHandle->handle, bufferSize));
    } else
        bytes = DEFAULT_BUFFER_SIZE;
    // Not sure when this happened, but unfortunately it now
    // appears that the bufferSize must be reported as a
    // power of 2. This might be for OSS compatibility.
    for (size_t i = 1; (bytes & ~i) != 0; i<<=1)
        bytes &= ~i;

    return bytes;
}

int ALSAStreamOps::format() const
{
    int pcmFormatBitWidth;
    int audioSystemFormat;

    snd_pcm_format_t ALSAFormat = mHandle->format;

    pcmFormatBitWidth = snd_pcm_format_physical_width(ALSAFormat);
    switch(pcmFormatBitWidth) {
    case 8:
        audioSystemFormat = AudioSystem::PCM_8_BIT;
        break;

    default:
        LOG_FATAL("Unknown AudioSystem bit width %i!", pcmFormatBitWidth);

    case 16:
        audioSystemFormat = AudioSystem::PCM_16_BIT;
        break;
    }

    return audioSystemFormat;
}

uint32_t ALSAStreamOps::channels() const
{
    unsigned int count = mHandle->channels;
    uint32_t channels = 0;

    if (mHandle->curDev & AudioSystem::DEVICE_OUT_ALL)
        switch(count) {
        case 4:
            channels |= AudioSystem::CHANNEL_OUT_BACK_LEFT;
            channels |= AudioSystem::CHANNEL_OUT_BACK_RIGHT;
            // Fall through...
        default:
        case 2:
            channels |= AudioSystem::CHANNEL_OUT_FRONT_RIGHT;
            // Fall through...
        case 1:
            channels |= AudioSystem::CHANNEL_OUT_FRONT_LEFT;
            break;
        }
    else
        switch(count) {
        default:
        case 2:
            channels |= AudioSystem::CHANNEL_IN_RIGHT;
            // Fall through...
        case 1:
            channels |= AudioSystem::CHANNEL_IN_LEFT;
            break;
        }

    return channels;
}

void ALSAStreamOps::close()
{
 //   mParent->mALSADevice->close(mHandle);
    // Call unset stream from route instead
    if(mAudioRoute) {
        mAudioRoute->unsetStream(this, mHandle->curMode);
        mAudioRoute = NULL;
    }
}

void ALSAStreamOps::doClose()
{
    LOGD("ALSAStreamOps::doClose");

    if(mHandle->handle)
    {
        //if BT SCO path is used in normal mode: disable bt sco path
        if(isBluetoothScoNormalInUse())
        {
            mParent->getVpcHwDevice()->set_bt_sco_path(VPC_ROUTE_CLOSE);
        }
    }

    mParent->getAlsaHwDevice()->close(mHandle);

    releasePowerLock();
}

void ALSAStreamOps::doStandby()
{
    LOGD("%s",__FUNCTION__);

    standby();
}

status_t ALSAStreamOps::standby()
{
    AutoW lock(mParent->mLock);
    LOGD("%s",__FUNCTION__);

    if(mHandle->handle)
    {
        snd_pcm_drain (mHandle->handle);

        //if BT SCO path is used in normal mode: disable bt sco path
        if(isBluetoothScoNormalInUse())
        {
            mParent->getVpcHwDevice()->set_bt_sco_path(VPC_ROUTE_CLOSE);
        }
    }
    if(mParent->getVpcHwDevice() && mParent->getVpcHwDevice()->mix_disable)
        mParent->getVpcHwDevice()->mix_disable(isOut());



    if(mParent->getAlsaHwDevice() && mParent->getAlsaHwDevice()->standby)
        mParent->getAlsaHwDevice()->standby(mHandle);

    releasePowerLock();

    mStandby = true;

    return NO_ERROR;
}

//
// Set playback or capture PCM device.  It's possible to support audio output
// or input from multiple devices by using the ALSA plugins, but this is
// not supported for simplicity.
//
// The AudioHardwareALSA API does not allow one to set the input routing.
//
// If the "routes" value does not map to a valid device, the default playback
// device is used.
//
status_t ALSAStreamOps::open(uint32_t devices, int mode)
{
    assert(routeAvailable());
    assert(!mHandle->handle);

    status_t err = BAD_VALUE;
    err = mParent->getAlsaHwDevice()->open(mHandle, devices, mode, mParent->getFmRxMode());

    if(err == NO_ERROR)
    {
        //if BT SCO path is used in normal mode: enable bt sco path
        if(isBluetoothScoNormalInUse())
        {
            mParent->getVpcHwDevice()->set_bt_sco_path(VPC_ROUTE_OPEN);
        }
    }

    LOGD("ALSAStreamOps::open");
    return err;
}

bool ALSAStreamOps::routeAvailable()
{
    if(mAudioRoute)
        return mAudioRoute->available(isOut());

    return false;
}

void ALSAStreamOps::vpcRoute(uint32_t devices, int mode)
{
    if((mode == AudioSystem::MODE_IN_COMMUNICATION) && (devices & DEVICE_OUT_BLUETOOTH_SCO_ALL) &&
       mParent->getVpcHwDevice()) {
        LOGD("%s: BT Playback INCOMMUNICATION", __FUNCTION__);
        mParent->getVpcHwDevice()->route(VPC_ROUTE_OPEN);
    }
}

void ALSAStreamOps::vpcUnroute(uint32_t curDev, int curMode)
{
    if((curMode == AudioSystem::MODE_IN_COMMUNICATION) && (curDev & DEVICE_OUT_BLUETOOTH_SCO_ALL) &&
       mParent->getVpcHwDevice())
    {
        LOGD("%s", __FUNCTION__);
        mParent->getVpcHwDevice()->route(VPC_ROUTE_CLOSE);
    }
}

status_t ALSAStreamOps::setRoute(AudioRoute *audioRoute, uint32_t devices, int mode)
{
    LOGD("setRoute mode=%d", mode);
    if((mAudioRoute == audioRoute) && (mHandle->curDev == devices) && (mHandle->curMode == mode) &&
            (mParent->getFmRxMode() == mParent->getPrevFmRxMode()) &&
            !mParent->isReconsiderRoutingForced()) {
        LOGD("setRoute: stream already attached to the route, identical conditions");
        return NO_ERROR;
    }

    bool restore_needed = false;
    if((mParent->getFmRxMode() != mParent->getPrevFmRxMode() ||
		(mHandle->curMode != mode &&
		 (mode == AudioSystem::MODE_IN_CALL ||
          mHandle->curMode == AudioSystem::MODE_IN_CALL))) &&
	   isOut()) {
        storeAndResetPmDownDelay();
        restore_needed = true;
    }

    // unset stream from previous route (if any)
    if(mAudioRoute != NULL) {
        // unset the streams in the mode in which streams
        // were running until now (usefull for tied attribute)...
        mAudioRoute->unsetStream(this, mHandle->curMode);
    }

    mAudioRoute = audioRoute;
    mDevices = devices;

    // SetRoute is called with a NULL route, it only expects for an "unset" of the route
    if(!mAudioRoute)
        return NO_ERROR;

    // set stream to new route
    int ret = mAudioRoute->setStream(this, mode);

    if (ret != NO_ERROR) {

        LOGD("%s: error while routing the stream...", __FUNCTION__);
        // Error while routing the stream to its route, unset it!!!
        mAudioRoute->unsetStream(this, mode);
        mAudioRoute = NULL;
    }

    if(restore_needed) {
        restorePmDownDelay();
    }

    return ret;
}

status_t ALSAStreamOps::doRoute(int mode)
{
    status_t err = NO_ERROR;
    LOGD("doRoute mode=%d", mode);

    vpcRoute(mDevices, mode);

    err = open(mDevices, mode);
    if (err < 0) {
        LOGE("Cannot open alsa device(0x%x) in mode (%d)", mDevices, mode);
        return err;
    }

    if(mStandby) {
        LOGD("doRoute standby mode -> standby the device immediately after routing");
        doStandby();
    }
    return NO_ERROR;
}

status_t ALSAStreamOps::undoRoute()
{
    LOGD("doRoute undoRoute");

    int curMode = mHandle->curMode;
    int curDev = mHandle->curDev;
    doClose();

    vpcUnroute(curDev, curMode);

    return NO_ERROR;
}

int ALSAStreamOps::readSysEntry(const char* filePath)
{
    int fd;
    if ((fd = ::open(filePath, O_RDONLY)) < 0)
    {
        LOGE("Could not open file %s", filePath);
        return 0;
    }
    char buff[20];
    int val = 0;
    uint32_t count;
    if( (count = ::read(fd, buff, sizeof(buff)-1)) < 1 )
    {
        LOGE("Could not read file");
    }
    else
    {
        // Zero terminate string
        buff[count] = '\0';
        LOGD("read %d bytes = %s", count, buff);
        val = strtol(buff, NULL, 0);
    }

    ::close(fd);
    return val;
}


void ALSAStreamOps::writeSysEntry(const char* filePath, int value)
{
    int fd;

    if ((fd = ::open(filePath, O_WRONLY)) < 0)
    {
        LOGE("Could not open file %s", filePath);
        return ;
    }
    char buff[20];
    uint32_t count;
    snprintf(buff, sizeof(buff), "%d", value);
    if ((count = ::write(fd, buff, strlen(buff))) != strlen(buff))
        LOGE("could not write ret=%d", count);
    else
        LOGD("written %d bytes = %s", count, buff);
    ::close(fd);
}

void ALSAStreamOps::storeAndResetPmDownDelay()
{
    LOGD("storeAndResetPmDownDelay");
    if (!isResetted)
    {
        LOGD("storeAndResetPmDownDelay not resetted");
        headsetPmDownDelay = readSysEntry(heasetPmDownDelaySysFile);
        speakerPmDownDelay = readSysEntry(speakerPmDownDelaySysFile);
        voicePmDownDelay = readSysEntry(voicePmDownDelaySysFile);

        writeSysEntry(heasetPmDownDelaySysFile, 0);
        writeSysEntry(speakerPmDownDelaySysFile, 0);
        writeSysEntry(voicePmDownDelaySysFile, 0);

        isResetted = true;
    }
    else
        LOGD("storeAndResetPmDownDelay already resetted -> nothing to do");
}

void ALSAStreamOps::restorePmDownDelay()
{
    LOGD("restorePmDownDelay");
    if(isResetted)
    {
        LOGD("restorePmDownDelay restoring");
        writeSysEntry(heasetPmDownDelaySysFile, headsetPmDownDelay);
        writeSysEntry(speakerPmDownDelaySysFile, speakerPmDownDelay);
        writeSysEntry(voicePmDownDelaySysFile, voicePmDownDelay);

        isResetted = false;
    }
    else
        LOGD("restorePmDownDelay -> nothing to do");
}

bool ALSAStreamOps::isDeviceBluetoothSCO(uint32_t devices)
{
    if(isOut())
    {
        return (bool)(devices & DEVICE_OUT_BLUETOOTH_SCO_ALL);
    }
    else
    {
        return (devices == AudioSystem::DEVICE_IN_BLUETOOTH_SCO_HEADSET);
    }
}

bool ALSAStreamOps::isBluetoothScoNormalInUse()
{
    return (mParent->getVpcHwDevice() && mParent->getVpcHwDevice()->set_bt_sco_path &&
            isDeviceBluetoothSCO(mHandle->curDev) && (mHandle->curMode == AudioSystem::MODE_NORMAL));
}

void ALSAStreamOps::acquirePowerLock()
{
    if (!mPowerLock) {
        acquire_wake_lock (PARTIAL_WAKE_LOCK, mPowerLockTag);
        mPowerLock = true;
    }
}

void ALSAStreamOps::releasePowerLock()
{
    if (mPowerLock) {
        release_wake_lock (mPowerLockTag);
        mPowerLock = false;
    }
}

}       // namespace android
