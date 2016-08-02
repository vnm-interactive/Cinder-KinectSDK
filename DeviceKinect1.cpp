#include "DepthSensor.h"
#include "KinectCommonBridgeLib.h"

#include "cinder/app/app.h"
#include "cinder/Log.h"
#include "cinder/msw/CinderMsw.h"

using namespace ci;
using namespace ci::app;
using namespace std;
using namespace Kinect;

#define SCOPED_COM_OBJECT(objName) auto scoped##__LINE__## = msw::makeComUnique(objName);

struct DeviceKinect1 : public Device
{
    static uint32_t getDeviceCount()
    {
        return KinectGetPortIDCount();
    }

    ~DeviceKinect1()
    {
        if (sensor != KCB_INVALID_HANDLE)
        {
            KinectCloseSensor(sensor);
        }
    }

    ivec2 getDepthSize() const
    {
        return{ depthDesc.dwWidth, depthDesc.dwHeight };
    }

    bool isValid() const
    {
        return sensor != KCB_INVALID_HANDLE;
    }

    DeviceKinect1(Option option)
    {
        this->option = option;

        HRESULT hr = S_OK;

        WCHAR portName[100];
        if (KinectGetPortIDByIndex(option.deviceId, 100, portName))
        {
            sensor = KinectOpenSensor(portName);
        }
        else
        {
            sensor = KinectOpenDefaultSensor();
        }

        if (KCB_INVALID_HANDLE == sensor)
        {
            hr = E_UNEXPECTED;
        }

        if (SUCCEEDED(hr))
        {
            KinectStopColorStream(sensor);
            depthDesc = { sizeof(KINECT_IMAGE_FRAME_FORMAT) };
            KinectGetDepthFrameFormat(sensor, &depthDesc);
            depthBuffer.reset(new uint8_t[depthDesc.cbBufferSize]);
            depthChannel = Channel16u(depthDesc.dwWidth, depthDesc.dwHeight,
                depthDesc.cbBytesPerPixel * depthDesc.dwWidth, 1, (uint16_t *)depthBuffer.get());
        }

        if (FAILED(hr))
        {
            CI_LOG_E("Failed to connect to Kinect V1");
        }

        App::get()->getSignalUpdate().connect(std::bind(&DeviceKinect1::update, this));
    }

    const vec3 toCi(const Vector4& pos)
    {
        return vec3(pos.x, pos.y, pos.z);
    }

    void update()
    {
        if (option.enableDepth && KinectIsDepthFrameReady(sensor))
        {
            if (SUCCEEDED(KinectGetDepthFrame(sensor, depthDesc.cbBufferSize, depthBuffer.get(), nullptr)))
            {
                signalDepthDirty.emit();
            }
        }

        if (option.enableBody && KinectIsSkeletonFrameReady(sensor))
        {
            if (SUCCEEDED(KinectGetSkeletonFrame(sensor, &skeletonFrame)))
            {
                for (auto& data : skeletonFrame.SkeletonData)
                {
                    if (data.eTrackingState != NUI_SKELETON_TRACKED) continue;
                    Body body;
                    body.id = data.dwTrackingID;

                    static std::pair<int, int> mappingPairs[] =
                    {
                        { Body::HIP_CENTER, NUI_SKELETON_POSITION_HIP_CENTER },
                        { Body::SPINE, NUI_SKELETON_POSITION_SPINE },
                        { Body::SHOULDER_CENTER, NUI_SKELETON_POSITION_SHOULDER_CENTER },
                        { Body::HEAD, NUI_SKELETON_POSITION_HEAD },
                        { Body::SHOULDER_LEFT, NUI_SKELETON_POSITION_SHOULDER_LEFT },
                        { Body::ELBOW_LEFT, NUI_SKELETON_POSITION_ELBOW_LEFT },
                        { Body::WRIST_LEFT, NUI_SKELETON_POSITION_WRIST_LEFT },
                        { Body::HAND_LEFT, NUI_SKELETON_POSITION_HAND_LEFT },
                        { Body::SHOULDER_RIGHT, NUI_SKELETON_POSITION_SHOULDER_RIGHT },
                        { Body::ELBOW_RIGHT, NUI_SKELETON_POSITION_ELBOW_RIGHT },
                        { Body::WRIST_RIGHT, NUI_SKELETON_POSITION_WRIST_RIGHT },
                        { Body::HAND_RIGHT, NUI_SKELETON_POSITION_HAND_RIGHT },
                        { Body::HIP_LEFT, NUI_SKELETON_POSITION_HIP_LEFT },
                        { Body::KNEE_LEFT, NUI_SKELETON_POSITION_KNEE_LEFT },
                        { Body::ANKLE_LEFT, NUI_SKELETON_POSITION_ANKLE_LEFT },
                        { Body::FOOT_LEFT, NUI_SKELETON_POSITION_FOOT_LEFT },
                        { Body::HIP_RIGHT, NUI_SKELETON_POSITION_HIP_RIGHT },
                        { Body::KNEE_RIGHT, NUI_SKELETON_POSITION_KNEE_RIGHT },
                        { Body::ANKLE_RIGHT, NUI_SKELETON_POSITION_ANKLE_RIGHT },
                        { Body::FOOT_RIGHT, NUI_SKELETON_POSITION_FOOT_RIGHT },
                    };
                    for (auto& mapping : mappingPairs)
                    {
                        vec3 pos3d = toCi(data.SkeletonPositions[mapping.second]);
                        vec2 pos2d;

                        if (pos3d.z > FLT_EPSILON)
                        {
                            //
                            // Center of depth sensor is at (0,0,0) in skeleton space, and
                            // and (width/2,height/2) in depth image coordinates.  Note that positive Y
                            // is up in skeleton space and down in image coordinates.
                            //

                            pos2d.x = 0.5f + pos3d.x * (depthDesc.dwWidth / 320.f) * NUI_CAMERA_SKELETON_TO_DEPTH_IMAGE_MULTIPLIER_320x240 / pos3d.z;
                            pos2d.y = 0.5f - pos3d.y * (depthDesc.dwHeight / 240.f) * NUI_CAMERA_SKELETON_TO_DEPTH_IMAGE_MULTIPLIER_320x240 / pos3d.z;

                        }
                        body.joints[mapping.first].pos3d = pos3d;
                        body.joints[mapping.first].pos2d = pos2d;
                    }
                    bodies.push_back(body);
                }
                signalBodyDirty.emit();
            }
        }
    }

    unique_ptr<uint8_t[]> depthBuffer;
    KINECT_IMAGE_FRAME_FORMAT depthDesc;
    NUI_SKELETON_FRAME skeletonFrame;
    int sensor;
};

uint32_t getKinect1Count()
{
    return DeviceKinect1::getDeviceCount();
}

DeviceRef createKinect1(Device::Option option)
{
    return DeviceRef(new DeviceKinect1(option));
}
