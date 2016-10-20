//----------------------------------------------------------------------------
// TSFile
//----------------------------------------------------------------------------

#include "TSFile.h"
#include <stdio.h>
#include <string.h>

//----------------------------------------------------------------------------
// Constructor
TSFile::TSFile(): packetBuffer{nullptr}, fileData{nullptr}, fileSize{0},
  numPackets{0}
{
}

//----------------------------------------------------------------------------
// Destructor
TSFile::~TSFile()
{
  delete [] fileData;
  delete [] packetBuffer;
}

//----------------------------------------------------------------------------
bool
TSFile::loadFile(std::string inputFilename)
{
  FILE* fd = fopen(inputFilename.data(), "r");
  if (fd == nullptr)
  {
    fprintf(stderr, "Cannot open input file '%s'\n", inputFilename.data());
    return(false);
  }

  fseek(fd, 0L, SEEK_END);
  fileSize = ftell(fd);
  fseek(fd, 0L, SEEK_SET);
  
  delete [] fileData;
  fileData = new unsigned char[fileSize];
  fread(fileData, fileSize, 1, fd);
  fclose(fd);
  
  setPacketPointers();
  return(true);
}

//----------------------------------------------------------------------------
void
TSFile::setPacketPointers()
{
  numPackets = fileSize / TS_PACKET_SIZE;

  delete [] packetBuffer;
  packetBuffer = new TSPacket[numPackets];

  for(unsigned int i=0; i < numPackets; ++i)
  {
    packetBuffer[i].setData(fileData, getPacketOffset(i));
  }
}

//----------------------------------------------------------------------------
unsigned int
TSFile::getPacketOffset(unsigned int packetNum)
{
  return(packetNum * TS_PACKET_SIZE);
}

//----------------------------------------------------------------------------
void
TSFile::scanMP4()
{
  unsigned long long int mp4_lastPCR = 0;
  unsigned int mp4_startPos = 0;
  unsigned int i = 0;

  for(i=0; i < numPackets; ++i)
  {
    TSPacket& p = packetBuffer[i];

    if ((p.pid() != 0)
     && (p.pid() != 0x20)
     && (p.pid() != 0x1fff))
    {
      // It's a data packet
      p.mp4_framePCR      = 0;
      p.mp4_startPos      = 0;
      p.mp4_payloadSize   = 0;
      p.mp4_payloadOffset = 0;
      
      p.setPayloadFlag();  // ffmpeg does this internally!
      
      // Detect start of frame
      if (p.getPUSI()
       && (p.adaptationField() != nullptr)
       && (p.afLen() == 7))
      {
        // Frame start
        if (p.hasPCR()) mp4_lastPCR = p.getPCR();
        else mp4_lastPCR = 0;
        
        p.mp4_payloadSize = p.getPayloadSize() - 16;
        p.mp4_payloadOffset = p.getPayloadOffset() + 16;
        mp4_startPos = 0;
      }
      else
      {
        // Normal data packet
        p.mp4_payloadSize = p.getPayloadSize();
        p.mp4_payloadOffset = p.getPayloadOffset();
      }
      
      p.mp4_framePCR = mp4_lastPCR >> 15;
      p.mp4_startPos = mp4_startPos;

      mp4_startPos += p.mp4_payloadSize;
    }
  }
}

//----------------------------------------------------------------------------
void
TSFile::insertBytes(unsigned int offset, unsigned int numBytes)
{
  unsigned int newFileSize = fileSize + numBytes;
  unsigned char* newFileData = new unsigned char[newFileSize];

  // First part
  memcpy(newFileData, fileData, offset + numBytes);
  
  // Second part
  memcpy(newFileData + offset + numBytes,
        fileData + offset,
        fileSize - offset);

  // Reassign pointers
  delete [] fileData;
  fileData = newFileData;
  fileSize = newFileSize;
  setPacketPointers();
}

