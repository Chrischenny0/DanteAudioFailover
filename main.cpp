#include <iostream>
#include <atomic>
#include <iomanip>
#include <thread>
#include <windows.h>
#include <fstream>
#include <cstring>
#include <ctime>
#include "libremidi/libremidi.hpp"
#include "libremidi/api.hpp"
#include "asiosys.h"
#include "asio.h"
#include "asiodrivers.h"

using namespace std;

extern AsioDrivers* asioDrivers;
bool loadAsioDriver(char *name);

enum {
    // number of input and outputs supported by the host application
    // you can change these to higher or lower values
    kMaxInputChannels = 64,
    kMaxOutputChannels = 64
};


typedef struct asioDriverInfo
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

    // Information from ASIOGetSamplePosition()
    // data is converted to double floats for easier use, however 64 bit integer can be used, too
    double         nanoSeconds;
    double         samples;
    double         tcSamples;	// time code samples

    // bufferSwitchTimeInfo()
    ASIOTime       tInfo;			// time info state
    unsigned long  sysRefTime;      // system reference time, when bufferSwitch() was called

    // Signal the end of processing in this example
    bool           stopped = false;
    bool           failOver = false;

    //Midi Flags
    atomic_bool    manualSwitch = false;
    atomic_bool    exit = false;
    atomic_bool    midiLearn = false;
    atomic_bool    restart = false;


    //Midi Driver
    libremidi::midi_in input;

    //Curr Midi Note Switch
    libremidi::message message;

} DriverInfo;

DriverInfo asioDriverInfo = {0};

ASIOTime *bufferSwitchTimeInfo(ASIOTime *timeInfo, long index, ASIOBool processNow)
{	// the actual processing callback.
    // Beware that this is normally in a seperate thread, hence be sure that you take care
    // about thread synchronization. This is omitted here for simplicity.
    static long processedSamples = 0;

    // store the timeInfo for later use
    asioDriverInfo.tInfo = *timeInfo;

    // buffer size in samples
    long buffSize = asioDriverInfo.preferredSize;

    int primary, secondary;

    primary = (!asioDriverInfo.manualSwitch) ? 31 : 63;
    secondary = (!asioDriverInfo.manualSwitch) ? 63 : 31;

    for(int k = 0; k < 64; k++){
        if(((char*)asioDriverInfo.bufferInfos[primary].buffers[(index + 1) % 2])[k] == 0 && ((char*)asioDriverInfo.bufferInfos[secondary].buffers[(index + 1) % 2])[k] != 0){
            asioDriverInfo.failOver = true;
            cout << "FAILOVER" << endl;
            break;
        }
    }


    int startChan;

    startChan = (!asioDriverInfo.failOver != !asioDriverInfo.manualSwitch) ? 32 : 0;


    for(int i = 0; i < 31; i++){
        memcpy(asioDriverInfo.bufferInfos[i + 64].buffers[index], asioDriverInfo.bufferInfos[i + startChan].buffers[(index + 1) % 2], buffSize * 3);
    }

    // finally if the driver supports the ASIOOutputReady() optimization, do it here, all data are in place
    if (asioDriverInfo.postOutput)
        ASIOOutputReady();


    asioDriverInfo.failOver = false;

    return 0L;
}

//----------------------------------------------------------------------------------
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


//----------------------------------------------------------------------------------
void sampleRateChanged(ASIOSampleRate sRate)
{
    // do whatever you need to do if the sample rate changed
    // usually this only happens during external sync.
    // Audio processing is not stopped by the driver, actual sample rate
    // might not have even changed, maybe only the sample rate status of an
    // AES/EBU or S/PDIF digital input at the audio device.
    // You might have to update time/sample related conversion routines, etc.
}

//----------------------------------------------------------------------------------
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
            asioDriverInfo.stopped;  // In this sample the processing will just stop
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

