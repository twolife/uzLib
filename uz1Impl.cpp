/*
uz1Impl.h: Contains implementation of the classes required for the uz1-compression/decompression.

Language: C++

Created by Gugi, 2010-2011
*/

#include "stdafx.h"

#include "uz1Impl.h"

#include <vector>
#include <algorithm>
#include <iterator>
#include <cassert>
#include <sstream>
#include <stdexcept>

#include <boost/dynamic_bitset.hpp>

#if (BWT_SORT_TYPE == BWT_EXT_SORT)
  #include "bwtsort.h"
#endif

using namespace std;
using namespace uzLib;

#define CUR_FUNC_NAME __FUNCTION__


// Activates more aggressive optimizations, which basically bypass the streams and work with the buffers directly.
// I am not completely sure if I produced correct code for the aggressive mode, but it worked in my tests... (I guess
// problems might occur in case of unexpected errors).
// Compression:
//    AS-HiSpeed.unr (4.21MB):
//        Aggressive: 5s
//        Normal: 12s
// Decompression:
//    AS-HiSpeed.unr (4.21MB):
//        Aggressive: 2s
//        Normal: 8s

#define AGRESSIVE_OPTIMIZATION



//!!!!!!!!!!!!!!!!!!
// This file should be compiled without the CLR
//!!!!!!!!!!!!!!!!!!
#ifdef __cplusplus_cli 
  #error This file is intended to be compiled as pure C++ code (not as managed code).
#endif


//============================================================================================================================
//============================================================================================================================
// General functions
//============================================================================================================================
//============================================================================================================================
namespace
{
  // Reads the specified type from the stream (unformatted).
  template <typename T>
  inline const T ReadData(uzLib::in_stream& InStream)
  {
    T Target;
    InStream.read(reinterpret_cast<BYTE*>(&Target), sizeof(T));
    return Target;
  }
  
  // Reads a byte from the stream.
  inline BYTE ReadByte(uzLib::in_stream& InStream)
  {
    return ReadData<BYTE>(InStream);
  }
  
  // Reads an int from the stream.
  inline int ReadInt(uzLib::in_stream& InStream)
  {
    return ReadData<int>(InStream);
  }
  
  // Write data to the stream.
  template <typename T>
  inline void WriteData(uzLib::out_stream& OutStream, const T& ToWrite)
  {
    OutStream.write(reinterpret_cast<const BYTE*>(&ToWrite), sizeof(T));
  }
  
  // Write a byte to the stream.
  inline void WriteByte(uzLib::out_stream& OutStream, BYTE ToWrite)
  {
#ifdef AGRESSIVE_OPTIMIZATION
    OutStream.rdbuf()->sputc(ToWrite);
#else
    WriteData(OutStream, ToWrite);
#endif
  }
  
  // Write an int to the stream.
  inline void WriteInt(uzLib::out_stream& OutStream, int ToWrite)
  {
    WriteData(OutStream, ToWrite);
  }

  // Returns the status of the stream as a string.
  std::string GetStreamStatusStr(const uzLib::in_stream& Stream)
  {
    if (Stream.good())
      return "good";
      
    std::string ToReturn;
    if (Stream.eof())
      ToReturn += "eof|";
    if (Stream.bad())
      ToReturn += "bad|";
    if ((Stream.rdstate() & std::ios::failbit) != 0)
      ToReturn += "fail|";
      
    if (ToReturn.length() == 0)
      return "unknown";
    else
      return ToReturn.substr(0, ToReturn.length()-1);
  }
  
  // Returns true, if the end of the stream is reached, i.e. if the last byte WAS already read, i.e. if the next read-operation sets the eof-bit.
  inline bool IsEOF(uzLib::in_stream& InStream)
  {
#ifdef AGRESSIVE_OPTIMIZATION
    if (!in_stream::traits_type::eq_int_type(InStream.rdbuf()->sgetc(), in_stream::traits_type::eof()))
      return false;
    
    InStream.setstate(ios::eofbit);
    return true;
#else
    // This is actually faster than "return InStream.peek() == in_stream::traits_type::eof()" (I tried it). No idea why... (even "return InStream.peek() == -1").
    InStream.peek();
    return !InStream.good();
#endif
  }
  
  // Tries to read the next byte from the stream. Returns false on failure.
  inline bool TryReadNextByte(uzLib::in_stream& InStream, BYTE& OutByte)
  {
#ifdef AGRESSIVE_OPTIMIZATION

    const in_stream::int_type ReadByte = InStream.rdbuf()->sbumpc();
    
    // Check if the byte is valid.
    if (!in_stream::traits_type::eq_int_type(ReadByte, in_stream::traits_type::eof()))
    {
      OutByte = static_cast<BYTE>(ReadByte);
      return true;
    }
    
    InStream.setstate(ios::eofbit);
    return false;
    
#else

    if (IsEOF(InStream)) // Call this first so that failbit isn't set (which causes an exception).
      return false;
    OutByte = ReadByte(InStream);
    return InStream.good();   
#endif
  }
    
  // Returns the total size of the stream. This should be a slow function as several seeking-operations are required.
  int GetTotalStreamLength(uzLib::in_stream& InStream)
  {
    // Save the current position.
    const ios::pos_type CurPos = InStream.tellg();
    assert(CurPos != (ios::pos_type)(-1));
    
    // Move the position to the beginning and get the start position.
    InStream.seekg(0, ios_base::beg);
    
    const ios::pos_type BegPos = InStream.tellg();
    assert(BegPos != (ios::pos_type)(-1));
    
    // Move the position to the end and get the end position.
    InStream.seekg(0, ios_base::end);
    
    const ios::pos_type EndPos = InStream.tellg();
    assert(EndPos != (ios::pos_type)(-1));
    
    if (BegPos < 0 || EndPos < 0)
      throw std::logic_error("Either BegPos or EndPos in GetTotalStreamLength is negative.");
    else if (BegPos > EndPos)
      throw std::logic_error("BegPos is greater than EndPos in GetTotalStreamLength.");
      
    // Move the position to the original one.
    InStream.seekg(CurPos, ios_base::beg);
    
    // Returns the size.
    return static_cast<int>(EndPos - BegPos);
  }
  
  // Copies max. the specified number of bytes into the destination vector (at the end) and returns the number of copied bytes.
  int CopyDataToVector(uzLib::in_stream& InStream, vector<BYTE>& Destination, const size_t Count)
  {
    // Faster version (at least in aggressive-optimization mode, the other one I did not test).
    size_t NumExtractedBytes = 0;
    BYTE CurByte;
    while (NumExtractedBytes < Count && TryReadNextByte(InStream, CurByte))
    {
      Destination.push_back(CurByte);
      ++NumExtractedBytes;
    }
    return NumExtractedBytes;
    
    // Slower version.
    
    //std::istreambuf_iterator<BYTE> InIter(InStream);
    //const std::istreambuf_iterator<BYTE> EndIter;
    //
    //size_t NumExtractedBytes = 0;
    //for (; 
    //    NumExtractedBytes < Count && InIter != EndIter; 
    //    ++NumExtractedBytes, ++InIter)
    //{
    //  Destination.push_back(*InIter);
    //}
    //
    //return NumExtractedBytes;
  }
  
  // Reads a compact index (compressed int) from the stream.
  int ReadCompactIndex(in_stream& InStream)
  {
    int ToReturn = 0;
  
    const BYTE FirstByte = ReadByte(InStream);
    if ((FirstByte & 0x40) != 0)
    {
      const BYTE SecondByte = ReadByte(InStream);
      if ((SecondByte & 0x80) != 0)
      {
        const BYTE ThirdByte = ReadByte(InStream);
        if ((ThirdByte & 0x80) != 0)
        {
          const BYTE FourthByte = ReadByte(InStream);
          if ((FourthByte & 0x80) != 0)
          {
            const BYTE FifthByte = ReadByte(InStream);
            ToReturn = FifthByte;
          }
          ToReturn = (ToReturn << 7) | (FourthByte & 0x7f);
        }
        ToReturn = (ToReturn << 7) | (ThirdByte & 0x7f);
      }
      ToReturn = (ToReturn << 7) | (SecondByte & 0x7f);
    }
    
    // EOF-bit shouldn't be set.
    if (!InStream.good())
      throw std::runtime_error("Status in InStream was set to " + GetStreamStatusStr(InStream) + " while reading a compact index.");
    
    ToReturn = (ToReturn << 6) | (FirstByte & 0x3f);
    
    if ((FirstByte & 0x80) != 0)
      ToReturn = -ToReturn;
    
    return ToReturn;
  }
  
