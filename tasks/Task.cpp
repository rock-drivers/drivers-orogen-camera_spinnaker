/* Generated from orogen/lib/orogen/templates/tasks/Task.cpp */

#include "Task.hpp"

#include <signal.h>

using namespace camera_spinnaker;

Task::Task(std::string const& name)
    : TaskBase(name)
{
    ::base::samples::frame::Frame *img = new ::base::samples::frame::Frame();
    this->camera_frame.reset(img);
    img = nullptr;

    this->output_frame.reset(nullptr);
}

Task::~Task()
{
}



/// The following lines are template definitions for the various state machine
// hooks defined by Orocos::RTT. See Task.hpp for more detailed
// documentation about them.

bool Task::configureHook()
{
    if (! TaskBase::configureHook())
        return false;

    this->config = _config.value();

    this->output_frame_mode = _output_format.value();
    if(this->output_frame_mode == ::base::samples::frame::frame_mode_t::MODE_UNDEFINED)
        this->output_frame_mode = camera_spinnaker::SpinnakerCamera::type2FrameType(this->config.image_format_color_coding);

    std::cout<<"output_frame_mode: "<<this->output_frame_mode<<std::endl;


    if(_undistort.value() || _scale_x.value() != 1 || _scale_y.value() != 1 || _offset_x.value() != 0 || _offset_y.value() != 0
    || camera_spinnaker::SpinnakerCamera::type2FrameType(this->config.image_format_color_coding) != this->output_frame_mode)
    {
        if(_undistort.value())
        {
            if( !_calibration_parameters.value().isValid() )
            {
               RTT::log(RTT::Error) << "Camera Driver Error: Undistort true, but calibration not valid." << RTT::endlog();
              return false;
          }
          frame_helper.setCalibrationParameter(_calibration_parameters.value());
        }
        this->process_image = true;
    }
    else
        this->process_image = false;

    /** Get the camera available **/
    Spinnaker::SystemPtr system = Spinnaker::System::GetInstance();
    Spinnaker::InterfaceList interfaceList = system->GetInterfaces();
    Spinnaker::CameraList camList = system->GetCameras();
    unsigned int num_cameras = camList.GetSize();
    if (num_cameras > 0)
    {
        /** Take first camera TO-Do improve **/
        Spinnaker::CameraPtr pCam = camList[0];
        Spinnaker::GenApi::INodeMap& nodeMapTLDevice = pCam->GetTLDeviceNodeMap();
        Spinnaker::GenApi::CStringPtr ptrDeviceSerialNumber = nodeMapTLDevice.GetNode("DeviceSerialNumber");
        this->config.name = std::string(ptrDeviceSerialNumber->ToString());
        this->spinnaker.setDesiredCamera((uint32_t) std::stoi(std::string(ptrDeviceSerialNumber->ToString())));
    }
    else
    {
        RTT::log(RTT::Error) <<"[ERROR] No camera available: FAILED TO CONNECT" << RTT::endlog(); 
        report(NO_CAMERA);
    }

    /** Connect and configure **/
    try
    {
        RTT::log(RTT::Info) << "connect to camera: "<<this->config.name<< RTT::endlog(); 
        this->spinnaker.connect();
    }
    catch (const std::runtime_error& e)
    {
        RTT::log(RTT::Error) << "[ERROR] "<<e.what() << RTT::endlog();
        report(NO_CAMERA);
    }

    try
    {
        RTT::log(RTT::Info) << "configure camera" << RTT::endlog(); 
        this->spinnaker.setNewConfiguration(this->config, SpinnakerCamera::LEVEL_RECONFIGURE_STOP);
        double timeout = 1.25 * (1.0/this->config.acquisition_frame_rate);
        this->spinnaker.setTimeout(timeout);
        TaskContext::setPeriod(1.0/this->config.acquisition_frame_rate);
    }
    catch (const std::runtime_error& e)
    {
        RTT::log(RTT::Error) << "[ERROR] "<<e.what() << RTT::endlog();
        report(CONFIGURE_ERROR);
    }

    return true;
}
bool Task::startHook()
{
    if (! TaskBase::startHook())
        return false;

    try
    {
        RTT::log(RTT::Info) << "start camera: " <<this->config.name<< RTT::endlog(); 
        this->spinnaker.start();
    }
    catch (std::runtime_error& e)
    {
        RTT::log(RTT::Error) << "[ERROR] "<<e.what() << RTT::endlog();
    }

    return true;
}
void Task::updateHook()
{
    TaskBase::updateHook();

    //while(((RTT::TaskContext*)this)->getActivity()->trigger())
    while(this->isActive())
    {
        try
        {
            /** Get the current image **/
            base::samples::frame::Frame *frame_ptr = this->camera_frame.write_access();
            frame_ptr->image.clear();
            this->spinnaker.grabImage(*frame_ptr, "spinnaker");
            /*std::cout<<"[time] "<<frame_ptr->time.toString()<<" img size "<<frame_ptr->size.width<<" x "<<frame_ptr->size.height<<" px_size: "<<frame_ptr->pixel_size <<" data size: "<<frame_ptr->image.size()
            <<" img mode "<<frame_ptr->frame_mode<<std::endl;*/
            this->camera_frame.reset(frame_ptr);

            /** Should we process the image **/
            if(this->process_image)
            {
                this->processImage();
                _image_frame.write(this->output_frame);
            }
            else
            {
                _image_frame.write(this->camera_frame);
            }
        }
        catch (const std::runtime_error& e)
        {
            RTT::log(RTT::Error) << "[ERROR] "<<e.what() << RTT::endlog();
        }
    }
}

void Task::errorHook()
{
    TaskBase::errorHook();
}
void Task::stopHook()
{
    TaskBase::stopHook();

    RTT::log(RTT::Info) <<"stop the camera: "<<this->config.name<< RTT::endlog(); 

    /** Stop the driver **/
    this->spinnaker.stop();
}
void Task::cleanupHook()
{
    TaskBase::cleanupHook();

    RTT::log(RTT::Info) << "disconnecting to the camera: "<<this->config.name<< RTT::endlog(); 
    this->spinnaker.disconnect();
}

void Task::processImage()
{
    if (!this->output_frame.valid())
    {
        //initialize output frame
        base::samples::frame::Frame *frame = new base::samples::frame::Frame(this->camera_frame->size.width *_scale_x,this->camera_frame->size.height*_scale_y,
                        this->camera_frame->data_depth, this->output_frame_mode);
        this->output_frame.reset(frame);
        frame = NULL;
    }

    base::samples::frame::Frame *frame_ptr = this->output_frame.write_access();
    try
    {

        frame_helper.convert(*camera_frame,*frame_ptr,_offset_x.value(),
                _offset_y.value(),_resize_algorithm.value(), _undistort.value());
        // write calibration values if the frame was not undistorted
        if(_calibration_parameters.value().isValid() && !_undistort.value())
            _calibration_parameters.value().toFrame(*frame_ptr );
    }
    catch(std::runtime_error e)
    {
        RTT::log(RTT::Error) << "processing error: " << e.what() << RTT::endlog();
        RTT::log(RTT::Error) << "Have you specified camera_format and output_format right?" << RTT::endlog();
        report(PROCESSING_ERROR);
    }
    output_frame.reset(frame_ptr);
}
