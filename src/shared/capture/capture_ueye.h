#ifndef CAPTURE_UEYE_H
#define CAPTURE_UEYE_H

#include "captureinterface.h"
#include "util.h"
#include <QMutex>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <dc1394/control.h>
#include <dc1394/conversions.h>
#include <ueye.h>
#include "VarTypes.h"

//if using QT, inherit QObject as a base
class CaptureUeye : public QObject, public CaptureInterface {
    Q_OBJECT
protected:
    QMutex mutex;

    int last_mem_id;
    int mem_id;
    unsigned int capture_width, capture_height;
    bool is_capturing;
    char *current_frame;
    HIDS hCam;

    // Capture dimensions
    VarStringEnum * v_dimensions;
    VarDouble *v_exposure;
    VarDouble *v_fps;
    VarInt *v_master_gain;
    VarInt *v_red_gain;
    VarInt *v_blue_gain;
    VarInt *v_green_gain;
    VarInt *v_edge_enhancement;
    VarInt *v_cam_id;

    std::map<int, char*> buffers;

public:
    CaptureUeye(VarList * _settings=0, int default_camera_id=0, QObject * parent=0);
    ~CaptureUeye();

    /// This returns a raw-image with a pointer directly to the video-buffer
    /// Note, that this pointer is only guaranteed to point to a valid
    /// memory location until releaseFrame() is called.
    RawImage getFrame();

    /// This function should return true, if your method is currently
    /// actively capturing data.
    bool     isCapturing();

    /// This releases the pointer of a previous \c getFrame() call.
    void     releaseFrame();

    /// This will make your method start capturing data
    /// Note, that upon construction, your class should NOT be starting
    /// to capture data automatically.
    /// Instead a call to startCapture() is required.
    bool     startCapture();

    /// This will make your method stop capturing data
    bool     stopCapture();

    /// If applicable, this will enforce a reinitialization / reset
    /// on your capturing hardware, in case something went wrong.
    ///
    /// This is applicable, for example for DC1394 based systems
    /// where a firewire bus is able to 'freeze up' and can be
    /// reinitialized by a bus reset.
    bool     resetBus();

    /// If your capture device provides some kind of parameter readout
    /// that isn't automatically read-out at every frame, then
    /// this function should force a readout of such parameters.
    void     readAllParameterValues();

    /// This function will allow the copying of a captured frame
    /// to another RawImage data-structure.
    /// Overloading this function is recommended to provide more advanced
    /// functionality, such as converting the frame from one format to another.
    /// which format to convert to, should be a VarTypes option, added
    /// to the settings above.
    /// For an example implementation, please see how this function
    /// is overloaded in the CaptureDC1394v2 class.
    ///
    /// In its current implementation in this base-interface,
    /// all this function will do is allocate the target image (if not
    /// already allocated, and then memcpy the data as-is.
    // bool     copyAndConvertFrame(const RawImage & src, RawImage & target);

    /// Return a string describing your capture method
    /// e.g. DC1394B, or GigEVision, or V4LCapture, or USBCam,...
    string   getCaptureMethodName() const;

    bool copyAndConvertFrame(const RawImage & src, RawImage & target);
};

#endif //  CAPTURE_UEYE_H
