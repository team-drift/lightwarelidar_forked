#ifndef SF45_COMMUNICATE_H
#define SF45_COMMUNICATE_H

#include "../include/lwNx.h"
#include "../include/common.h"

#include <vector>
#include <string>
#include <vector>
#include <thread>
#include <mutex>

#include "ros/ros.h"
#include "sensor_msgs/PointCloud2.h"

struct lwDistanceResult {
	float distance;
	float angle;
};

class SF45Communicate {
    /*
    This class is meant to serve as a library to communicate with the SF45B Lidar sensor

    SF45Communicate builds off of the SF45B ROS driver from LightWare
    */
    private:
        ros::NodeHandle* n = nullptr;
        ros::NodeHandle* privateNode = nullptr;

        ros::Publisher pointCloudPub;

        lwSerialPort* serial = nullptr;

        int32_t baudRate = 115200;
        std::string portName = "/dev/tty.usbmodem38S45_156941";

        sensor_msgs::PointCloud2 pointCloudMsg;

        int maxPointsPerMsg = 100;
        int currentPoint = 0;
        std::vector<lwDistanceResult> distanceResults;

        //Threads
        std::mutex dataMutex;
        std::thread threading;

        bool running = true;

    public:
        //Constructor and destructor
        SF45Communicate(int argc, char** argv);
        ~SF45Communicate();

        //Start lidar
        int startUp();
        int run();

        //Getters and Setters
        int32_t getBaudRate();
        void setBaudRate(int32_t rate);

        std::string getPortName();
        void setPortName(std::string name);

        //Confirmation of builds working together
        void testBuildSystem();

        //Get data
        std::vector<float> getData();

        //For threads
        void start();
	    void stop();


};

#endif
