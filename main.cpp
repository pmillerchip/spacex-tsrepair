//----------------------------------------------------------------------------
// TSView
//----------------------------------------------------------------------------

#include <stdio.h>
#include <string>
#include <sstream>
#include <string.h>
#include <stdlib.h>
#include "TSPacket.h"

#define MPEGTS_CLOCK_RATE 90000
#define SPACEX_PID 0x3e8

double lastSeconds = 0.0;
unsigned int numFixedAutoInterpolate = 0;
unsigned int numFixedPayloadOrder    = 0;
unsigned int numFixedBadPCR          = 0;
unsigned int lastDataPCC             = 0xff;
unsigned long long int lastPCR       = 0;
unsigned long long int lastPTS       = 0;
bool outputting       = false;
bool optionFix        = true;
bool optionTSRealign  = false;
bool shownPCCDiscon   = false;

//----------------------------------------------------------------------------
double
clockToSeconds(unsigned long long int clock)
{
  return(((double)clock) / MPEGTS_CLOCK_RATE);
}

//----------------------------------------------------------------------------
unsigned int
numBitsDifference(unsigned int a, unsigned int b)
{
  unsigned int rv = 0;
  unsigned int d = a^b;
  while(d != 0)
  {
    if ((d & 1) != 0) ++rv;
    d = d >> 1;
  }
  return(rv);
}

//----------------------------------------------------------------------------
void
processTime(long int whichOne, double t)
{
  if (t >= lastSeconds)
  {
    // Time has ticked forwards
    lastSeconds = t;
  }
  else
  {
    // Time has ticked backwards - lag!
    printf("%f %f\n", t, lastSeconds - t);
    fflush(stdout);
  }
}

//----------------------------------------------------------------------------
bool
pidIsValid(unsigned int pid)
{
  return((pid == 0x0000)
      || (pid == 0x0020)
      || (pid == SPACEX_PID)
      || (pid == 0x1fff));
}

//----------------------------------------------------------------------------
bool
processPacket(TSPacket* packetBuffer, long int whichOne, FILE* ofd, FILE* mp4fd)
{
  TSPacket p = packetBuffer[whichOne];
  printf("Packet %ld at 0x%08x: ", whichOne, p.getFileOffset());

  if (!p.isValid())
  {
    printf("Invalid ");
  }

  printf("PID 0x%04x ", p.pid());

  if (p.getTEI())      printf("TEI ");
  if (p.getPUSI())     printf("PUSI ");
  if (p.getPRI())      printf("PRI ");
  if (p.isScrambled()) printf("SCR ");

  if (p.adaptationField() != NULL) printf("AF[%u] ", p.afLen());

  if (p.hasPayload())
  {
    printf("Pay%d:", p.payloadContinuityCounter());
    for(int i=0; i < 32; ++i)
    {
      printf("%02x", p.payload()[i]);
    }
    printf(" ");
  }

  unsigned long long int ticks;
  if (p.hasPCR())
  {
    ticks = p.getPCR() >> 15;
    printf("PCR: %llu (gap %llu) ", ticks, ticks - lastPCR);
    lastPCR = ticks;
    //processTime(whichOne, clockToSeconds(ticks));
  }

  if (p.hasPTS())
  {
    ticks = p.getPTS();
    printf("PTS: %llu (gap %llu) ", ticks, ticks - lastPTS);
    lastPTS = ticks;
    //processTime(whichOne, clockToSeconds(ticks));
  }
  
  if (p.hasPCR() && p.hasPTS())
  {
    printf("Lag: %llu ", (p.getPCR() >> 15) - p.getPTS());
  }
  
  if ((p.pid() == 0) && p.hasPayload())
  {
    printf("PAT:");
    for(int i=0; i < 4; ++i)
    {
      printf("%02x%02x=%02x%02x,",
        p.payload()[7+(i*4)],
        p.payload()[8+(i*4)],
        p.payload()[9+(i*4)] & 0x1f,
        p.payload()[10+(i*4)]);
    }
    
    //unsigned int dstpid = ((p.payload()[9] & 0x1f) << 8) | p.payload()[10];
  }
  
  printf("\n");
  
  if (!pidIsValid(p.pid())) return(false);
  
  // Continuity counter check
  if (p.pid() == SPACEX_PID)
  {
    if (lastDataPCC != 0xff)
    {
      if ((p.payloadContinuityCounter() != ((lastDataPCC + 1) & 0xf)) && !shownPCCDiscon)
      {
        printf("PCC discontinuity!\r\n");
        shownPCCDiscon = true;
        return(false);
      }
    }
    lastDataPCC = p.payloadContinuityCounter();
  }
  
  if (ofd != NULL)
  {
    fwrite(p.getData(), TS_PACKET_SIZE, 1, ofd);
  }

  if ((mp4fd != NULL)
   && p.hasPayload()
   && (p.pid() == SPACEX_PID))
  {
    fwrite(p.payload(), p.getPayloadSize(), 1, mp4fd);
  }
  
  return(true);
}

