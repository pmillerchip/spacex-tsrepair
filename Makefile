CXXFLAGS=-g -Wall -std=c++11
TSFILE=raw.ts
TSFILE_ALIGNED=raw_aligned.ts
TSFILE_FIXED=fixed.ts

SOURCES=\
  TSFile.cpp\
  TSPacket.cpp\
  main.cpp

OBJECTS=$(SOURCES:.cpp=.o)
EXECUTABLE=tsrepair

all: $(TSFILE_FIXED)

$(EXECUTABLE): $(OBJECTS)
	$(CXX) -o $@ $(OBJECTS) $(LDFLAGS)

$(TSFILE_ALIGNED): $(TSFILE) $(EXECUTABLE)
	./$(EXECUTABLE) $(TSFILE) -noprintmp4 -nofix -fix:382a8,insert,56/d7250,insert,56/215d4c,insert,56/3571ec,insert,56/3dc0ac,insert,56 $@ > aligned.txt

$(TSFILE_FIXED): $(TSFILE_ALIGNED) $(EXECUTABLE) fixcommands.cmd
	./$(EXECUTABLE) $(TSFILE) -noprintmp4 -fix:@fixcommands.cmd $@ > fixed.txt

clean:
	rm -f *.o *.txt $(EXECUTABLE) $(TSFILE_ALIGNED) *~

$(TSFILE):
	wget -O $@ http://www.spacex.com/sites/spacex/files/raw.ts

.cpp.o:
	$(CXX) $(CXXFLAGS) -c -o $@ $<

