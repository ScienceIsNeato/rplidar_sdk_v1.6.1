#include "Scanner.h"

Scanner::Scanner()
{
}

Scanner::~Scanner()
{
}

bool Scanner::Initialize(RPlidarDriver * drv, int argc, const char * argv[])
{
	const char * opt_com_path = NULL;
	_u32         baudrateArray[2] = { 115200, 256000 };
	_u32         opt_com_baudrate = 0;
	u_result     op_result;

	bool useArgcBaudrate = false;

	printf("Ultra simple LIDAR data grabber for RPLIDAR.\n"
		"Version: 1.6.1\n");

	// read serial port from the command line...
	if (argc > 1) opt_com_path = argv[1]; // or set to a fixed value: e.g. "com3" 

	// read baud rate from the command line if specified...
	if (argc > 2)
	{
		opt_com_baudrate = strtoul(argv[2], NULL, 10);
		useArgcBaudrate = true;
	}

	if (!opt_com_path) {
#ifdef _WIN32
		// use default com port
		opt_com_path = "\\\\.\\com3";
#else
		opt_com_path = "/dev/ttyUSB0";
#endif
	}

	rplidar_response_device_info_t devinfo;
	bool connectSuccess = false;
	// make connection...
	if (useArgcBaudrate)
	{
		if (!drv)
			drv = RPlidarDriver::CreateDriver(DRIVER_TYPE_SERIALPORT);
		if (IS_OK(drv->connect(opt_com_path, opt_com_baudrate)))
		{
			op_result = drv->getDeviceInfo(devinfo);

			if (IS_OK(op_result))
			{
				connectSuccess = true;
			}
			else
			{
				delete drv;
				drv = NULL;
			}
		}
	}
	else
	{
		size_t baudRateArraySize = (sizeof(baudrateArray)) / (sizeof(baudrateArray[0]));
		for (size_t i = 0; i < baudRateArraySize; ++i)
		{
			if (!drv)
				drv = RPlidarDriver::CreateDriver(DRIVER_TYPE_SERIALPORT);
			if (IS_OK(drv->connect(opt_com_path, baudrateArray[i])))
			{
				op_result = drv->getDeviceInfo(devinfo);

				if (IS_OK(op_result))
				{
					connectSuccess = true;
					break;
				}
				else
				{
					delete drv;
					drv = NULL;
				}
			}
		}
	}
	if (!connectSuccess) {

		fprintf(stderr, "Error, cannot bind to the specified serial port %s.\n"
			, opt_com_path);
		return false;
	}

	// print out the device serial number, firmware and hardware version number..
	printf("RPLIDAR S/N: ");
	for (int pos = 0; pos < 16; ++pos)
	{
		printf("%02X", devinfo.serialnum[pos]);
	}

	printf("\n"
		"Firmware Ver: %d.%02d\n"
		"Hardware Rev: %d\n"
		, devinfo.firmware_version >> 8
		, devinfo.firmware_version & 0xFF
		, (int)devinfo.hardware_version);

	return true;
}

bool Scanner::CheckRPLIDARHealth(RPlidarDriver * drv)
{
	u_result     op_result;
	rplidar_response_device_health_t healthinfo;

	op_result = drv->getHealth(healthinfo);
	if (IS_OK(op_result)) { // the macro IS_OK is the preperred way to judge whether the operation is succeed.
		printf("RPLidar health status : %d\n", healthinfo.status);
		if (healthinfo.status == RPLIDAR_STATUS_ERROR) {
			fprintf(stderr, "Error, rplidar internal error detected. Please reboot the device to retry.\n");
			// enable the following code if you want rplidar to be reboot by software
			// drv->reset();
			return false;
		}
		else {
			return true;
		}

	}
	else {
		fprintf(stderr, "Error, cannot retrieve the lidar health code: %x\n", op_result);
		return false;
	}
}

void Scanner::Close(RPlidarDriver * drv)
{
	RPlidarDriver::DisposeDriver(drv);
	drv = NULL;
}

