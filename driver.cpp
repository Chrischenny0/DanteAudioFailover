#include <iostream>
#include <atomic>
#include <thread>
#include <windows.h>
#include <fstream>
#include <cstring>
#include <ctime>
#include <libremidi/libremidi.hpp>
#include "asiosys.h"
#include "asio.h"
#include "asiodrivers.h"

using namespace std;


//ASIO DRIVER DATA
bool loadAsioDriver(char *name);

enum {
    // number of input and outputs supported by the host application
    // you can change these to higher or lower values
    kMaxInputChannels = 64,
    kMaxOutputChannels = 64
};

typedef struct driverData
{
    // ASIOInit()
    ASIODriverInfo driverInfo;

    // ASIOGetChannels()
    long           inputChannels;
    long           outputChannels;

    // ASIOGetBufferSize()
    long           minSize;
    long           maxSize;
    long           preferredSize;
    long           granularity;

    // ASIOGetSampleRate()
    ASIOSampleRate sampleRate;

    // ASIOOutputReady()
    bool           postOutput;

    // ASIOGetLatencies ()
    long           inputLatency;
    long           outputLatency;

    // ASIOCreateBuffers ()
    long inputBuffers;	// becomes number of actual created input buffers
    long outputBuffers;	// becomes number of actual created output buffers
    ASIOBufferInfo bufferInfos[kMaxInputChannels + kMaxOutputChannels]; // buffer info's

    // ASIOGetChannelInfo()
    ASIOChannelInfo channelInfos[kMaxInputChannels + kMaxOutputChannels]; // channel info's
    // The above two arrays share the same indexing, as the data in them are linked together

    // Signal the end of processing in this example
    atomic_bool    stop = false;
    atomic_bool    failOver = false;
    atomic_bool    halt = false;

    //Midi Flags
    atomic_bool    manualSwitch = false;
    atomic_bool    midiLearn = false;


    //Midi Driver
    libremidi::midi_in input;
    libremidi::midi_out midiOut;

    //Current Midi Note Switch
    libremidi::message message;

} DriverInfo;

DriverInfo driverData = {0};

//DRIVER CALLBACKS
//Main data process callback
ASIOTime *bufferSwitchTimeInfo(ASIOTime *, long, ASIOBool);

//Purely for backwards compatibility, just calls bufferSwitchTimeInfo
void bufferSwitch(long, ASIOBool);

//Placeholder for right now
void sampleRateChanged(ASIOSampleRate);

//Get messages from the driver to the application
long asioMessages(long, long, void*, double*);


//APPLICATION FUNCTIONS
//Read command via midi input
void readMidi();

//Learn the new manual switch midi note
void learnMidi();

//Get cli input message from the user
void getUserMessages();