  // Writes the specified integer in a compressed format to the stream.
  void WriteCompactIndex(out_stream& OutStream, int ToWrite)
  {
    unsigned int AbsIn = ::abs(ToWrite);
    
    BYTE FirstByte = ((ToWrite>=0) ? 0 : 0x80);
    if (AbsIn < 0x40)
      FirstByte += static_cast<BYTE>(AbsIn);
    else
      FirstByte += ((AbsIn & 0x3f) + 0x40);
    
    WriteByte(OutStream, FirstByte);
    if ((FirstByte & 0x40) != 0)
    {
      AbsIn >>= 6;
      const BYTE SecondByte = (AbsIn < 0x80) ? static_cast<BYTE>(AbsIn) : ((AbsIn & 0x7f) + 0x80);
      WriteByte(OutStream, SecondByte);
      
      if ((SecondByte & 0x80) != 0)
      {
        AbsIn >>= 7;
        const BYTE ThirdByte = (AbsIn < 0x80) ? static_cast<BYTE>(AbsIn) : ((AbsIn & 0x7f) + 0x80);
        WriteByte(OutStream, ThirdByte);
        
        if ((ThirdByte & 0x80) != 0)
        {
          AbsIn >>= 7;
          const BYTE FourthByte = (AbsIn < 0x80) ? static_cast<BYTE>(AbsIn) : ((AbsIn & 0x7f) + 0x80);
          WriteByte(OutStream, FourthByte);
          
          if ((FourthByte & 0x80) != 0)
          {
            AbsIn >>= 7;
            BYTE FifthByte = static_cast<BYTE>(AbsIn);
            WriteByte(OutStream, FifthByte);
          }
        }
      }
    }
  }
  
  // Reads data as chars from the stream until it encounters a 0 byte; the data is returned as a string.
  std::string ReadASCIIString(in_stream& InStream)
  {
    std::string ToReturn;
    
    char LastChar = 0;
    do
    {
      LastChar = ReadData<char>(InStream);
      if (LastChar != '\0')
        ToReturn += LastChar;
    } 
    while (LastChar != 0 && !IsEOF(InStream));
    
    return ToReturn;
  }
  
  // Reads data as wchar_ts from the stream until it encounters a 0 byte; the data is returned as a string.
  std::wstring ReadUnicodeString(in_stream& InStream)
  {
    std::wstring ToReturn;
    
    wchar_t LastChar = 0;
    do
    {
      LastChar = ReadData<wchar_t>(InStream);
      if (LastChar != L'\0')
        ToReturn += LastChar;
    } 
    while (LastChar != 0 && !IsEOF(InStream));
    
    return ToReturn;
  }

}


//============================================================================================================================
//============================================================================================================================
// uz1 compression/decompression implementation
//============================================================================================================================
//============================================================================================================================

namespace
{
  // Copies the data from the InStream to the TargetStream and enables its exceptions.
  /*void InitTargetStream(in_stream& InStream, std::basic_stringstream<BYTE>& TargetStream)
  {
    // Read all the original data into the target stream.
    TargetStream.exceptions(std::ios::badbit | std::ios::failbit);
    while (!IsEOF(InStream))
    {
      const BYTE CurByte = ReadByte(InStream);
      WriteByte(TargetStream, CurByte);
    }
    assert(!InStream.fail());
  }*/

  // Removes the content from the specified str-stream and resets the object.
  bool ResetBuffer(std::basic_stringstream<BYTE>& ToReset, const std::basic_string<BYTE>& EmptyBufferValue)
  {  
    ToReset.clear();
    ToReset.seekg(0, std::ios::beg);
    ToReset.seekp(0, std::ios::beg);
    ToReset.str(EmptyBufferValue);
    
    if (!ToReset.good())
      throw std::runtime_error("Failed reseting buffer.");
    
    //return (ToReset.good());
    return true;
  }
  
  // Does the decompressing of the input buffer, and sets the buffers up for the next decompressing step.
  bool DoDecompressing(uzLib::uz1AlgorithmBase& Algorithm, 
      std::basic_stringstream<BYTE>*& pInBuffer, std::basic_stringstream<BYTE>*& pOutBuffer, 
      const std::basic_string<BYTE>& EmptyBufferValue)
  {
    if (!Algorithm.Decompress(*pInBuffer, *pOutBuffer))
      return false;
    
    std::swap(pInBuffer, pOutBuffer);
    return ResetBuffer(*pOutBuffer, EmptyBufferValue);    
  }
  
  // Does the cmpressing of the input buffer, and sets the buffers up for the next compressing step.
  bool DoCompressing(uzLib::uz1AlgorithmBase& Algorithm, 
      std::basic_stringstream<BYTE>*& pInBuffer, std::basic_stringstream<BYTE>*& pOutBuffer, 
      const std::basic_string<BYTE>& EmptyBufferValue)
  {
    if (!Algorithm.Compress(*pInBuffer, *pOutBuffer))
      return false;
    
    std::swap(pInBuffer, pOutBuffer);
    return ResetBuffer(*pOutBuffer, EmptyBufferValue);    
  }

  // Writes the filename to the stream (including length); in ASCII chars.
  void WriteFilename(out_stream& OutStream, const std::string& PkgFilename)
  {
    const int FilenameLen = PkgFilename.length()+1; // +1: Include terminating 0 char.
  
    WriteCompactIndex(OutStream, FilenameLen); // positive, to indicate an unicode string.
    OutStream.write(reinterpret_cast<const BYTE*>(PkgFilename.c_str()), FilenameLen);
  }
  
  // Writes the filename to the stream (including length); in Unicode chars.
  void WriteFilename(out_stream& OutStream, const std::wstring& PkgFilename)
  {
    const int FilenameLen = PkgFilename.length()+1; // +1: Include terminating 0 char.
  
    WriteCompactIndex(OutStream, -FilenameLen); // "-" to indicate an unicode string.
    OutStream.write(reinterpret_cast<const BYTE*>(PkgFilename.c_str()), FilenameLen*2); // 2: sizeof(wchar_t)
  }
  
  // In case the unicode string only contains ASCII chars (i.e. chars <= 0x7F), the string is converted and
  // true is returned. Else, false is returned (ASCII-string might have changed).
  bool TryConvertUnicodeToASCII(const std::wstring& UnicodeStr, std::string& ASCII)
  {
    for (size_t CurCharIndex = 0; CurCharIndex < UnicodeStr.length(); ++CurCharIndex)
    {
      const wchar_t CurChar = UnicodeStr.at(CurCharIndex);
      if (CurChar > 0x7F) // In case it is not an ASCII char, return false immediatly.
        return false;
      
      ASCII += static_cast<char>(CurChar);
    }
    
    return true;
  }
}