//----------------------------------------------------------------------------
// If p1 and p3 are valid and p2 isn't, set p2 as valid
void
repairSingleInvalid(TSPacket& packet1, TSPacket& packet2, TSPacket& packet3)
{
  if (packet1.isValid() && packet3.isValid()
   && ((!packet2.isValid()) || (!pidIsValid(packet2.pid()))))
  {
    packet2.setValid();
    
    // Repair PID
    if ((packet1.pid() == packet3.pid())
     && ((packet1.pid() == SPACEX_PID) || (packet1.pid() == 0x1fff))
     && packet1.hasPayload()
     && packet3.hasPayload()
     && (((packet1.payloadContinuityCounter() + 2) & 0xf) == packet3.payloadContinuityCounter()))
    {
      packet2.setPID(packet1.pid());
      packet2.setPayloadFlag();
      packet2.setPayloadContinuityCounter(packet1.payloadContinuityCounter() + 1);
    }
  }
}

//----------------------------------------------------------------------------
void
repairInvalidNeighbour(TSPacket& packet1, TSPacket& packet2)
{
  if (packet1.isValid()
   && pidIsValid(packet1.pid())
   && (!packet2.isValid())
   && (packet1.pid() == packet2.pid()))
  {
    packet2.setValid();
  }
  else
  if (packet2.isValid()
   && pidIsValid(packet2.pid())
   && (!packet1.isValid())
   && (packet1.pid() == packet2.pid()))
  {
    packet1.setValid();
  }
}

//----------------------------------------------------------------------------
unsigned int
getPacketOffset(unsigned int packetNum)
{
  unsigned int offset = packetNum * TS_PACKET_SIZE;
  if (!optionTSRealign) return(offset);
  
  // These numbers below are ONLY for the SpaceX raw.ts file!
  
  if (packetNum < 1224) return(offset);
  
  unsigned int correction = 0x382e0 - 0x382a8;
  
  if (packetNum < 4688) return(offset - correction);
  
  correction += (0xd7288 - 0xd7250);

  if (packetNum < 11637) return(offset - correction);

  correction += (0x21617c - 0x216144);

  if (packetNum < 18633) return(offset - correction);

  correction += (0x3572f4 - 0x3572bc);

  if (packetNum < 21551) return(offset - correction);

  correction += (0x3dd1a4 - 0x3dd16c);
  
  return(offset - correction);
}

//----------------------------------------------------------------------------
void
repairPID(TSPacket& packet)
{
  unsigned int pid = packet.pid();
  unsigned int tolerance;
  
  if (pidIsValid(pid)) return;
  
  for(tolerance = 1; tolerance < 3; ++tolerance)
  {
    if (numBitsDifference(pid, 0x1fffu) <= tolerance)
    {
      packet.setPID(0x1fffu);
      return;
    }
    
    if (numBitsDifference(pid, 0x03e8u) <= tolerance)
    {
      packet.setPID(0x03e8u);
      return;
    }
  }
}

