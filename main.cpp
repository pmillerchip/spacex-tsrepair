//----------------------------------------------------------------------------
// TSView
//----------------------------------------------------------------------------

#include <stdio.h>
#include <string>
#include <sstream>
#include <string.h>
#include <stdlib.h>
#include "TSFile.h"

#define MPEGTS_CLOCK_RATE 90000
#define SPACEX_PID 0x3e8

double lastSeconds = 0.0;
unsigned int numFixedAutoInterpolate = 0;
unsigned int numFixedPayloadOrder    = 0;
unsigned int numFixedBadPCR          = 0;
unsigned int lastDataPCC             = 0xff;
unsigned int payloadDisplayWidth     = 32;
unsigned int afDisplayWidth          = 32;
unsigned int numSkipOnOutput         = 0;
unsigned long long int lastPCR       = 0;
unsigned long long int lastPTS       = 0;
bool outputting        = false;
bool optionFix         = true;
bool optionFixMP4AF    = false;
bool optionTSRealign   = false;
bool optionDumpAF      = false;
bool optionFrameInfo   = false;
bool optionPrintMP4    = true;
bool optionPrintOffset = true;
bool shownPCCDiscon    = false;

// MPEG4 decoding state
unsigned long long int mp4_lastPCR = 0;
unsigned int mp4_startPos = 0;

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
void
printAFAndPayload(TSPacket& p)
{
  if (p.adaptationField() != NULL)
  {
    printf("AF[%u] ", p.afLen());
    if (optionDumpAF)
    {
      const unsigned char* af = p.adaptationField();
      unsigned int i;
      unsigned int afSize = p.afLen() + 1;  // +1 is length byte at start
      unsigned int sz = afSize;
      if (sz > afDisplayWidth) sz = afDisplayWidth;

      printf("(");
      for(i=afSize - sz; i < afSize; ++i)
      {
        printf("%02x", af[i]);
      }
      printf(") ");
    }
  }

  if (p.hasPayload())
  {
    unsigned int sz = p.getPayloadSize();
    if (sz > payloadDisplayWidth) sz = payloadDisplayWidth;
    
    printf("Pay%d:", p.payloadContinuityCounter());
    for(unsigned int i=0; i < sz; ++i)
    {
      printf("%02x", p.payload()[i]);
    }
    printf(" ");
  }
}