namespace
{
  // Compression (ASCII or Unicode package name); basically the reverse of the decompression algorithm.
  template <class T>
  bool CompressToUz1_Templ(in_stream& InStream, out_stream& OutStream, const T& PkgFilename, EUz1Signature Uz1Sig, 
      uzLib::pUz1UpdateFunc UpdateFunc, void* UserObj)
  {
    // Send an initial update.
    if (UpdateFunc != NULL)
    {
      bool bCancel = false;
      (*UpdateFunc)(0, 1, L"Initializing...", bCancel, UserObj);
      if (bCancel)
        return false;
    }
    
    InStream.clear(); // Clear any bad-flags.
    InStream.seekg(0, ios_base::beg); // Move the in-pointer to the beginning.
    InStream.exceptions(std::ios::badbit | std::ios::failbit);
    
    const int Uz1Signature = static_cast<int>(Uz1Sig);
    
    // Write signature
    OutStream.exceptions(std::ios::badbit | std::ios::failbit);
    WriteInt(OutStream, Uz1Signature);
    
    // Write the filename (including length).
    WriteFilename(OutStream, PkgFilename);
    
    // Create buffer 1.
    std::basic_stringstream<BYTE> Buffer1(std::ios::binary | std::ios::in | std::ios::out);
    const std::basic_string<BYTE> EmptyBufferValue = Buffer1.str();
    Buffer1.exceptions(std::ios::badbit | std::ios::failbit);
    
    // Create second buffer.
    std::basic_stringstream<BYTE> Buffer2(std::ios::binary | std::ios::in | std::ios::out);
    Buffer2.exceptions(std::ios::badbit | std::ios::failbit);
    
    // Pointers used to access always the correct stream easily
    std::basic_stringstream<BYTE>* pInBuffer = &Buffer1;
    std::basic_stringstream<BYTE>* pOutBuffer = &Buffer2;
    
    
    // Compress the data from here on.
    const int NumSteps = (Uz1Sig == USIG_5678) ? 5 : 4; // The number of compression steps.
    int CurStep = 0;
    
    // RLE encoding.
    uz1RLEAlgorithm RLE(UpdateFunc, UserObj, ++CurStep, NumSteps);
    if (!RLE.Compress(InStream, *pInBuffer))
      return false;
  
    // BW encoding.
    uz1BurrowsWheelerAlgorithm BW(UpdateFunc, UserObj, ++CurStep, NumSteps);
    if (!DoCompressing(BW, pInBuffer, pOutBuffer, EmptyBufferValue))
      return false;
  
    // MTF encoding.
    uz1MoveToFrontAlgorithm MTF(UpdateFunc, UserObj, ++CurStep, NumSteps);
    if (!DoCompressing(MTF, pInBuffer, pOutBuffer, EmptyBufferValue))
      return false;

    // RLE encoding.
    if (Uz1Sig == USIG_5678)
    {
      uz1RLEAlgorithm RLE(UpdateFunc, UserObj, ++CurStep, NumSteps);
      if (!DoCompressing(RLE, pInBuffer, pOutBuffer, EmptyBufferValue))
        return false;
    }
    
    // Huffman encoding.
    uz1HuffmanAlgorithm Huffman(UpdateFunc, UserObj, ++CurStep, NumSteps);
    if (!Huffman.Compress(*pInBuffer, OutStream))
      return false;
   
    return true;
  }
}

// Compression (ASCII)
bool uzLib::CompressToUz1(in_stream& InStream, out_stream& OutStream, const std::string& PkgFilename, 
    EUz1Signature Uz1Sig, uzLib::pUz1UpdateFunc UpdateFunc, void* UserObj)
{
  return CompressToUz1_Templ(InStream, OutStream, PkgFilename, Uz1Sig, UpdateFunc, UserObj);
}

// Compression (Unicode)
bool uzLib::CompressToUz1(in_stream& InStream, out_stream& OutStream, const std::wstring& PkgFilename, 
    EUz1Signature Uz1Sig, uzLib::pUz1UpdateFunc UpdateFunc, void* UserObj)
{
  // Try to convert the filename to ASCII. If it fails, we need to use unicode.
  std::string ConvFilename;
  if (TryConvertUnicodeToASCII(PkgFilename, ConvFilename))
    return CompressToUz1_Templ(InStream, OutStream, ConvFilename, Uz1Sig, UpdateFunc, UserObj);
  else
    return CompressToUz1_Templ(InStream, OutStream, PkgFilename, Uz1Sig, UpdateFunc, UserObj);
}


// Decompression: See USetupDefinition.cpp from the UT99 public source or the UTPackage delphi library.
bool uzLib::DecompressFromUz1(in_stream& InStream, out_stream& OutStream, SFilename& OrigFilename, pUz1UpdateFunc UpdateFunc, void* UserObj)
{
  // Send an initial update.
  if (UpdateFunc != NULL)
  {
    bool bCancel = false;
    (*UpdateFunc)(0, 1, L"Initializing...", bCancel, UserObj);
    if (bCancel)
      return false;
  }

  InStream.clear(); // Clear any bad-flags.
  InStream.seekg(0, ios_base::beg); // Move the in-pointer to the beginning.
  InStream.exceptions(std::ios::badbit | std::ios::failbit);

  // uz file format
  // 1) DWORD: Sig
  // 2) FCompactIndex: StrLen Incl 0 char
  // 3) char-array: Orig filename (ends with \0).
  // 4) File data
  
  // Check if it is a valid uz1 file.
  const int Uz1Signature = ReadInt(InStream);
  if (Uz1Signature != 1234 && Uz1Signature != 5678)
    throw runtime_error("Input stream is not a valid uz-file.");
  
  // Read the length of the saved original filename. Includes the terminating 0 character.
  const int OrigFilenameLen = ReadCompactIndex(InStream);
  if (OrigFilenameLen == 0)
    throw runtime_error("Original filename length is 0.");
  
  // Read the original filename.
  if (OrigFilenameLen > 0) // > 0: ASCII string.
  {
    OrigFilename.FilenameType = FT_ASCII;
    OrigFilename.ASCIIStr = ReadASCIIString(InStream);
    
    if (static_cast<int>(OrigFilename.ASCIIStr.length()) != OrigFilenameLen-1) // -1: terminating 0 byte
      throw runtime_error("Original filename and its saved length are different.");
  }
  else // < 0: Unicode string.
  {
    OrigFilename.FilenameType = FT_UNICODE;
    OrigFilename.UnicodeStr = ReadUnicodeString(InStream);
    
    if (static_cast<int>(OrigFilename.UnicodeStr.length()) != (-OrigFilenameLen)-1) // -1: terminating 0 byte
      throw runtime_error("Original filename and its saved length are different.");
  }
    
  // Create buffer 1.
  std::basic_stringstream<BYTE> Buffer1(std::ios::binary | std::ios::in | std::ios::out);
  const std::basic_string<BYTE> EmptyBufferValue = Buffer1.str();
  Buffer1.exceptions(std::ios::badbit | std::ios::failbit);
  
  // Create second buffer.
  std::basic_stringstream<BYTE> Buffer2(std::ios::binary | std::ios::in | std::ios::out);
  Buffer2.exceptions(std::ios::badbit | std::ios::failbit);
  
  // Pointers used to access always the correct stream easily.
  std::basic_stringstream<BYTE>* pInBuffer = &Buffer1;
  std::basic_stringstream<BYTE>* pOutBuffer = &Buffer2;
  
  
  // Decompress the data from here on.
  const int NumSteps = (Uz1Signature == 5678) ? 5 : 4; // The number of decompression steps.
  int CurStep = 0;
  
  // Huffman decoding.
  uz1HuffmanAlgorithm Huffman(UpdateFunc, UserObj, ++CurStep, NumSteps);
  if (!Huffman.Decompress(InStream, *pInBuffer, InStream.tellg())) // tellg(): The data starts at the current position.
    return false;
  
  // RLE decoding.
  if (Uz1Signature == 5678)
  {
    uz1RLEAlgorithm RLE(UpdateFunc, UserObj, ++CurStep, NumSteps);
    if (!DoDecompressing(RLE, pInBuffer, pOutBuffer, EmptyBufferValue))
      return false;
  }
  
  // MTF decoding.
  uz1MoveToFrontAlgorithm MTF(UpdateFunc, UserObj, ++CurStep, NumSteps);
  if (!DoDecompressing(MTF, pInBuffer, pOutBuffer, EmptyBufferValue))
    return false;
  
  // BW decoding.
  uz1BurrowsWheelerAlgorithm BW(UpdateFunc, UserObj, ++CurStep, NumSteps);
  if (!DoDecompressing(BW, pInBuffer, pOutBuffer, EmptyBufferValue))
    return false;
  
  // RLE decoding.
  OutStream.exceptions(std::ios::badbit | std::ios::failbit);
  uz1RLEAlgorithm RLE(UpdateFunc, UserObj, ++CurStep, NumSteps);
  if (!RLE.Decompress(*pInBuffer, OutStream))
    return false;
  
  return true;
}