int main() {
    do{
        Sleep(20);
        fstream programData("prog.dat", ios::in | ios::binary);
        AsioDrivers currDrivers;

        //INITIALIZE MIDI
        while(!driverData.input.is_port_open() || !driverData.midiOut.is_port_open()){
            for(int i = 0; i < driverData.input.get_port_count(); i++){

                //Find the specified midi port
                if(driverData.input.get_port_name(i) == "NUCMidi 1"){
                    driverData.input.open_port(i);
                    cout << "MIDI Input Port FOUND: " << driverData.input.get_port_name(i) << endl;
                    break;
                }
            }
            Sleep(150);

            for(int i = 0; i < driverData.midiOut.get_port_count(); i++){
                //Find the specified midi port
                if(driverData.midiOut.get_port_name(i) == "NUCMidi 1"){
                    driverData.midiOut.open_port(i);
                    cout << "MIDI Output Port FOUND: " << driverData.midiOut.get_port_name(i) << endl;
                    break;
                }
            }
            Sleep(150);

            //Keep looping till the correct midi port is found
            if(!driverData.input.is_port_open()){
                cout << "MIDI INPUT NETWORK NOT FOUND: Network name should be \"NUCMidiIn\"" << endl;
                cout << "Retry in 3 Seconds" << endl;
                Sleep(3000);
                system("CLS");
            }
            //Keep looping till the correct midi port is found
            if(!driverData.midiOut.is_port_open()){
                cout << "MIDI OUTPUT NETWORK NOT FOUND: Network name should be \"NUCdriverData.midiOut\"" << endl;
                cout << "Retry in 3 Seconds" << endl;
                Sleep(3000);
                system("CLS");
            }
        }

        //INITIALIZE THE DRIVER
        char danteName[] = "Dante Virtual Soundcard (x64)";

        //Loop while dante virtual soundcard is not found
        do{
            loadAsioDriver(danteName);

            ASIOInit(&driverData.driverInfo);
            cout << driverData.driverInfo.name << endl;
            cout << ((!strcmp(driverData.driverInfo.errorMessage, "No ASIO Driver Error")) ? "No Errors" : driverData.driverInfo.errorMessage) << endl;

            if(strcmp(danteName, driverData.driverInfo.name)){
                Sleep(1000);
            }
        } while(strcmp(danteName, driverData.driverInfo.name));

        //INITIALIZE MIDI SWITCH NOTE
        vector<unsigned char> data = {0,0,0};
        programData.read((char*)&data.at(0), 3);
        programData.close();

        driverData.message.bytes = data;


        //GET NUMBER OF CHANNELS
        ASIOGetChannels(&driverData.inputChannels, &driverData.outputChannels);

        //Get DRIVER LATENCIES
        ASIOGetLatencies(&driverData.inputLatency, &driverData.outputLatency);

        //GET PREFERRED BUFFER SIZE
        ASIOGetBufferSize(&driverData.minSize, &driverData.maxSize, &driverData.preferredSize, &driverData.granularity);

        //GET SAMPLE RATE
        ASIOGetSampleRate(&driverData.sampleRate);

        //CHECK FOR OUTPUT READY
        if(ASIOOutputReady() == ASE_OK){
            driverData.postOutput = true;
        } else{
            driverData.postOutput = false;
        }

        //CREATE BUFFERS
        ASIOBufferInfo* indx = driverData.bufferInfos;
        ASIOChannelInfo *idx = driverData.channelInfos;
        ASIOCallbacks callbacks;

        //LOAD BUFFERS
        for(int i = 0; i < driverData.inputChannels; i++, indx++, idx++){
            idx->isInput = indx->isInput = ASIOTrue;
            idx->channel = indx->channelNum = i;
            indx->buffers[0] = indx->buffers[1] = nullptr;
        }
        for(int i = 0; i < driverData.outputChannels; i++, indx++, idx++){
            idx->isInput = indx->isInput = ASIOFalse;
            idx->channel = indx->channelNum = i;
            indx->buffers[0] = indx->buffers[1] = nullptr;
        }

        //PASS CALLBACKS TO DRIVER
        callbacks.bufferSwitch = &bufferSwitch;
        callbacks.bufferSwitchTimeInfo = &bufferSwitchTimeInfo;
        callbacks.asioMessage = &asioMessages;
        callbacks.sampleRateDidChange = &sampleRateChanged;

        //INITIALIZE BUFFERS IN THE DRIVER
        ASIOCreateBuffers(driverData.bufferInfos, driverData.inputChannels +
                                                      driverData.outputChannels, driverData.preferredSize, &callbacks);

        int channelTotal = driverData.inputChannels + driverData.outputChannels;
        for(int i = 0; i < channelTotal; i++) {
            ASIOGetChannelInfo(&driverData.channelInfos[i]);
        }

        //START THE AUDIO STREAM AND MIDI THREADS
        ASIOStart();
        thread cliInputs(getUserMessages);
        thread readMidiIn(readMidi);

        bool currComp = false;
        vector<unsigned char> buttonOn;
        buttonOn.push_back(144);
        buttonOn.push_back(93);
        buttonOn.push_back(127);

        vector<unsigned char> buttonOff;
        buttonOff.push_back(128);
        buttonOff.push_back(105);
        buttonOff.push_back(0);

        driverData.midiOut.send_message(buttonOn);

        while(!(driverData.stop || driverData.halt)){
            if(currComp != (driverData.failOver != driverData.manualSwitch)){
                if(!currComp){
                    driverData.midiOut.send_message(buttonOff);
                } else{
                    driverData.midiOut.send_message(buttonOn);
                }
                cout << "Switch LED" << endl;
                currComp = !currComp;
            }
            Sleep(10);
        }

        //END STREAM TO DRIVER
        ASIOStop();
        ASIODisposeBuffers();
        ASIOExit();

        //WAIT FOR THREADS
        readMidiIn.join();
        cliInputs.join();

        //CLOSE MIDI PORT
        driverData.input.close_port();
        driverData.midiOut.close_port();

        //RESET FLAGS
        driverData.halt = driverData.manualSwitch = driverData.failOver = false;
        system("CLS");
    } while(!driverData.stop);
    return 0;
}

