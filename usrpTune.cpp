#include <iostream>
#include <vector>
#include <complex>
#include <thread>
#include <fstream>
#include <csignal>
#include <chrono>

#include <boost/program_options.hpp>
#include <boost/format.hpp>

#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/exception.hpp>
#include <uhd/types/tune_request.hpp>
#include <uhd/types/device_addr.hpp>
#include <uhd/utils/thread.hpp>

namespace po = boost::program_options;
//==============================================================================

int main (int argc, char* argv[]){
	uhd::set_thread_priority_safe();
	
    // Network adapters need some configuration to work with x300. This script does all that. Proved to work for GNU Radio 100 times before.
    std::cout << "Configuring network adapter settings" << std::endl;
	// NB: This file should be used to set ALL variables
    system("./usrp_x300_init.sh");

	//variables to be set by po
    std::string devAddresses, file, ref, pps;
    size_t spb;
    double rate, freq, bw, setup_time, wait_for_lock, tuneMin, tuneMax, stepSize, total_num_samps;
	uhd::rx_metadata_t md;
	
    //setup the program options
    po::options_description desc("Allowed options");
    desc.add_options()
        ("help", "help message")
        ("dev", po::value<std::string>(&devAddresses)->default_value("addr0=192.168.40.2"), "multi uhd device address args")
        ("file", po::value<std::string>(&file)->default_value("usrp_samples.bin"), "name of the file to write binary samples to")
        ("spb", po::value<size_t>(&spb), "samples per buffer")
		("tuneMin", po::value<double>(&tuneMin)->default_value(0.0), "Minimum tune range")
		("tuneMax", po::value<double>(&tuneMax)->default_value(70.0), "Maximum tune range")
		("step", po::value<double>(&stepSize)->default_value(1.0), "Setp size")
		("nsamps", po::value<double>(&total_num_samps)->default_value(100.0), "total number of samples to receive")
        ("rate", po::value<double>(&rate)->default_value(0.0), "rate of incoming samples")
        ("freq", po::value<double>(&freq)->default_value(0.0), "RF center frequency in Hz")
		("wait", po::value<double>(&wait_for_lock)->default_value(120), "wait time for gps lock")
        ("bw", po::value<double>(&bw)->default_value(0.0), "analog frontend filter bandwidth in Hz")
        ("pps", po::value<std::string>(&pps)->default_value("internal"), "pps source (gpsdo, internal, external)")
		("ref", po::value<std::string>(&ref)->default_value("internal"), "reference source (gpsdo, internal, external)")
        ("setup", po::value<double>(&setup_time)->default_value(1.0), "seconds of setup time")
    ;
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    //print the help message
    if (vm.count("help")) {
        std::cout << boost::format("USRP Tune Gain %s") % desc << std::endl;
        std::cout << std::endl << "This application tunes a single USRP channel for the correct gain.\n" << std::endl;
        return ~0;
    }
	
    // construct a multi usrp from the device adresses
    std::cout << "\nConstructing the multi USRP object" << std::endl;
    uhd::usrp::multi_usrp::sptr usrp = uhd::usrp::multi_usrp::make (devAddresses);
	
	// setup the sub device and antenna ports to be used with each channel
	// subdev_spec_t((daughterboard, daughterboard channel),URSP Number))
	// set_rx_antenna(port, system channel)
	usrp->set_rx_subdev_spec(uhd::usrp::subdev_spec_t("A:0"), 0);
	usrp->set_rx_antenna ("RX1",0);	

	double mcr0 = usrp->get_master_clock_rate(0);
	std::cout << "\nMaster clock for USRP 1: " << mcr0 << std::endl;
	
	// clocking and syncing
	if(ref == "gpsdo" or pps == "gpsdo") {
		size_t num_mboards    = usrp->get_num_mboards();
		size_t num_gps_locked = 0;
		for (size_t mboard = 0; mboard < num_mboards; mboard++) {
			std::cout << "Synchronizing mboard " << mboard << ": " << usrp->get_mboard_name(mboard) << std::endl;			
			// Wait for GPS lock
			uhd::sensor_value_t gps_locked = usrp->get_mboard_sensor("gps_locked", mboard);
			// Wait 2 minutes for the clock to settle
			std::cout << "\nWaiting for GPS lock\n" << std::flush;
			for (int i = 0; i < wait_for_lock and not gps_locked.to_bool(); i++) {
				gps_locked = usrp->get_mboard_sensor("gps_locked", mboard);
				if (gps_locked.to_bool()) {
					num_gps_locked++;
				} else {
					std::cout << i+1 << "/" << wait_for_lock << "\r" << std::flush;
					std::this_thread::sleep_for(std::chrono::seconds(1));
				}
			}
			
			// check for gps and reference clock lock
			gps_locked = usrp->get_mboard_sensor("gps_locked", mboard);
			uhd::sensor_value_t ref_locked = usrp->get_mboard_sensor("ref_locked", mboard);
			
			if (gps_locked.to_bool() and ref_locked.to_bool()) {
				// Set to GPS time
				std::cout << "\nGPS LOCKED on mboard: " << mboard << std::endl << std::endl;
				usrp->set_time_source(pps, mboard);
				usrp->set_clock_source(ref, mboard);
				
				const uhd::time_spec_t last_pps_time = usrp->get_time_last_pps();
				while (last_pps_time == usrp->get_time_last_pps()){
					//sleep 100 milliseconds (give or take)
					std::this_thread::sleep_for (std::chrono::milliseconds(50));
				}
				// Sync the GPS and USRP clocks
				// TODO: I am not sure if we need to actually set this manually or whether the driver handles this for you??
				// As I understand it from the documentation, its automatic: https://files.ettus.com/manual/page_sync.html
				
				// TODO: THIS DOES NOT WORK PROPERLY
				// uhd::time_spec_t gps_time = uhd::time_spec_t(int64_t(usrp->get_mboard_sensor("gps_time", mboard).to_int()));
				// usrp->set_time_next_pps(gps_time + 1, mboard);
				// usrp->set_time_next_pps(uhd::time_spec_t(usrp->get_mboard_sensor("gps_time").to_int()+1.0), mboard);
				
				usrp->set_time_next_pps(uhd::time_spec_t(0.0), mboard);
				
				// TODO: This resyncs the two boards but this needs to be improved
				if (mboard == 1) {
					usrp->set_time_next_pps(uhd::time_spec_t(0.0), 0);
					usrp->set_time_next_pps(uhd::time_spec_t(0.0), 1);
				}

			} else {
				// Set to unsynced time.
				std::cout << "\nNO GPS LOCK\n" << std::endl;
				return ~0;
			}
		}	
	} else {
		size_t num_mboards = usrp->get_num_mboards();
		for (size_t mboard = 0; mboard < num_mboards; mboard++) {
			std::cout << "Setting device timestamp" << std::endl;
			std::cout << "Synchronizing mboard " << mboard << ": " << usrp->get_mboard_name(mboard) << std::endl;
			usrp->set_clock_source(ref, mboard);
			usrp->set_time_source(pps, mboard);
			// set_sync_source(device_addr_t("clock_source=$CLOCK_SOURCE,time_source=$TIME_SOURCE"))
			const uhd::time_spec_t last_pps_time = usrp->get_time_last_pps();
			while (last_pps_time == usrp->get_time_last_pps()){
				//sleep 100 milliseconds (give or take)
				std::this_thread::sleep_for (std::chrono::milliseconds(50));
			}
			// This command will be processed fairly soon after the last PPS edge:
			usrp->set_time_next_pps(uhd::time_spec_t(0.0), mboard);
			// TODO: This resyncs the two boards but this needs to be improved
			if (mboard == 1) {
				usrp->set_time_next_pps(uhd::time_spec_t(0.0), 0);
				usrp->set_time_next_pps(uhd::time_spec_t(0.0), 1);
			}
		}
	}
	
	// Once set, we need to wait for the settings to propagate through the system
	std::this_thread::sleep_for (std::chrono::seconds(1));
	
	//set the center frequency
    std::cout << boost::format("\nSetting RX Freq: %f MHz...") % (freq/1e6) << std::endl;
    uhd::tune_request_t tune_request(freq);
    if(vm.count("int-n")) {
		tune_request.args = uhd::device_addr_t("mode_n=integer");
	}
	// We need to address and setup each channel
	usrp->set_rx_freq(tune_request);
	std::cout << boost::format("Actual RX Freq: %f MHz...") % (usrp->get_rx_freq()/1e6) << std::endl << std::endl;

    //set the sample rate
    if (rate <= 0.0){
        std::cerr << "Please specify a valid sample rate" << std::endl;
        return ~0;
    }
    std::cout << boost::format("Setting RX Rate: %f Msps...") % (rate/1e6) << std::endl;
	// We need to address and setup each channel
	usrp->set_rx_rate(rate);
	std::cout << boost::format("Actual RX Rate: %f Msps...") % (usrp->get_rx_rate()/1e6) << std::endl << std::endl;
	
	//set the IF filter bandwidth, by default it is set to the sampling rate
	if (bw <= 0.0) {
		bw = rate;
	}
	std::cout << boost::format("Setting RX Bandwidth: %f MHz...") % (bw/1e6) << std::endl;
	usrp->set_rx_bandwidth(bw);
	std::cout << boost::format("Actual RX Bandwidth: %f MHz...") % (usrp->get_rx_bandwidth()/1e6) << std::endl << std::endl;

	double gain = tuneMin;
	std::cout << "Starting calibration" << std::endl;
	while (gain <= tuneMax) {
		//set the rf gain for each channel
		std::cout << boost::format("\nSetting RX Gain: %f dB...") % gain << std::endl;
		usrp->set_rx_gain (gain);
		std::cout << boost::format("Actual RX Gain: %f dB...") % usrp->get_rx_gain() << std::endl << std::endl;
	
		// give the device a little bit of time to configure
		std::this_thread::sleep_for (std::chrono::milliseconds(100));
		
		// this will map the subdevice inputs to the input channels and create the input stream
		uhd::stream_args_t rxStreamArgs ("sc16");
		rxStreamArgs.channels.push_back(0);
		uhd::rx_streamer::sptr rxStream = usrp->get_rx_stream (rxStreamArgs);
		
		// print some general information
		unsigned int numRxChannels = rxStream->get_num_channels();
		
		// allocate buffers to receive with samples (one buffer per channel)
		const int samplesPerBuffer = rxStream->get_max_num_samps();
		std::vector<std::vector<std::complex<short>>> buffs (numRxChannels, std::vector<std::complex<short>> (samplesPerBuffer));
		std::cout << "Allocated " << numRxChannels << " buffers with " << samplesPerBuffer << " complex short samples" << std::endl;

		// create a vector of pointers to point to each of the channel buffers
		std::vector<std::complex<short> *> buffPtrs;
		for (unsigned int i = 0; i < buffs.size(); i++) {
			//buffPtrs.push_back(buffs[i].data());
			buffPtrs.push_back(&buffs[i].front());
		}
			
		// allocate one big plain short buffer per channel for the final channels to store to disk
		int numSamplesToReceive = samplesPerBuffer;
		if (vm.count("spb")) {
			numSamplesToReceive = spb;
		}
		
		std::vector<std::vector<short>> fileBuffers (numRxChannels, std::vector<short> (2*numSamplesToReceive));

		// set the total number of samples to receive
		int totalSamplesToReceive = total_num_samps;
		
		// create the start command
		uhd::stream_cmd_t startCmd = uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS;
		startCmd.stream_now = false;
		startCmd.time_spec = uhd::time_spec_t (1.9);
		rxStream->issue_stream_cmd(startCmd);
		
		int numSamplesReceived = 0;
		uhd::rx_metadata_t rxMetadata;
		
		// Start receiving
		// Write metadata to file	
		std::string filePath (file);
		std::ofstream metadata;
		std::string fileName(filePath + "_metadata.txt");
		metadata.open(fileName);
		
		// TODO: Need the EPOCH parser
		uhd::sensor_value_t gps_locked = usrp->get_mboard_sensor("gps_locked");
		uhd::sensor_value_t NMEA = usrp->get_mboard_sensor("gps_gpgga");
		metadata << boost::format("Device: %s") % devAddresses << std::endl;
		metadata << boost::format("Clock Reference: %s") % ref << std::endl;
		if (gps_locked.to_bool()) {
			uhd::sensor_value_t gps_time = usrp->get_mboard_sensor("gps_time");
			metadata << boost::format("Start %s") % gps_time.to_pp_string() << std::endl;
		} else {
			uhd::time_spec_t gps_time = usrp->get_time_last_pps();
			// TODO: This needs to be fixed to display the correct CPU
			metadata << boost::format("Start time: %0.9f") % gps_time.get_real_secs() << std::endl;
		}
		metadata << boost::format("%s") % gps_locked.to_pp_string() << std::endl;
		metadata << boost::format("Total samples per step: %i") % totalSamplesToReceive << std::endl;
		// TODO: parse NMEA data
		metadata << boost::format("Lat: %s") % NMEA.to_pp_string() << std::endl;
		metadata << boost::format("Lon: %s") % NMEA.to_pp_string() << std::endl;
		metadata << boost::format("Channels: %i") % numRxChannels << std::endl;
		for (unsigned int i = 0; i < numRxChannels; i++) {
			metadata << boost::format("Channel %i parameters:") % i << std::endl;
			metadata << boost::format("Fc: %f [MHz]") % (usrp->get_rx_freq(i)/1e6) << std::endl;
			metadata << boost::format("BW: %f [MHz]") % (usrp->get_rx_bandwidth(i)/1e6) << std::endl;
			metadata << boost::format("Fs: %f [Msps]") % (usrp->get_rx_rate(i)/1e6) << std::endl;
			metadata << boost::format("Min Gain: %f [dB]") % (tuneMin) << std::endl;
			metadata << boost::format("Max Gain: %f [dB]") % (tuneMax) << std::endl;
			metadata << boost::format("Step Size: %f [dB]") % (stepSize) << std::endl;
		}
		metadata.close();
				
		while (numSamplesReceived < totalSamplesToReceive) {
			int numSamplesForThisBlock = totalSamplesToReceive - numSamplesReceived;
			// receive a complete buffer or the last missing samples
			if (numSamplesForThisBlock > samplesPerBuffer) {
				numSamplesForThisBlock = samplesPerBuffer;
			}
			
			// request new data from the uhd driver
			size_t numNewSamples = rxStream->recv(buffPtrs, numSamplesForThisBlock, rxMetadata);
			// copy the received samples to the file buffer
			for (unsigned int i = 0; i < numRxChannels; i++) {
				// copying complex<short> values to a plain short vector - so 2 shorts to copy for each complex sample
				short* pDestination 			= fileBuffers[i].data();
				std::complex<short>* pSource	= buffPtrs[i];
				memcpy (pDestination, pSource, 2 * numNewSamples * sizeof(short));
				
				// write buffer to file
				// NOTE: currently this is a terrible implementation and will just append to any existing file, this should be fixed
				std::string filePath (file);
				std::ofstream outfile;
				std::string fileName(filePath + "_chan" + std::to_string (i) + ".bin"); 
				outfile.open(fileName, std::ofstream::app);
				outfile.write(reinterpret_cast<char*> (fileBuffers[i].data()), 2 * numNewSamples * sizeof (short));
			}

	/*
			// Write the time to file for sync purposes
			uhd::sensor_value_t gps_time = usrp->get_mboard_sensor("gps_time");
			// uhd::sensor_value_t NMEA = usrp->get_mboard_sensor("gps_gpgga");
			
			std::string filePath (file);
			std::ofstream gps_data;
			std::string fileName(filePath + "_gpsData.txt");
			gps_data.open(fileName, std::ofstream::app);
			gps_data << boost::format("%s") % gps_time.to_pp_string() << std::endl;
			gps_data.close();		
	*/

			// increase the received samples count
			// NOTE: for some reason, this does not always update, this should be investigated
			numSamplesReceived += numNewSamples;
		}
		std::cout << boost::format("\nGain: %f dB complete") % gain << std::endl;
		gain = gain + stepSize;
		std::this_thread::sleep_for (std::chrono::milliseconds(200));
	}
    return 0;
}