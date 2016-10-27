//----------------------------------------------------------------------------
// TSPacket
//----------------------------------------------------------------------------

#include "TSPacket.h"

//----------------------------------------------------------------------------
// Constructor
TSPacket::TSPacket():
  mp4_framePCR{0},
  mp4_startPos{0},
  mp4_payloadSize{0},
  mp4_payloadOffset{0},
  data{nullptr},
  fileOffset{0}
{
}

//----------------------------------------------------------------------------
// Destructor
TSPacket::~TSPacket()
{
}

//----------------------------------------------------------------------------
bool
TSPacket::isValid() const
{
  return(data[0] == 0x47);
}

//----------------------------------------------------------------------------
unsigned int
TSPacket::pid() const
{
  return(data[2] | ((data[1] & 0x1f) << 8));
}

//----------------------------------------------------------------------------
bool
TSPacket::getTEI() const
{
  return((data[1] & 0x80) != 0);
}

//----------------------------------------------------------------------------
bool
TSPacket::getPUSI() const
{
  return((data[1] & 0x40) != 0);
}

//----------------------------------------------------------------------------
void
TSPacket::setPUSI()
{
  data[1] |= 0x40;
}

//----------------------------------------------------------------------------
void
TSPacket::removePUSI()
{
  data[1] &= ~0x40;
}

//----------------------------------------------------------------------------
bool
TSPacket::getPRI() const
{
  return((data[1] & 0x20) != 0);
}

//----------------------------------------------------------------------------
void
TSPacket::removePRI()
{
  data[1] &= ~0x20;
}

//----------------------------------------------------------------------------
bool
TSPacket::isScrambled() const
{
  return((data[3] & 0xc0) != 0);
}

//----------------------------------------------------------------------------
void
TSPacket::removeScramble()
{
  data[3] &= ~0xc0;
}

//----------------------------------------------------------------------------
unsigned char*
TSPacket::adaptationField() const
{
  if ((data[3] & 0x20) == 0) return(nullptr);
  
  return(data + 4);
}

//----------------------------------------------------------------------------
void
TSPacket::removeAF()
{
  data[3] &= ~0x20;
}

//----------------------------------------------------------------------------
bool
TSPacket::hasPCR() const
{
  const unsigned char* af = adaptationField();
  if (af == nullptr) return(false);  // PCR is in AF, and AF is not present
  
  return((af[1] & 0x10) != 0);
}

//----------------------------------------------------------------------------
void
TSPacket::removePCR()
{
  unsigned char* af = adaptationField();
  if (af == nullptr) return;  // PCR is in AF, and AF is not present
  
  af[1] &= ~0x10;
}

//----------------------------------------------------------------------------
bool
TSPacket::hasOPCR() const
{
  const unsigned char* af = adaptationField();
  if (af == nullptr) return(false);  // OPCR is in AF, and AF is not present
  
  return((af[1] & 0x08) != 0);
}

//----------------------------------------------------------------------------
unsigned long long int
TSPacket::getPCR() const
{
  const unsigned char* af = adaptationField();
  if (af == nullptr) return(false);  // PCR is in AF, and AF is not present
  
  unsigned long long int rv =
    (static_cast<unsigned long long int>(af[2]) << 40)
  | (static_cast<unsigned long long int>(af[3]) << 32)
  | (static_cast<unsigned long long int>(af[4]) << 24)
  | (static_cast<unsigned long long int>(af[5]) << 16)
  | (static_cast<unsigned long long int>(af[6]) << 8)
  | (static_cast<unsigned long long int>(af[7]));
  return(rv);
}

//----------------------------------------------------------------------------
void
TSPacket::setPCR(unsigned long long int newPCR)
{
  unsigned char* af = adaptationField();
  if (af == nullptr) return;  // PCR is in AF, and AF is not present
  
  // Set PCR present bit
  af[1] |= 0x10;
  
  // Copy PCR value
  af[2] = (newPCR >> 40) & 0xff;
  af[3] = (newPCR >> 32) & 0xff;
  af[4] = (newPCR >> 24) & 0xff;
  af[5] = (newPCR >> 16) & 0xff;
  af[6] = (newPCR >> 8)  & 0xff;
  af[7] =  newPCR        & 0xff;
}

//----------------------------------------------------------------------------
bool
TSPacket::hasPayload() const
{
  return((data[3] & 0x10) != 0);
}

//----------------------------------------------------------------------------
unsigned char*
TSPacket::payload() const
{
  if ((data[3] & 0x10) == 0) return(nullptr);
  
  return(data + getPayloadOffset());
}