void getUserMessages(){
    char command;

    cout << "WELCOME to Celebration Dante fail-over for Ableton: The program is already running" << endl;

    while(!driverData.halt){
        cout << "\nCurrent Computer: " << ((driverData.manualSwitch != driverData.failOver)? "SECONDARY" : "PRIMARY") << endl;
        cout << "[M]idi Learn  [R]estart  [S]top" << endl << endl;

        Sleep(20);

        cin >> command;

        switch (command) {
            case('M'):
                cout << "LEARNING MIDI, Timeout in 20 Seconds:" << endl;
                driverData.midiLearn = true;
                learnMidi();
                driverData.midiLearn = false;
                break;
            case('R'):
                cout << "RESTARTING: AUDIO WILL DROP FOR A SECOND" << endl;
                driverData.halt = true;
                break;
            case('S'):
                cout << "STOPPING PROGRAM: GOOD BYE" << endl;
                Sleep(100);
                driverData.stop = driverData.halt = true;
                break;
            default:
                cout << "MESSAGE NOT UNDERSTOOD" << endl;
                break;
        }
    }
}

void readMidi(){
    libremidi::message message;

    while(!driverData.halt){
        while(!(driverData.midiLearn || driverData.halt)){
            if(driverData.input.get_message(message)){
                if(!strncmp((char*)&message.bytes[0], (char*)&driverData.message.bytes[0], 3)){
                    if(driverData.failOver){
                        driverData.failOver = false;
                        system("CLS");
                        Sleep(2);
                        cout << ((driverData.manualSwitch)? "REVERTED TO SECONDARY":"REVERTED TO PRIMARY") << endl;
                    } else{
                        driverData.manualSwitch = !driverData.manualSwitch;
                        system("CLS");
                        Sleep(2);
                        cout << "MANUAL SWITCH" << endl;
                        cout << "Current Computer: " << ((driverData.manualSwitch != driverData.failOver)? "SECONDARY" : "PRIMARY") << endl;
                    }
                }
            }
        }
        Sleep(30);
    }
}

void learnMidi(){
    libremidi::message message;
    fstream programData("prog.dat", ios::out | ios::binary);

    auto startTime = time(nullptr);

    while(driverData.midiLearn && time(nullptr) - startTime < 20){
        Sleep(100);

        if(driverData.input.get_message(message)){
            memcpy((char*)&driverData.message.bytes.at(0), (char*)&message.bytes.at(0), driverData.message.bytes.size());
            cout << "Learned Midi" << endl;
            cout << "Message: " << (int)driverData.message.bytes.at(0) << " "
                 << (int)driverData.message.bytes.at(1) << " "
                 << (int)driverData.message.bytes.at(2) << endl;

            Sleep(1500);

            programData.write((char*)(&driverData.message.bytes.at(0)), (int)driverData.message.bytes.size());
            driverData.midiLearn = false;
        }
    }
    if(driverData.midiLearn){
        cout << "TIME OUT" << endl;
        Sleep(50);
        system("CLS");
        Sleep(50);
    }
    programData.close();
}



