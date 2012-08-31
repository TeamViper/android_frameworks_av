/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "Camera2::Parameters"
#define ATRACE_TAG ATRACE_TAG_CAMERA
//#define LOG_NDEBUG 0

#include <math.h>
#include <stdlib.h>

#include "Parameters.h"
#include "system/camera.h"
#include "camera/CameraParameters.h"

namespace android {
namespace camera2 {

Parameters::Parameters(int cameraId,
        int cameraFacing) :
        cameraId(cameraId),
        cameraFacing(cameraFacing),
        info(NULL) {
}

Parameters::~Parameters() {
}

status_t Parameters::initialize(const CameraMetadata *info) {
    status_t res;

    if (info->entryCount() == 0) {
        ALOGE("%s: No static information provided!", __FUNCTION__);
        return BAD_VALUE;
    }
    Parameters::info = info;

    res = buildFastInfo();
    if (res != OK) return res;

    CameraParameters params;

    camera_metadata_ro_entry_t availableProcessedSizes =
        staticInfo(ANDROID_SCALER_AVAILABLE_PROCESSED_SIZES, 2);
    if (!availableProcessedSizes.count) return NO_INIT;

    // TODO: Pick more intelligently
    previewWidth = availableProcessedSizes.data.i32[0];
    previewHeight = availableProcessedSizes.data.i32[1];
    videoWidth = previewWidth;
    videoHeight = previewHeight;

    params.setPreviewSize(previewWidth, previewHeight);
    params.setVideoSize(videoWidth, videoHeight);
    params.set(CameraParameters::KEY_PREFERRED_PREVIEW_SIZE_FOR_VIDEO,
            String8::format("%dx%d",
                    previewWidth, previewHeight));
    {
        String8 supportedPreviewSizes;
        for (size_t i=0; i < availableProcessedSizes.count; i += 2) {
            if (i != 0) supportedPreviewSizes += ",";
            supportedPreviewSizes += String8::format("%dx%d",
                    availableProcessedSizes.data.i32[i],
                    availableProcessedSizes.data.i32[i+1]);
        }
        params.set(CameraParameters::KEY_SUPPORTED_PREVIEW_SIZES,
                supportedPreviewSizes);
        params.set(CameraParameters::KEY_SUPPORTED_VIDEO_SIZES,
                supportedPreviewSizes);
    }

    camera_metadata_ro_entry_t availableFpsRanges =
        staticInfo(ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES, 2);
    if (!availableFpsRanges.count) return NO_INIT;

    previewFpsRange[0] = availableFpsRanges.data.i32[0];
    previewFpsRange[1] = availableFpsRanges.data.i32[1];

    params.set(CameraParameters::KEY_PREVIEW_FPS_RANGE,
            String8::format("%d,%d",
                    previewFpsRange[0],
                    previewFpsRange[1]));

    {
        String8 supportedPreviewFpsRange;
        for (size_t i=0; i < availableFpsRanges.count; i += 2) {
            if (i != 0) supportedPreviewFpsRange += ",";
            supportedPreviewFpsRange += String8::format("(%d,%d)",
                    availableFpsRanges.data.i32[i],
                    availableFpsRanges.data.i32[i+1]);
        }
        params.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FPS_RANGE,
                supportedPreviewFpsRange);
    }

    previewFormat = HAL_PIXEL_FORMAT_YCrCb_420_SP;
    params.set(CameraParameters::KEY_PREVIEW_FORMAT,
            formatEnumToString(previewFormat)); // NV21

    previewTransform = degToTransform(0,
            cameraFacing == CAMERA_FACING_FRONT);

    camera_metadata_ro_entry_t availableFormats =
        staticInfo(ANDROID_SCALER_AVAILABLE_FORMATS);

    {
        String8 supportedPreviewFormats;
        bool addComma = false;
        for (size_t i=0; i < availableFormats.count; i++) {
            if (addComma) supportedPreviewFormats += ",";
            addComma = true;
            switch (availableFormats.data.i32[i]) {
            case HAL_PIXEL_FORMAT_YCbCr_422_SP:
                supportedPreviewFormats +=
                    CameraParameters::PIXEL_FORMAT_YUV422SP;
                break;
            case HAL_PIXEL_FORMAT_YCrCb_420_SP:
                supportedPreviewFormats +=
                    CameraParameters::PIXEL_FORMAT_YUV420SP;
                break;
            case HAL_PIXEL_FORMAT_YCbCr_422_I:
                supportedPreviewFormats +=
                    CameraParameters::PIXEL_FORMAT_YUV422I;
                break;
            case HAL_PIXEL_FORMAT_YV12:
                supportedPreviewFormats +=
                    CameraParameters::PIXEL_FORMAT_YUV420P;
                break;
            case HAL_PIXEL_FORMAT_RGB_565:
                supportedPreviewFormats +=
                    CameraParameters::PIXEL_FORMAT_RGB565;
                break;
            case HAL_PIXEL_FORMAT_RGBA_8888:
                supportedPreviewFormats +=
                    CameraParameters::PIXEL_FORMAT_RGBA8888;
                break;
            // Not advertizing JPEG, RAW_SENSOR, etc, for preview formats
            case HAL_PIXEL_FORMAT_RAW_SENSOR:
            case HAL_PIXEL_FORMAT_BLOB:
                addComma = false;
                break;

            default:
                ALOGW("%s: Camera %d: Unknown preview format: %x",
                        __FUNCTION__, cameraId, availableFormats.data.i32[i]);
                addComma = false;
                break;
            }
        }
        params.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FORMATS,
                supportedPreviewFormats);
    }

    // PREVIEW_FRAME_RATE / SUPPORTED_PREVIEW_FRAME_RATES are deprecated, but
    // still have to do something sane for them

    params.set(CameraParameters::KEY_PREVIEW_FRAME_RATE,
            previewFpsRange[0]);

