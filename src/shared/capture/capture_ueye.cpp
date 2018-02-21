#include <iostream>
#include <ueye.h>
#include "capture_ueye.h"

CaptureUeye::CaptureUeye(VarList * _settings, int default_camera_id, QObject * parent)
: QObject(parent), CaptureInterface(_settings)
{
    cam_id = default_camera_id + 1;
    is_capturing = false;
    capture_width = 640;
    capture_height = 480;
    current_frame = NULL;

    // Capture dimensions
    v_dimensions = new VarStringEnum("Frame dimension", "1280x1024");
    v_dimensions->addItem("1280x1024");
    v_dimensions->addItem("1280x960");
    v_dimensions->addItem("1280x720");
    v_dimensions->addItem("1024x1024");
    v_dimensions->addItem("1024x768");
    v_dimensions->addItem("800x600");
    v_dimensions->addItem("640x480");
    v_dimensions->addItem("320x240");
    _settings->addChild(v_dimensions);

    v_exposure = new VarDouble("Exposure (ms)", 12);
    _settings->addChild(v_exposure);

    v_fps = new VarDouble("FPS", 60);
    _settings->addChild(v_fps);

    v_master_gain = new VarInt("Master gain", 80);
    _settings->addChild(v_master_gain);

    v_red_gain = new VarInt("Red gain", 17);
    _settings->addChild(v_red_gain);

    v_green_gain = new VarInt("Green gain", 0);
    _settings->addChild(v_green_gain);

    v_blue_gain = new VarInt("Blue gain", 42);
    _settings->addChild(v_blue_gain);

    v_edge_enhancement = new VarInt("Edge enhancement", 9);
    _settings->addChild(v_edge_enhancement);
}

CaptureUeye::~CaptureUeye()
{
    if (is_capturing) {
        stopCapture();
    }
}

RawImage CaptureUeye::getFrame()
{
    char *dummy;
    char *old_frame = current_frame;
    is_GetActSeqBuf(hCam, &mem_id, &dummy, &current_frame);
    mem_id -= 1;
    if (mem_id == 0) mem_id = 8;

    if (old_frame == current_frame) {
        mutex.lock();
        is_WaitEvent(hCam, IS_SET_EVENT_FRAME, 100);
        mutex.unlock();
        is_GetActSeqBuf(hCam, &mem_id, &dummy, &current_frame);
    }

    RawImage result;
    result.setWidth(capture_width);
    result.setHeight(capture_height);
    result.setData((unsigned char*)current_frame);
    result.setColorFormat(COLOR_YUV422_UYVY);

    UEYEIMAGEINFO info;
    is_GetImageInfo(hCam, mem_id, &info, sizeof(UEYEIMAGEINFO));
    result.setTime(info.u64TimestampDevice/10000000.0);
    // printf("Time: %f\n", result.getTime());

    return result;
}

bool CaptureUeye::isCapturing()
{
    return is_capturing;
}

void CaptureUeye::releaseFrame()
{
}

bool CaptureUeye::startCapture()
{
    mutex.lock();
    hCam = cam_id;
    std::cout << "uEye: Starting capturing! index: " << hCam << std::endl;

    is_capturing = false;
    if (is_InitCamera(&hCam, NULL) != IS_SUCCESS) {
        mutex.unlock();
        return false;
    }

    // Getting dimensions
    string dimension = v_dimensions->getSelection();
    for (int k=0; k<dimension.length(); k++) {
        if (dimension[k] == 'x') {
            capture_width = atoi(dimension.substr(0, k).c_str());
            capture_height = atoi(dimension.substr(k+1).c_str());
        }
    }

    // Searching for format
    uint32_t entries;
    is_ImageFormat(hCam, IMGFRMT_CMD_GET_NUM_ENTRIES, &entries, sizeof(entries));

    char formats[sizeof(IMAGE_FORMAT_LIST) + (entries-1)*sizeof(IMAGE_FORMAT_INFO)];
    IMAGE_FORMAT_LIST *formatList = (IMAGE_FORMAT_LIST*)formats;
    formatList->nNumListElements = entries;
    formatList->nSizeOfListEntry = sizeof(IMAGE_FORMAT_INFO);
    is_ImageFormat(hCam, IMGFRMT_CMD_GET_LIST, formats, sizeof(formats));
    bool found = false;
    for (size_t k=0; k<entries; k++) {
        IMAGE_FORMAT_INFO *info = &formatList->FormatInfo[k];
        // printf("w: %d, h: %d\n", info->nWidth, info->nHeight);
        if (info->nWidth == capture_width && info->nHeight == capture_height) {
            is_ImageFormat(hCam, IMGFRMT_CMD_SET_FORMAT, &info->nFormatID, sizeof(info->nFormatID));
            found = true;
        }
    }
    if (!found) {
        std::cerr << "Unsupported resolution: " << capture_width << "x" << capture_height << std::endl;
    }

    // Image memory allocation
    is_ClearSequence(hCam);
    buffers.clear();
    for (int k=0; k<8; k++) {
        char *mem;
        int memId;
        is_AllocImageMem(hCam, capture_width, capture_height, 16, &mem, &memId);
        is_AddToSequence(hCam, mem, memId);
        buffers[memId] = mem;
    }
    last_mem_id = 1;

    // Exposure
    double exposure = v_exposure->getDouble();
    is_Exposure(hCam, IS_EXPOSURE_CMD_SET_EXPOSURE, (void*)&exposure, sizeof(exposure));

    // White balance
    is_SetHardwareGain(hCam, v_master_gain->getInt(), v_red_gain->getInt(), v_green_gain->getInt(), v_blue_gain->getInt());

    // Enabling anti-flicker
    double flicker = ANTIFLCK_MODE_SENS_50_FIXED;
    is_SetAutoParameter(hCam, IS_SET_ANTI_FLICKER_MODE, &flicker, NULL);

    // Setting framerate
    is_SetColorMode(hCam, IS_CM_UYVY_PACKED);
    double fps = v_fps->getDouble(), newFps = v_fps->getDouble();
    is_SetFrameRate(hCam, fps, &newFps);

    // Event frame event enabled
    is_EnableEvent(hCam, IS_SET_EVENT_FRAME);

    // Video capture
    is_CaptureVideo(hCam, IS_WAIT);

    // Edge enhancement
    UINT nEdgeEnhancement = v_edge_enhancement->getInt();
    printf("EdgeEnhancement: %d\n", is_EdgeEnhancement(hCam, IS_EDGE_ENHANCEMENT_CMD_SET, (void*)&nEdgeEnhancement, sizeof(nEdgeEnhancement)));

    is_capturing = true;
    mutex.unlock();
    return true;
}

bool CaptureUeye::stopCapture()
{
    is_capturing = false;

    mutex.lock();
    is_ClearSequence(hCam);

    for (std::map<int, char*>::iterator it = buffers.begin();
        it != buffers.end(); it++) {
        is_FreeImageMem(hCam, it->second, it->first);
    }
    is_ExitCamera(hCam);
    current_frame = NULL;
    mutex.unlock();

    return true;
}

bool CaptureUeye::resetBus()
{
    return true;
}

void CaptureUeye::readAllParameterValues()
{
}

string CaptureUeye::getCaptureMethodName() const
{
    return "uEye";
}

bool CaptureUeye::copyAndConvertFrame(const RawImage & src, RawImage & target)
{
    target.allocate(COLOR_YUV422_UYVY, src.getWidth(), src.getHeight());
    memcpy(target.getData(),src.getData(),src.getNumBytes());

    return true;
}