ASIOTime *bufferSwitchTimeInfo(ASIOTime *timeInfo, long index, ASIOBool processNow)
{    // buffer size in samples
    long buffSize = driverData.preferredSize;

    int currIdx = (index + 1) % 2;

    int priCount = 0, secCount = 0;

    int primary, secondary;

    primary = (!driverData.manualSwitch) ? 31 : 63;
    secondary = (!driverData.manualSwitch) ? 63 : 31;

    //Check signal channel for a dead signal
    if(!driverData.failOver) {
        for (int k = 0; k < 64; k++) {
            if (((char *) driverData.bufferInfos[primary].buffers[currIdx])[k] != 0) {
                priCount++;
            }
            if (((char *) driverData.bufferInfos[secondary].buffers[currIdx])[k] != 0) {
                secCount++;
            }
        }

        //Switch to back up if computer failed
        if (!priCount && secCount) {
            driverData.failOver = true;
            cout << "FAILOVER" << endl;
        }
    }

    int startChan = (!driverData.failOver != !driverData.manualSwitch) ? 32 : 0;

    //Copy either primary or secondary computer input stream to the output stream
    for(int i = 0; i < 31; i++){
        memcpy(driverData.bufferInfos[i + 64].buffers[index], driverData.bufferInfos[i + startChan].buffers[(index + 1) % 2], buffSize * 3);
    }

    // finally if the driver supports the ASIOOutputReady() optimization, do it here, all data are in place
    if (driverData.postOutput)
        ASIOOutputReady();

    return 0L;
}

void bufferSwitch(long index, ASIOBool processNow)
{	// the actual processing callback.
    // Beware that this is normally in a seperate thread, hence be sure that you take care
    // about thread synchronization. This is omitted here for simplicity.

    // as this is a "back door" into the bufferSwitchTimeInfo a timeInfo needs to be created
    // though it will only set the timeInfo.samplePosition and timeInfo.systemTime fields and the according flags
    ASIOTime  timeInfo;
    memset (&timeInfo, 0, sizeof (timeInfo));

    // get the time stamp of the buffer, not necessary if no
    // synchronization to other media is required
    if(ASIOGetSamplePosition(&timeInfo.timeInfo.samplePosition, &timeInfo.timeInfo.systemTime) == ASE_OK)
        timeInfo.timeInfo.flags = kSystemTimeValid | kSamplePositionValid;


    bufferSwitchTimeInfo (&timeInfo, index, processNow);
}

void sampleRateChanged(ASIOSampleRate sRate)
{
    //Placeholder for right now
}

long asioMessages(long selector, long value, void* message, double* opt)
{
    // currently the parameters "value", "message" and "opt" are not used.
    long ret = 0;
    switch(selector)
    {
        case kAsioSelectorSupported:
            if(value == kAsioResetRequest
               || value == kAsioEngineVersion
               || value == kAsioResyncRequest
               || value == kAsioLatenciesChanged
               // the following three were added for ASIO 2.0, you don't necessarily have to support them
               || value == kAsioSupportsTimeInfo
               || value == kAsioSupportsTimeCode
               || value == kAsioSupportsInputMonitor)
                ret = 1L;
            break;
        case kAsioResetRequest:
            // defer the task and perform the reset of the driver during the next "safe" situation
            // You cannot reset the driver right now, as this code is called from the driver.
            // Reset the driver is done by completely destruct is. I.e. ASIOStop(), ASIODisposeBuffers(), Destruction
            // Afterwards you initialize the driver again.
            driverData.stop;  // In this sample the processing will just stop
            ret = 1L;
            break;
        case kAsioResyncRequest:
            // This informs the application, that the driver encountered some non fatal data loss.
            // It is used for synchronization purposes of different media.
            // Added mainly to work around the Win16Mutex problems in Windows 95/98 with the
            // Windows Multimedia system, which could loose data because the Mutex was hold too long
            // by another thread.
            // However a driver can issue it in other situations, too.
            ret = 1L;
            break;
        case kAsioLatenciesChanged:
            // This will inform the host application that the drivers were latencies changed.
            // Beware, it this does not mean that the buffer sizes have changed!
            // You might need to update internal delay data.
            ret = 1L;
            break;
        case kAsioEngineVersion:
            // return the supported ASIO version of the host application
            // If a host applications does not implement this selector, ASIO 1.0 is assumed
            // by the driver
            ret = 2L;
            break;
        case kAsioSupportsTimeInfo:
            // informs the driver wether the asioCallbacks.bufferSwitchTimeInfo() callback
            // is supported.
            // For compatibility with ASIO 1.0 drivers the host application should always support
            // the "old" bufferSwitch method, too.
            ret = 1;
            break;
        case kAsioSupportsTimeCode:
            // informs the driver wether application is interested in time code info.
            // If an application does not need to know about time code, the driver has less work
            // to do.
            ret = 0;
            break;
    }
    return ret;
}