    {
        String8 supportedPreviewFrameRates;
        for (size_t i=0; i < availableFpsRanges.count; i += 2) {
            if (i != 0) supportedPreviewFrameRates += ",";
            supportedPreviewFrameRates += String8::format("%d",
                    availableFpsRanges.data.i32[i]);
        }
        params.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FRAME_RATES,
                supportedPreviewFrameRates);
    }

    camera_metadata_ro_entry_t availableJpegSizes =
        staticInfo(ANDROID_SCALER_AVAILABLE_JPEG_SIZES, 2);
    if (!availableJpegSizes.count) return NO_INIT;

    // TODO: Pick maximum
    pictureWidth = availableJpegSizes.data.i32[0];
    pictureHeight = availableJpegSizes.data.i32[1];

    params.setPictureSize(pictureWidth,
            pictureHeight);

    {
        String8 supportedPictureSizes;
        for (size_t i=0; i < availableJpegSizes.count; i += 2) {
            if (i != 0) supportedPictureSizes += ",";
            supportedPictureSizes += String8::format("%dx%d",
                    availableJpegSizes.data.i32[i],
                    availableJpegSizes.data.i32[i+1]);
        }
        params.set(CameraParameters::KEY_SUPPORTED_PICTURE_SIZES,
                supportedPictureSizes);
    }

    params.setPictureFormat(CameraParameters::PIXEL_FORMAT_JPEG);
    params.set(CameraParameters::KEY_SUPPORTED_PICTURE_FORMATS,
            CameraParameters::PIXEL_FORMAT_JPEG);

    camera_metadata_ro_entry_t availableJpegThumbnailSizes =
        staticInfo(ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES, 4);
    if (!availableJpegThumbnailSizes.count) return NO_INIT;

    // TODO: Pick default thumbnail size sensibly
    jpegThumbSize[0] = availableJpegThumbnailSizes.data.i32[0];
    jpegThumbSize[1] = availableJpegThumbnailSizes.data.i32[1];

    params.set(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH,
            jpegThumbSize[0]);
    params.set(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT,
            jpegThumbSize[1]);

    {
        String8 supportedJpegThumbSizes;
        for (size_t i=0; i < availableJpegThumbnailSizes.count; i += 2) {
            if (i != 0) supportedJpegThumbSizes += ",";
            supportedJpegThumbSizes += String8::format("%dx%d",
                    availableJpegThumbnailSizes.data.i32[i],
                    availableJpegThumbnailSizes.data.i32[i+1]);
        }
        params.set(CameraParameters::KEY_SUPPORTED_JPEG_THUMBNAIL_SIZES,
                supportedJpegThumbSizes);
    }

    jpegThumbQuality = 90;
    params.set(CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY,
            jpegThumbQuality);
    jpegQuality = 90;
    params.set(CameraParameters::KEY_JPEG_QUALITY,
            jpegQuality);
    jpegRotation = 0;
    params.set(CameraParameters::KEY_ROTATION,
            jpegRotation);

    gpsEnabled = false;
    gpsProcessingMethod = "unknown";
    // GPS fields in CameraParameters are not set by implementation

    wbMode = ANDROID_CONTROL_AWB_AUTO;
    params.set(CameraParameters::KEY_WHITE_BALANCE,
            CameraParameters::WHITE_BALANCE_AUTO);

    camera_metadata_ro_entry_t availableWhiteBalanceModes =
        staticInfo(ANDROID_CONTROL_AWB_AVAILABLE_MODES);
    {
        String8 supportedWhiteBalance;
        bool addComma = false;
        for (size_t i=0; i < availableWhiteBalanceModes.count; i++) {
            if (addComma) supportedWhiteBalance += ",";
            addComma = true;
            switch (availableWhiteBalanceModes.data.u8[i]) {
            case ANDROID_CONTROL_AWB_AUTO:
                supportedWhiteBalance +=
                    CameraParameters::WHITE_BALANCE_AUTO;
                break;
            case ANDROID_CONTROL_AWB_INCANDESCENT:
                supportedWhiteBalance +=
                    CameraParameters::WHITE_BALANCE_INCANDESCENT;
                break;
            case ANDROID_CONTROL_AWB_FLUORESCENT:
                supportedWhiteBalance +=
                    CameraParameters::WHITE_BALANCE_FLUORESCENT;
                break;
            case ANDROID_CONTROL_AWB_WARM_FLUORESCENT:
                supportedWhiteBalance +=
                    CameraParameters::WHITE_BALANCE_WARM_FLUORESCENT;
                break;
            case ANDROID_CONTROL_AWB_DAYLIGHT:
                supportedWhiteBalance +=
                    CameraParameters::WHITE_BALANCE_DAYLIGHT;
                break;
            case ANDROID_CONTROL_AWB_CLOUDY_DAYLIGHT:
                supportedWhiteBalance +=
                    CameraParameters::WHITE_BALANCE_CLOUDY_DAYLIGHT;
                break;
            case ANDROID_CONTROL_AWB_TWILIGHT:
                supportedWhiteBalance +=
                    CameraParameters::WHITE_BALANCE_TWILIGHT;
                break;
            case ANDROID_CONTROL_AWB_SHADE:
                supportedWhiteBalance +=
                    CameraParameters::WHITE_BALANCE_SHADE;
                break;
            // Skipping values not mappable to v1 API
            case ANDROID_CONTROL_AWB_OFF:
                addComma = false;
                break;
            default:
                ALOGW("%s: Camera %d: Unknown white balance value: %d",
                        __FUNCTION__, cameraId,
                        availableWhiteBalanceModes.data.u8[i]);
                addComma = false;
                break;
            }
        }
        params.set(CameraParameters::KEY_SUPPORTED_WHITE_BALANCE,
                supportedWhiteBalance);
    }

    effectMode = ANDROID_CONTROL_EFFECT_OFF;
    params.set(CameraParameters::KEY_EFFECT,
            CameraParameters::EFFECT_NONE);

    camera_metadata_ro_entry_t availableEffects =
        staticInfo(ANDROID_CONTROL_AVAILABLE_EFFECTS);
    if (!availableEffects.count) return NO_INIT;
    {
        String8 supportedEffects;
        bool addComma = false;
        for (size_t i=0; i < availableEffects.count; i++) {
            if (addComma) supportedEffects += ",";
            addComma = true;
            switch (availableEffects.data.u8[i]) {
                case ANDROID_CONTROL_EFFECT_OFF:
                    supportedEffects +=
                        CameraParameters::EFFECT_NONE;
                    break;
                case ANDROID_CONTROL_EFFECT_MONO:
                    supportedEffects +=
                        CameraParameters::EFFECT_MONO;
                    break;
                case ANDROID_CONTROL_EFFECT_NEGATIVE:
                    supportedEffects +=
                        CameraParameters::EFFECT_NEGATIVE;
                    break;
                case ANDROID_CONTROL_EFFECT_SOLARIZE:
                    supportedEffects +=
                        CameraParameters::EFFECT_SOLARIZE;
                    break;
                case ANDROID_CONTROL_EFFECT_SEPIA:
                    supportedEffects +=
                        CameraParameters::EFFECT_SEPIA;
                    break;
                case ANDROID_CONTROL_EFFECT_POSTERIZE:
                    supportedEffects +=
                        CameraParameters::EFFECT_POSTERIZE;
                    break;
                case ANDROID_CONTROL_EFFECT_WHITEBOARD:
                    supportedEffects +=
                        CameraParameters::EFFECT_WHITEBOARD;
                    break;
                case ANDROID_CONTROL_EFFECT_BLACKBOARD:
                    supportedEffects +=
                        CameraParameters::EFFECT_BLACKBOARD;
                    break;
                case ANDROID_CONTROL_EFFECT_AQUA:
                    supportedEffects +=
                        CameraParameters::EFFECT_AQUA;
                    break;
                default:
                    ALOGW("%s: Camera %d: Unknown effect value: %d",
                        __FUNCTION__, cameraId, availableEffects.data.u8[i]);
                    addComma = false;
                    break;
            }
        }
        params.set(CameraParameters::KEY_SUPPORTED_EFFECTS, supportedEffects);
    }

    antibandingMode = ANDROID_CONTROL_AE_ANTIBANDING_AUTO;
    params.set(CameraParameters::KEY_ANTIBANDING,
            CameraParameters::ANTIBANDING_AUTO);

    camera_metadata_ro_entry_t availableAntibandingModes =
        staticInfo(ANDROID_CONTROL_AE_AVAILABLE_ANTIBANDING_MODES);
    if (!availableAntibandingModes.count) return NO_INIT;
    {
        String8 supportedAntibanding;
        bool addComma = false;
        for (size_t i=0; i < availableAntibandingModes.count; i++) {
            if (addComma) supportedAntibanding += ",";
            addComma = true;
            switch (availableAntibandingModes.data.u8[i]) {
                case ANDROID_CONTROL_AE_ANTIBANDING_OFF:
                    supportedAntibanding +=
                        CameraParameters::ANTIBANDING_OFF;
                    break;
                case ANDROID_CONTROL_AE_ANTIBANDING_50HZ:
                    supportedAntibanding +=
                        CameraParameters::ANTIBANDING_50HZ;
                    break;
                case ANDROID_CONTROL_AE_ANTIBANDING_60HZ:
                    supportedAntibanding +=
                        CameraParameters::ANTIBANDING_60HZ;
                    break;
                case ANDROID_CONTROL_AE_ANTIBANDING_AUTO:
                    supportedAntibanding +=
                        CameraParameters::ANTIBANDING_AUTO;
                    break;
                default:
                    ALOGW("%s: Camera %d: Unknown antibanding value: %d",
                        __FUNCTION__, cameraId,
                            availableAntibandingModes.data.u8[i]);
                    addComma = false;
                    break;
            }
        }
        params.set(CameraParameters::KEY_SUPPORTED_ANTIBANDING,
                supportedAntibanding);
    }

    sceneMode = ANDROID_CONTROL_OFF;
    params.set(CameraParameters::KEY_SCENE_MODE,
            CameraParameters::SCENE_MODE_AUTO);

    camera_metadata_ro_entry_t availableSceneModes =
        staticInfo(ANDROID_CONTROL_AVAILABLE_SCENE_MODES);
    if (!availableSceneModes.count) return NO_INIT;
    {
        String8 supportedSceneModes(CameraParameters::SCENE_MODE_AUTO);
        bool addComma = true;
        bool noSceneModes = false;
        for (size_t i=0; i < availableSceneModes.count; i++) {
            if (addComma) supportedSceneModes += ",";
            addComma = true;
            switch (availableSceneModes.data.u8[i]) {
                case ANDROID_CONTROL_SCENE_MODE_UNSUPPORTED:
                    noSceneModes = true;
                    break;
                case ANDROID_CONTROL_SCENE_MODE_FACE_PRIORITY:
                    // Not in old API
                    addComma = false;
                    break;
                case ANDROID_CONTROL_SCENE_MODE_ACTION:
                    supportedSceneModes +=
                        CameraParameters::SCENE_MODE_ACTION;
                    break;
                case ANDROID_CONTROL_SCENE_MODE_PORTRAIT:
                    supportedSceneModes +=
                        CameraParameters::SCENE_MODE_PORTRAIT;
                    break;
                case ANDROID_CONTROL_SCENE_MODE_LANDSCAPE:
                    supportedSceneModes +=
                        CameraParameters::SCENE_MODE_LANDSCAPE;
                    break;
                case ANDROID_CONTROL_SCENE_MODE_NIGHT:
                    supportedSceneModes +=
                        CameraParameters::SCENE_MODE_NIGHT;
                    break;
                case ANDROID_CONTROL_SCENE_MODE_NIGHT_PORTRAIT:
                    supportedSceneModes +=
                        CameraParameters::SCENE_MODE_NIGHT_PORTRAIT;
                    break;
                case ANDROID_CONTROL_SCENE_MODE_THEATRE:
                    supportedSceneModes +=
                        CameraParameters::SCENE_MODE_THEATRE;
                    break;
                case ANDROID_CONTROL_SCENE_MODE_BEACH:
                    supportedSceneModes +=
                        CameraParameters::SCENE_MODE_BEACH;
                    break;
                case ANDROID_CONTROL_SCENE_MODE_SNOW:
                    supportedSceneModes +=
                        CameraParameters::SCENE_MODE_SNOW;
                    break;
                case ANDROID_CONTROL_SCENE_MODE_SUNSET:
                    supportedSceneModes +=
                        CameraParameters::SCENE_MODE_SUNSET;
                    break;
                case ANDROID_CONTROL_SCENE_MODE_STEADYPHOTO:
                    supportedSceneModes +=
                        CameraParameters::SCENE_MODE_STEADYPHOTO;
                    break;
                case ANDROID_CONTROL_SCENE_MODE_FIREWORKS:
                    supportedSceneModes +=
                        CameraParameters::SCENE_MODE_FIREWORKS;
                    break;
                case ANDROID_CONTROL_SCENE_MODE_SPORTS:
                    supportedSceneModes +=
                        CameraParameters::SCENE_MODE_SPORTS;
                    break;
                case ANDROID_CONTROL_SCENE_MODE_PARTY:
                    supportedSceneModes +=
                        CameraParameters::SCENE_MODE_PARTY;
                    break;
                case ANDROID_CONTROL_SCENE_MODE_CANDLELIGHT:
                    supportedSceneModes +=
                        CameraParameters::SCENE_MODE_CANDLELIGHT;
                    break;
                case ANDROID_CONTROL_SCENE_MODE_BARCODE:
                    supportedSceneModes +=
                        CameraParameters::SCENE_MODE_BARCODE;
                    break;
                default:
                    ALOGW("%s: Camera %d: Unknown scene mode value: %d",
                        __FUNCTION__, cameraId,
                            availableSceneModes.data.u8[i]);
                    addComma = false;
                    break;
            }
        }
        if (!noSceneModes) {
            params.set(CameraParameters::KEY_SUPPORTED_SCENE_MODES,
                    supportedSceneModes);
        }
    }

    camera_metadata_ro_entry_t flashAvailable =
        staticInfo(ANDROID_FLASH_AVAILABLE, 1, 1);
    if (!flashAvailable.count) return NO_INIT;

    camera_metadata_ro_entry_t availableAeModes =
        staticInfo(ANDROID_CONTROL_AE_AVAILABLE_MODES);
    if (!availableAeModes.count) return NO_INIT;

    if (flashAvailable.data.u8[0]) {
        flashMode = Parameters::FLASH_MODE_AUTO;
        params.set(CameraParameters::KEY_FLASH_MODE,
                CameraParameters::FLASH_MODE_AUTO);

        String8 supportedFlashModes(CameraParameters::FLASH_MODE_OFF);
        supportedFlashModes = supportedFlashModes +
            "," + CameraParameters::FLASH_MODE_AUTO +
            "," + CameraParameters::FLASH_MODE_ON +
            "," + CameraParameters::FLASH_MODE_TORCH;
        for (size_t i=0; i < availableAeModes.count; i++) {
            if (availableAeModes.data.u8[i] ==
                    ANDROID_CONTROL_AE_ON_AUTO_FLASH_REDEYE) {
                supportedFlashModes = supportedFlashModes + "," +
                    CameraParameters::FLASH_MODE_RED_EYE;
                break;
            }
        }
        params.set(CameraParameters::KEY_SUPPORTED_FLASH_MODES,
                supportedFlashModes);
    } else {
        flashMode = Parameters::FLASH_MODE_OFF;
        params.set(CameraParameters::KEY_FLASH_MODE,
                CameraParameters::FLASH_MODE_OFF);
        params.set(CameraParameters::KEY_SUPPORTED_FLASH_MODES,
                CameraParameters::FLASH_MODE_OFF);
    }

    camera_metadata_ro_entry_t minFocusDistance =
        staticInfo(ANDROID_LENS_MINIMUM_FOCUS_DISTANCE, 1, 1);
    if (!minFocusDistance.count) return NO_INIT;

    camera_metadata_ro_entry_t availableAfModes =
        staticInfo(ANDROID_CONTROL_AF_AVAILABLE_MODES);
    if (!availableAfModes.count) return NO_INIT;

    if (minFocusDistance.data.f[0] == 0) {
        // Fixed-focus lens
        focusMode = Parameters::FOCUS_MODE_FIXED;
        params.set(CameraParameters::KEY_FOCUS_MODE,
                CameraParameters::FOCUS_MODE_FIXED);
        params.set(CameraParameters::KEY_SUPPORTED_FOCUS_MODES,
                CameraParameters::FOCUS_MODE_FIXED);
    } else {
        focusMode = Parameters::FOCUS_MODE_AUTO;
        params.set(CameraParameters::KEY_FOCUS_MODE,
                CameraParameters::FOCUS_MODE_AUTO);
        String8 supportedFocusModes(CameraParameters::FOCUS_MODE_INFINITY);
        bool addComma = true;

        for (size_t i=0; i < availableAfModes.count; i++) {
            if (addComma) supportedFocusModes += ",";
            addComma = true;
            switch (availableAfModes.data.u8[i]) {
                case ANDROID_CONTROL_AF_AUTO:
                    supportedFocusModes +=
                        CameraParameters::FOCUS_MODE_AUTO;
                    break;
                case ANDROID_CONTROL_AF_MACRO:
                    supportedFocusModes +=
                        CameraParameters::FOCUS_MODE_MACRO;
                    break;
                case ANDROID_CONTROL_AF_CONTINUOUS_VIDEO:
                    supportedFocusModes +=
                        CameraParameters::FOCUS_MODE_CONTINUOUS_VIDEO;
                    break;
                case ANDROID_CONTROL_AF_CONTINUOUS_PICTURE:
                    supportedFocusModes +=
                        CameraParameters::FOCUS_MODE_CONTINUOUS_PICTURE;
                    break;
                case ANDROID_CONTROL_AF_EDOF:
                    supportedFocusModes +=
                        CameraParameters::FOCUS_MODE_EDOF;
                    break;
                // Not supported in old API
                case ANDROID_CONTROL_AF_OFF:
                    addComma = false;
                    break;
                default:
                    ALOGW("%s: Camera %d: Unknown AF mode value: %d",
                        __FUNCTION__, cameraId, availableAfModes.data.u8[i]);
                    addComma = false;
                    break;
            }
        }
        params.set(CameraParameters::KEY_SUPPORTED_FOCUS_MODES,
                supportedFocusModes);
    }

    camera_metadata_ro_entry_t max3aRegions =
        staticInfo(ANDROID_CONTROL_MAX_REGIONS, 1, 1);
    if (!max3aRegions.count) return NO_INIT;

    params.set(CameraParameters::KEY_MAX_NUM_FOCUS_AREAS,
            max3aRegions.data.i32[0]);
    params.set(CameraParameters::KEY_FOCUS_AREAS,
            "(0,0,0,0,0)");
    focusingAreas.clear();
    focusingAreas.add(Parameters::Area(0,0,0,0,0));

    camera_metadata_ro_entry_t availableFocalLengths =
        staticInfo(ANDROID_LENS_AVAILABLE_FOCAL_LENGTHS);
    if (!availableFocalLengths.count) return NO_INIT;

    float minFocalLength = availableFocalLengths.data.f[0];
    params.setFloat(CameraParameters::KEY_FOCAL_LENGTH, minFocalLength);

    camera_metadata_ro_entry_t sensorSize =
        staticInfo(ANDROID_SENSOR_PHYSICAL_SIZE, 2, 2);
    if (!sensorSize.count) return NO_INIT;

    // The fields of view here assume infinity focus, maximum wide angle
    float horizFov = 180 / M_PI *
            2 * atanf(sensorSize.data.f[0] / (2 * minFocalLength));
    float vertFov  = 180 / M_PI *
            2 * atanf(sensorSize.data.f[1] / (2 * minFocalLength));
    params.setFloat(CameraParameters::KEY_HORIZONTAL_VIEW_ANGLE, horizFov);
    params.setFloat(CameraParameters::KEY_VERTICAL_VIEW_ANGLE, vertFov);

    exposureCompensation = 0;
    params.set(CameraParameters::KEY_EXPOSURE_COMPENSATION,
                exposureCompensation);

    camera_metadata_ro_entry_t exposureCompensationRange =
        staticInfo(ANDROID_CONTROL_AE_EXP_COMPENSATION_RANGE, 2, 2);
    if (!exposureCompensationRange.count) return NO_INIT;

    params.set(CameraParameters::KEY_MAX_EXPOSURE_COMPENSATION,
            exposureCompensationRange.data.i32[1]);
    params.set(CameraParameters::KEY_MIN_EXPOSURE_COMPENSATION,
            exposureCompensationRange.data.i32[0]);

    camera_metadata_ro_entry_t exposureCompensationStep =
        staticInfo(ANDROID_CONTROL_AE_EXP_COMPENSATION_STEP, 1, 1);
    if (!exposureCompensationStep.count) return NO_INIT;

    params.setFloat(CameraParameters::KEY_EXPOSURE_COMPENSATION_STEP,
            (float)exposureCompensationStep.data.r[0].numerator /
            exposureCompensationStep.data.r[0].denominator);

    autoExposureLock = false;
    params.set(CameraParameters::KEY_AUTO_EXPOSURE_LOCK,
            CameraParameters::FALSE);
    params.set(CameraParameters::KEY_AUTO_EXPOSURE_LOCK_SUPPORTED,
            CameraParameters::TRUE);

    autoWhiteBalanceLock = false;
    params.set(CameraParameters::KEY_AUTO_WHITEBALANCE_LOCK,
            CameraParameters::FALSE);
    params.set(CameraParameters::KEY_AUTO_WHITEBALANCE_LOCK_SUPPORTED,
            CameraParameters::TRUE);

    meteringAreas.add(Parameters::Area(0, 0, 0, 0, 0));
    params.set(CameraParameters::KEY_MAX_NUM_METERING_AREAS,
            max3aRegions.data.i32[0]);
    params.set(CameraParameters::KEY_METERING_AREAS,
            "(0,0,0,0,0)");

    zoom = 0;
    params.set(CameraParameters::KEY_ZOOM, zoom);
    params.set(CameraParameters::KEY_MAX_ZOOM, NUM_ZOOM_STEPS - 1);

    camera_metadata_ro_entry_t maxDigitalZoom =
        staticInfo(ANDROID_SCALER_AVAILABLE_MAX_ZOOM, 1, 1);
    if (!maxDigitalZoom.count) return NO_INIT;

    {
        String8 zoomRatios;
        float zoom = 1.f;
        float zoomIncrement = (maxDigitalZoom.data.f[0] - zoom) /
                (NUM_ZOOM_STEPS-1);
        bool addComma = false;
        for (size_t i=0; i < NUM_ZOOM_STEPS; i++) {
            if (addComma) zoomRatios += ",";
            addComma = true;
            zoomRatios += String8::format("%d", static_cast<int>(zoom * 100));
            zoom += zoomIncrement;
        }
        params.set(CameraParameters::KEY_ZOOM_RATIOS, zoomRatios);
    }

    params.set(CameraParameters::KEY_ZOOM_SUPPORTED,
            CameraParameters::TRUE);
    params.set(CameraParameters::KEY_SMOOTH_ZOOM_SUPPORTED,
            CameraParameters::TRUE);

    params.set(CameraParameters::KEY_FOCUS_DISTANCES,
            "Infinity,Infinity,Infinity");

    params.set(CameraParameters::KEY_MAX_NUM_DETECTED_FACES_HW,
            fastInfo.maxFaces);
    params.set(CameraParameters::KEY_MAX_NUM_DETECTED_FACES_SW,
            0);

    params.set(CameraParameters::KEY_VIDEO_FRAME_FORMAT,
            CameraParameters::PIXEL_FORMAT_ANDROID_OPAQUE);

    params.set(CameraParameters::KEY_RECORDING_HINT,
            CameraParameters::FALSE);

    params.set(CameraParameters::KEY_VIDEO_SNAPSHOT_SUPPORTED,
            CameraParameters::TRUE);

    params.set(CameraParameters::KEY_VIDEO_STABILIZATION,
            CameraParameters::FALSE);

    camera_metadata_ro_entry_t availableVideoStabilizationModes =
        staticInfo(ANDROID_CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES);
    if (!availableVideoStabilizationModes.count) return NO_INIT;

    if (availableVideoStabilizationModes.count > 1) {
        params.set(CameraParameters::KEY_VIDEO_STABILIZATION_SUPPORTED,
                CameraParameters::TRUE);
    } else {
        params.set(CameraParameters::KEY_VIDEO_STABILIZATION_SUPPORTED,
                CameraParameters::FALSE);
    }

    // Set up initial state for non-Camera.Parameters state variables

    storeMetadataInBuffers = true;
    playShutterSound = true;
    enableFaceDetect = false;

    enableFocusMoveMessages = false;
    afTriggerCounter = 0;
    currentAfTriggerId = -1;

    previewCallbackFlags = 0;

    state = STOPPED;

    paramsFlattened = params.flatten();

    return OK;
}

