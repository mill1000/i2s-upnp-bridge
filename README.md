# I2S UPNP Bridge
Snoop an I2S bus and stream the audio to UPNP renderers with an ESP32. Audio samples are available as raw PCM and WAV streams via HTTP. Playback on selected UPNP renderers is automatically started when activity is detected.

![Diagram](images/diagram.png)

## Manual Streaming
The audio streams are always available at the following endpoints:
* `http://your-device-ip-address/stream.wav`
* `http://your-device-ip-address/stream.pcm`

The WAV stream is preferred as support for the `audio/L16` MIME type is finicky.

## Selecting Renderers
A simple web interface is provided to select renderers for automatic control. Renderers on the network are detected via SSDP. All selected renderers automatically begin playback when activity is detected, and stop when activity halts.

The web interface can be found at `http://your-device-ip-address`

![Web Interface](images/web_interface.png)

## Echo Dot Integration
The real motivation behind this project was to find a way to stream Amazon Music via the Raspberry Pi + DAC HAT I was already using. Unfortunately, it seems that neither Amazon, nor Google, are interested in making their protocols freely available.

Based on other's work, I knew it was possible to get audio data from an Echo Dot by snooping the I2S bus on it's way to the DAC.
* [Hi-Fi Digital Audio from the Echo Dot](https://hackaday.io/project/28109-hi-fi-digital-audio-from-the-echo-dot)
* [S/PDIF from Echo Dot](https://hackaday.io/project/162309-spdif-from-echo-dot)

Similarly it is possible on a Google Home Mini, but with some caveats. e.g. not stereo, probably fixed DSP.
* [Google Home Headphone Jack](https://blog.usedbytes.com/2019/06/google-home-headphone-jack/)

### I2S Wiring
The Echo Dot uses a TI TLV320DAC3203 to convert digital audio to an analog for it's internal speaker and headphone jack. Using the data sheet its trivia to identify which traces on the PCB carry I2S data.
![Pins](images/pins.jpg)

I made solder points by scraping soldermask away.
![Solder Points](images/solder_points.jpg)

With a steady hand, some luck, and solder I wired the following signals on the Echo Dot PCB to the ESP32.
* WCLK - GPIO12
* BCLK - GPIO14
* DIN - GPIO27
* GND - Ground

The wires were secured to the board using UV glue to provide strain relief. The additional 2 wires were for additional experiments and can be ignored.
![Wires](images/wires.jpg)

### Disabling DSP
The Echo Dot uses a large amount of DSP to improve the internal speaker sound. It also mixes down to mono. This is disabled when the headphone jack is inserted. Compare these two clips to see for yourself:
* [DSP On](images/dsp_on.wav)
* [DSP Off](images/dsp_off.wav)

For this application we need the DSP disabled. The solution is to insert a headphone plug or other substitute into the jack to actuate the internal switches. I used a Q-tip with a small amount of heatshrink to get a snug fit.
![Dummy Headphone Plug](images/dummy_plug.jpg)
