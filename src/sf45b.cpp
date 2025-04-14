//----------------------------------------------------------------------------------------------------------------------------------
// LightWare SF45B ROS driver.
//----------------------------------------------------------------------------------------------------------------------------------
#include "sf45b.h"

#include <math.h>
#include "ros/ros.h"
#include "sensor_msgs/PointCloud2.h"

struct lwSf45Params {
	int32_t updateRate;
	int32_t cycleDelay;
	float lowAngleLimit;
	float highAngleLimit;
};

void validateParams(lwSf45Params* Params) {
	if (Params->updateRate < 1) Params->updateRate = 1;
	else if (Params->updateRate > 12) Params->updateRate = 12;

	if (Params->cycleDelay < 5) Params->cycleDelay = 5;
	else if (Params->cycleDelay > 2000) Params->cycleDelay = 2000;

	if (Params->lowAngleLimit < -160) Params->lowAngleLimit = -160;
	else if (Params->lowAngleLimit > -10) Params->lowAngleLimit = -10;

	if (Params->highAngleLimit < 10) Params->highAngleLimit = 10;
	else if (Params->highAngleLimit > 160) Params->highAngleLimit = 160;
}

int driverStart(lwSerialPort** Serial, const char* PortName, int32_t BaudRate) {
	platformInit();

	lwSerialPort* serial = platformCreateSerialPort();
	*Serial = serial;
	if (!serial->connect(PortName, BaudRate)) {
		ROS_ERROR("Could not establish serial connection on %s", PortName);
		return 1;
	};

	// Disable streaming of point data. (Command 30: Stream)
	if (!lwnxCmdWriteUInt32(serial, 30, 0)) { return 1; }

	// Read the product name. (Command 0: Product name)
	char modelName[16];
	if (!lwnxCmdReadString(serial, 0, modelName)) { return 1; }

	// Read the hardware version. (Command 1: Hardware version)
	uint32_t hardwareVersion;
	if (!lwnxCmdReadUInt32(serial, 1, &hardwareVersion)) { return 1; }

	// Read the firmware version. (Command 2: Firmware version)
	uint32_t firmwareVersion;	
	if (!lwnxCmdReadUInt32(serial, 2, &firmwareVersion)) { return 1; }
	char firmwareVersionStr[16];
	lwnxConvertFirmwareVersionToStr(firmwareVersion, firmwareVersionStr);

	// Read the serial number. (Command 3: Serial number)
	char serialNumber[16];
	if (!lwnxCmdReadString(serial, 3, serialNumber)) { return 1; }

	ROS_INFO("Model: %.16s", modelName);
	ROS_INFO("Hardware: %d", hardwareVersion);
	ROS_INFO("Firmware: %.16s (%d)", firmwareVersionStr, firmwareVersion);
	ROS_INFO("Serial: %.16s", serialNumber);

	return 0;
}

int driverScanStart(lwSerialPort* Serial, lwSf45Params* Params) {
	// Configre distance output for first return and angle. (Command 27: Distance output)
	if (!lwnxCmdWriteUInt32(Serial, 27, 0x101)) { return 1; }

	// (Command 66: Update rate)
	if (!lwnxCmdWriteUInt8(Serial, 66, Params->updateRate)) { return 1; }

	// (Command 85: Scan speed)
	if (!lwnxCmdWriteUInt16(Serial, 85, Params->cycleDelay)) { return 1; }

	// (Command 98: Scan low angle)
	if (!lwnxCmdWriteFloat(Serial, 98, Params->lowAngleLimit)) { return 1; }

	// (Command 99: Scan high angle)
	if (!lwnxCmdWriteFloat(Serial, 99, Params->highAngleLimit)) { return 1; }

	// Enable streaming of point data. (Command 30: Stream)
	if (!lwnxCmdWriteUInt32(Serial, 30, 5)) { return 1; }

	return 0;
}



int driverScan(lwSerialPort* Serial, lwDistanceResult* DistanceResult) {
	// The incoming point data packet is Command 44: Distance data in cm.
	lwResponsePacket response;

	if (lwnxRecvPacket(Serial, 44, &response, 1000)) {
		int16_t distanceCm = (response.data[5] << 8) | response.data[4];
		int16_t angleHundredths = (response.data[7] << 8) | response.data[6];

		float distance = distanceCm / 100.0f;
		float angle = angleHundredths / 100.0f;
		float faceAngle = (angle - 90) * M_PI / 180.0;

		DistanceResult->x = distance * -cos(faceAngle);
		DistanceResult->y = distance * sin(faceAngle);
		DistanceResult->z = 0;

		return 1;
	}

	return 0;
}