status_t Parameters::buildFastInfo() {

    camera_metadata_ro_entry_t activeArraySize =
        staticInfo(ANDROID_SENSOR_ACTIVE_ARRAY_SIZE, 2, 2);
    if (!activeArraySize.count) return NO_INIT;
    int32_t arrayWidth = activeArraySize.data.i32[0];
    int32_t arrayHeight = activeArraySize.data.i32[1];

    camera_metadata_ro_entry_t availableFaceDetectModes =
        staticInfo(ANDROID_STATS_AVAILABLE_FACE_DETECT_MODES);
    if (!availableFaceDetectModes.count) return NO_INIT;

    uint8_t bestFaceDetectMode =
        ANDROID_STATS_FACE_DETECTION_OFF;
    for (size_t i = 0 ; i < availableFaceDetectModes.count; i++) {
        switch (availableFaceDetectModes.data.u8[i]) {
            case ANDROID_STATS_FACE_DETECTION_OFF:
                break;
            case ANDROID_STATS_FACE_DETECTION_SIMPLE:
                if (bestFaceDetectMode !=
                        ANDROID_STATS_FACE_DETECTION_FULL) {
                    bestFaceDetectMode =
                        ANDROID_STATS_FACE_DETECTION_SIMPLE;
                }
                break;
            case ANDROID_STATS_FACE_DETECTION_FULL:
                bestFaceDetectMode =
                    ANDROID_STATS_FACE_DETECTION_FULL;
                break;
            default:
                ALOGE("%s: Camera %d: Unknown face detect mode %d:",
                        __FUNCTION__, cameraId,
                        availableFaceDetectModes.data.u8[i]);
                return NO_INIT;
        }
    }

    camera_metadata_ro_entry_t maxFacesDetected =
        staticInfo(ANDROID_STATS_MAX_FACE_COUNT, 1, 1);
    if (!maxFacesDetected.count) return NO_INIT;

    int32_t maxFaces = maxFacesDetected.data.i32[0];

    fastInfo.arrayWidth = arrayWidth;
    fastInfo.arrayHeight = arrayHeight;
    fastInfo.bestFaceDetectMode = bestFaceDetectMode;
    fastInfo.maxFaces = maxFaces;
    return OK;
}

