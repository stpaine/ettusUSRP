#include <csignal>
#include <iostream>
#include <vector>
#include <complex>
#include <thread>
#include <fstream>

#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/exception.hpp>
#include <uhd/types/tune_request.hpp>
#include <uhd/types/device_addr.hpp>
#include <uhd/utils/thread.hpp>


//==============================================================================
int main (int argc, char* argv[])
{
    // Network adapters need some configuration to work with x300. This script does all that. Proved to work for GNU Radio 100 times before.
    std::cout << "Configuring network adapter settings" << std::endl;
    system(".usrp_x300_init.sh");

    uhd::device_addr_t devAddresses;
    
    devAddresses["addr0"] = "192.168.40.2";
    devAddresses["addr1"] = "192.168.50.2";

    // construct a multi usrp from the device adresses
    std::cout << "Constructing the multi USRP object" << std::endl;
    uhd::usrp::multi_usrp::sptr usrp = uhd::usrp::multi_usrp::make (devAddresses);
    
    // clocking and syncing
    usrp->set_clock_source ("internal");
    std::cout << "Setting device timestamp" << std::endl;
    usrp->set_time_source ("internal");
    usrp->set_time_unknown_pps(uhd::time_spec_t(0.0));
    std::this_thread::sleep_for (std::chrono::seconds(1));
    
    // choose the subdevices to use
    usrp->set_rx_subdev_spec(uhd::usrp::subdev_spec_t("A:0 B:0"), 0);
    usrp->set_rx_subdev_spec(uhd::usrp::subdev_spec_t("A:0 B:0"), 1);
    
    // choose the antenna ports for each subdevice
    usrp->set_rx_antenna ("RX1", 0);
    usrp->set_rx_antenna ("RX1", 1);
    usrp->set_rx_antenna ("RX1", 2);
    usrp->set_rx_antenna ("RX1", 3);
    
    // set center frequency, samplerate and bandwidth
    uhd::tune_request_t rxCenterFreq (1e9);
    usrp->set_rx_freq (rxCenterFreq);
    usrp->set_rx_rate(10e6);
    usrp->set_rx_bandwidth (10e6, 0);
    usrp->set_rx_bandwidth (10e6, 1);
    usrp->set_rx_bandwidth (10e6, 2);
    usrp->set_rx_bandwidth (10e6, 3);

    // all gains should be normalized
    usrp->set_rx_gain (40, 0);
    usrp->set_rx_gain (40, 1);
    usrp->set_rx_gain (40, 2);
    usrp->set_rx_gain (40, 3);
    
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
	const int totalSamplesToReceive = 100000000;//rate * total_time;
    const int numSamplesToReceive = samplesPerBuffer;
    std::vector<std::vector<float>> fileBuffers (numRxChannels, std::vector<float> (numSamplesToReceive));
    
    // create the start command
    uhd::stream_cmd_t startCmd = uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS;
    startCmd.stream_now = false;
    startCmd.time_spec = uhd::time_spec_t (1.9);
    rxStream->issue_stream_cmd(startCmd);
    
    
    int numSamplesReceived = 0;
    uhd::rx_metadata_t rxMetadata;
    std::cout << "Starting to receive" << std::endl;
    
    
    // Start receiving
	bool null = false;
	bool close = false;
    while (numSamplesReceived < totalSamplesToReceive) {
        
        int numSamplesForThisBlock = totalSamplesToReceive - numSamplesReceived;
        // receive a complete buffer or the last missing samples
        if (numSamplesForThisBlock > samplesPerBuffer) {
            numSamplesForThisBlock = samplesPerBuffer;
		}
        else {
			std::cout << "close" << std::endl;
			close = true;
		}
        size_t numNewSamples = rxStream->recv(buffPtrs, numSamplesForThisBlock, rxMetadata);
        
        // copy the received samples to the file buffer
        for (int i = 0; i < numRxChannels; i++) {
            // ---- the only non stl or uhd call here - just a simple simd-accelerated vectore copy that definetively works and won't be the problem. -------
            // copying complex<float> values to a plain float vector - so 2 floats to copy for each complex sample
			float* pDestination 			= fileBuffers[i].data();
			std::complex<float>* pSource	= buffPtrs[i];
			memcpy (pDestination, pSource, numNewSamples*sizeof(float));
			
			std::string filePath ("/home/radar/Documents/testRecordings/");
			std::ofstream outfile;
			std::string fileName(filePath + "chan" + std::to_string (i) + ".bin"); 
			//if (not null) {
				//std::cout << "Creating file: " << i << std::endl;
				outfile.open(fileName, std::ofstream::app);
			//}
			
			outfile.write(reinterpret_cast<char*> (fileBuffers[i].data()), numNewSamples * sizeof (float));
			
			//if (close){
				outfile.close();
				//std::cout << "Closing file: " << i << std::endl;
			//}
		}
		null = true;
/*		
		std::cout << "numNewSamples: " << numNewSamples << std::endl;
		std::cout << "numSamplesReceived: " << numSamplesReceived << std::endl;
		std::cout << "totalSamplesToReceive: " << totalSamplesToReceive << std::endl;
*/		
        // increase the received samples count        
		numSamplesReceived += numNewSamples;
    }
        
    return 0;
}