void readMidi(){
    libremidi::message message;

    while(!asioDriverInfo.stopped){
        while(!(asioDriverInfo.midiLearn || asioDriverInfo.stopped)){
            if(asioDriverInfo.input.get_message(message)){
                if(!strncmp((char*)&message.bytes[0], (char*)&asioDriverInfo.message.bytes[0], 3)){
                    asioDriverInfo.manualSwitch = !asioDriverInfo.manualSwitch;
                    cout << "MANUAL SWITCH" << endl;
                    cout << "Current Computer: " << ((asioDriverInfo.manualSwitch != asioDriverInfo.failOver)? "SECONDARY" : "PRIMARY") << endl;

                }
            }
        }
        cout << "here" << endl;
        Sleep(30);
    }
}

void learnMidi(){
    libremidi::message message;
    string tmp;
    fstream programData("prog.dat", ios::out | ios::binary);

    auto startTime = time(NULL);

    while(asioDriverInfo.midiLearn && time(nullptr) - startTime < 20){
        Sleep(100);

        if(asioDriverInfo.input.get_message(message)){
            memcpy((char*)&asioDriverInfo.message.bytes.at(0), (char*)&message.bytes.at(0), asioDriverInfo.message.bytes.size());
            cout << "Learned Midi" << endl;
            cout << "Message: " << (int)asioDriverInfo.message.bytes.at(0);
            cout << " " << (int)asioDriverInfo.message.bytes.at(1);
            cout << " " << (int)asioDriverInfo.message.bytes.at(2) << endl;


            Sleep(1500);

            programData.write((char*)(&asioDriverInfo.message.bytes.at(0)), asioDriverInfo.message.bytes.size());
            asioDriverInfo.midiLearn = false;
        }
    }
    if(asioDriverInfo.midiLearn){
        asioDriverInfo.midiLearn = false;
        cout << "TIME OUT" << endl;
        Sleep(50);
        system("CLS");
        Sleep(50);
    }
    programData.close();
}

void getUserMessages(){
    char command;

    thread readMidiIn(readMidi);


    cout << "WELCOME to Celebration Dante fail-over for Ableton: The program is already running" << endl;

    while(!asioDriverInfo.exit){
        cout << "\nCurrent Computer: " << ((asioDriverInfo.manualSwitch != asioDriverInfo.failOver)? "SECONDARY" : "PRIMARY") << endl;
        cout << "[M]idi Learn  [R]estart  [S]top" << endl << endl;

        Sleep(20);
        system("Color 07");

        cin >> command;

        switch (command) {
            case('M'):
                cout << "LEARNING MIDI, Timeout in 20 Seconds:" << endl;
                asioDriverInfo.midiLearn = true;
                learnMidi();
                asioDriverInfo.midiLearn = false;
                break;
            case('R'):
                cout << "RESTARTING: AUDIO WILL DROP FOR A SECOND" << endl;
                asioDriverInfo.exit = asioDriverInfo.restart = true;
                break;
            case('S'):
                cout << "STOPPING PROGRAM: GOOD BYE" << endl;
                Sleep(100);
                asioDriverInfo.stopped = asioDriverInfo.exit = true;
                break;
            default:
                cout << "MESSAGE NOT UNDERSTOOD, WHATCHU TRYIN TO SAY FOO" << endl;
                break;
        }
    }
    readMidiIn.join();
}

