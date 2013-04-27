/*
 **
 ** Copyright 2013 Intel Corporation
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

#define LOG_TAG "Resampler"

#include "Resampler.h"
#include <cutils/log.h>
#include <iasrc_resampler.h>
#include <limits.h>

#define base CAudioConverter

using namespace android;

namespace android_audio_legacy{

CResampler::CResampler(SampleSpecItem eSampleSpecItem) :
    base(eSampleSpecItem),
    mMaxFrameCnt(0),
    mContext(NULL), mFloatInp(NULL), mFloatOut(NULL)
{
}

CResampler::~CResampler()
{
    if (mContext) {
        iaresamplib_reset(mContext);
        iaresamplib_delete(&mContext);
    }

    delete []mFloatInp;
    delete []mFloatOut;
}

status_t CResampler::allocateBuffer()
{
    if (mMaxFrameCnt == 0) {
        mMaxFrameCnt = BUF_SIZE;
    } else {
        mMaxFrameCnt *= 2; // simply double the buf size
    }

    delete []mFloatInp;
    delete []mFloatOut;

    mFloatInp = new float[(mMaxFrameCnt + 1) * _ssSrc.getChannelCount()];
    mFloatOut = new float[(mMaxFrameCnt + 1) * _ssSrc.getChannelCount()];

    if (!mFloatInp || !mFloatOut) {

        LOGE("cannot allocate resampler tmp buffers.\n");
        delete []mFloatInp;
        delete []mFloatOut;

        return NO_MEMORY;
    }
    return NO_ERROR;
}

status_t CResampler::doConfigure(const CSampleSpec& ssSrc, const CSampleSpec& ssDst)
{
    ALOGD("%s: SOURCE rate=%d format=%d channels=%d", __FUNCTION__, ssSrc.getSampleRate(), ssSrc.getFormat(), ssSrc.getChannelCount());
    ALOGD("%s: DST rate=%d format=%d channels=%d", __FUNCTION__, ssDst.getSampleRate(), ssDst.getFormat(), ssDst.getChannelCount());

    if (ssSrc.getSampleRate() == _ssSrc.getSampleRate() &&
        ssDst.getSampleRate() == _ssDst.getSampleRate() && mContext) {

        return NO_ERROR;
    }

    status_t status = base::doConfigure(ssSrc, ssDst);
    if (status != NO_ERROR) {

        return status;
    }

    if (mContext) {
        iaresamplib_reset(mContext);
        iaresamplib_delete(&mContext);
        mContext = NULL;
    }

    if (!iaresamplib_supported_conversion(ssSrc.getSampleRate(), ssDst.getSampleRate())) {

        ALOGE("%s: SRC lib doesn't support this conversion", __FUNCTION__);
        return INVALID_OPERATION;
    }

    iaresamplib_new(&mContext, ssSrc.getChannelCount(), ssSrc.getSampleRate(), ssDst.getSampleRate());
    if (!mContext) {
        ALOGE("cannot create resampler handle for lacking of memory.\n");
        return BAD_VALUE;
    }

    _pfnConvertSamples = (SampleConverter)(&CResampler::resampleFrames);
    return NO_ERROR;
}

void CResampler::convert_short_2_float(int16_t *inp, float * out, size_t sz) const
{
    size_t i;
    for (i = 0; i < sz; i++) {
        *out++ = (float) *inp++;
    }
}

void CResampler::convert_float_2_short(float *inp, int16_t * out, size_t sz) const
{
    size_t i;
    for (i = 0; i < sz; i++) {
        if (*inp > SHRT_MAX) {
            *inp = SHRT_MAX;
        } else if (*inp < SHRT_MIN) {
            *inp = SHRT_MIN;
        }
        *out++ = (short) *inp++;
    }
}

status_t CResampler::resampleFrames(const void *in, void *out, const uint32_t inFrames, uint32_t *outFrames)
{
    size_t outFrameCount = convertSrcToDstInFrames(inFrames);

    while (outFrameCount > mMaxFrameCnt) {

        status_t ret = allocateBuffer();
        if (ret != NO_ERROR) {

            ALOGE("%s: could not allocate memory for resampling operation", __FUNCTION__);
            return ret;
        }
    }
    unsigned int out_n_frames;
    convert_short_2_float((short*)in, mFloatInp, inFrames * _ssSrc.getChannelCount());
    iaresamplib_process_float(mContext, mFloatInp, inFrames, mFloatOut, &out_n_frames);
    convert_float_2_short(mFloatOut, (short*)out, out_n_frames * _ssSrc.getChannelCount());

    *outFrames = out_n_frames;

    return NO_ERROR;
}

}; // namespace android