//----------------------------------------------------------------------------
unsigned int
TSPacket::getPayloadOffset() const
{
  if ((data[3] & 0x10) == 0) return(0);
  
  unsigned char* af = adaptationField();
  if (af == nullptr) return(4);
  
  unsigned int afLen = static_cast<unsigned int>(af[0]);
  return(4 + 1 + afLen);
}

//----------------------------------------------------------------------------
unsigned int
TSPacket::afLen() const
{
  unsigned char* af = adaptationField();
  if (af == nullptr) return(0);
  
  return((unsigned int)af[0]);
}

//----------------------------------------------------------------------------
void
TSPacket::setAFLen(unsigned int newLen)
{
  // Make sure we have an AF!
  data[3] |= 0x20;

  unsigned char* af = adaptationField();
  if (af == nullptr) return;
  
  af[0] = newLen;
}

//----------------------------------------------------------------------------
unsigned int
TSPacket::getPayloadSize() const
{
  if ((data[3] & 0x10) == 0) return(0);

  const unsigned char* af = adaptationField();
  if (af == nullptr) return(TS_PACKET_SIZE - 4);
  
  unsigned int afLen = static_cast<unsigned int>(af[0]);
  if (afLen > (TS_PACKET_SIZE - 4 - 1)) return(0);
  return(TS_PACKET_SIZE - afLen - 4 - 1);
}

//----------------------------------------------------------------------------
unsigned int
TSPacket::payloadContinuityCounter() const
{
  return(data[3] & 0x0f);
}

//----------------------------------------------------------------------------
bool
TSPacket::hasPTS() const
{
  const unsigned char* pay = payload();
  if (pay == nullptr) return(false);
  
  if ((pay[0] != 0)
   || (pay[1] != 0)
   || (pay[2] != 1))
  {
    // No PES header
    return(false);
  }
  
  // It's a PES packet
  return((pay[7] & 0x80) != 0);
}

//----------------------------------------------------------------------------
unsigned long long int
TSPacket::getPTS() const
{
  const unsigned char* pay = payload();
  if (pay == nullptr) return(0);
  
  if ((pay[0] != 0)
   || (pay[1] != 0)
   || (pay[2] != 1))
  {
    // No PES header
    return(0);
  }
  
  // It's a PES packet
  if ((pay[7] & 0x80) == 0) return(0);
  
  return((static_cast<unsigned long long int>(pay[9]  & 0x0e) << 29)
       | (static_cast<unsigned long long int>(pay[10] & 0xff) << 22)
       | (static_cast<unsigned long long int>(pay[11] & 0xfe) << 14)
       | (static_cast<unsigned long long int>(pay[12] & 0xff) << 7)
       | (static_cast<unsigned long long int>(pay[13] & 0xfe) >> 1));
}

//----------------------------------------------------------------------------
void
TSPacket::setPTS(unsigned long long int newPTS)
{
  unsigned char* pay = payload();
  if (pay == nullptr) return;
  
  pay[9]  = (pay[9] & 0xf1) | ((newPTS >> 29) & 0x0e);
  pay[10] = (newPTS >> 22) & 0xff;
  pay[11] = (pay[11] & 0x01) | ((newPTS >> 14) & 0xfe);
  pay[12] = (newPTS >> 7) & 0xff;
  pay[13] = (pay[13] & 0x01) | ((newPTS << 1) & 0xfe);
}

//----------------------------------------------------------------------------
void
TSPacket::setValid()
{
  data[0] = 0x47;
}

//----------------------------------------------------------------------------
void
TSPacket::setPID(unsigned int pid)
{
  data[1] = (data[1] & 0xe0) | ((pid & 0x1f00) >> 8);
  data[2] = pid & 0xff;
}

//----------------------------------------------------------------------------
void
TSPacket::setPayloadFlag()
{
  data[3] |= 0x10;
}

//----------------------------------------------------------------------------
void
TSPacket::setPayloadContinuityCounter(unsigned int c)
{
  data[3] = (data[3] & 0xf0) | (c & 0xf);
}

//----------------------------------------------------------------------------
void
TSPacket::setData(unsigned char* d, unsigned int off)
{
  data = d + off;
  fileOffset = off;
}

//----------------------------------------------------------------------------
void
TSPacket::clearTEIFlag()
{
  data[1] &= 0x7f;
}

//----------------------------------------------------------------------------
void
TSPacket::writePadding()
{
  if (!hasPayload()) return;
  
  unsigned int offset = getPayloadOffset();
  while(offset < TS_PACKET_SIZE)
  {
    data[offset++] = 0xff;
  }
}

//----------------------------------------------------------------------------
// Read unsigned int32, big endian
unsigned int
TSPacket::readUInt32BE(const unsigned char* data)
{
  unsigned int rv =
    (data[0] << 24)
  | (data[1] << 16)
  | (data[2] << 8)
  | (data[3]);
  return(rv);
}