bool uzLib::DecompressFromUz1(in_stream& InStream, out_stream& OutStream, pUz1UpdateFunc UpdateFunc, void* UserObj)
{
  SFilename TempFilename;
  return DecompressFromUz1(InStream, OutStream, TempFilename, UpdateFunc, UserObj);
}


//============================================================================================================================
//============================================================================================================================
// Algorithm implementations: See FCodec.h from the UT99 public source.
//============================================================================================================================
//============================================================================================================================


//============================================================================================================================
// uz1AlgorithmBase
//============================================================================================================================
uzLib::uz1AlgorithmBase::uz1AlgorithmBase(pUz1UpdateFunc UpdateFunc, void* UserObj, int ThisStepNum, int NumSteps):
  m_UpdateFunc(UpdateFunc), m_pUserObj(UserObj), m_ThisStepNum(ThisStepNum)
{ 
  if (ThisStepNum >= 0)
  {
    std::wstringstream StrStream;
    StrStream << NumSteps;
    m_NumStepsStr = StrStream.str();
  }
}

int uzLib::uz1AlgorithmBase::AlgorithmPreamble(in_stream& InStream, out_stream&, ios::pos_type InStreamStartPos)
{
  InStream.clear(); // Clear any bad-flags.
  InStream.seekg(InStreamStartPos, ios_base::beg); // Move the in-pointer to the desired position.
  return GetTotalStreamLength(InStream);
}

bool uzLib::uz1AlgorithmBase::CallUpdateFunction(unsigned int CurStatus, unsigned int CompletedStatus, const std::wstring& Msg)
{
  if (m_UpdateFunc == NULL)
    return false;
  
  std::wstring TheMessage = Msg;
  if (m_ThisStepNum >= 0)
  {
    std::wstringstream StrStream;
    StrStream << m_ThisStepNum;
    
    TheMessage = L"(" + StrStream.str() + L"/" + m_NumStepsStr + L") " + TheMessage;
  }

  bool bCancel = false;
  (*m_UpdateFunc)(CurStatus, CompletedStatus, TheMessage, bCancel, m_pUserObj);
  return bCancel;
}


//============================================================================================================================
// uz1BurrowsWheelerAlgorithm
//============================================================================================================================

namespace
{
#if (BWT_SORT_TYPE == BWT_EXT_SORT)

  // Wraps the KeyPrefix pointer used in uz1BurrowsWheelerAlgorithm::Compress. Used to prevent a memory-leak in
  // case an exception is thrown.
  class KeyPrefixPtr
  {
    public:
      // Constructor
      KeyPrefixPtr(): m_KeyPrefix(NULL) { }
      explicit KeyPrefixPtr(KeyPrefix* KeyPrefixArray): 
          m_KeyPrefix(KeyPrefixArray) { }
      
      // Destructor: Frees the memory.
      ~KeyPrefixPtr()
      {
        Free();
      }
      
      // Frees the memory.
      void Free()
      {
        if (m_KeyPrefix != NULL)
        {
          free(m_KeyPrefix);
          m_KeyPrefix = NULL;
        }
      }
      
      // Frees the current array and sets a new one.
      void Set(KeyPrefix* KeyPrefixArray)
      {
        Free();
        m_KeyPrefix = KeyPrefixArray;
      }
      
      // Accessor to the array.
      unsigned int& operator[](int Index)
      {
        if (m_KeyPrefix == NULL)
          throw std::runtime_error("m_KeyPrefix == NULL in BWT.");
        return m_KeyPrefix[Index].offset;
      }
            
      
    private:
      KeyPrefix* m_KeyPrefix;
  };
#endif

}

//-----------------------------------------------------------------------------------------
// Static data definition.
//-----------------------------------------------------------------------------------------
#if (BWT_SORT_TYPE == BWT_STD_SORT)
  std::vector<BYTE>* uzLib::uz1BurrowsWheelerAlgorithm::Temp_CompressBuffer = NULL;
  int uzLib::uz1BurrowsWheelerAlgorithm::Temp_CompressLength = 0;
#elif (BWT_SORT_TYPE == BWT_C_SORT)
  unsigned char* uzLib::uz1BurrowsWheelerAlgorithm::Temp_CStyle_CompressBuffer = NULL;
  int uzLib::uz1BurrowsWheelerAlgorithm::Temp_CStyle_CompressLength = 0;
#endif

//-----------------------------------------------------------------------------------------
// Function implementation
//-----------------------------------------------------------------------------------------

// Constructor
uzLib::uz1BurrowsWheelerAlgorithm::uz1BurrowsWheelerAlgorithm(pUz1UpdateFunc UpdateFunc, void* UserObj, int ThisStepNum, int NumSteps):
    uz1AlgorithmBase(UpdateFunc, UserObj, ThisStepNum, NumSteps)
{ }