int main() {
    do{
        Sleep(20);
        fstream programData("prog.dat", ios::in | ios::binary);
        AsioDrivers currDrivers;

        //INITIALIZE MIDI
        while(!asioDriverInfo.input.is_port_open()){
            for(int i = 0; i < asioDriverInfo.input.get_port_count(); i++){
                if(asioDriverInfo.input.get_port_name(i) == "NUCMidi 1"){
                    asioDriverInfo.input.open_port(i);
                    cout << "MIDI Port FOUND: " << asioDriverInfo.input.get_port_name(i) << endl;
                    break;
                }
            }
            if(!asioDriverInfo.input.is_port_open()){
                cout << "MIDI NETWORK NOT FOUND: Network name should be \"NUCMidi\"" << endl;
                cout << "Retry in 3 Seconds" << endl;
                Sleep(3000);
                system("CLS");
            }
        }

        //INITIALIZE THE DRIVER
        char danteName[] = "Dante Virtual Soundcard (x64)";

        do{
            loadAsioDriver(danteName);

            ASIOInit(&asioDriverInfo.driverInfo);
            cout << asioDriverInfo.driverInfo.name << endl;
            cout << ((!strcmp(asioDriverInfo.driverInfo.errorMessage, "No ASIO Driver Error")) ? "No Errors" : asioDriverInfo.driverInfo.errorMessage) << endl;

            if(strcmp(danteName, asioDriverInfo.driverInfo.name)){
                Sleep(1000);
            }
        } while(strcmp(danteName, asioDriverInfo.driverInfo.name));

        //INITIALIZE MIDI SWITCH NOTE
        vector<unsigned char> data(3);
        programData.read((char*)&data.at(0), 3);
        programData.close();

        asioDriverInfo.message.bytes = data;


        //GET NUMBER OF CHANNELS
        ASIOGetChannels(&asioDriverInfo.inputChannels, &asioDriverInfo.outputChannels);


        //GET PREFERRED BUFFER SIZE
        ASIOGetBufferSize(&asioDriverInfo.minSize, &asioDriverInfo.maxSize, &asioDriverInfo.preferredSize, &asioDriverInfo.granularity);

        //GET SAMPLE RATE
        ASIOGetSampleRate(&asioDriverInfo.sampleRate);

        //SET SAMPLE RATE
        ASIOSetSampleRate(asioDriverInfo.sampleRate);


        //CHECK FOR OUTPUT READY
        if(ASIOOutputReady() == ASE_OK){
            asioDriverInfo.postOutput = true;
        } else{
            asioDriverInfo.postOutput = false;
        }


        //CREATE BUFFERS
        ASIOBufferInfo* indx = asioDriverInfo.bufferInfos;
        ASIOChannelInfo *idx = asioDriverInfo.channelInfos;
        ASIOCallbacks callbacks;

        for(int i = 0; i < asioDriverInfo.inputChannels; i++, indx++, idx++){
            idx->isInput = indx->isInput = ASIOTrue;
            idx->channel = indx->channelNum = i;
            indx->buffers[0] = indx->buffers[1] = nullptr;
        }
        for(int i = 0; i < asioDriverInfo.outputChannels; i++, indx++, idx++){
            idx->isInput = indx->isInput = ASIOFalse;
            idx->channel = indx->channelNum = i;
            indx->buffers[0] = indx->buffers[1] = nullptr;
        }

        callbacks.bufferSwitch = &bufferSwitch;
        callbacks.bufferSwitchTimeInfo = &bufferSwitchTimeInfo;
        callbacks.asioMessage = &asioMessages;
        callbacks.sampleRateDidChange = &sampleRateChanged;

        ASIOCreateBuffers(asioDriverInfo.bufferInfos, asioDriverInfo.inputChannels +
                                                      asioDriverInfo.outputChannels, asioDriverInfo.preferredSize, &callbacks);

        int channelTotal = asioDriverInfo.inputChannels + asioDriverInfo.outputChannels;
        for(int i = 0; i < channelTotal; i++){
            ASIOGetChannelInfo(&asioDriverInfo.channelInfos[i]);
        }

        asioDriverInfo.failOver = false;

        ASIOStart();

        thread midiRead(getUserMessages);

        while(!asioDriverInfo.stopped){
            if(asioDriverInfo.restart){
                asioDriverInfo.stopped = true;
            }
            Sleep(10);
        }
        ASIOStop();
        ASIODisposeBuffers();
        ASIOExit();

        midiRead.join();

        asioDriverInfo.input.close_port();

        if(asioDriverInfo.restart){
            asioDriverInfo.stopped = false;
            Sleep(1000);
        }
        asioDriverInfo.exit = asioDriverInfo.restart = false;
        system("CLS");
    } while(!asioDriverInfo.stopped);
    return 0;
}