camera_metadata_ro_entry_t Parameters::staticInfo(uint32_t tag,
        size_t minCount, size_t maxCount) const {
    status_t res;
    camera_metadata_ro_entry_t entry = info->find(tag);

    if (CC_UNLIKELY( entry.count == 0 )) {
        const char* tagSection = get_camera_metadata_section_name(tag);
        if (tagSection == NULL) tagSection = "<unknown>";
        const char* tagName = get_camera_metadata_tag_name(tag);
        if (tagName == NULL) tagName = "<unknown>";

        ALOGE("Error finding static metadata entry '%s.%s' (%x)",
                tagSection, tagName, tag);
    } else if (CC_UNLIKELY(
            (minCount != 0 && entry.count < minCount) ||
            (maxCount != 0 && entry.count > maxCount) ) ) {
        const char* tagSection = get_camera_metadata_section_name(tag);
        if (tagSection == NULL) tagSection = "<unknown>";
        const char* tagName = get_camera_metadata_tag_name(tag);
        if (tagName == NULL) tagName = "<unknown>";
        ALOGE("Malformed static metadata entry '%s.%s' (%x):"
                "Expected between %d and %d values, but got %d values",
                tagSection, tagName, tag, minCount, maxCount, entry.count);
    }

    return entry;
}

status_t Parameters::set(const String8& params) {
    status_t res;

    CameraParameters newParams(params);

    // TODO: Currently ignoring any changes to supposedly read-only parameters
    // such as supported preview sizes, etc. Should probably produce an error if
    // they're changed.

    /** Extract and verify new parameters */

    size_t i;

    Parameters validatedParams(*this);

    // PREVIEW_SIZE
    newParams.getPreviewSize(&validatedParams.previewWidth,
            &validatedParams.previewHeight);

    if (validatedParams.previewWidth != previewWidth ||
            validatedParams.previewHeight != previewHeight) {
        if (state >= PREVIEW) {
            ALOGE("%s: Preview size cannot be updated when preview "
                    "is active! (Currently %d x %d, requested %d x %d",
                    __FUNCTION__,
                    previewWidth, previewHeight,
                    validatedParams.previewWidth, validatedParams.previewHeight);
            return BAD_VALUE;
        }
        camera_metadata_ro_entry_t availablePreviewSizes =
            staticInfo(ANDROID_SCALER_AVAILABLE_PROCESSED_SIZES);
        for (i = 0; i < availablePreviewSizes.count; i += 2 ) {
            if ((availablePreviewSizes.data.i32[i] ==
                    validatedParams.previewWidth) &&
                (availablePreviewSizes.data.i32[i+1] ==
                    validatedParams.previewHeight)) break;
        }
        if (i == availablePreviewSizes.count) {
            ALOGE("%s: Requested preview size %d x %d is not supported",
                    __FUNCTION__, validatedParams.previewWidth,
                    validatedParams.previewHeight);
            return BAD_VALUE;
        }
    }

    // PREVIEW_FPS_RANGE
    bool fpsRangeChanged = false;
    newParams.getPreviewFpsRange(&validatedParams.previewFpsRange[0],
            &validatedParams.previewFpsRange[1]);
    if (validatedParams.previewFpsRange[0] != previewFpsRange[0] ||
            validatedParams.previewFpsRange[1] != previewFpsRange[1]) {
        fpsRangeChanged = true;
        camera_metadata_ro_entry_t availablePreviewFpsRanges =
            staticInfo(ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES, 2);
        for (i = 0; i < availablePreviewFpsRanges.count; i += 2) {
            if ((availablePreviewFpsRanges.data.i32[i] ==
                    validatedParams.previewFpsRange[0]) &&
                (availablePreviewFpsRanges.data.i32[i+1] ==
                    validatedParams.previewFpsRange[1]) ) {
                break;
            }
        }
        if (i == availablePreviewFpsRanges.count) {
            ALOGE("%s: Requested preview FPS range %d - %d is not supported",
                __FUNCTION__, validatedParams.previewFpsRange[0],
                    validatedParams.previewFpsRange[1]);
            return BAD_VALUE;
        }
        validatedParams.previewFps = validatedParams.previewFpsRange[0];
    }

    // PREVIEW_FORMAT
    validatedParams.previewFormat =
            formatStringToEnum(newParams.getPreviewFormat());
    if (validatedParams.previewFormat != previewFormat) {
        if (state >= PREVIEW) {
            ALOGE("%s: Preview format cannot be updated when preview "
                    "is active!", __FUNCTION__);
            return BAD_VALUE;
        }
        camera_metadata_ro_entry_t availableFormats =
            staticInfo(ANDROID_SCALER_AVAILABLE_FORMATS);
        for (i = 0; i < availableFormats.count; i++) {
            if (availableFormats.data.i32[i] == validatedParams.previewFormat)
                break;
        }
        if (i == availableFormats.count) {
            ALOGE("%s: Requested preview format %s (0x%x) is not supported",
                    __FUNCTION__, newParams.getPreviewFormat(),
                    validatedParams.previewFormat);
            return BAD_VALUE;
        }
    }

    // PREVIEW_FRAME_RATE
    // Deprecated, only use if the preview fps range is unchanged this time.
    // The single-value FPS is the same as the minimum of the range.
    if (!fpsRangeChanged) {
        validatedParams.previewFps = newParams.getPreviewFrameRate();
        if (validatedParams.previewFps != previewFps) {
            camera_metadata_ro_entry_t availableFrameRates =
                staticInfo(ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES);
            for (i = 0; i < availableFrameRates.count; i+=2) {
                if (availableFrameRates.data.i32[i] ==
                        validatedParams.previewFps) break;
            }
            if (i == availableFrameRates.count) {
                ALOGE("%s: Requested preview frame rate %d is not supported",
                        __FUNCTION__, validatedParams.previewFps);
                return BAD_VALUE;
            }
            validatedParams.previewFpsRange[0] =
                    availableFrameRates.data.i32[i];
            validatedParams.previewFpsRange[1] =
                    availableFrameRates.data.i32[i+1];
        }
    }

    // PICTURE_SIZE
    newParams.getPictureSize(&validatedParams.pictureWidth,
            &validatedParams.pictureHeight);
    if (validatedParams.pictureWidth == pictureWidth ||
            validatedParams.pictureHeight == pictureHeight) {
        camera_metadata_ro_entry_t availablePictureSizes =
            staticInfo(ANDROID_SCALER_AVAILABLE_JPEG_SIZES);
        for (i = 0; i < availablePictureSizes.count; i+=2) {
            if ((availablePictureSizes.data.i32[i] ==
                    validatedParams.pictureWidth) &&
                (availablePictureSizes.data.i32[i+1] ==
                    validatedParams.pictureHeight)) break;
        }
        if (i == availablePictureSizes.count) {
            ALOGE("%s: Requested picture size %d x %d is not supported",
                    __FUNCTION__, validatedParams.pictureWidth,
                    validatedParams.pictureHeight);
            return BAD_VALUE;
        }
    }

    // JPEG_THUMBNAIL_WIDTH/HEIGHT
    validatedParams.jpegThumbSize[0] =
            newParams.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH);
    validatedParams.jpegThumbSize[1] =
            newParams.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT);
    if (validatedParams.jpegThumbSize[0] != jpegThumbSize[0] ||
            validatedParams.jpegThumbSize[1] != jpegThumbSize[1]) {
        camera_metadata_ro_entry_t availableJpegThumbSizes =
            staticInfo(ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES);
        for (i = 0; i < availableJpegThumbSizes.count; i+=2) {
            if ((availableJpegThumbSizes.data.i32[i] ==
                    validatedParams.jpegThumbSize[0]) &&
                (availableJpegThumbSizes.data.i32[i+1] ==
                    validatedParams.jpegThumbSize[1])) break;
        }
        if (i == availableJpegThumbSizes.count) {
            ALOGE("%s: Requested JPEG thumbnail size %d x %d is not supported",
                    __FUNCTION__, validatedParams.jpegThumbSize[0],
                    validatedParams.jpegThumbSize[1]);
            return BAD_VALUE;
        }
    }

    // JPEG_THUMBNAIL_QUALITY
    validatedParams.jpegThumbQuality =
            newParams.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY);
    if (validatedParams.jpegThumbQuality < 0 ||
            validatedParams.jpegThumbQuality > 100) {
        ALOGE("%s: Requested JPEG thumbnail quality %d is not supported",
                __FUNCTION__, validatedParams.jpegThumbQuality);
        return BAD_VALUE;
    }

    // JPEG_QUALITY
    validatedParams.jpegQuality =
            newParams.getInt(CameraParameters::KEY_JPEG_QUALITY);
    if (validatedParams.jpegQuality < 0 || validatedParams.jpegQuality > 100) {
        ALOGE("%s: Requested JPEG quality %d is not supported",
                __FUNCTION__, validatedParams.jpegQuality);
        return BAD_VALUE;
    }

    // ROTATION
    validatedParams.jpegRotation =
            newParams.getInt(CameraParameters::KEY_ROTATION);
    if (validatedParams.jpegRotation != 0 &&
            validatedParams.jpegRotation != 90 &&
            validatedParams.jpegRotation != 180 &&
            validatedParams.jpegRotation != 270) {
        ALOGE("%s: Requested picture rotation angle %d is not supported",
                __FUNCTION__, validatedParams.jpegRotation);
        return BAD_VALUE;
    }

    // GPS

    const char *gpsLatStr =
            newParams.get(CameraParameters::KEY_GPS_LATITUDE);
    if (gpsLatStr != NULL) {
        const char *gpsLongStr =
                newParams.get(CameraParameters::KEY_GPS_LONGITUDE);
        const char *gpsAltitudeStr =
                newParams.get(CameraParameters::KEY_GPS_ALTITUDE);
        const char *gpsTimeStr =
                newParams.get(CameraParameters::KEY_GPS_TIMESTAMP);
        const char *gpsProcMethodStr =
                newParams.get(CameraParameters::KEY_GPS_PROCESSING_METHOD);
        if (gpsLongStr == NULL ||
                gpsAltitudeStr == NULL ||
                gpsTimeStr == NULL ||
                gpsProcMethodStr == NULL) {
            ALOGE("%s: Incomplete set of GPS parameters provided",
                    __FUNCTION__);
            return BAD_VALUE;
        }
        char *endPtr;
        errno = 0;
        validatedParams.gpsCoordinates[0] = strtod(gpsLatStr, &endPtr);
        if (errno || endPtr == gpsLatStr) {
            ALOGE("%s: Malformed GPS latitude: %s", __FUNCTION__, gpsLatStr);
            return BAD_VALUE;
        }
        errno = 0;
        validatedParams.gpsCoordinates[1] = strtod(gpsLongStr, &endPtr);
        if (errno || endPtr == gpsLongStr) {
            ALOGE("%s: Malformed GPS longitude: %s", __FUNCTION__, gpsLongStr);
            return BAD_VALUE;
        }
        errno = 0;
        validatedParams.gpsCoordinates[2] = strtod(gpsAltitudeStr, &endPtr);
        if (errno || endPtr == gpsAltitudeStr) {
            ALOGE("%s: Malformed GPS altitude: %s", __FUNCTION__,
                    gpsAltitudeStr);
            return BAD_VALUE;
        }
        errno = 0;
        validatedParams.gpsTimestamp = strtoll(gpsTimeStr, &endPtr, 10);
        if (errno || endPtr == gpsTimeStr) {
            ALOGE("%s: Malformed GPS timestamp: %s", __FUNCTION__, gpsTimeStr);
            return BAD_VALUE;
        }
        validatedParams.gpsProcessingMethod = gpsProcMethodStr;

        validatedParams.gpsEnabled = true;
    } else {
        validatedParams.gpsEnabled = false;
    }

    // WHITE_BALANCE
    validatedParams.wbMode = wbModeStringToEnum(
        newParams.get(CameraParameters::KEY_WHITE_BALANCE) );
    if (validatedParams.wbMode != wbMode) {
        camera_metadata_ro_entry_t availableWbModes =
            staticInfo(ANDROID_CONTROL_AWB_AVAILABLE_MODES);
        for (i = 0; i < availableWbModes.count; i++) {
            if (validatedParams.wbMode == availableWbModes.data.u8[i]) break;
        }
        if (i == availableWbModes.count) {
            ALOGE("%s: Requested white balance mode %s is not supported",
                    __FUNCTION__,
                    newParams.get(CameraParameters::KEY_WHITE_BALANCE));
            return BAD_VALUE;
        }
    }

    // EFFECT
    validatedParams.effectMode = effectModeStringToEnum(
        newParams.get(CameraParameters::KEY_EFFECT) );
    if (validatedParams.effectMode != effectMode) {
        camera_metadata_ro_entry_t availableEffectModes =
            staticInfo(ANDROID_CONTROL_AVAILABLE_EFFECTS);
        for (i = 0; i < availableEffectModes.count; i++) {
            if (validatedParams.effectMode == availableEffectModes.data.u8[i]) break;
        }
        if (i == availableEffectModes.count) {
            ALOGE("%s: Requested effect mode \"%s\" is not supported",
                    __FUNCTION__,
                    newParams.get(CameraParameters::KEY_EFFECT) );
            return BAD_VALUE;
        }
    }

    // ANTIBANDING
    validatedParams.antibandingMode = abModeStringToEnum(
        newParams.get(CameraParameters::KEY_ANTIBANDING) );
    if (validatedParams.antibandingMode != antibandingMode) {
        camera_metadata_ro_entry_t availableAbModes =
            staticInfo(ANDROID_CONTROL_AE_AVAILABLE_ANTIBANDING_MODES);
        for (i = 0; i < availableAbModes.count; i++) {
            if (validatedParams.antibandingMode == availableAbModes.data.u8[i])
                break;
        }
        if (i == availableAbModes.count) {
            ALOGE("%s: Requested antibanding mode \"%s\" is not supported",
                    __FUNCTION__,
                    newParams.get(CameraParameters::KEY_ANTIBANDING));
            return BAD_VALUE;
        }
    }

    // SCENE_MODE
    validatedParams.sceneMode = sceneModeStringToEnum(
        newParams.get(CameraParameters::KEY_SCENE_MODE) );
    if (validatedParams.sceneMode != sceneMode &&
            validatedParams.sceneMode !=
            ANDROID_CONTROL_SCENE_MODE_UNSUPPORTED) {
        camera_metadata_ro_entry_t availableSceneModes =
            staticInfo(ANDROID_CONTROL_AVAILABLE_SCENE_MODES);
        for (i = 0; i < availableSceneModes.count; i++) {
            if (validatedParams.sceneMode == availableSceneModes.data.u8[i])
                break;
        }
        if (i == availableSceneModes.count) {
            ALOGE("%s: Requested scene mode \"%s\" is not supported",
                    __FUNCTION__,
                    newParams.get(CameraParameters::KEY_SCENE_MODE));
            return BAD_VALUE;
        }
    }

    // FLASH_MODE
    validatedParams.flashMode = flashModeStringToEnum(
        newParams.get(CameraParameters::KEY_FLASH_MODE) );
    if (validatedParams.flashMode != flashMode) {
        camera_metadata_ro_entry_t flashAvailable =
            staticInfo(ANDROID_FLASH_AVAILABLE, 1, 1);
        if (!flashAvailable.data.u8[0] &&
                validatedParams.flashMode != Parameters::FLASH_MODE_OFF) {
            ALOGE("%s: Requested flash mode \"%s\" is not supported: "
                    "No flash on device", __FUNCTION__,
                    newParams.get(CameraParameters::KEY_FLASH_MODE));
            return BAD_VALUE;
        } else if (validatedParams.flashMode == Parameters::FLASH_MODE_RED_EYE) {
            camera_metadata_ro_entry_t availableAeModes =
                staticInfo(ANDROID_CONTROL_AE_AVAILABLE_MODES);
            for (i = 0; i < availableAeModes.count; i++) {
                if (validatedParams.flashMode == availableAeModes.data.u8[i])
                    break;
            }
            if (i == availableAeModes.count) {
                ALOGE("%s: Requested flash mode \"%s\" is not supported",
                        __FUNCTION__,
                        newParams.get(CameraParameters::KEY_FLASH_MODE));
                return BAD_VALUE;
            }
        } else if (validatedParams.flashMode == -1) {
            ALOGE("%s: Requested flash mode \"%s\" is unknown",
                    __FUNCTION__,
                    newParams.get(CameraParameters::KEY_FLASH_MODE));
            return BAD_VALUE;
        }
    }

    // FOCUS_MODE
    validatedParams.focusMode = focusModeStringToEnum(
        newParams.get(CameraParameters::KEY_FOCUS_MODE));
    if (validatedParams.focusMode != focusMode) {
        validatedParams.currentAfTriggerId = -1;
        if (validatedParams.focusMode != Parameters::FOCUS_MODE_FIXED) {
            camera_metadata_ro_entry_t minFocusDistance =
                staticInfo(ANDROID_LENS_MINIMUM_FOCUS_DISTANCE);
            if (minFocusDistance.data.f[0] == 0) {
                ALOGE("%s: Requested focus mode \"%s\" is not available: "
                        "fixed focus lens",
                        __FUNCTION__,
                        newParams.get(CameraParameters::KEY_FOCUS_MODE));
                return BAD_VALUE;
            } else if (validatedParams.focusMode !=
                    Parameters::FOCUS_MODE_INFINITY) {
                camera_metadata_ro_entry_t availableFocusModes =
                    staticInfo(ANDROID_CONTROL_AF_AVAILABLE_MODES);
                for (i = 0; i < availableFocusModes.count; i++) {
                    if (validatedParams.focusMode ==
                            availableFocusModes.data.u8[i]) break;
                }
                if (i == availableFocusModes.count) {
                    ALOGE("%s: Requested focus mode \"%s\" is not supported",
                            __FUNCTION__,
                            newParams.get(CameraParameters::KEY_FOCUS_MODE));
                    return BAD_VALUE;
                }
            }
        }
    } else {
        validatedParams.currentAfTriggerId = currentAfTriggerId;
    }

    // FOCUS_AREAS
    res = parseAreas(newParams.get(CameraParameters::KEY_FOCUS_AREAS),
            &validatedParams.focusingAreas);
    size_t max3aRegions =
        (size_t)staticInfo(ANDROID_CONTROL_MAX_REGIONS, 1, 1).data.i32[0];
    if (res == OK) res = validateAreas(validatedParams.focusingAreas,
            max3aRegions);
    if (res != OK) {
        ALOGE("%s: Requested focus areas are malformed: %s",
                __FUNCTION__, newParams.get(CameraParameters::KEY_FOCUS_AREAS));
        return BAD_VALUE;
    }

    // EXPOSURE_COMPENSATION
    validatedParams.exposureCompensation =
        newParams.getInt(CameraParameters::KEY_EXPOSURE_COMPENSATION);
    camera_metadata_ro_entry_t exposureCompensationRange =
        staticInfo(ANDROID_CONTROL_AE_EXP_COMPENSATION_RANGE);
    if ((validatedParams.exposureCompensation <
            exposureCompensationRange.data.i32[0]) ||
        (validatedParams.exposureCompensation >
            exposureCompensationRange.data.i32[1])) {
        ALOGE("%s: Requested exposure compensation index is out of bounds: %d",
                __FUNCTION__, validatedParams.exposureCompensation);
        return BAD_VALUE;
    }

    // AUTO_EXPOSURE_LOCK (always supported)
    validatedParams.autoExposureLock = boolFromString(
        newParams.get(CameraParameters::KEY_AUTO_EXPOSURE_LOCK));

    // AUTO_WHITEBALANCE_LOCK (always supported)
    validatedParams.autoWhiteBalanceLock = boolFromString(
        newParams.get(CameraParameters::KEY_AUTO_WHITEBALANCE_LOCK));

    // METERING_AREAS
    res = parseAreas(newParams.get(CameraParameters::KEY_METERING_AREAS),
            &validatedParams.meteringAreas);
    if (res == OK) {
        res = validateAreas(validatedParams.meteringAreas, max3aRegions);
    }
    if (res != OK) {
        ALOGE("%s: Requested metering areas are malformed: %s",
                __FUNCTION__,
                newParams.get(CameraParameters::KEY_METERING_AREAS));
        return BAD_VALUE;
    }

    // ZOOM
    validatedParams.zoom = newParams.getInt(CameraParameters::KEY_ZOOM);
    if (validatedParams.zoom < 0 || validatedParams.zoom > (int)NUM_ZOOM_STEPS) {
        ALOGE("%s: Requested zoom level %d is not supported",
                __FUNCTION__, validatedParams.zoom);
        return BAD_VALUE;
    }

    // VIDEO_SIZE
    newParams.getVideoSize(&validatedParams.videoWidth,
            &validatedParams.videoHeight);
    if (validatedParams.videoWidth != videoWidth ||
            validatedParams.videoHeight != videoHeight) {
        if (state == RECORD) {
            ALOGE("%s: Video size cannot be updated when recording is active!",
                    __FUNCTION__);
            return BAD_VALUE;
        }
        camera_metadata_ro_entry_t availableVideoSizes =
            staticInfo(ANDROID_SCALER_AVAILABLE_PROCESSED_SIZES);
        for (i = 0; i < availableVideoSizes.count; i += 2 ) {
            if ((availableVideoSizes.data.i32[i] ==
                    validatedParams.videoWidth) &&
                (availableVideoSizes.data.i32[i+1] ==
                    validatedParams.videoHeight)) break;
        }
        if (i == availableVideoSizes.count) {
            ALOGE("%s: Requested video size %d x %d is not supported",
                    __FUNCTION__, validatedParams.videoWidth,
                    validatedParams.videoHeight);
            return BAD_VALUE;
        }
    }

    // RECORDING_HINT (always supported)
    validatedParams.recordingHint = boolFromString(
        newParams.get(CameraParameters::KEY_RECORDING_HINT) );

    // VIDEO_STABILIZATION
    validatedParams.videoStabilization = boolFromString(
        newParams.get(CameraParameters::KEY_VIDEO_STABILIZATION) );
    camera_metadata_ro_entry_t availableVideoStabilizationModes =
        staticInfo(ANDROID_CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES);
    if (validatedParams.videoStabilization &&
            availableVideoStabilizationModes.count == 1) {
        ALOGE("%s: Video stabilization not supported", __FUNCTION__);
    }

    /** Update internal parameters */

    validatedParams.paramsFlattened = params;
    *this = validatedParams;

    return OK;
}