//----------------------------------------------------------------------------
bool
processPacket(TSFile& tsFile, long int whichOne, FILE* ofd, FILE* mp4fd)
{
  TSPacket p = tsFile[whichOne];
  
  if (optionPrintOffset)
  {
    printf("Packet %ld at 0x%08x: ", whichOne, p.getFileOffset());
  }

  if (!p.isValid())
  {
    printf("Invalid ");
  }

  printf("PID 0x%04x ", p.pid());

  if (p.getTEI())      printf("TEI ");
  if (p.getPUSI())     printf("PUSI ");
  if (p.getPRI())      printf("PRI ");
  if (p.isScrambled()) printf("SCR ");

  printAFAndPayload(p);

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

  // MP4 decoding state
  if (optionPrintMP4
   && (p.pid() != 0)
   && (p.pid() != 0x20)
   && (p.pid() != 0x1fff)
   && p.hasPayload())
  {
    // It's a data packet
    printf("MP4 frame@%u bytes %u-%u (@%u)",
      p.mp4_framePCR,
      p.mp4_startPos,
      p.mp4_startPos + p.mp4_payloadSize,
      p.mp4_payloadOffset);
  }
  
  printf("\n");
  
  if (ofd != NULL)
  {
    fwrite(p.getData(), TS_PACKET_SIZE, 1, ofd);
  }
  
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

  if ((mp4fd != NULL)
   && p.hasPayload()
   && (p.pid() == SPACEX_PID))
  {
    if (p.getPUSI())
    {
      fwrite(p.payload() + 16, p.getPayloadSize() - 16, 1, mp4fd);
    }
    else
    {
      fwrite(p.payload(), p.getPayloadSize(), 1, mp4fd);
    }
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
fixPacket(TSFile& tsFile, unsigned int packetNum,
  unsigned int pid, unsigned int counter)
{
  tsFile[packetNum].setValid();
  tsFile[packetNum].setPID(pid);
  tsFile[packetNum].setPayloadFlag();
  tsFile[packetNum].setPayloadContinuityCounter(counter & 0x0f);
}

//----------------------------------------------------------------------------
void
fixBetween(TSFile& tsFile, unsigned int startPacket, unsigned int endPacket)
{
  unsigned int i;
  unsigned int pid = tsFile[startPacket].pid();
  unsigned int counter = tsFile[startPacket].payloadContinuityCounter();

  for(i = startPacket + 1; i < endPacket; ++i)
  {
    ++counter;
    fixPacket(tsFile, i, pid, counter);
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
canAutoFix(TSFile& tsFile, unsigned int startPacket, unsigned int pid)
{
  unsigned int packetNum;
  unsigned int counter = tsFile[startPacket-1].payloadContinuityCounter();

  for(packetNum = startPacket; packetNum < tsFile.getNumPackets(); ++packetNum)
  {
    ++counter;
    if (isInterpolateGoodPacket(tsFile[packetNum], pid))
    {
      if ((counter & 0xf) == tsFile[packetNum].payloadContinuityCounter())
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
autoInterpolate(TSFile& tsFile, unsigned int pid)
{
  unsigned int i;
  unsigned int counter;
  for(i=1; i < tsFile.getNumPackets(); ++i)
  {
    if (isInterpolateBadPacket(tsFile[i])
     && isInterpolateGoodPacket(tsFile[i-1], pid)
     && canAutoFix(tsFile, i, pid))
    {
      // We can fix this
      counter = tsFile[i-1].payloadContinuityCounter() + 1;
      while(isInterpolateBadPacket(tsFile[i]))
      {
        fixPacket(tsFile, i, pid, counter);
        ++counter;
        ++i;
        ++numFixedAutoInterpolate;
      }
    }
  }
}

//----------------------------------------------------------------------------
bool
payloadsConsecutive(TSFile& tsFile, unsigned int a)
{
  return(((tsFile[a].payloadContinuityCounter()+1) & 0xf) ==
           tsFile[a+1].payloadContinuityCounter());
}

//----------------------------------------------------------------------------
void
fixPayloadOrder(TSFile& tsFile, unsigned int i)
{
  if ((i + 4) >= tsFile.getNumPackets()) return;
  
  if (tsFile[i].isValid() && pidIsValid(tsFile[i].pid())
   && tsFile[i+1].isValid() && (tsFile[i].pid() == tsFile[i+1].pid())
   && tsFile[i+2].isValid() && (tsFile[i].pid() == tsFile[i+2].pid())
   && tsFile[i+3].isValid() && (tsFile[i].pid() == tsFile[i+3].pid())
   && payloadsConsecutive(tsFile, i)
   && payloadsConsecutive(tsFile, i+1)
   && !payloadsConsecutive(tsFile, i+2))
  {
    ++numFixedPayloadOrder;
    tsFile[i+3].setPayloadContinuityCounter(tsFile[i+2].payloadContinuityCounter() + 1);
  }
}

//----------------------------------------------------------------------------
#define GOOD_PAT_PACKET 4602
void
doFakePAT(TSFile& tsFile, unsigned int numPackets)
{
  unsigned int i;
  for(i=0; i < numPackets; ++i)
  {
    if (tsFile[i].isValid()
     && (tsFile[i].pid() == 0)
     && (i != GOOD_PAT_PACKET))
    {
      memcpy(tsFile[i].getData(), tsFile[GOOD_PAT_PACKET].getData(), TS_PACKET_SIZE);
    }
  }
}

//----------------------------------------------------------------------------
void
doFixes(TSFile& tsFile)
{
  // Repair pass: fix bitflip errors in PID
  unsigned int i;
  for(i=0; i < tsFile.getNumPackets(); ++i)
  {
    repairPID(tsFile[i]);
  }

  for(i=0; i < tsFile.getNumPackets()-1; ++i)
  {
    repairInvalidNeighbour(tsFile[i], tsFile[i+1]);
  }
  
  autoInterpolate(tsFile, SPACEX_PID);
  autoInterpolate(tsFile, 0x1fff);

  // Repair pass: set all packets valid that have valid PIDs
  for(i=0; i < tsFile.getNumPackets(); ++i)
  {
    if (pidIsValid(tsFile[i].pid()))
    {
      tsFile[i].setValid();
    }
  }

  autoInterpolate(tsFile, SPACEX_PID);
  autoInterpolate(tsFile, 0x1fff);

  // Repair pass: fix payload order
  for(i=0; i < tsFile.getNumPackets()-4; ++i)
  {
    fixPayloadOrder(tsFile, i);
  }

  autoInterpolate(tsFile, SPACEX_PID);
  autoInterpolate(tsFile, 0x1fff);

  // Repair pass: AF can't be longer than packet len - 4
  for(i=0; i < tsFile.getNumPackets(); ++i)
  {
    if ((tsFile[i].adaptationField() != NULL)
     && (tsFile[i].afLen() > (TS_PACKET_SIZE - 4)))
    {
      tsFile[i].removeAF();
    }
  }

  // Repair pass: PCR can't be greater than 0x710000
  // This is only valid for the SpaceX video!
  for(i=0; i < tsFile.getNumPackets(); ++i)
  {
    if (tsFile[i].isValid()
     && tsFile[i].hasPCR()
     && ((tsFile[i].getPCR() >> 15) > 0x710000))
    {
      // PCR is corrupt, remove the adaptation field as it's
      // probably bad too
      tsFile[i].removeAF();
      ++numFixedBadPCR;
    }
  }

  // Repair pass: clear all TEIs, scrambling and PRIs
  for(i=0; i < tsFile.getNumPackets(); ++i)
  {
    tsFile[i].clearTEIFlag();
    tsFile[i].removePRI();
    tsFile[i].removeScramble();
  }

  // Repair pass: type 0x1fff doesn't have PUSI set or an AF
  for(i=0; i < tsFile.getNumPackets(); ++i)
  {
    if (tsFile[i].isValid()
     && (tsFile[i].pid() == 0x1fff))
    {
      tsFile[i].removePUSI();
      tsFile[i].removeAF();
      tsFile[i].setPayloadFlag();
      tsFile[i].writePadding();
    }
  }

  // Repair pass: type 0x0000
  for(i=0; i < tsFile.getNumPackets(); ++i)
  {
    if (tsFile[i].isValid()
     && (tsFile[i].pid() == 0x0000))
    {
      tsFile[i].setPUSI();  // PAT packets have PUSI set
      tsFile[i].removeAF();
      tsFile[i].setPayloadFlag();
      unsigned char* data = tsFile[i].payload();
      if (data != NULL)
      {
        // Good PAT table from SpaceX video
        data[0]  = 0x00;
        data[1]  = 0x00;
        data[2]  = 0xb0;
        data[3]  = 0x11;
        data[4]  = 0x00;
        data[5]  = 0x00;
        data[6]  = 0xc1;
        data[7]  = 0x00;
        data[8]  = 0x00;
        data[9]  = 0x00;
        data[10] = 0x00;
        data[11] = 0xe0;
        data[12] = 0x10;
        data[13] = 0x00;
        data[14] = 0x01;
        data[15] = 0xe0;
        data[16] = 0x20;
        data[17] = 0xd3;
        data[18] = 0x6a;
        data[19] = 0xf0;
        data[20] = 0xac;
        
        unsigned int psize = tsFile[i].getPayloadSize();
        unsigned int offset;
        for(offset = 21; offset < psize; ++offset)
        {
          data[offset] = 0xff;
        }
      }
      else
      {
        fprintf(stderr, "Error in packet %d: can't set PAT table!\n", i);
      }
    }
  }

  // Repair pass: type 0x0020
  for(i=0; i < tsFile.getNumPackets(); ++i)
  {
    if (tsFile[i].isValid()
     && (tsFile[i].pid() == 0x0020))
    {
      tsFile[i].setPUSI();  // These packets have PUSI set
      tsFile[i].removeAF();
      tsFile[i].setPayloadFlag();
      unsigned char* data = tsFile[i].payload();
      if (data != NULL)
      {
        // Good PMT table from SpaceX video
        data[0]  = 0x00;
        data[1]  = 0x02;
        data[2]  = 0xb0;
        data[3]  = 0x1f;
        data[4]  = 0x00;
        data[5]  = 0x01;
        data[6]  = 0xc1;
        data[7]  = 0x00;
        data[8]  = 0x00;
        data[9]  = 0xe3;
        data[10] = 0xe8;
        data[11] = 0xf0;
        data[12] = 0x00;
        data[13] = 0x10;
        data[14] = 0xe3;
        data[15] = 0xe8;
        data[16] = 0xf0;
        data[17] = 0x03;
        data[18] = 0x1b;
        data[19] = 0x01;
        data[20] = 0xf5;
        data[21] = 0x80;
        data[22] = 0xe3;
        data[23] = 0xe9;
        data[24] = 0xf0;
        data[25] = 0x00;
        data[26] = 0x81;
        data[27] = 0xe3;
        data[28] = 0xf3;
        data[29] = 0xf0;
        data[30] = 0x00;
        data[31] = 0x3f;
        data[32] = 0x64;
        data[33] = 0xf1;
        data[34] = 0x15;
        
        unsigned int psize = tsFile[i].getPayloadSize();
        unsigned int offset;
        for(offset = 35; offset < psize; ++offset)
        {
          data[offset] = 0xff;
        }
      }
    }
  }

  // Repair pass: SpaceX packets that aren't AF[7] shouldn't have PUSI
  for(i=0; i < tsFile.getNumPackets(); ++i)
  {
    if (tsFile[i].isValid()
     && (tsFile[i].pid() == SPACEX_PID)
     && ((tsFile[i].adaptationField() == NULL)
      || (tsFile[i].afLen() != 7)))
    {
      tsFile[i].removePUSI();
    }
  }

  // Repair pass: SpaceX packets that have AF but no PUSI are used for
  // padding at the end of a frame
  for(i=0; i < tsFile.getNumPackets(); ++i)
  {
    if (tsFile[i].isValid()
     && (tsFile[i].pid() == SPACEX_PID)
     && (tsFile[i].adaptationField() == NULL)
     && (!tsFile[i].getPUSI()))
    {
      unsigned char* af = tsFile[i].adaptationField();
      unsigned int offset;
      if (tsFile[i].afLen() > 1) af[1] = 0;
      for(offset=2; offset < tsFile[i].afLen(); ++offset)
      {
        af[offset] = 0xff;
      }
    }
  }
}

//----------------------------------------------------------------------------
void
runSingleFix(TSFile& tsFile, std::string cmd)
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
    tsFile[packetNum].setAFLen(atoi(param.data()));
  }
  else if (op == "insert")
  {
    tsFile.insertBytes(strtoul(packet.data(), NULL, 16),
                       strtoul(param.data(), NULL, 10));
  }
  else if (op == "noaf")
  {
    tsFile[packetNum].removeAF();
  }
  else if (op == "nopcr")
  {
    tsFile[packetNum].removePCR();
  }
  else if (op == "nopusi")
  {
    tsFile[packetNum].removePUSI();
  }
  else if (op == "pay")
  {
    tsFile[packetNum].setPayloadFlag();
    tsFile[packetNum].setPayloadContinuityCounter(atoi(param.data()));
  }
  else if (op == "pcr")
  {
    unsigned long long int newPCR = strtoul(param.data(), NULL, 10);
    tsFile[packetNum].setPCR(newPCR << 15);
  }
  else if (op == "pes")
  {
    // User is indicating it's a PES header packet
    tsFile[packetNum].setAFLen(7);
    tsFile[packetNum].setPUSI();
    tsFile[packetNum].setPID(SPACEX_PID);
    tsFile[packetNum].setPayloadFlag();
    
    unsigned char* data = tsFile[packetNum].payload();
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
    unsigned char* data = tsFile[packetNum].payload();
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
    tsFile[packetNum].setPID(strtoul(param.data(), NULL, 16));
  }
  else if (op == "ptsauto")
  {
    if (!tsFile[packetNum].hasPCR())
    {
      fprintf(stderr, "Error in packet %d: can't set ptsauto, no PCR!\n", packetNum);
    }
    else
    {
      tsFile[packetNum].setPTS((tsFile[packetNum].getPCR() >> 15) - 10000);
    }
  }
  else if (op == "pusi")
  {
    tsFile[packetNum].setPUSI();
  }
  else if (op == "valid")
  {
    tsFile[packetNum].setValid();
    tsFile[packetNum].clearTEIFlag();
  }
}

//----------------------------------------------------------------------------
void
runFixCommand(TSFile& tsFile, std::string fixCommand)
{
  std::istringstream iss(fixCommand);
  std::string singleCmd;
  
  do
  {
    std::getline(iss, singleCmd, '/');
    if (singleCmd != "") runSingleFix(tsFile, singleCmd);
  } while(!iss.eof());
}

//----------------------------------------------------------------------------
bool
isFrameStart(TSPacket& packet)
{
  return((packet.pid() == 0x3e8) && packet.getPUSI());
}

//----------------------------------------------------------------------------
bool
isIFrame(TSPacket& packet)
{
  if (!isFrameStart(packet)) return(false);
  
  unsigned char* payload = packet.payload();
  return(payload[19] == 0xb0);
}

//----------------------------------------------------------------------------
void
processMP4SingleFrame(TSFile& tsFile, unsigned int startPacket, unsigned int frameNum)
{
  unsigned int endPacket = startPacket;
  unsigned int i;
  
  for(i=startPacket + 1; (i < tsFile.getNumPackets()) && !isFrameStart(tsFile[i]); ++i)
  {
    if (tsFile[i].pid() == 0x3e8) endPacket = i;
  }
  
  if (optionFixMP4AF)
  {
    // Remove AF from all data packets except first and last in frame
    for(i=startPacket + 1; i < endPacket; ++i)
    {
      tsFile[i].removeAF();
    }
  }

  if (optionFrameInfo)
  {
    printf("%c-Frame %d (packets %d - %d): Time %f ",
      isIFrame(tsFile[startPacket])? 'I': 'P',
      frameNum, startPacket, endPacket,
      clockToSeconds(tsFile[startPacket].getPCR() >> 15));

    // Check af[1] is 0x00 on end packet
    if ((tsFile[endPacket].adaptationField() != NULL)
     && (tsFile[endPacket].afLen() > 0))
    {
      unsigned char afFlags = tsFile[endPacket].adaptationField()[1];
      if (afFlags != 0)
      {
        printf("BAD af[1]:%02x ", (unsigned int)afFlags);
      }
    }
    
    printAFAndPayload(tsFile[endPacket]);
    
    printf("\n");
  }
}

//----------------------------------------------------------------------------
void
processMP4(TSFile& tsFile)
{
  unsigned int i;
  unsigned int frameNum = 0;
  for(i=0; i < tsFile.getNumPackets(); ++i)
  {
    if (isFrameStart(tsFile[i]))
    {
      ++frameNum;
      processMP4SingleFrame(tsFile, i, frameNum);
    }
  }
}

//----------------------------------------------------------------------------
int
processFile(std::string inputFilename,
  std::string fixCommand,
  std::string outputTSFilename,
  std::string outputMP4Filename)
{
  TSFile tsFile;

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
 
  if (!tsFile.loadFile(inputFilename)) return(1);

  if (fixCommand != "") runFixCommand(tsFile, fixCommand);

  if (optionFix)
  {
    // We're not just viewing the file, we're trying to repair it
    doFixes(tsFile);
    fprintf(stderr, "Num auto interpolate: %d\n", numFixedAutoInterpolate);
    fprintf(stderr, "   Num payload order: %d\n", numFixedPayloadOrder);
    fprintf(stderr, "         Num bad PCR: %d\n", numFixedBadPCR);
  }
  
  // Work out the MP4 info
  tsFile.scanMP4();
  processMP4(tsFile);
    
  // Normal processing pass
  bool foundBad = false;
  for(unsigned int i=numSkipOnOutput; i < tsFile.getNumPackets(); ++i)
  {
    if (!processPacket(tsFile, i, ofd, mp4fd))
    {
      if (!foundBad)
      {
        foundBad = true;
        printf("----- Stream is bad from here onwards -----\n");
        fprintf(stderr, "Stream is bad from packet %d onwards\n", i);
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
      if      (strcmp(argv[i], "-nofix")      == 0) optionFix         = false;
      else if (strcmp(argv[i], "-realign")    == 0) optionTSRealign   = true;
      else if (strcmp(argv[i], "-dumpaf")     == 0) optionDumpAF      = true;
      else if (strcmp(argv[i], "-fixmp4af")   == 0) optionFixMP4AF    = true;
      else if (strcmp(argv[i], "-frameinfo")  == 0) optionFrameInfo   = true;
      else if (strcmp(argv[i], "-noprintmp4") == 0) optionPrintMP4    = false;
      else if (strcmp(argv[i], "-noprintoff") == 0) optionPrintOffset = false;
      else if (strncmp(argv[i], "-fix:", 5)   == 0) fixCommand = argv[i] + 5;
      else if (strncmp(argv[i], "-pdw:", 5)   == 0) payloadDisplayWidth = atoi(argv[i] + 5);
      else if (strncmp(argv[i], "-adw:", 5)   == 0) afDisplayWidth = atoi(argv[i] + 5);
      else if (strncmp(argv[i], "-skip:", 6)  == 0) numSkipOnOutput = atoi(argv[i] + 6);
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

