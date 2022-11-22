# Dante Audio Failover

This project is created to join the popular networked audio ecosystem "Dante."
This project aims to fill the need for easily integrating a backup device into a Dante Network. This program leverages the ease of use of Dante and the speed of the ASIO audio driver, all in a very simple, lean package.

## Requirements
***This driver only works on windows and it requires the 
machine to have a Dante Virtual Soundcard Installed***
 * [ASIO Driver](https://www.steinberg.net/developers/)
 * [libremidi](https://github.com/jcelerier/libremidi)

## Usage

This program is currently setup for a 64x64 Dante Virtual Soundcard. The two input signals will come in on channels 1-32 and 33-64 respectively. The channels 32 and 64 are reserved for a "keep alive" input that signals to the program that either the primary or back up machine have gone offline. This signal can be practically anything as long as it has a constant sound (a sine wave is recommended).


## Currently Used By

Celebration.church: Georgetown, Texas