bool uzLib::uz1BurrowsWheelerAlgorithm::Compress(uzLib::in_stream& InStream, uzLib::out_stream& OutStream, ios::pos_type InStreamBeg)
{
  static const std::wstring UPDATE_MSG = L"Burrows Wheeler Encoding";

  const int InStreamLength = AlgorithmPreamble(InStream, OutStream, InStreamBeg);
    
  if (CallUpdateFunction(0, InStreamLength, UPDATE_MSG))
    return false;

  // CompressBuffer will hold the data-chunks from the file.
  vector<uzLib::BYTE> CompressBuffer(MAX_BUFFER_SIZE);
  
  // CompressPosition will point to or is the index-array which is used to rearange the data.
#if (BWT_SORT_TYPE == BWT_EXT_SORT)
  KeyPrefixPtr CompressPosition;
#else
  vector<int> CompressPosition(MAX_BUFFER_SIZE+1);
#endif

#if (BWT_SORT_TYPE == BWT_7Z_SORT)
  const int kBlockSizeMultMax = 9;
  const UInt32 kBlockSizeStep = 100000;
  const UInt32 kBlockSizeMax = kBlockSizeMultMax * kBlockSizeStep;

  // "Temporary" array which is required by the 7zip-style.
  UInt32* pBlockSorterIndex = new UInt32[BLOCK_SORT_BUF_SIZE(kBlockSizeMax) * sizeof(UInt32)];
#endif
  
  
  // Loop through all the bytes in the input.
  int ProcessedBytes = 0;
  while (!IsEOF(InStream))
  {
    if (CallUpdateFunction(ProcessedBytes, InStreamLength, UPDATE_MSG))
      return false;

    // Copy the next data-chunk into the buffer.
    CompressBuffer.clear();
    const int CompressLength = CopyDataToVector(InStream, CompressBuffer, MAX_BUFFER_SIZE);
    if (CompressLength <= 0 || CompressBuffer.empty()) // Shouldn't happen as there should always be something to copy.
      throw std::logic_error("Couldn't read next chunk into the buffer in uz1BurrowsWheelerAlgorithm::Compress.");
    ProcessedBytes += CompressLength;
    
    
    // The following step is the time-expensive one. It sorts an index-array (CompressPosition) with the help of the CompressBuffer-data.
    // CompressPosition has to have a length of CompressLength+1. The last element is always CompressLength
    // (i.e. CompressPosition[CompressLength] == CompressLength).
    // - Reason (for the std or c-style): CompressPosition[CompressLength] is after the initialization set to CompressLength. If you look at the
    //   ClampedBufferCompare()-function, you realize that in case P2 points to that last element, the for-loop is never executed, 
    //   because P2 > Temp_CompressLength. So always the last return-statement is executed, so that last element stays in that position.
    // - bwtsort (i.e. BWT_EXT_SORT) returns an array which is identical to the std and c styles, but the last element (i.e. with the index
    //   CompressLength) needs to be set manually.
    // - BlockSort (i.e. BWT_7Z_SORT) returns an array which is ALMOST identical. I haven't explored it further as bwtsort is faster anyway.
    
    // Required time: STD > C > 7z > Ext
    // Speed tests (Normal AS-HiSpeed.unr; complete BWT):
    //    STD: 416.43s
    //    C: 235.195s
    //    EXT: 1.68164s
    //    7Z: 1.97155s
    // STD is so slow, I guess, because of the overhead of the STL (even when everything "suitable" is inlined). This is missing in the C-style, so
    // that one is faster. The problem with these two approaches is that it is the straight-forward way to sort the CompressPosition using
    // a quick-sort, which is slow for large data amounts. 7z and "Ext" use an optimized algorithm of BWT.
    
    // C-Style: Basically the original UT-algorithm
    // STD: My port to C++
    // Ext: From http://sourceforge.net/projects/bwtcoder/files/bwtcoder/preliminary-2/
    // 7zip: From the 7zip sources.

#if (BWT_SORT_TYPE == BWT_STD_SORT)

    // Init the position-vector with: { 0, 1, 2, 3, ..., CompressLength }
    InitCompressPositionVector(CompressPosition, CompressLength);
    
    // Sort the position-vector.
    // Don't use std::sort; std::stable_sort normally uses merge sort, which is in case of the BWT much faster than quicksort (normally used by std::sort)
    // (also: http://stackoverflow.com/questions/810951/how-big-is-the-performance-gap-between-stdsort-and-stdstable-sort-in-practice).
    // std::sort (Editor.u): ~40s
    // std::stable_sort (Editor.u): ~15s
    Temp_CompressBuffer = &CompressBuffer;
    Temp_CompressLength = CompressLength;
    std::stable_sort(CompressPosition.begin(), CompressPosition.end(), uz1BurrowsWheelerAlgorithm::ClampedBufferCompare);

#elif (BWT_SORT_TYPE == BWT_C_SORT)

    typedef int (__cdecl *CStyle_QSortFunc)(const void *,const void *);

    // Init the position-vector with: { 0, 1, 2, 3, ..., CompressLength }
    InitCompressPositionVector(CompressPosition, CompressLength);

    // Sort. std::vector gurantees that the array is saved in 1 continuous memory-chunk (unlike std::list),
    // so &(CompressBuffer[0]) is legal (and because the vector isn't empty).
    Temp_CStyle_CompressLength = CompressLength;
    Temp_CStyle_CompressBuffer = &(CompressBuffer[0]);
    ::qsort( &(CompressPosition[0]), CompressLength+1, sizeof(int), (CStyle_QSortFunc)uz1BurrowsWheelerAlgorithm::CStyle_ClampedBufferCompare );
    
#elif (BWT_SORT_TYPE == BWT_EXT_SORT)
    // Get the sorted position-array. std::vector gurantees that the array is saved in 1 continuous memory-chunk (unlike std::list),
    // so &(CompressBuffer[0]) is legal (and because the vector isn't empty).
    CompressPosition.Set(bwtsort(&(CompressBuffer[0]), CompressLength));
    
    // The length of the array returned by bwtsort() is CompressLength+1 (check the source). So this is legal.
    CompressPosition[CompressLength] = CompressLength;
        

#elif (BWT_SORT_TYPE == BWT_7Z_SORT)
    {
    #error BWT_SORT_TYPE == BWT_7Z_SORT
    /*UInt32 Ret =*/ BlockSort(pBlockSorterIndex, &(CompressBuffer[0]), CompressLength);
    
    CompressPosition.clear();
    for (int i = 0; i < CompressLength; ++i)
      CompressPosition.push_back(pBlockSorterIndex[i]);
   
    CompressPosition.push_back(CompressLength);
    
    }

#else

  #error Unknown Sort type
    
#endif
    
    
    // From here on the standard UT algorithm again.
    int First = 0;
    int Last = 0;
    for (int i = 0; i < CompressLength+1; ++i)
    {
      if (CompressPosition[i] == 1)
        First = i;
      else if (CompressPosition[i] == 0)
        Last = i;
    }

    WriteInt(OutStream, CompressLength);
    
    // UTPackages-delphi-library reads 2 compact indices in the decompress function, but UT99 seems to use 2 ints.
    //WriteCompactIndex(OutStream, First);
    //WriteCompactIndex(OutStream, Last);
    WriteInt(OutStream, First);
    WriteInt(OutStream, Last);
    
    // Write the data to the output.
    for (int i = 0; i < CompressLength+1; ++i)
    {
      const int Index = CompressPosition[i];
      WriteByte(OutStream, CompressBuffer[Index != 0 ? Index-1 : 0]);
    }
        
  }
  
#if (BWT_SORT_TYPE == BWT_7Z_SORT)
  // I know, a memory leak can occur if an exception is thrown in the above code.
  // 7-zip version isn't used anyway and was only for testing, so I don't care to use a smart-pointer or whatever.
  delete[] pBlockSorterIndex;
#endif
  
  return true;
}

bool uzLib::uz1BurrowsWheelerAlgorithm::Decompress(uzLib::in_stream& InStream, uzLib::out_stream& OutStream, ios::pos_type InStreamBeg)
{
  static const std::wstring UPDATE_MSG = L"Burrows Wheeler Decoding";

  const int InStreamLength = AlgorithmPreamble(InStream, OutStream, InStreamBeg);
  
  if (CallUpdateFunction(0, InStreamLength, UPDATE_MSG))
    return false;

  vector<BYTE> DecompressBuffer(MAX_BUFFER_SIZE+1);
  vector<int> Temp(MAX_BUFFER_SIZE+1);
    
  int DecompressCount[256+1];
  int RunningTotal[256+1];
  
  int ProcessedBytes = 0;
  
  while (!IsEOF(InStream))
  {
    if (CallUpdateFunction(ProcessedBytes, InStreamLength, UPDATE_MSG))
      return false;

    int DecompressLength = ReadInt(InStream);
    
    // UTPackages-delphi-library reads 2 compact indices in the decompress function, but UT99 seems to use 2 ints.
    //const int First = ReadCompactIndex(InStream);
    //const int Last = ReadCompactIndex(InStream);
    const int First = ReadInt(InStream);
    const int Last = ReadInt(InStream);
    if (!InStream.good())
      throw std::runtime_error("Reached EOF too early in uz1BurrowsWheelerAlgorithm::Decompress.");
    else if (DecompressLength > MAX_BUFFER_SIZE+1 || DecompressLength > InStreamLength-InStream.tellg())
      throw std::runtime_error("Invalid DecompressLength in uz1BurrowsWheelerAlgorithm::Decompress.");
        
    DecompressBuffer.clear();
    const int CopyCount = CopyDataToVector(InStream, DecompressBuffer, ++DecompressLength);
    if (CopyCount != DecompressLength || DecompressBuffer.size() != static_cast<size_t>(DecompressLength))
      throw std::runtime_error("Couldn't read the complete compressed chunk in uz1BurrowsWheelerAlgorithm::Decompress.");
    
    ProcessedBytes += CopyCount + 8; // +8: The First and Last integers.
    
    for (int i = 0; i < 257; ++i)
      DecompressCount[i] = 0;
    
    for (int i = 0; i < DecompressLength; ++i)
      DecompressCount[ (i!=Last) ? DecompressBuffer[i] : 256]++;
    
    int Sum = 0;
    for (int i = 0; i < 257; ++i)
    {
      RunningTotal[i] = Sum;
      Sum += DecompressCount[i];
      DecompressCount[i] = 0;
    }
    
    for (int i = 0; i < DecompressLength; ++i)
    {
      const int Index = ( (i != Last) ? DecompressBuffer[i] : 256);
      Temp.at(RunningTotal[Index] + DecompressCount[Index]++) = i;
    }
    
    for (int i = First, j = 0; j < DecompressLength-1; i = Temp.at(i), ++j)
      WriteByte(OutStream, DecompressBuffer.at(i));
  }
  
  return true;
}

void uzLib::uz1BurrowsWheelerAlgorithm::InitCompressPositionVector(vector<int>& CompressPositionVect, const int CompressLength)
{
  CompressPositionVect.clear(); // Remove existing elements.
  for (int CurNum = 0; CurNum < CompressLength + 1; ++CurNum)
    CompressPositionVect.push_back(CurNum);
}