const char* Parameters::getStateName(State state) {
#define CASE_ENUM_TO_CHAR(x) case x: return(#x); break;
    switch(state) {
        CASE_ENUM_TO_CHAR(DISCONNECTED)
        CASE_ENUM_TO_CHAR(STOPPED)
        CASE_ENUM_TO_CHAR(WAITING_FOR_PREVIEW_WINDOW)
        CASE_ENUM_TO_CHAR(PREVIEW)
        CASE_ENUM_TO_CHAR(RECORD)
        CASE_ENUM_TO_CHAR(STILL_CAPTURE)
        CASE_ENUM_TO_CHAR(VIDEO_SNAPSHOT)
        default:
            return "Unknown state!";
            break;
    }
#undef CASE_ENUM_TO_CHAR
}

int Parameters::formatStringToEnum(const char *format) {
    return
        !strcmp(format, CameraParameters::PIXEL_FORMAT_YUV422SP) ?
            HAL_PIXEL_FORMAT_YCbCr_422_SP : // NV16
        !strcmp(format, CameraParameters::PIXEL_FORMAT_YUV420SP) ?
            HAL_PIXEL_FORMAT_YCrCb_420_SP : // NV21
        !strcmp(format, CameraParameters::PIXEL_FORMAT_YUV422I) ?
            HAL_PIXEL_FORMAT_YCbCr_422_I :  // YUY2
        !strcmp(format, CameraParameters::PIXEL_FORMAT_YUV420P) ?
            HAL_PIXEL_FORMAT_YV12 :         // YV12
        !strcmp(format, CameraParameters::PIXEL_FORMAT_RGB565) ?
            HAL_PIXEL_FORMAT_RGB_565 :      // RGB565
        !strcmp(format, CameraParameters::PIXEL_FORMAT_RGBA8888) ?
            HAL_PIXEL_FORMAT_RGBA_8888 :    // RGB8888
        !strcmp(format, CameraParameters::PIXEL_FORMAT_BAYER_RGGB) ?
            HAL_PIXEL_FORMAT_RAW_SENSOR :   // Raw sensor data
        -1;
}

