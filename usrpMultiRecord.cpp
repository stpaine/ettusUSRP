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
    std::string devAddresses, file, type, ref;
    size_t total_num_samps, spb;
    double rate, freq, gain, bw, total_time, setup_time;
	uhd::rx_metadata_t md;
	
    //setup the program options
    po::options_description desc("Allowed options");
    desc.add_options()
        ("help", "help message")
        ("dev", po::value<std::string>(&devAddresses)->default_value("addr0=192.168.40.2"), "multi uhd device address args")
        ("file", po::value<std::string>(&file)->default_value("usrp_samples.bin"), "name of the file to write binary samples to")
        ("type", po::value<std::string>(&type)->default_value("float"), "sample type: double, float, or short")
        ("nsamps", po::value<size_t>(&total_num_samps), "total number of samples to receive")
        ("duration", po::value<double>(&total_time)->default_value(0), "total number of seconds to receive")
        ("spb", po::value<size_t>(&spb), "samples per buffer")
        ("rate", po::value<double>(&rate)->default_value(0.0), "rate of incoming samples")
        ("freq", po::value<double>(&freq)->default_value(0.0), "RF center frequency in Hz")
        ("gain", po::value<double>(&gain)->default_value(0.0), "gain for the RF chain")
        ("bw", po::value<double>(&bw)->default_value(0.0), "analog frontend filter bandwidth in Hz")
        ("ref", po::value<std::string>(&ref)->default_value("gpsdo"), "reference source (gpsdo, internal, external, mimo)")
        ("setup", po::value<double>(&setup_time)->default_value(1.0), "seconds of setup time")
    ;
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    //print the help message
    if (vm.count("help")) {
        std::cout << boost::format("Rx multi samples to file %s") % desc << std::endl;
        std::cout
            << std::endl
            << "This application streams data from a USRP x300 with two TwinRXs to file.\n"
            << std::endl;
        return ~0;
    }

    // construct a multi usrp from the device adresses
    std::cout << "\nConstructing the multi USRP object" << std::endl;
    uhd::usrp::multi_usrp::sptr usrp = uhd::usrp::multi_usrp::make (devAddresses);
	
    // clocking and syncing
	if(vm.count("ref") and ref != "gpsdo") {
		usrp->set_clock_source (ref);
		std::cout << "Setting device timestamp" << std::endl;
		usrp->set_time_source (ref);
		usrp->set_time_unknown_pps(uhd::time_spec_t(0.0));
		std::this_thread::sleep_for (std::chrono::seconds(1));
	} else {
		// Set references to GPSDO
		size_t num_gps_locked = 0;
		int wait_for_lock = 120;		
		
		// Wait for GPS lock
		bool gps_locked = false;
		// Wait 2 minutes for the clock to settle
		std::cout << "\nWaiting for GPS lock\n" << std::flush;
		for (int i = 0; i < wait_for_lock and not gps_locked; i++) {
			gps_locked = usrp->get_mboard_sensor("gps_locked").to_bool();
			if (gps_locked) {
				num_gps_locked++;
			} else {
				std::cout << i+1 << "/" << wait_for_lock << "\r" << std::flush;
				std::this_thread::sleep_for(std::chrono::seconds(1));
			}
		}
	}

	// check for gps lock
	uhd::sensor_value_t gps_locked = usrp->get_mboard_sensor("gps_locked");
	if (gps_locked.to_bool() and ref == "gpsdo") {
		// Set to GPS time					
		std::cout << "\nGPS LOCKED\n" << std::endl;
		usrp->set_time_source("gpsdo");
		usrp->set_clock_source("gpsdo");
		
		// TODO: I am not sure if we need to actually set this manually or whether the driver handles this for you??
		// As I understand it from the documentation, its automatic: https://files.ettus.com/manual/page_sync.html
		
		usrp->set_time_unknown_pps(uhd::time_spec_t(0.0));
		std::this_thread::sleep_for (std::chrono::seconds(1));
	} else {
		// Set to unsynced time.
		std::cout << "\nNO GPS LOCK\nSync to internal CPU instead\n" << std::endl;
		// We need to reset the clock source to internal if GPS lock fails
		usrp->set_clock_source ("internal");
		usrp->set_time_source ("internal");
		usrp->set_time_unknown_pps(uhd::time_spec_t(0.0));
		std::this_thread::sleep_for (std::chrono::seconds(1));
	}
	
	// setup the sub device
	usrp->set_rx_subdev_spec(uhd::usrp::subdev_spec_t("A:0 A:1 B:0 B:1"), 0);
	
	// setup the antenna ports to be used with each channel
	// daughterboard A (port, channel number)
	usrp->set_rx_antenna ("RX1",0);
	usrp->set_rx_antenna ("RX2",1);
	// daughterboard B (port, channel number)
	usrp->set_rx_antenna ("RX1",2);
	usrp->set_rx_antenna ("RX2",3);

	//set the center frequency
    std::cout << boost::format("\nSetting RX Freq: %f MHz...") % (freq/1e6) << std::endl;
    uhd::tune_request_t tune_request(freq);
    if(vm.count("int-n")) tune_request.args = uhd::device_addr_t("mode_n=integer");
    usrp->set_rx_freq(tune_request);
    std::cout << boost::format("Actual RX Freq: %f MHz...") % (usrp->get_rx_freq()/1e6) << std::endl << std::endl;

    //set the sample rate
    if (rate <= 0.0){
        std::cerr << "Please specify a valid sample rate" << std::endl;
        return ~0;
    }
    std::cout << boost::format("Setting RX Rate: %f Msps...") % (rate/1e6) << std::endl;
    usrp->set_rx_rate(rate);
    std::cout << boost::format("Actual RX Rate: %f Msps...") % (usrp->get_rx_rate()/1e6) << std::endl << std::endl;
	
	//set the IF filter bandwidth, by default it is set to the sampling rate
	if (bw <= 0.0) {
		bw = rate;
	}
	std::cout << boost::format("Setting RX Bandwidth: %f MHz...") % (bw/1e6) << std::endl;
	usrp->set_rx_bandwidth(bw,0);
	usrp->set_rx_bandwidth(bw,1);
	usrp->set_rx_bandwidth(bw,2);
	usrp->set_rx_bandwidth(bw,3);
	std::cout << boost::format("Actual RX Bandwidth: %f MHz...") % (usrp->get_rx_bandwidth()/1e6) << std::endl << std::endl;
		
    //set the rf gain, the default gain is 0
	std::cout << boost::format("Setting RX Gain: %f dB...") % gain << std::endl;
	usrp->set_rx_gain (gain,0);
	usrp->set_rx_gain (gain,1);
	usrp->set_rx_gain (gain,2);
	usrp->set_rx_gain (gain,3);
    std::cout << boost::format("Actual RX Gain: %f dB...") % usrp->get_rx_gain() << std::endl << std::endl;
 	
    // give the device a little bit of time to configure
    std::this_thread::sleep_for (std::chrono::milliseconds(100));
    
    // this will map the subdevice inputs to the input channels and create the input stream
    uhd::stream_args_t rxStreamArgs ("fc32", "sc16");
    rxStreamArgs.channels.push_back(0);
    rxStreamArgs.channels.push_back(1);
    rxStreamArgs.channels.push_back(2);
    rxStreamArgs.channels.push_back(3);
    uhd::rx_streamer::sptr rxStream = usrp->get_rx_stream (rxStreamArgs);
    
    // print some general information
    int numRxChannels = rxStream->get_num_channels();
    std::cout << "Set up RX stream. Num input channels: " << numRxChannels << std::endl;
    std::cout << usrp->get_pp_string();
    
    // allocate buffers to receive with samples (one buffer per channel)
    const int samplesPerBuffer = rxStream->get_max_num_samps();
    std::vector<std::vector<std::complex<float>>> buffs (numRxChannels, std::vector<std::complex<float>> (samplesPerBuffer));
    std::cout << "Allocated " << numRxChannels << " buffers with " << samplesPerBuffer << " complex float samples" << std::endl;

    // create a vector of pointers to point to each of the channel buffers
    std::vector<std::complex<float> *> buffPtrs;
    for (int i = 0; i < buffs.size(); i++) 
        buffPtrs.push_back(buffs[i].data());
        
    // allocate one big plain float buffer per channel for the final channels to store to disk
	int numSamplesToReceive = samplesPerBuffer;
    if (vm.count("spb")) {
		numSamplesToReceive = spb;
	}
	
    std::vector<std::vector<float>> fileBuffers (numRxChannels, std::vector<float> (numSamplesToReceive));

	// set the total number of samples to receive
	int totalSamplesToReceive = rate * total_time;
	if (vm.count("nsamps")) {
		totalSamplesToReceive = total_num_samps;
	}
    
	// create the start command
    uhd::stream_cmd_t startCmd = uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS;
    startCmd.stream_now = false;
    startCmd.time_spec = uhd::time_spec_t (1.9);
    rxStream->issue_stream_cmd(startCmd);
    
    int numSamplesReceived = 0;
    uhd::rx_metadata_t rxMetadata;
    std::cout << "Starting to receive" << std::endl;
    
    // Start receiving
	// Write metadata to file	
	std::string filePath (file);
	std::ofstream metadata;
	std::string fileName(filePath + "_metadata.txt");
	metadata.open(fileName);
	
	// TODO: Need the EPOCH parser
	uhd::sensor_value_t NMEA = usrp->get_mboard_sensor("gps_gpgga");
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
	metadata << boost::format("Duration: %i [s]") % total_time << std::endl;
	metadata << boost::format("Total samples: %i") % totalSamplesToReceive << std::endl;
	// TODO: parse NMEA data
	metadata << boost::format("Lat: %s") % NMEA.to_pp_string() << std::endl;
	metadata << boost::format("Lon: %s") % NMEA.to_pp_string() << std::endl;
	metadata << boost::format("Channels: %i") % numRxChannels << std::endl;
	metadata << boost::format("Fc: %f [MHz]") % (usrp->get_rx_freq()/1e6) << std::endl;
	metadata << boost::format("BW: %f [MHz]") % (usrp->get_rx_bandwidth()/1e6) << std::endl;
	metadata << boost::format("Fs: %f [Msps]") % (usrp->get_rx_rate()/1e6) << std::endl;
	metadata << boost::format("Gain: %f [dB]") % (usrp->get_rx_gain()) << std::endl;
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
        for (int i = 0; i < numRxChannels; i++) {
            // copying complex<float> values to a plain float vector - so 2 floats to copy for each complex sample
			float* pDestination 			= fileBuffers[i].data();
			std::complex<float>* pSource	= buffPtrs[i];
			memcpy (pDestination, pSource, numNewSamples*sizeof(float));
			
			// write buffer to file
			// NOTE: currently this will just append to any existing file, this should be changed and improved.
			std::string filePath (file);
			std::ofstream outfile;
			std::string fileName(filePath + "_chan" + std::to_string (i) + ".bin"); 
			outfile.open(fileName, std::ofstream::app);			
			outfile.write(reinterpret_cast<char*> (fileBuffers[i].data()), numNewSamples * sizeof (float));
			outfile.close();
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
	std::cout << "\nStopped Recording" << std::endl;
    return 0;
}