#if (BWT_SORT_TYPE == BWT_STD_SORT)

  bool uzLib::uz1BurrowsWheelerAlgorithm::ClampedBufferCompare(int P1, int P2)
  {
    if (Temp_CompressBuffer == NULL || Temp_CompressLength < 0)
      throw std::logic_error("Temp_CompressBuffer == NULL in uz1BurrowsWheelerAlgorithm::ClampedBufferCompare.");

    int B1Pos = P1;
    int B2Pos = P2;
    
    for (int Count = Temp_CompressLength-std::max(P1, P2); Count > 0; --Count, ++B1Pos, ++B2Pos)
    {
      // Note: This function and thus these lines below are called A LOT.
      // It happens to be faster to cache the values B1 and B2 (actually tested).
      // Also don't use the at() function, to gain another performance boost.
      const BYTE B1 = (*Temp_CompressBuffer)[B1Pos];
      const BYTE B2 = (*Temp_CompressBuffer)[B2Pos];
      
      if (B1 < B2)
        return true;
      else if (B1 > B2)
        return false;
    }

    return ((P1 - P2) > 0 ? false : true);
  }
  
#elif (BWT_SORT_TYPE == BWT_C_SORT)

  int uzLib::uz1BurrowsWheelerAlgorithm::CStyle_ClampedBufferCompare(const int* P1, const int* P2)
  {
	  uzLib::BYTE* B1 = Temp_CStyle_CompressBuffer + *P1;
	  uzLib::BYTE* B2 = Temp_CStyle_CompressBuffer + *P2;
	  for( int Count=Temp_CStyle_CompressLength-std::max(*P1,*P2); Count>0; Count--,B1++,B2++ )
	  {
		  if( *B1 < *B2 )
			  return -1;
		  else if( *B1 > *B2 )
			  return 1;
	  }
	  return *P1 - *P2;
  }
  
#endif

//============================================================================================================================
// uz1BurrowsWheelerAlgorithm
//============================================================================================================================

//-----------------------------------------------------------------------------------------
// Function implementation
//-----------------------------------------------------------------------------------------

// Constructor
uzLib::uz1RLEAlgorithm::uz1RLEAlgorithm(pUz1UpdateFunc UpdateFunc, void* UserObj, int ThisStepNum, int NumSteps):
    uz1AlgorithmBase(UpdateFunc, UserObj, ThisStepNum, NumSteps)
{ }

bool uzLib::uz1RLEAlgorithm::Compress(uzLib::in_stream& InStream, uzLib::out_stream& OutStream, ios::pos_type InStreamBeg)
{
  static const std::wstring UPDATE_MSG = L"Runtime-Length-Encoding";

  const int InStreamLength = AlgorithmPreamble(InStream, OutStream, InStreamBeg);

  if (CallUpdateFunction(0, InStreamLength, UPDATE_MSG))
    return false;
  
  BYTE PrevChar = 0;
  BYTE PrevCount = 0;  
  
  int ProcessedBytes = 0;
  
  BYTE B;
  while (TryReadNextByte(InStream, B))
  {
    if ((ProcessedBytes % BYTE_UPDATE_INTERVALL == 0) && CallUpdateFunction(ProcessedBytes, InStreamLength, UPDATE_MSG))
      return false;

    ++ProcessedBytes;
    
    // Check if we need to write the data to the output.
    if (B != PrevChar || PrevCount == 255)
    {
      EncodeEmitRun(OutStream, PrevChar, PrevCount);
      PrevChar = B;
      PrevCount = 0;
    }
    
    ++PrevCount;
  }
  
  // Write the missing bytes to the stream.
  EncodeEmitRun(OutStream, PrevChar, PrevCount);
  
  return true;
}

bool uzLib::uz1RLEAlgorithm::Decompress(uzLib::in_stream& InStream, uzLib::out_stream& OutStream, ios::pos_type InStreamBeg)
{
  static const std::wstring UPDATE_MSG = L"Runtime-Length-Decoding";

  const int InStreamLength = AlgorithmPreamble(InStream, OutStream, InStreamBeg);
  
  if (CallUpdateFunction(0, InStreamLength, UPDATE_MSG))
    return false;

  int Count = 0;
  BYTE PrevChar = 0;
  
  int ProcessedBytes = 0;
  
  BYTE CurByte;
  while (TryReadNextByte(InStream, CurByte))
  {
    if ((ProcessedBytes % BYTE_UPDATE_INTERVALL ==0) && CallUpdateFunction(ProcessedBytes, InStreamLength, UPDATE_MSG))
      return false;

    ++ProcessedBytes;
    
    WriteByte(OutStream, CurByte);
    
    if (CurByte != PrevChar)
    {
      PrevChar = CurByte;
      Count = 1;
    }
    // Check if the byte which has just been read was the fifth byte in a row. In that case, the chunk was compressed.
    else if (++Count == RLE_LEAD)
    {
      if ((ProcessedBytes % BYTE_UPDATE_INTERVALL ==0) && CallUpdateFunction(ProcessedBytes, InStreamLength, UPDATE_MSG))
        return false;

      // Read the number of "missing" bytes.
      BYTE RLE_Count = ReadByte(InStream);
      ++ProcessedBytes;

      if (InStream.eof())
        throw std::runtime_error("Couldn't read RLE_Count because the EOF was reached early in uz1RLEAlgorithm::Decompress.");
      if (RLE_Count < 2)
        throw std::runtime_error("The read RLE_Count is too small, i.e. invalid (in uz1RLEAlgorithm::Decompress).");
      
      // Write the missing bytes to the output-stream.
      while (RLE_Count-- > RLE_LEAD)
        WriteByte(OutStream, CurByte);
      
      Count = 0;
    }
  }
  
  return true;
}

void uzLib::uz1RLEAlgorithm::EncodeEmitRun(out_stream& OutStream, BYTE Char, BYTE Count)
{
/*
  // Write max. 5 characters to the stream.
  for (int Down = std::min(Count, RLE_LEAD); Down > 0; --Down)
    WriteByte(OutStream, Char);
  
  // In case 5 or more characters where written, append the length.
  if (Count >= RLE_LEAD)
    WriteByte(OutStream, Count);
*/
}



//============================================================================================================================
// uz1HuffmanAlgorithm
//============================================================================================================================

//-----------------------------------------------------------------------------------------
// Helper functions
//-----------------------------------------------------------------------------------------
namespace
{

//-----------------------------------------------------------------------------------------
// HuffmanNode class: Helper class for the Huffman-algorithm which is used to build the tree.
//-----------------------------------------------------------------------------------------
class HuffmanNode
{
  public:
    int Count; // Used from outside of the class to store the number of character for the character associated with this node (default value: 0).

  public:
    // Constructor
    explicit HuffmanNode(int InChar):
      Char(InChar), Count(0)
    { }
    
    // Destructor
    ~HuffmanNode();
    
    void PrependBit(BYTE B);
    
    // Writes the compressed data to the buffer.
    void WriteTable(boost::dynamic_bitset<BYTE>& Buffer)const;
    
    // Writes the bits of this node (and only of this node) to the buffer.
    void WriteBits(boost::dynamic_bitset<BYTE>& Buffer)const;
    
    // Reads the compressed data. NextBit must contain the index of the first bit, and is advanced as the data is read from the bitset.
    void ReadTable(const boost::dynamic_bitset<BYTE>& Source, size_t& NextBit);
  
    // Returns the character of this node.
    int GetChar()const { return Char; }
    
    // Returns the number of bits in the "Bits" vector.
    size_t GetBitCount()const { return Bits.size(); }
  
    // If bChildOne is true, the child with the index 1 is returned, else the child with the index 0 is returned.
    HuffmanNode* GetChild(bool bChildOne)const { return Childs[bChildOne ? 1 : 0]; }
  
    // Extracts the last 2 elements from the vector and uses them to initialize the childs. I.e. NodeSrc.size() is afterwards smaller by 2.
    void InitializeChilds(vector<HuffmanNode*>& NodeSrc);

