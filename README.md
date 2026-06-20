# wsprmon
Receive the WSPR protocol and print to stdout.
Captures aligned two-minute slots from a sound card via portaudio,
and decodes each slot with the wsprd decoder.
Written in C++, using FFTW.
Runs on Linux, MacOS, and FreeBSD.

To compile:

```
  make
```

wsprmon decodes each slot by invoking the wsprd binary, which must be
available at run time. wsprmon finds it via -wsprd path, else the
$WSPRD environment variable, else $PATH, else next to the wsprmon
binary.

To get a list of sound card numbers:

```
  ./wsprmon -list
```

To listen to sound card X, left channel, on the 40m WSPR dial:

```
  ./wsprmon -card X 0 -f 7.0386
```

You should see output like this:

```
033200 -28  5.7   7.040012  0  AA1QR FN43 23 
033200  -7  0.6   7.040035  0  KH2R FN21 30 
033200 -10  0.4   7.040073  0  WB8ZLK EN81 23 
033200   3  0.2   7.040097  0  AD9BE EN71 37 
033200 -20  0.2   7.040143  0  N9VP FM06 23 
033200  -9  0.3   7.040163  1  KD2EIB FM29 20 
```

The columns are HHMMSS, SNR, time offset, frequency in MHz, drift in Hz,
and the message (callsign, grid, and transmit power in dBm). After each
two-minute cycle wsprmon prints a header line, "HH:MM:SS decodes: N".

To read input from a recorded WAV file:

```
  ./wsprmon -f 7.0386 -file xxx.wav
```

For Airspy HF+ Discovery support, install the airspyhf
and liquid dsp libraries, and uncomment the relevant lines in the
Makefile. For RFspace SDR-IP, NetSDR, CloudIQ, and CloudSDR
support, install liquid dsp and edit the Makefile. Similarly
for the Apache ANAN-7000dle. Then try commands like these:

```
  ./wsprmon -card airspy ,7.0386 -f 7.0386
  ./wsprmon -card hpsdr 192.168.3.100,7.0386 -f 7.0386
  ./wsprmon -card sdrip 192.168.3.100,7.0386 -f 7.0386
```

## Acknowledgements

- [ft8mon](https://github.com/rtmrtmrtmrtm/ft8mon) by AB1HL
- [wsprd](https://wsjt.sourceforge.io/) WSPR decoder from WSJT-X via [https://github.com/pavel-demin/wsprd](https://github.com/pavel-demin/wsprd)