//----------------------------------------------------------------------------
void
fixPacket(TSPacket* packetBuffer, unsigned int packetNum,
  unsigned int pid, unsigned int counter)
{
  packetBuffer[packetNum].setValid();
  packetBuffer[packetNum].setPID(pid);
  packetBuffer[packetNum].setPayloadFlag();
  packetBuffer[packetNum].setPayloadContinuityCounter(counter & 0x0f);
}

//----------------------------------------------------------------------------
void
fixBetween(TSPacket* packetBuffer, unsigned int startPacket, unsigned int endPacket)
{
  unsigned int i;
  unsigned int pid = packetBuffer[startPacket].pid();
  unsigned int counter = packetBuffer[startPacket].payloadContinuityCounter();

  for(i = startPacket + 1; i < endPacket; ++i)
  {
    ++counter;
    fixPacket(packetBuffer, i, pid, counter);
  }
}

//----------------------------------------------------------------------------
bool
isInterpolateGoodPacket(TSPacket& packet, unsigned int pid)
{
  return(packet.isValid()
      && packet.hasPayload()
      && (packet.pid() == pid));
}

//----------------------------------------------------------------------------
bool
isInterpolateBadPacket(TSPacket& packet)
{
  return((!packet.isValid())
      || (!pidIsValid(packet.pid()))
      || (!packet.hasPayload()));
}

//----------------------------------------------------------------------------
// Assume start packet is bad, find a run of invalid packets that end in a
// good packet that's all correct
bool
canAutoFix(TSPacket* packetBuffer, unsigned int numPackets,
  unsigned int startPacket, unsigned int pid)
{
  unsigned int packetNum;
  unsigned int counter = packetBuffer[startPacket-1].payloadContinuityCounter();

  for(packetNum = startPacket; packetNum < numPackets; ++packetNum)
  {
    ++counter;
    if (isInterpolateGoodPacket(packetBuffer[packetNum], pid))
    {
      if ((counter & 0xf) == packetBuffer[packetNum].payloadContinuityCounter())
      {
        return(true);
      }
      else
      {
        return(false);
      }
    }
  }
  
  return(false);
}

//----------------------------------------------------------------------------
void
autoInterpolate(TSPacket* packetBuffer, unsigned int numPackets, unsigned int pid)
{
  unsigned int i;
  unsigned int counter;
  for(i=1; i < numPackets; ++i)
  {
    if (isInterpolateBadPacket(packetBuffer[i])
     && isInterpolateGoodPacket(packetBuffer[i-1], pid)
     && canAutoFix(packetBuffer, numPackets, i, pid))
    {
      // We can fix this
      counter = packetBuffer[i-1].payloadContinuityCounter() + 1;
      while(isInterpolateBadPacket(packetBuffer[i]))
      {
        fixPacket(packetBuffer, i, pid, counter);
        ++counter;
        ++i;
        ++numFixedAutoInterpolate;
      }
    }
  }
}

//----------------------------------------------------------------------------
bool
payloadsConsecutive(TSPacket* packetBuffer, unsigned int a)
{
  return(((packetBuffer[a].payloadContinuityCounter()+1) & 0xf) ==
           packetBuffer[a+1].payloadContinuityCounter());
}

//----------------------------------------------------------------------------
void
fixPayloadOrder(TSPacket* packetBuffer, unsigned int numPackets,
  unsigned int i)
{
  if ((i + 4) >= numPackets) return;
  
  if (packetBuffer[i].isValid() && pidIsValid(packetBuffer[i].pid())
   && packetBuffer[i+1].isValid() && (packetBuffer[i].pid() == packetBuffer[i+1].pid())
   && packetBuffer[i+2].isValid() && (packetBuffer[i].pid() == packetBuffer[i+2].pid())
   && packetBuffer[i+3].isValid() && (packetBuffer[i].pid() == packetBuffer[i+3].pid())
   && payloadsConsecutive(packetBuffer, i)
   && payloadsConsecutive(packetBuffer, i+1)
   && !payloadsConsecutive(packetBuffer, i+2))
  {
    ++numFixedPayloadOrder;
    packetBuffer[i+3].setPayloadContinuityCounter(packetBuffer[i+2].payloadContinuityCounter() + 1);
  }
}