  private:
    // Reads a byte from the bitset (NextBit is thus advanced by 8).
    BYTE ExtractByteFromBitset(const boost::dynamic_bitset<BYTE>& Source, size_t& NextBit);
  
  private:
    int Char; // The byte of that node. Only valid if Childs.size()==0.
    
    vector<HuffmanNode*> Childs; // The child-nodes.
    vector<BYTE> Bits;
};

// Helper function:
// Clears the specified vector.
void CleanupHuffmanVector(vector<HuffmanNode*>& ToClean)
{
  for (size_t CurIndex = 0; CurIndex < ToClean.size(); ++CurIndex)
  {
    HuffmanNode* CurNode = ToClean.at(CurIndex);
    if (CurNode != NULL)
      delete CurNode;
  }
  
  ToClean.clear();
}

HuffmanNode::~HuffmanNode()
{
  // Cleanup
  CleanupHuffmanVector(Childs);
}

void HuffmanNode::PrependBit(BYTE B)
{
  Bits.insert(Bits.begin(), B);
  
  for (size_t CurChildIndex = 0; CurChildIndex < Childs.size(); ++CurChildIndex)
    Childs[CurChildIndex]->PrependBit(B);
}

void HuffmanNode::WriteTable(boost::dynamic_bitset<BYTE>& Buffer)const
{
  // Write a flag which indicates if childs are available.
  Buffer.push_back(Childs.size() != 0);
  
  // Either write all childs to the buffer or our own byte.
  if (Childs.size() > 0)
  {
    for (size_t CurChildIndex = 0; CurChildIndex < Childs.size(); ++CurChildIndex)
      Childs[CurChildIndex]->WriteTable(Buffer);
  }
  else
    Buffer.append(static_cast<BYTE>(Char));
}

void HuffmanNode::WriteBits(boost::dynamic_bitset<BYTE>& Buffer)const
{
  // Iterate through all bits and write each one to the buffer.
  for (size_t CurBitIndex = 0; CurBitIndex < Bits.size(); ++CurBitIndex)
    Buffer.push_back((Bits[CurBitIndex] == 0) ? false : true);
}

void HuffmanNode::ReadTable(const boost::dynamic_bitset<BYTE>& Source, size_t& NextBit)
{
  // Check if this node should have childs.
  if (Source[NextBit++])
  {
    // Add 2 new childs and initialize them.
    for (size_t CurChildIndex = 0; CurChildIndex < 2; ++CurChildIndex)
    {
      HuffmanNode* NewNode = new HuffmanNode(-1);
      NewNode->ReadTable(Source, NextBit);
      
      Childs.push_back(NewNode);
    }
  }
  else
    Char = ExtractByteFromBitset(Source, NextBit);
}

BYTE HuffmanNode::ExtractByteFromBitset(const boost::dynamic_bitset<BYTE>& Source, size_t& NextBit)
{
  BYTE Mask = 0x01; // Defines which bit is extracted next from the bitset.
  BYTE ToReturn = 0; // Used to build the byte.
  
  // Loop through the next 8 bits in the bitset.
  for (size_t NumExtractedBits = 0; NumExtractedBits < sizeof(BYTE)*8; ++NumExtractedBits)
  {
    // In case the bit is set, set the corresponding bit in 'ToReturn'.
    if (Source.test(NextBit++))
      ToReturn |= Mask;
    
    // Extract the next more significant bit the next time.
    Mask <<= 1;
  }
  
  return ToReturn;
}

void HuffmanNode::InitializeChilds(vector<HuffmanNode*>& NodeSrc)
{
  assert(NodeSrc.size() > 1);
  
  // Add 2 new childs with the nodes from the back of the vector.
  for (size_t CurChildIndex = 0; CurChildIndex < 2; ++CurChildIndex)
  {
    HuffmanNode* NewChildNode = NodeSrc.back();
   
    // Add the new child and remove the node from the input-vector. 
    Childs.push_back(NewChildNode);
    NodeSrc.pop_back();
    
    // Init child.
    NewChildNode->PrependBit(static_cast<BYTE>(CurChildIndex));
    Count += NewChildNode->Count;
  }
}

} // End anonymious namespace

//-----------------------------------------------------------------------------------------
// Function implementation
//-----------------------------------------------------------------------------------------

// Constructor
uzLib::uz1HuffmanAlgorithm::uz1HuffmanAlgorithm(pUz1UpdateFunc UpdateFunc, void* UserObj, int ThisStepNum, int NumSteps):
    uz1AlgorithmBase(UpdateFunc, UserObj, ThisStepNum, NumSteps)
{ }

bool uzLib::uz1HuffmanAlgorithm::Compress(uzLib::in_stream& InStream, uzLib::out_stream& OutStream, ios::pos_type InStreamBeg)
{
  static const std::wstring UPDATE_MSG1 = L"Huffman-Encoding (1)";
  static const std::wstring UPDATE_MSG2 = L"Huffman-Encoding (2)";

  const int InStreamLength = AlgorithmPreamble(InStream, OutStream, InStreamBeg);
  const int NumSteps = InStreamLength * 2; // We need to iterate through the input stream twice.
  
  if (CallUpdateFunction(0, NumSteps, UPDATE_MSG1))
    return false;

  const size_t SavedInPos = InStream.tellg();
  
  vector<HuffmanNode*> HuffmanNodes;
  HuffmanNodes.reserve(256);
  
  try
  {
    //- - - - - - - - - - - - - - - - - - - - - - - - - - -
		// Compute character frequencies.

    // Create a list of nodes. Each node corresponds to 1 character.
    for (int i = 0; i < 256; ++i)
      HuffmanNodes.push_back(new HuffmanNode(i));
    assert(HuffmanNodes.size() == 256);
        
    // Initialize the huffman nodes (set the number of times the corresponding character appears).
    int Total = 0; // After the loop this will contain the number of bytes in the input-stream.
    BYTE CurByte;
    while (TryReadNextByte(InStream, CurByte))
    {
      if ((Total % BYTE_UPDATE_INTERVALL ==0) && CallUpdateFunction(Total, NumSteps, UPDATE_MSG1))
      {
        CleanupHuffmanVector(HuffmanNodes);
        return false;
      }
      
      HuffmanNodes[CurByte]->Count++;
      ++Total;
    }
    
    InStream.clear();
    InStream.seekg(SavedInPos);
    WriteInt(OutStream, Total);
    
    
    //- - - - - - - - - - - - - - - - - - - - - - - - - - -
    // Build compression table, i.e. the huffman-tree. This is done so that the character which appears the most is at the top of the tree.
    
    // Remove unused nodes from the back of the node-array.
    while (HuffmanNodes.size() > 1 && HuffmanNodes.back()->Count == 0)
    {  
      delete HuffmanNodes.back();
      HuffmanNodes.pop_back();
    }
  } // Try ends here because below all nodes are connected with each other and a node deletes all its childs. Bad design, I know...
  catch(...)
  {
    CleanupHuffmanVector(HuffmanNodes);
    throw; // Rethrow
  }

    
  // Create a copy of the node-pointers. All nodes should stay valid from here on.
  // It is used to access the correct node by a given byte (which directly comes from the input stream).
  vector<HuffmanNode*> ByteToNodeVector(HuffmanNodes);
  
  //size_t BitCount = HuffmanNodes.size() * (8+1);
  while (HuffmanNodes.size() > 1)
  {
    // Create a new node and initialize its childs. The size of the HuffmanNodes-vector is reduced by 2.
    HuffmanNode* NewNode = new HuffmanNode(-1);
    NewNode->InitializeChilds(HuffmanNodes);
    
    // Insert the new node in the HuffmanNodes-vector, so that the elements are sorted by the 'Count'-member after the end of this "big" loop.
    vector<HuffmanNode*>::iterator InsertPos = HuffmanNodes.begin();
    while (InsertPos != HuffmanNodes.end())
    {
      if ((*InsertPos)->Count < NewNode->Count)
        break;
    
      InsertPos++;
    }
        
    HuffmanNodes.insert(InsertPos, NewNode);
    //++BitCount;
  }
  
  //- - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Calc stats.
  /*while (!IsEOF(InStream))
  {
    const BYTE CurByte = ReadByte(InStream);
    BitCount += ByteToNodeVector.at(CurByte)->GetBitCount();
  }
  InStream.clear();
  InStream.seekg(SavedInPos);*/
  
  //- - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Save table and bitstream.
  
  // Get the root node.
  HuffmanNode* RootNode = HuffmanNodes.back();
  HuffmanNodes.pop_back();    
  
  // Write the whole huffman tree.
  boost::dynamic_bitset<BYTE> OutBits;
  RootNode->WriteTable(OutBits);
  
  // Encode each byte in the input stream, i.e. write each byte in the compressed format.
  int ProcessedBytes = InStreamLength;
  BYTE CurByte;
  while (TryReadNextByte(InStream, CurByte))
  {
    if ((ProcessedBytes % BYTE_UPDATE_INTERVALL == 0) && CallUpdateFunction(ProcessedBytes, NumSteps, UPDATE_MSG2))
    {
      // Cleanup ALL nodes (every node is an (indirect) child of the root node, and in the node's destructor the childs are deleted).
      delete RootNode;
      RootNode = NULL;
      HuffmanNodes.clear();
      ByteToNodeVector.clear();
      return false;
    }

    ByteToNodeVector.at(CurByte)->WriteBits(OutBits);
    ++ProcessedBytes;
  }
  
  // Cleanup ALL nodes (every node is an (indirect) child of the root node, and in the node's destructor the childs are deleted).
  delete RootNode;
  RootNode = NULL;
  HuffmanNodes.clear();
  ByteToNodeVector.clear();
  
  //assert(OutBits.size() == BitCount);
  //if (OutBits.size() != BitCount)
  //  return false;

  // Write all bits to the output stream.
  std::ostream_iterator<BYTE, BYTE> OutIter(OutStream);
  boost::to_block_range(OutBits, OutIter);

  return true;
}