const char* Parameters::formatEnumToString(int format) {
    const char *fmt;
    switch(format) {
        case HAL_PIXEL_FORMAT_YCbCr_422_SP: // NV16
            fmt = CameraParameters::PIXEL_FORMAT_YUV422SP;
            break;
        case HAL_PIXEL_FORMAT_YCrCb_420_SP: // NV21
            fmt = CameraParameters::PIXEL_FORMAT_YUV420SP;
            break;
        case HAL_PIXEL_FORMAT_YCbCr_422_I: // YUY2
            fmt = CameraParameters::PIXEL_FORMAT_YUV422I;
            break;
        case HAL_PIXEL_FORMAT_YV12:        // YV12
            fmt = CameraParameters::PIXEL_FORMAT_YUV420P;
            break;
        case HAL_PIXEL_FORMAT_RGB_565:     // RGB565
            fmt = CameraParameters::PIXEL_FORMAT_RGB565;
            break;
        case HAL_PIXEL_FORMAT_RGBA_8888:   // RGBA8888
            fmt = CameraParameters::PIXEL_FORMAT_RGBA8888;
            break;
        case HAL_PIXEL_FORMAT_RAW_SENSOR:
            ALOGW("Raw sensor preview format requested.");
            fmt = CameraParameters::PIXEL_FORMAT_BAYER_RGGB;
            break;
        default:
            ALOGE("%s: Unknown preview format: %x",
                    __FUNCTION__,  format);
            fmt = NULL;
            break;
    }
    return fmt;
}

