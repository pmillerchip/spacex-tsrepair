//----------------------------------------------------------------------------
// TSPacket
//----------------------------------------------------------------------------

#ifndef _INCL_TSPACKET_H
#define _INCL_TSPACKET_H 1

#define TS_PACKET_SIZE 188

//----------------------------------------------------------------------------
class TSPacket
{
  public:
                           TSPacket();
                           ~TSPacket();

    // Returns true if the packet is valid, false if not
    bool                   isValid() const;
    
    // Returns true if the packet has its TEI bit set, false if not
    bool                   getTEI() const;
    
    // Returns true if the packet has its PUSI (payload unit start
    // indicator) bit set, false if not
    bool                   getPUSI() const;
    
    // Returns true if the packet has its PRI bit set, false if not
    bool                   getPRI() const;
    
    // Returns true if the packet has a payload, false if not
    bool                   hasPayload() const;
    
    // Returns the PID (packet ID) field
    unsigned int           pid() const;
    
    // Returns the payload continuity counter
    unsigned int           payloadContinuityCounter() const;
    
    // Returns a pointer to the adaption field, or NULL if none exists
    unsigned char*         adaptationField() const;
    
    // Returns the length of the adaptation field
    unsigned int           afLen() const;
    
    // Returns a pointer to the payload, or NULL if none exists
    unsigned char*         payload() const;
    
    // Returns a pointer to the raw data, or NULL if none exists
    unsigned char*         getData() const { return(data); }

    // Returns true if the packet has a PCR field, false if not
    bool                   hasPCR() const;

    // Returns true if the packet has an OPCR field, false if not
    bool                   hasOPCR() const;
    
    // Returns the PCR field, or zero if none exists
    unsigned long long int getPCR() const;
    
    // Returns true if the packet has a PTS timestamp, false if not
    bool                   hasPTS() const;
    
    // Returns the PTS field, or zero if none exists
    unsigned long long int getPTS() const;
    
    // Get the offset within the file of this packet
    unsigned int           getFileOffset() const { return(fileOffset); }
    
    // Get the size of the payload
    unsigned int           getPayloadSize() const;
    
    // Returns true if the packet is marked as scrambled
    bool                   isScrambled() const;
    
    // SETTERS
    
    void                   setData(unsigned char* d, unsigned int offset);
    void                   setValid();
    void                   setPID(unsigned int pid);
    void                   setPCR(unsigned long long int);
    void                   setPTS(unsigned long long int);
    void                   setPayloadFlag();
    void                   setPayloadContinuityCounter(unsigned int);
    void                   setPUSI();
    void                   clearTEIFlag();
    void                   removeAF();
    void                   removePCR();
    void                   removePRI();
    void                   removePUSI();
    void                   removeScramble();
    void                   setAFLen(unsigned int);

  private:
    // Variables
    unsigned char*         data;
    unsigned int           fileOffset;
};

#endif

