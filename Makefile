CXX = c++ -O
FLAGS = -std=c++17 -I/opt/local/include -I/usr/local/include -I/opt/local/include/libairspyhf -I/usr/include/libairspyhf
LIBS = -L/opt/local/lib -L/usr/local/lib -lfftw3 -lsndfile

MOREC =

# uncomment if you have the airspyhf and liquid dsp libraries.
# try -lusb or -lusb-1.0 ; also apt install libusb-1.0-0-dev
# CXX += -DUSE_AIRSPYHF
# LIBS += -lairspyhf -lliquid -lusb-1.0

# for the Apache ANAN-7000dle, and possibly other HPSDR radios.
# CXX += -DUSE_HPSDR
# MOREC += hpsdr.cc
# LIBS += -lliquid

# for the RFSpace SDR-IP, NetSDR, CloudIQ and CloudSDR in I/Q mode.
# CXX += -DUSE_SDRIP
# MOREC += sdrip.cc
# LIBS += -lliquid

wsprmon: wsprmon.cc snd.cc util.cc fft.cc cloudsdr.cc $(MOREC)
	$(CXX) $(FLAGS) wsprmon.cc snd.cc util.cc fft.cc cloudsdr.cc $(MOREC) -o wsprmon $(LIBS) -lportaudio -pthread

clean:
	rm -f wsprmon
