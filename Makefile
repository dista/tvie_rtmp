CC=g++
CFLAGS=-c -g -Wall -O0 -fPIC -I/usr/local/tvie/include
LDFLAGS=-L/usr/local/tvie/lib -lboost_system -lboost_thread
SOURCES=readbuffer.cpp rtmpconnection.cpp rtmpserver.cpp utility.cpp writebuffer.cpp \
		rtmpparser.cpp amf0.cpp livereceiveractor.cpp
OBJECTS=$(SOURCES:.cpp=.o)
EXECUTABLE=tvie_rtmp
LIBRARY=libtvie_rtmp
TEST_EXE=tvie_rtmp_test
FFMPEG_DEP=-lfmp4 -lx264 -lavformat -lavcodec -lavutil

all: $(SOURCES) $(LIBRARY) $(EXECUTABLE) $(TEST_EXE)

main.o : main.cpp
	$(CC) $(CFLAGS) $< -o $@

$(TEST_EXE): main.o $(OBJECTS)
	$(CC) $(LDFLAGS) main.o $(FFMPEG_DEP) $(OBJECTS) -o $@


$(EXECUTABLE): main.o $(LIBRARY)
	$(CC) main.o $(LDFLAGS) $(FFMPEG_DEP) -L ./ -ltvie_rtmp -o $@

$(LIBRARY): $(OBJECTS)
	$(CC) $(LDFLAGS) -shared -Wl,-soname,$(LIBRARY).so.1 $(OBJECTS) -o $@.so.1.0
	ln -sf $@.so.1.0 $@.so.1
	ln -sf $@.so.1.0 $@.so

%.o: %.cpp
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm $(OBJECTS) main.o $(EXECUTABLE) $(LIBRARY)* $(TEST_EXE) -f