int Parameters::wbModeStringToEnum(const char *wbMode) {
    return
        !strcmp(wbMode, CameraParameters::WHITE_BALANCE_AUTO) ?
            ANDROID_CONTROL_AWB_AUTO :
        !strcmp(wbMode, CameraParameters::WHITE_BALANCE_INCANDESCENT) ?
            ANDROID_CONTROL_AWB_INCANDESCENT :
        !strcmp(wbMode, CameraParameters::WHITE_BALANCE_FLUORESCENT) ?
            ANDROID_CONTROL_AWB_FLUORESCENT :
        !strcmp(wbMode, CameraParameters::WHITE_BALANCE_WARM_FLUORESCENT) ?
            ANDROID_CONTROL_AWB_WARM_FLUORESCENT :
        !strcmp(wbMode, CameraParameters::WHITE_BALANCE_DAYLIGHT) ?
            ANDROID_CONTROL_AWB_DAYLIGHT :
        !strcmp(wbMode, CameraParameters::WHITE_BALANCE_CLOUDY_DAYLIGHT) ?
            ANDROID_CONTROL_AWB_CLOUDY_DAYLIGHT :
        !strcmp(wbMode, CameraParameters::WHITE_BALANCE_TWILIGHT) ?
            ANDROID_CONTROL_AWB_TWILIGHT :
        !strcmp(wbMode, CameraParameters::WHITE_BALANCE_SHADE) ?
            ANDROID_CONTROL_AWB_SHADE :
        -1;
}

int Parameters::effectModeStringToEnum(const char *effectMode) {
    return
        !strcmp(effectMode, CameraParameters::EFFECT_NONE) ?
            ANDROID_CONTROL_EFFECT_OFF :
        !strcmp(effectMode, CameraParameters::EFFECT_MONO) ?
            ANDROID_CONTROL_EFFECT_MONO :
        !strcmp(effectMode, CameraParameters::EFFECT_NEGATIVE) ?
            ANDROID_CONTROL_EFFECT_NEGATIVE :
        !strcmp(effectMode, CameraParameters::EFFECT_SOLARIZE) ?
            ANDROID_CONTROL_EFFECT_SOLARIZE :
        !strcmp(effectMode, CameraParameters::EFFECT_SEPIA) ?
            ANDROID_CONTROL_EFFECT_SEPIA :
        !strcmp(effectMode, CameraParameters::EFFECT_POSTERIZE) ?
            ANDROID_CONTROL_EFFECT_POSTERIZE :
        !strcmp(effectMode, CameraParameters::EFFECT_WHITEBOARD) ?
            ANDROID_CONTROL_EFFECT_WHITEBOARD :
        !strcmp(effectMode, CameraParameters::EFFECT_BLACKBOARD) ?
            ANDROID_CONTROL_EFFECT_BLACKBOARD :
        !strcmp(effectMode, CameraParameters::EFFECT_AQUA) ?
            ANDROID_CONTROL_EFFECT_AQUA :
        -1;
}

int Parameters::abModeStringToEnum(const char *abMode) {
    return
        !strcmp(abMode, CameraParameters::ANTIBANDING_AUTO) ?
            ANDROID_CONTROL_AE_ANTIBANDING_AUTO :
        !strcmp(abMode, CameraParameters::ANTIBANDING_OFF) ?
            ANDROID_CONTROL_AE_ANTIBANDING_OFF :
        !strcmp(abMode, CameraParameters::ANTIBANDING_50HZ) ?
            ANDROID_CONTROL_AE_ANTIBANDING_50HZ :
        !strcmp(abMode, CameraParameters::ANTIBANDING_60HZ) ?
            ANDROID_CONTROL_AE_ANTIBANDING_60HZ :
        -1;
}