//----------------------------------------------------------------------------
#define GOOD_PAT_PACKET 4602
void
doFakePAT(TSPacket* packetBuffer, unsigned int numPackets)
{
  unsigned int i;
  for(i=0; i < numPackets; ++i)
  {
    if (packetBuffer[i].isValid()
     && (packetBuffer[i].pid() == 0)
     && (i != GOOD_PAT_PACKET))
    {
      memcpy(packetBuffer[i].getData(), packetBuffer[GOOD_PAT_PACKET].getData(), TS_PACKET_SIZE);
    }
  }
}

//----------------------------------------------------------------------------
void
doFixes(TSPacket* packetBuffer, unsigned int numPackets)
{
  // Repair pass: fix bitflip errors in PID
  unsigned int i;
  for(i=0; i < numPackets; ++i)
  {
    repairPID(packetBuffer[i]);
  }

  for(i=0; i < numPackets-1; ++i)
  {
    repairInvalidNeighbour(packetBuffer[i], packetBuffer[i+1]);
  }
  
  autoInterpolate(packetBuffer, numPackets, SPACEX_PID);
  autoInterpolate(packetBuffer, numPackets, 0x1fff);

  // Repair pass: set all packets valid that have valid PIDs
  for(i=0; i < numPackets; ++i)
  {
    if (pidIsValid(packetBuffer[i].pid()))
    {
      packetBuffer[i].setValid();
    }
  }

  autoInterpolate(packetBuffer, numPackets, SPACEX_PID);
  autoInterpolate(packetBuffer, numPackets, 0x1fff);

  // Repair pass: fix payload order
  for(i=0; i < numPackets-4; ++i)
  {
    fixPayloadOrder(packetBuffer, numPackets, i);
  }

  autoInterpolate(packetBuffer, numPackets, SPACEX_PID);
  autoInterpolate(packetBuffer, numPackets, 0x1fff);

  // Repair pass: AF can't be longer than packet len - 4
  for(i=0; i < numPackets; ++i)
  {
    if ((packetBuffer[i].adaptationField() != NULL)
     && (packetBuffer[i].afLen() > (TS_PACKET_SIZE - 4)))
    {
      packetBuffer[i].removeAF();
    }
  }

  // Repair pass: PCR can't be greater than 0x710000
  // This is only valid for the SpaceX video!
  for(i=0; i < numPackets; ++i)
  {
    if (packetBuffer[i].isValid()
     && packetBuffer[i].hasPCR()
     && ((packetBuffer[i].getPCR() >> 15) > 0x710000))
    {
      // PCR is corrupt, remove the adaptation field as it's
      // probably bad too
      packetBuffer[i].removeAF();
      ++numFixedBadPCR;
    }
  }

  // Repair pass: clear all TEIs, scrambling and PRIs
  for(i=0; i < numPackets; ++i)
  {
    packetBuffer[i].clearTEIFlag();
    packetBuffer[i].removePRI();
    packetBuffer[i].removeScramble();
  }

  // Repair pass: type 0x1fff doesn't have PUSI set or an AF
  for(i=0; i < numPackets; ++i)
  {
    if (packetBuffer[i].isValid()
     && (packetBuffer[i].pid() == 0x1fff))
    {
      packetBuffer[i].removePUSI();
      packetBuffer[i].removeAF();
    }
  }

  // Repair pass: SpaceX packets that aren't AF[7] shouldn't have PUSI
  for(i=0; i < numPackets; ++i)
  {
    if (packetBuffer[i].isValid()
     && (packetBuffer[i].pid() == SPACEX_PID)
     && ((packetBuffer[i].adaptationField() == NULL)
      || (packetBuffer[i].afLen() != 7)))
    {
      packetBuffer[i].removePUSI();
    }
  }
}