void Scanner::Calibrate(RPlidarDriver * drv, int num_samples, double (&calibration_results) [NUM_SAMPLE_POINTS])
{
	int max_attempts = 500;
	int good_samples = 0;
	int attempts = 0;
	double smoothed_cal_vals[NUM_SAMPLE_POINTS];


	for (int i = 0; i < NUM_SAMPLE_POINTS; i++)
	{
		calibration_results[i] = DEFAULT_CALIBRATION_VALUE;
		smoothed_cal_vals[i] = 0;
	}

	for (int i = 100; i > 0; i--)
	{
		std::cout << "Calibrating in " << i << " ...\n";
		//std::this_thread::sleep_for(std::chrono::seconds(1)); This is broken
	}

	std::cout << "Calibration countdown! -- " << num_samples << std::endl;

	while((attempts < max_attempts) && (good_samples < num_samples))
	{
		rplidar_response_measurement_node_t nodes[NUM_SAMPLE_POINTS];
		size_t   count = _countof(nodes);
		u_result op_result = drv->grabScanData(nodes, count);

		if (IS_OK(op_result)) 
		{
			drv->ascendScanData(nodes, count);
			std::cout << num_samples - good_samples << "\n" << std::flush;
			
			for (int pos = 0; pos < (int)count; ++pos)
			{
				double dist = nodes[pos].distance_q2 / 4.0f;
				if (dist > 0)
				{
					if (dist < calibration_results[pos])
					{
						calibration_results[pos] = dist;
					}
				}
			}
			good_samples++;
		}
		else
		{
			std::cout << "attempt " << attempts << " Failed.\n" << std::flush;
		}
		attempts++;
	}

	std::cout << "Calibration gathered " << good_samples << " good samples out of " << attempts << "attempts.\n" << std::flush;

	// Multiply the results by the scale factor
	int bad_samples = 0;
	for (int i = 0; i < NUM_SAMPLE_POINTS; i++)
	{
		if (calibration_results[i] == DEFAULT_CALIBRATION_VALUE)
		{
			bad_samples++;
		}
	}
	std::cout << "Calibration found " << NUM_SAMPLE_POINTS - bad_samples << " valid samples out of " << NUM_SAMPLE_POINTS << " total collected.\n" << std::flush;
	SmoothCalibrationResults(calibration_results, smoothed_cal_vals, .98);
}

void Scanner::SmoothCalibrationResults(double(&calibration_results)[NUM_SAMPLE_POINTS], double(&smoothed_cal_vals)[NUM_SAMPLE_POINTS], double scale_factor)
{
	int range = 5;
	int adjusted = 0;
	double adjustment_sum = 0.0;
	for (int i = 0; i < NUM_SAMPLE_POINTS; i++)
	{
		double val = calibration_results[i];
		for (int index = range * -1; index <= range; index++)
		{
			int relative_index = i + index;
			if ( (relative_index > 0) && (relative_index < NUM_SAMPLE_POINTS) && (calibration_results[relative_index] < val))
			{
				val = calibration_results[relative_index];
			}
		}

		smoothed_cal_vals[i] = val * scale_factor;

		if (val != calibration_results[i])
		{
			adjusted++;
			adjustment_sum += calibration_results[i] - val;
		}

	}
	std::cout << "Smoothing complete. " << adjusted << " points have been adjusted. Avg ajdustment: " << adjustment_sum/double(adjusted) << "\n" << std::flush;
	
	// Assign the smoothed values as the final calibration results to be passed back up the stack
	for (int i = 0; i < NUM_SAMPLE_POINTS; i++)
	{
		calibration_results[i] = smoothed_cal_vals[i];
	}
}

bool Scanner::Start(RPlidarDriver *drv, int argc, const char * argv[])
{
	if (!(Initialize(drv, argc, argv)) || (!CheckRPLIDARHealth(drv)))
	{
		return false;
	}
	drv->startMotor();
	drv->startScan(0, 1);

	return true;
}

void Scanner::Stop(RPlidarDriver *drv)
{
	drv->stop();
	drv->stopMotor();
}

ScanResult Scanner::Scan(RPlidarDriver * drv, double(calibration_values)[NUM_SAMPLE_POINTS])
{
	u_result     op_result;
	ScanResult ret_val;
	ret_val.valid = false;

	// fetech result and print it out...
	rplidar_response_measurement_node_t nodes[NUM_SAMPLE_POINTS];
	size_t count = _countof(nodes);
	op_result = drv->grabScanData(nodes, count);

	if (IS_OK(op_result))
	{
		drv->ascendScanData(nodes, count);
		for (int pos = 0; pos < (int)count; ++pos)
		{
			double dist = nodes[pos].distance_q2 / 4.0f;
			if ((dist > 0) &&
				(dist < calibration_values[pos]) &&
				nodes[pos].sync_quality > 40)
			{
				ret_val.closest_distance = dist;
				ret_val.closest_angle = (nodes[pos].angle_q6_checkbit >> RPLIDAR_RESP_MEASUREMENT_ANGLE_SHIFT) / 64.0f;
				ret_val.closest_index = pos;
				ret_val.valid = true;
				//std::cout << "accepting b/c " << dist << " is less than " << calibration_values[pos] << " for " << shortest_angle << std::endl;
			}

			//printf("%s theta: %03.2f Dist: %08.2f Q: %d \n", 
			//    (nodes[pos].sync_quality & RPLIDAR_RESP_MEASUREMENT_SYNCBIT) ?"S ":"  ", 
			//    (nodes[pos].angle_q6_checkbit >> RPLIDAR_RESP_MEASUREMENT_ANGLE_SHIFT)/64.0f,
			//    nodes[pos].distance_q2/4.0f,
			//    nodes[pos].sync_quality >> RPLIDAR_RESP_MEASUREMENT_QUALITY_SHIFT);
		}
	}	
	return ret_val;
}

