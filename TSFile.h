//----------------------------------------------------------------------------
// TSFile
//----------------------------------------------------------------------------

#ifndef _INCL_TSFILE_H
#define _INCL_TSFILE_H 1

#include "TSPacket.h"
#include <string>

//----------------------------------------------------------------------------
class TSFile
{
  public:
                           TSFile();
                           ~TSFile();

    TSPacket&              operator[](unsigned int i)
                           { return(packetBuffer[i]); }

    bool                   loadFile(std::string inputFilename);
    
    unsigned int           getNumPackets() const
                           { return(numPackets); }

    void                   scanMP4();
    
    void                   insertBytes(unsigned int offset, unsigned int numBytes);
    
    unsigned int           getFileSize() const { return(fileSize); }
    unsigned char*         getFileData() const { return(fileData); }

  private:
    unsigned int           getPacketOffset(unsigned int packetNum);
    void                   setPacketPointers();
    
    // Variables
    TSPacket*              packetBuffer;
    unsigned char*         fileData;
    unsigned int           fileSize;
    unsigned int           numPackets;
};

#endif