int Parameters::sceneModeStringToEnum(const char *sceneMode) {
    return
        !strcmp(sceneMode, CameraParameters::SCENE_MODE_AUTO) ?
            ANDROID_CONTROL_SCENE_MODE_UNSUPPORTED :
        !strcmp(sceneMode, CameraParameters::SCENE_MODE_ACTION) ?
            ANDROID_CONTROL_SCENE_MODE_ACTION :
        !strcmp(sceneMode, CameraParameters::SCENE_MODE_PORTRAIT) ?
            ANDROID_CONTROL_SCENE_MODE_PORTRAIT :
        !strcmp(sceneMode, CameraParameters::SCENE_MODE_LANDSCAPE) ?
            ANDROID_CONTROL_SCENE_MODE_LANDSCAPE :
        !strcmp(sceneMode, CameraParameters::SCENE_MODE_NIGHT) ?
            ANDROID_CONTROL_SCENE_MODE_NIGHT :
        !strcmp(sceneMode, CameraParameters::SCENE_MODE_NIGHT_PORTRAIT) ?
            ANDROID_CONTROL_SCENE_MODE_NIGHT_PORTRAIT :
        !strcmp(sceneMode, CameraParameters::SCENE_MODE_THEATRE) ?
            ANDROID_CONTROL_SCENE_MODE_THEATRE :
        !strcmp(sceneMode, CameraParameters::SCENE_MODE_BEACH) ?
            ANDROID_CONTROL_SCENE_MODE_BEACH :
        !strcmp(sceneMode, CameraParameters::SCENE_MODE_SNOW) ?
            ANDROID_CONTROL_SCENE_MODE_SNOW :
        !strcmp(sceneMode, CameraParameters::SCENE_MODE_SUNSET) ?
            ANDROID_CONTROL_SCENE_MODE_SUNSET :
        !strcmp(sceneMode, CameraParameters::SCENE_MODE_STEADYPHOTO) ?
            ANDROID_CONTROL_SCENE_MODE_STEADYPHOTO :
        !strcmp(sceneMode, CameraParameters::SCENE_MODE_FIREWORKS) ?
            ANDROID_CONTROL_SCENE_MODE_FIREWORKS :
        !strcmp(sceneMode, CameraParameters::SCENE_MODE_SPORTS) ?
            ANDROID_CONTROL_SCENE_MODE_SPORTS :
        !strcmp(sceneMode, CameraParameters::SCENE_MODE_PARTY) ?
            ANDROID_CONTROL_SCENE_MODE_PARTY :
        !strcmp(sceneMode, CameraParameters::SCENE_MODE_CANDLELIGHT) ?
            ANDROID_CONTROL_SCENE_MODE_CANDLELIGHT :
        !strcmp(sceneMode, CameraParameters::SCENE_MODE_BARCODE) ?
            ANDROID_CONTROL_SCENE_MODE_BARCODE:
        -1;
}

Parameters::Parameters::flashMode_t Parameters::flashModeStringToEnum(
        const char *flashMode) {
    return
        !strcmp(flashMode, CameraParameters::FLASH_MODE_OFF) ?
            Parameters::FLASH_MODE_OFF :
        !strcmp(flashMode, CameraParameters::FLASH_MODE_AUTO) ?
            Parameters::FLASH_MODE_AUTO :
        !strcmp(flashMode, CameraParameters::FLASH_MODE_ON) ?
            Parameters::FLASH_MODE_ON :
        !strcmp(flashMode, CameraParameters::FLASH_MODE_RED_EYE) ?
            Parameters::FLASH_MODE_RED_EYE :
        !strcmp(flashMode, CameraParameters::FLASH_MODE_TORCH) ?
            Parameters::FLASH_MODE_TORCH :
        Parameters::FLASH_MODE_INVALID;
}

Parameters::Parameters::focusMode_t Parameters::focusModeStringToEnum(
        const char *focusMode) {
    return
        !strcmp(focusMode, CameraParameters::FOCUS_MODE_AUTO) ?
            Parameters::FOCUS_MODE_AUTO :
        !strcmp(focusMode, CameraParameters::FOCUS_MODE_INFINITY) ?
            Parameters::FOCUS_MODE_INFINITY :
        !strcmp(focusMode, CameraParameters::FOCUS_MODE_MACRO) ?
            Parameters::FOCUS_MODE_MACRO :
        !strcmp(focusMode, CameraParameters::FOCUS_MODE_FIXED) ?
            Parameters::FOCUS_MODE_FIXED :
        !strcmp(focusMode, CameraParameters::FOCUS_MODE_EDOF) ?
            Parameters::FOCUS_MODE_EDOF :
        !strcmp(focusMode, CameraParameters::FOCUS_MODE_CONTINUOUS_VIDEO) ?
            Parameters::FOCUS_MODE_CONTINUOUS_VIDEO :
        !strcmp(focusMode, CameraParameters::FOCUS_MODE_CONTINUOUS_PICTURE) ?
            Parameters::FOCUS_MODE_CONTINUOUS_PICTURE :
        Parameters::FOCUS_MODE_INVALID;
}

status_t Parameters::parseAreas(const char *areasCStr,
        Vector<Parameters::Area> *areas) {
    static const size_t NUM_FIELDS = 5;
    areas->clear();
    if (areasCStr == NULL) {
        // If no key exists, use default (0,0,0,0,0)
        areas->push();
        return OK;
    }
    String8 areasStr(areasCStr);
    ssize_t areaStart = areasStr.find("(", 0) + 1;
    while (areaStart != 0) {
        const char* area = areasStr.string() + areaStart;
        char *numEnd;
        int vals[NUM_FIELDS];
        for (size_t i = 0; i < NUM_FIELDS; i++) {
            errno = 0;
            vals[i] = strtol(area, &numEnd, 10);
            if (errno || numEnd == area) return BAD_VALUE;
            area = numEnd + 1;
        }
        areas->push(Parameters::Area(
            vals[0], vals[1], vals[2], vals[3], vals[4]) );
        areaStart = areasStr.find("(", areaStart) + 1;
    }
    return OK;
}

status_t Parameters::validateAreas(const Vector<Parameters::Area> &areas,
                                      size_t maxRegions) {
    // Definition of valid area can be found in
    // include/camera/CameraParameters.h
    if (areas.size() == 0) return BAD_VALUE;
    if (areas.size() == 1) {
        if (areas[0].left == 0 &&
                areas[0].top == 0 &&
                areas[0].right == 0 &&
                areas[0].bottom == 0 &&
                areas[0].weight == 0) {
            // Single (0,0,0,0,0) entry is always valid (== driver decides)
            return OK;
        }
    }
    if (areas.size() > maxRegions) {
        ALOGE("%s: Too many areas requested: %d",
                __FUNCTION__, areas.size());
        return BAD_VALUE;
    }

    for (Vector<Parameters::Area>::const_iterator a = areas.begin();
         a != areas.end(); a++) {
        if (a->weight < 1 || a->weight > 1000) return BAD_VALUE;
        if (a->left < -1000 || a->left > 1000) return BAD_VALUE;
        if (a->top < -1000 || a->top > 1000) return BAD_VALUE;
        if (a->right < -1000 || a->right > 1000) return BAD_VALUE;
        if (a->bottom < -1000 || a->bottom > 1000) return BAD_VALUE;
        if (a->left >= a->right) return BAD_VALUE;
        if (a->top >= a->bottom) return BAD_VALUE;
    }
    return OK;
}

bool Parameters::boolFromString(const char *boolStr) {
    return !boolStr ? false :
        !strcmp(boolStr, CameraParameters::TRUE) ? true :
        false;
}

int Parameters::degToTransform(int degrees, bool mirror) {
    if (!mirror) {
        if (degrees == 0) return 0;
        else if (degrees == 90) return HAL_TRANSFORM_ROT_90;
        else if (degrees == 180) return HAL_TRANSFORM_ROT_180;
        else if (degrees == 270) return HAL_TRANSFORM_ROT_270;
    } else {  // Do mirror (horizontal flip)
        if (degrees == 0) {           // FLIP_H and ROT_0
            return HAL_TRANSFORM_FLIP_H;
        } else if (degrees == 90) {   // FLIP_H and ROT_90
            return HAL_TRANSFORM_FLIP_H | HAL_TRANSFORM_ROT_90;
        } else if (degrees == 180) {  // FLIP_H and ROT_180
            return HAL_TRANSFORM_FLIP_V;
        } else if (degrees == 270) {  // FLIP_H and ROT_270
            return HAL_TRANSFORM_FLIP_V | HAL_TRANSFORM_ROT_90;
        }
    }
    ALOGE("%s: Bad input: %d", __FUNCTION__, degrees);
    return -1;
}

int Parameters::arrayXToNormalized(int width) const {
    return width * 2000 / (fastInfo.arrayWidth - 1) - 1000;
}

int Parameters::arrayYToNormalized(int height) const {
    return height * 2000 / (fastInfo.arrayHeight - 1) - 1000;
}

int Parameters::normalizedXToArray(int x) const {
    return (x + 1000) * (fastInfo.arrayWidth - 1) / 2000;
}

int Parameters::normalizedYToArray(int y) const {
    return (y + 1000) * (fastInfo.arrayHeight - 1) / 2000;
}

}; // namespace camera2
}; // namespace android