//----------------------------------------------------------------------------
void
runSingleFix(TSPacket* packetBuffer, std::string cmd)
{
  fprintf(stderr, "Fix: %s\n", cmd.data());
  
  std::string packet;
  std::string op;
  std::string param;

  std::istringstream iss(cmd);
  std::getline(iss, packet, ',');
  std::getline(iss, op, ',');
  std::getline(iss, param, ',');
  
  unsigned int packetNum = atoi(packet.data());
  
  if (op == "af")
  {
    packetBuffer[packetNum].setAFLen(atoi(param.data()));
  }
  else if (op == "noaf")
  {
    packetBuffer[packetNum].removeAF();
  }
  else if (op == "nopcr")
  {
    packetBuffer[packetNum].removePCR();
  }
  else if (op == "nopusi")
  {
    packetBuffer[packetNum].removePUSI();
  }
  else if (op == "pay")
  {
    packetBuffer[packetNum].setPayloadFlag();
    packetBuffer[packetNum].setPayloadContinuityCounter(atoi(param.data()));
  }
  else if (op == "pcr")
  {
    unsigned long long int newPCR = strtoul(param.data(), NULL, 10);
    packetBuffer[packetNum].setPCR(newPCR << 15);
  }
  else if (op == "pes")
  {
    // User is indicating it's a PES header packet
    packetBuffer[packetNum].setAFLen(7);
    packetBuffer[packetNum].setPUSI();
    packetBuffer[packetNum].setPID(SPACEX_PID);
    packetBuffer[packetNum].setPayloadFlag();
    
    unsigned char* data = packetBuffer[packetNum].payload();
    if (data != NULL)
    {
      data[0] = 0;
      data[1] = 0;
      data[2] = 1;     // PES marker
      data[3] = 0xe0;  // SpaceX streams use ID 0xe0
      data[4] = 0;
      data[5] = 0;     // PES length is zero for SpaceX video
      data[6] = 0x81;  // PES header data
      data[7] = 0x80;  // PES header data
      data[8] = 0x07;  // PES extra length (7 bytes)
      data[14] = 0xff; // PES stuffing
      data[15] = 0xff; // PES stuffing
    }
    else
    {
      fprintf(stderr, "Error in packet %d: can't set PES data!\n", packetNum);
    }
  }
  else if (op == "pframe")
  {
    unsigned char* data = packetBuffer[packetNum].payload();
    if (data != NULL)
    {
      // MPEG4 P-frame header
      data[16] = 0x00;
      data[17] = 0x00;
      data[18] = 0x01;
      data[19] = 0xb6;
    }
    else
    {
      fprintf(stderr, "Error in packet %d: can't set PES data!\n", packetNum);
    }
  }
  else if (op == "pid")
  {
    packetBuffer[packetNum].setPID(strtoul(param.data(), NULL, 16));
  }
  else if (op == "ptsauto")
  {
    if (!packetBuffer[packetNum].hasPCR())
    {
      fprintf(stderr, "Error in packet %d: can't set ptsauto, no PCR!\n", packetNum);
    }
    else
    {
      packetBuffer[packetNum].setPTS((packetBuffer[packetNum].getPCR() >> 15) - 10000);
    }
  }
  else if (op == "pusi")
  {
    packetBuffer[packetNum].setPUSI();
  }
}

//----------------------------------------------------------------------------
void
runFixCommand(TSPacket* packetBuffer, std::string fixCommand)
{
  std::istringstream iss(fixCommand);
  std::string singleCmd;
  
  do
  {
    std::getline(iss, singleCmd, '/');
    if (singleCmd != "") runSingleFix(packetBuffer, singleCmd);
  } while(!iss.eof());
}