SF45Communicate::SF45Communicate(int argc, char** argv) : distanceResults(maxPointsPerMsg){ 
	ros::init(argc, argv, "sf45b");

    n = new ros::NodeHandle();
    privateNode = new ros::NodeHandle("~");

	pointCloudPub = n->advertise<sensor_msgs::PointCloud2>("sf45/points", 10);
}

SF45Communicate::~SF45Communicate(){
	delete n;
	delete privateNode;
}

//Baud Rate
int32_t SF45Communicate::getBaudRate(){
	return baudRate;
}

void SF45Communicate::setBaudRate(int32_t rate){
	baudRate = rate;
}

//Port Name
std::string SF45Communicate::getPortName(){
	return portName;
}

void SF45Communicate::setPortName(std::string name){
	portName = name;
}

//Start
int SF45Communicate::startUp(){
	std::string frameId;
	privateNode->param(std::string("frame_id"), frameId, std::string("laser"));

	lwSf45Params params;
	privateNode->param(std::string("updateRate"), params.updateRate, 6);
	privateNode->param(std::string("cycleDelay"), params.cycleDelay, 5);
	privateNode->param(std::string("lowAngleLimit"), params.lowAngleLimit, -45.0f);
	privateNode->param(std::string("highAngleLimit"), params.highAngleLimit, 45.0f);
	validateParams(&params);
	
	privateNode->param(std::string("maxPoints"), maxPointsPerMsg, 100);
	if (maxPointsPerMsg < 1) maxPointsPerMsg = 1;

	ROS_INFO("Starting SF45B node");
	
	if (driverStart(&serial, portName.c_str(), baudRate) != 0) {
		ROS_ERROR("Failed to start driver");
		return 1;
	}

	if (driverScanStart(serial, &params) != 0) {
		ROS_ERROR("Failed to start scan");
		return 1;
	}


	pointCloudMsg.header.frame_id = frameId;
	pointCloudMsg.height = 1;
	pointCloudMsg.width = maxPointsPerMsg;
	
	pointCloudMsg.fields.resize(3);
	pointCloudMsg.fields[0].name = "x";
	pointCloudMsg.fields[0].offset = 0;	
	pointCloudMsg.fields[0].datatype = 7;
	pointCloudMsg.fields[0].count = 1;

	pointCloudMsg.fields[1].name = "y";
	pointCloudMsg.fields[1].offset = 4;
	pointCloudMsg.fields[1].datatype = 7;
	pointCloudMsg.fields[1].count = 1;
	
	pointCloudMsg.fields[2].name = "z";
	pointCloudMsg.fields[2].offset = 8;
	pointCloudMsg.fields[2].datatype = 7;
	pointCloudMsg.fields[2].count = 1;

	pointCloudMsg.is_bigendian = false;
	pointCloudMsg.point_step = 12;
	pointCloudMsg.row_step = 12 * maxPointsPerMsg;
	pointCloudMsg.is_dense = true;

	pointCloudMsg.data = std::vector<uint8_t>(maxPointsPerMsg * 12);
	
	return 1;
}

//Run
int SF45Communicate::run(){
	startUp();

	while (ros::ok()) {
		while (true) {
			lwDistanceResult distanceResult;
			int status = driverScan(serial, &distanceResult);

			if (status == 0) {
				break;
			} else {
				distanceResults[currentPoint] = distanceResult;
				++currentPoint;
			}

			if (currentPoint == maxPointsPerMsg) {
				memcpy(&pointCloudMsg.data[0], &distanceResults[0], maxPointsPerMsg * 12);

				pointCloudMsg.header.stamp = ros::Time::now();
				pointCloudPub.publish(pointCloudMsg);
				
				currentPoint = 0;
			}
		}

		ros::spinOnce();
	}

	return 0;
}

void SF45Communicate::testBuildSystem(){
	std::cout << "Got to library defined in sf4b.cpp" << std::endl;
}

