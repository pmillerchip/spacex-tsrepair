CXXFLAGS = -g -Wall
LDFLAGS =

SOURCES=\
  TSPacket.cpp\
  main.cpp

OBJECTS=$(SOURCES:.cpp=.o)
EXECUTABLE=tsrepair

$(EXECUTABLE): $(OBJECTS)
	$(CXX) -o $@ $(OBJECTS) $(LDFLAGS)

clean:
	rm -f *.o $(EXECUTABLE) *~ *.txt *.mp4

.cpp.o:
	$(CXX) $(CXXFLAGS) -c -o $@ $<

