/* AudioConversion.cpp
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

#define LOG_TAG "AudioConversion"
//#define LOG_NDEBUG 0

#include <stdlib.h>
#include <string.h>
#include <cutils/log.h>

#include "AudioBufferProvider.h"
#include "AudioConversion.h"
#include "AudioConverter.h"
#include "AudioReformatter.h"
#include "AudioRemapper.h"
#include "AudioResampler.h"
#include "AudioUtils.h"

namespace android_audio_legacy{

const uint32_t CAudioConversion::MAX_RATE = 92000;

const uint32_t CAudioConversion::MIN_RATE = 8000;

// ----------------------------------------------------------------------------

CAudioConversion::CAudioConversion() :
    _ulConvOutBufferIndex(0),
    _ulConvOutFrames(0),
    _ulConvOutBufferSizeInFrames(0),
    _pConvOutBuffer(NULL)
{
    _apAudioConverter[EChannelCountSampleSpecItem] = new CAudioRemapper(EChannelCountSampleSpecItem);
    _apAudioConverter[EFormatSampleSpecItem] = new CAudioReformatter(EFormatSampleSpecItem);
    _apAudioConverter[ERateSampleSpecItem] = new CAudioResampler(ERateSampleSpecItem);
}

CAudioConversion::~CAudioConversion()
{
    for (int i = 0; i < ENbSampleSpecItems; i++) {

        delete _apAudioConverter[i];
    }

    free(_pConvOutBuffer);

}


//
// This function configures the chain of converter that is required to convert
// samples from ssSrc to ssDst sample specifications.
//
// To optimize the convertion and make the processing as light as possible, the
// order of converter is important.
// This function will call the recursive function configureAndAddConverter starting
// from the remapper operation (ie the converter working on the number of channels),
// then the reformatter operation (ie converter changing the format of the samples),
// and finally the resampler (ie converter changing the sample rate).
//
status_t CAudioConversion::configure(const CSampleSpec& ssSrc, const CSampleSpec& ssDst)
{
    LOGD("%s", __FUNCTION__);
    status_t ret = NO_ERROR;

    emptyConversionChain();

    free(_pConvOutBuffer);

    _pConvOutBuffer = NULL;
    _ulConvOutBufferIndex = 0;
    _ulConvOutFrames = 0;
    _ulConvOutBufferSizeInFrames = 0;

    _ssSrc = ssSrc;
    _ssDst = ssDst;

    if (ssSrc == ssDst) {

        LOGD("%s: no convertion required", __FUNCTION__);
        return ret;
    }

    LOGD("%s: SOURCE rate=%d format=%d channels=%d", __FUNCTION__, ssSrc.getSampleRate(), ssSrc.getFormat(), ssSrc.getChannelCount());
    LOGD("%s: DST rate=%d format=%d channels=%d", __FUNCTION__, ssDst.getSampleRate(), ssDst.getFormat(), ssDst.getChannelCount());

    CSampleSpec tmpSsSrc = ssSrc;

    // Start by adding the remapper, it will add consequently the reformatter and resampler
    // This function may alter the source sample spec
    ret = configureAndAddConverter(EChannelCountSampleSpecItem, &tmpSsSrc, &ssDst);
    if (ret != NO_ERROR) {

        return ret;
    }

    // Assert the temporary sample spec equals the destination sample spec
    assert(tmpSsSrc == ssDst);

    return ret;
}

//
// Convert must output outFrames
// AudioFlinger expects outFrames to be read/written, not less, as it does not
// care about the returned bytes of the read/write function
//
status_t CAudioConversion::getConvertedBuffer(void *dst, const uint32_t outFrames, AudioBufferProvider* pBufferProvider)
{
    assert(pBufferProvider);
    assert(dst);

    status_t status = NO_ERROR;

    if (_activeAudioConvList.empty()) {

        LOGE("%s: conversion called with empty converter list", __FUNCTION__);
        return NO_INIT;
    }

    //
    // Realloc the Output of the conversion if required (with margin of the worst case)
    //
    if (_ulConvOutBufferSizeInFrames < outFrames) {

        _ulConvOutBufferSizeInFrames = outFrames + (MAX_RATE / MIN_RATE) * 2;
        _pConvOutBuffer = (int16_t *)realloc(_pConvOutBuffer,
                                              _ssDst.convertFramesToBytes(_ulConvOutBufferSizeInFrames));
    }

    size_t framesRequested = outFrames;

    //
    // Frames are already available from the ConvOutBuffer, empty it first!
    //
    if (_ulConvOutFrames) {

        size_t frameToCopy = min(framesRequested, _ulConvOutFrames);
        framesRequested -= frameToCopy;
        _ulConvOutBufferIndex += _ssDst.convertFramesToBytes(frameToCopy);
    }

    //
    // Frames still needed? (_pConvOutBuffer emptied!)
    //
    while (framesRequested) {

        //
        // Outputs in the convOutBuffer
        //
        AudioBufferProvider::Buffer& buffer(_pConvInBuffer);

        // Calculate the frames we need to get from buffer provider
        // (Runs at ssSrc sample spec)
        // Note that is is rounded up.
        buffer.frameCount = CAudioUtils::convertSrcToDstInFrames(framesRequested, _ssDst, _ssSrc);

        //
        // Acquire next buffer from buffer provider
        //
        status = pBufferProvider->getNextBuffer(&buffer);
        if (status != NO_ERROR) {

            return status;
        }

        //
        // Convert
        //
        size_t convertedFrames;
        char *convBuf = (char*)_pConvOutBuffer + _ulConvOutBufferIndex;
        status = convert(buffer.raw, (void**)&convBuf, buffer.frameCount, &convertedFrames);
        if (status != NO_ERROR) {

            pBufferProvider->releaseBuffer(&buffer);
            return status;
        }

        _ulConvOutFrames += convertedFrames;
        _ulConvOutBufferIndex += _ssDst.convertFramesToBytes(convertedFrames);

        size_t framesToCopy = min(framesRequested, convertedFrames);
        framesRequested -= framesToCopy;

        //
        // Release the buffer
        //
        pBufferProvider->releaseBuffer(&buffer);
    }

    //
    // Copy requested outFrames from the output buffer of the conversion.
    // Move the remaining frames to the beginning of the convOut buffer
    //
    memcpy(dst, _pConvOutBuffer, _ssDst.convertFramesToBytes(outFrames));
    _ulConvOutFrames -= outFrames;

    //
    // Move the remaining frames to the beginning of the convOut buffer
    //
    if (_ulConvOutFrames) {

        memmove(_pConvOutBuffer, (char*)_pConvOutBuffer + _ssDst.convertFramesToBytes(outFrames), _ssDst.convertFramesToBytes(_ulConvOutFrames));
    }

    // Reset buffer Index
    _ulConvOutBufferIndex = 0;

    return status;
}

status_t CAudioConversion::convert(const void* src, void** dst, const uint32_t inFrames, uint32_t* outFrames)
{
    void *srcBuf = (void* )src;
    void *dstBuf = NULL;
    size_t srcFrames = inFrames;
    size_t dstFrames = 0;
    status_t status = NO_ERROR;

    if (_activeAudioConvList.empty()) {

        // Empty converter list -> No need for convertion
        // Copy the input on the ouput if provided by the client
        // or points on the imput buffer
        if (*dst) {

            memcpy(*dst, src, _ssSrc.convertFramesToBytes(inFrames));
            *outFrames = inFrames;
        } else {

            *dst = (void* )src;
            *outFrames = inFrames;
        }
        return NO_ERROR;
    }

    AudioConverterListIterator it;
    for (it = _activeAudioConvList.begin(); it != _activeAudioConvList.end(); ++it) {

        CAudioConverter* pConv = *it;
        dstBuf = NULL;
        dstFrames = 0;

        if (*dst && pConv == _activeAudioConvList.back()) {

            // Last converter must output within the provided buffer (if provided!!!)
            dstBuf = *dst;
        }
        status = pConv->convert(srcBuf, &dstBuf, srcFrames, &dstFrames);
        if (status != NO_ERROR) {

            return status;
        }
        srcBuf = dstBuf;
        srcFrames = dstFrames;
    }
    *dst = dstBuf;
    *outFrames = dstFrames;

    return status;
}

void CAudioConversion::emptyConversionChain()
{
    _activeAudioConvList.clear();
}


//
// This function pushes the converter to the list
// and alters the source sample spec according to the sample spec reached
// after this convertion.
//
// Lets take an example:
// ssSrc = {a, b, c} and ssDst = {a', b', c'}
//
// Our converter works on sample spec item b
// After the converter, temporary destination sample spec will be: {a, b', c}
//
// Update the source Sample Spec to this temporary sample spec for the
// next convertion that might have to be added.
// ssSrc = temp dest = {a, b', c}
//
status_t CAudioConversion::doConfigureAndAddConverter(SampleSpecItem eSampleSpecItem, CSampleSpec* pSsSrc, const CSampleSpec* pSsDst)
{
    assert(eSampleSpecItem < ENbSampleSpecItems);

    CSampleSpec tmpSsDst = *pSsSrc;
    tmpSsDst.setSampleSpecItem(eSampleSpecItem, pSsDst->getSampleSpecItem(eSampleSpecItem));

    LOGD("%s: SOURCE rate=%d format=%d channels=%d", __FUNCTION__, pSsSrc->getSampleRate(), pSsSrc->getFormat(), pSsSrc->getChannelCount());
    LOGD("%s: DST rate=%d format=%d channels=%d", __FUNCTION__, tmpSsDst.getSampleRate(), tmpSsDst.getFormat(), tmpSsDst.getChannelCount());

    status_t ret = _apAudioConverter[eSampleSpecItem]->doConfigure(*pSsSrc, tmpSsDst);
    if (ret != NO_ERROR) {

        return ret;
    }
    _activeAudioConvList.push_back(_apAudioConverter[eSampleSpecItem]);
    *pSsSrc = tmpSsDst;

    return NO_ERROR;
}

//
// Recursive function to add converters to the chain of convertion required.
//
// When a converter is added, the source sample specification is altered to reflects
// the sample spec reached after this convertion. This sample spec will be used
// as the source for next convertion.
//
// Let's take an example:
// ssSrc = {a, b, c} and ssDst = {a', b', c'} with (a' > a) and (b' < b)
// As all sample spec items are different, we need to use 3 resamplers:
//
// First take into account a (number of channels):
//      As a' is higher than a, first performs the remapping:
//      ssSrc = {a, b, c} dst = {a', b', c'}
//      The temporary output becomes the new source for next converter
//      ssSrc = temporary Output = {a', b, c}
//
// Then, take into account b (format size).
//      As b' is lower than b, do not perform the reformating now...
//
// Finally, take into account the sample rate:
//      as they are different, use a resampler:
//      ssSrc = {a', b, c} dst = {a', b', c'}
//      The temporary output becomes the new source for next converter
//      ssSrc = temporary Output = {a', b, c'}
//
// No more converter: exit from last recursive call
// Taking into account b again...(format size)
//      as b' < b, use a reformatter
//      ssSrc = {a', b, c'} dst = {a', b', c'}
//      The temporary output becomes the new source for next converter
//      ssSrc = temporary Output = {a', b', c'}
//
// Exit from recursive call
//
status_t CAudioConversion::configureAndAddConverter(SampleSpecItem eSampleSpecItem, CSampleSpec* pSsSrc, const CSampleSpec* pSsDst)
{
    status_t ret;
    assert(eSampleSpecItem < ENbSampleSpecItems);

    // If the input format size is higher, first perform the reformat
    // then add the resampler
    // and perform the reformat (if not already done)
    if (pSsSrc->getSampleSpecItem(eSampleSpecItem) > pSsDst->getSampleSpecItem(eSampleSpecItem)) {

        ret = doConfigureAndAddConverter(eSampleSpecItem, pSsSrc, pSsDst);
        if (ret != NO_ERROR) {

            return ret;
        }
    }

    if ((eSampleSpecItem + 1) < ENbSampleSpecItems) {
        // Dive
        ret = configureAndAddConverter((SampleSpecItem)(eSampleSpecItem + 1), pSsSrc, pSsDst);
        if (ret != NO_ERROR) {

            return ret;
        }
    }

    if (pSsSrc->getSampleSpecItem(eSampleSpecItem) < pSsDst->getSampleSpecItem(eSampleSpecItem)) {

        ret = doConfigureAndAddConverter(eSampleSpecItem, pSsSrc, pSsDst);
        if (ret != NO_ERROR) {

            return ret;
        }
    }
    return NO_ERROR;
}

// ----------------------------------------------------------------------------
}; // namespace android