bool uzLib::uz1HuffmanAlgorithm::Decompress(uzLib::in_stream& InStream, uzLib::out_stream& OutStream, ios::pos_type InStreamBeg)
{
  static const std::wstring UPDATE_MSG1 = L"Huffman-Decoding (reading)";
  static const std::wstring UPDATE_MSG2 = L"Huffman-Decoding (reconstructing)";

  const int InStreamLength = AlgorithmPreamble(InStream, OutStream, InStreamBeg);
  
  if (CallUpdateFunction(0, InStreamLength, UPDATE_MSG1))
    return false;

  // Read the size of the uncompressed data.
  int Total = ReadInt(InStream);
  if (!InStream.good())
    throw std::runtime_error("Failed reading total byte count in uz1HuffmanAlgorithm::Decompress");
  
  if (CallUpdateFunction(0, InStreamLength, UPDATE_MSG1))
    return false;
  
  // Read all bits into memory. Don't use istream_iterator, because it will read formatted data.
  std::istreambuf_iterator<BYTE> InStreamIter(InStream);
  std::istreambuf_iterator<BYTE> EndOfInStreamIter;
  boost::dynamic_bitset<BYTE> InBits(InStreamIter, EndOfInStreamIter);
  
  // Build the huffman tree.
  HuffmanNode RootNode(-1);
  size_t NextBit = 0;
  RootNode.ReadTable(InBits, NextBit);
  
  const size_t TotalBitCount = InBits.size();
  
  // Reconstruct the uncompressed data.
  while(Total-- > 0)
  {
    if (((NextBit/8) % BYTE_UPDATE_INTERVALL == 0) && CallUpdateFunction(NextBit/8, TotalBitCount/8, UPDATE_MSG2))
      return false;

    if (NextBit >= TotalBitCount)
      throw std::runtime_error("Tried to read more bits than in the input stream (in uz1HuffmanAlgorithm::Decompress).");
  
    // Get the correct node.
    HuffmanNode* Node = &RootNode;
    while(Node->GetChar() == -1)
      Node = Node->GetChild(InBits.test( NextBit++ ));
    
    // Get the corresponding byte and write it.
    const BYTE CurByte = static_cast<BYTE>(Node->GetChar());
    WriteByte(OutStream, CurByte);
  }
  
  return true;
}


//============================================================================================================================
// uz1MoveToFrontAlgorithm
//============================================================================================================================

//-----------------------------------------------------------------------------------------
// Function implementation
//-----------------------------------------------------------------------------------------

// Constructor
uzLib::uz1MoveToFrontAlgorithm::uz1MoveToFrontAlgorithm(pUz1UpdateFunc UpdateFunc, void* UserObj, int ThisStepNum, int NumSteps):
    uz1AlgorithmBase(UpdateFunc, UserObj, ThisStepNum, NumSteps)
{ }

bool uzLib::uz1MoveToFrontAlgorithm::Compress(uzLib::in_stream& InStream, uzLib::out_stream& OutStream, ios::pos_type InStreamBeg)
{
  static const std::wstring UPDATE_MSG = L"Move-to-front encoding";

  const int InStreamLength = AlgorithmPreamble(InStream, OutStream, InStreamBeg);
  if (CallUpdateFunction(0, InStreamLength, UPDATE_MSG))
    return false;
  
  // Create the byte list.
  BYTE List[256];
  for (int CurByte = 0; CurByte < 256; ++CurByte)
    List[CurByte] = static_cast<BYTE>(CurByte);
  
  int ProcessedBytes = 0;
  
  // Iterate through all bytes in the input stream.
  BYTE CurByte;
  while (TryReadNextByte(InStream, CurByte))
  {
    if ((ProcessedBytes % BYTE_UPDATE_INTERVALL == 0) && CallUpdateFunction(ProcessedBytes, InStreamLength, UPDATE_MSG))
      return false;

    ++ProcessedBytes;
    
    // Find the index of the current byte in the list.
    int ByteIndexInList = 0;
    while (ByteIndexInList < 256)
    {
      if (List[ByteIndexInList] == CurByte)
        break;
    
      ++ByteIndexInList;
    }
    
    if (ByteIndexInList >= 256)
      throw std::logic_error("Couldn't find index of current byte (in uz1MoveToFrontAlgorithm::Compress)");
    
    // Write the position of the byte in the list to the output stream.
    WriteByte(OutStream, static_cast<BYTE>(ByteIndexInList));
    
    // Move the entry of the current byte in the list to the front of the list.
    const int NewPos = 0;
    while (ByteIndexInList > NewPos)
    {
      List[ByteIndexInList] = List[ByteIndexInList - 1];
      --ByteIndexInList;
    }
    List[NewPos] = CurByte;
  }
  
  return true;
}

bool uzLib::uz1MoveToFrontAlgorithm::Decompress(uzLib::in_stream& InStream, uzLib::out_stream& OutStream, ios::pos_type InStreamBeg)
{
  static const std::wstring UPDATE_MSG = L"Move-to-front decoding";

  const int InStreamLength = AlgorithmPreamble(InStream, OutStream, InStreamBeg);
  if (CallUpdateFunction(0, InStreamLength, UPDATE_MSG))
    return false;
  
  // Create the byte list.
  BYTE List[256];
  for (int CurByte = 0; CurByte < 256; ++CurByte)
    List[CurByte] = static_cast<BYTE>(CurByte);
  
  int ProcessedBytes = 0;
  
  // Iterate through all bytes in the input stream.
  BYTE CurByte;
  while (TryReadNextByte(InStream, CurByte))
  {
    if ((ProcessedBytes % BYTE_UPDATE_INTERVALL == 0) && CallUpdateFunction(ProcessedBytes, InStreamLength, UPDATE_MSG))
      return false;

    ++ProcessedBytes;

    // Get the original byte and write it to the stream.
    const BYTE DecompressedByte = List[CurByte];
    WriteByte(OutStream, DecompressedByte);
    
    const int NewPos = 0;
    for (int Index = CurByte; Index > NewPos; --Index)
      List[Index] = List[Index-1];
    List[NewPos] = DecompressedByte;
  }
  
  return true;
}
