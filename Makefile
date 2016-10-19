CXXFLAGS = -g -Wall
LDFLAGS =

SOURCES=\
  TSFile.cpp\
  TSPacket.cpp\
  main.cpp

OBJECTS=$(SOURCES:.cpp=.o)
EXECUTABLE=tsrepair

$(EXECUTABLE): $(OBJECTS)
	$(CXX) -o $@ $(OBJECTS) $(LDFLAGS)

clean:
	rm -f *.o $(EXECUTABLE) *~

raw.ts:
	wget -O $@ http://www.spacex.com/sites/spacex/files/raw.ts

.cpp.o:
	$(CXX) $(CXXFLAGS) -c -o $@ $<