//----------------------------------------------------------------------------
int
processFile(std::string inputFilename,
  std::string fixCommand,
  std::string outputTSFilename,
  std::string outputMP4Filename)
{
  FILE* fd = fopen(inputFilename.data(), "r");
  if (fd == NULL)
  {
    fprintf(stderr, "Cannot open input file '%s'\n", inputFilename.data());
    return(1);
  }

  FILE* ofd = NULL;
  if (outputTSFilename != "")
  {
    ofd = fopen(outputTSFilename.data(), "w");
    if (ofd == NULL)
    {
      fprintf(stderr, "Cannot open TS output file '%s'\n", outputTSFilename.data());
      return(1);
    }
  }

  FILE* mp4fd = NULL;
  if (outputMP4Filename != "")
  {
    mp4fd = fopen(outputMP4Filename.data(), "w");
    if (mp4fd == NULL)
    {
      fprintf(stderr, "Cannot open MP4 output file '%s'\n", outputMP4Filename.data());
      return(1);
    }
  }
 
  fseek(fd, 0L, SEEK_END);
  long int fileSize = ftell(fd);
  fseek(fd, 0L, SEEK_SET);
  
  unsigned char* fileData = new unsigned char[fileSize];
  fread(fileData, fileSize, 1, fd);
  fclose(fd);
  
  long int i;
  unsigned int numPackets;
  TSPacket packetBuffer[32768];
  
  // Create packet buffer
  for(numPackets=0; (numPackets < 32768) && (getPacketOffset(numPackets) < fileSize); ++numPackets)
  {
    packetBuffer[numPackets].setData(fileData, getPacketOffset(numPackets));
  }

  if (optionFix)
  {
    // We're not just viewing the file, we're trying to repair it
    if (fixCommand != "") runFixCommand(packetBuffer, fixCommand);
    doFixes(packetBuffer, numPackets);
    fprintf(stderr, "Num auto interpolate: %d\n", numFixedAutoInterpolate);
    fprintf(stderr, "   Num payload order: %d\n", numFixedPayloadOrder);
    fprintf(stderr, "         Num bad PCR: %d\n", numFixedBadPCR);
  }
    
  // Normal processing pass
  bool foundBad = false;
  for(i=0; i < numPackets; ++i)
  {
    if (!processPacket(packetBuffer, i, ofd, mp4fd))
    {
      if (!foundBad)
      {
        foundBad = true;
        printf("----- Stream is bad from here onwards -----\n");
        fprintf(stderr, "Stream is bad from packet %ld onwards\n", i);
      }
    }
  }
  
  if (ofd != NULL) fclose(ofd);
  if (mp4fd != NULL) fclose(mp4fd);
  return(0);
}

//----------------------------------------------------------------------------
int
main(int argc, char** argv)
{
  std::string inputFilename;
  std::string outputFilenameTS;
  std::string outputFilenameMP4;
  std::string fixCommand;
  int whichFilename = 0;
  
  int i;
  for(i=1; i < argc; ++i)
  {
    if (argv[i][0] == '-')
    {
      if      (strcmp(argv[i], "-nofix")      == 0) optionFix        = false;
      else if (strcmp(argv[i], "-realign")    == 0) optionTSRealign  = true;
      else if (strncmp(argv[i], "-fix:", 5)   == 0) fixCommand = argv[i] + 5;
      else
      {
        fprintf(stderr, "Unexpected option '%s'\n", argv[i]);
        return(1);
      }
    }
    else
    {
      switch(whichFilename)
      {
        case 0: inputFilename     = argv[i]; break;
        case 1: outputFilenameTS  = argv[i]; break;
        case 2: outputFilenameMP4 = argv[i]; break;
        default:
          fprintf(stderr, "Unexpected filename '%s'\n", argv[i]);
          return(1);
      }
      ++whichFilename;
    }
  }
  
  return(processFile(inputFilename, fixCommand, outputFilenameTS, outputFilenameMP4));
}

