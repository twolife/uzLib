/*
uz1Impl.h: Contains the classes required for the uz1-compression/decompression.

Language: C++

Created by Gugi, 2010-2011
*/

#pragma once

#include <fstream>
#include <vector>
#include <exception>
#include <string>
#include <zlib.h>


//===========================================================================
//===========================================================================
// Header file used for the implementation of the uz1-format.
// The complete implementation of the necessary codecs/compression algorithms
// can be found in the FCodec.h file of the public SDK of UT99.
// The order of the uz-decompression in USetupDefinition.cpp and
// in the UT-Package-Delphi-library.
// It is assumed, that all in_stream's behave like memory streams, i.e. the stream
// stays the same on read operations (unlike e.g. the cin-object). That means, you
// should only use fstreams and strstreams for it.
//===========================================================================
//===========================================================================
namespace uzLib 
{
  typedef unsigned char BYTE;
  typedef std::basic_istream<BYTE> in_stream;
  typedef std::basic_ostream<BYTE> out_stream;
  
  typedef void (*pUz1UpdateFunc)(unsigned int CurStatus, unsigned int CompletedStatus, const std::wstring& Msg, bool& bCancel, void* UserObj);


  //==================================================
  // uz compression
  //==================================================
  
  // Used to specify the uz1-version. The difference between the two is an additional RLE-step in 5678.
  // The uz1-file begins with the signature.
  enum EUz1Signature
  {
    USIG_UT99 = 1234, // E.g.: UT99
    USIG_5678 = 5678 // E.g.: Postal
  };
  
  // Speed tests:
  // - AS-HiSpeed.unr (4.21MB):
  //   Standard UT (as displayed by UCC): 149.13s
  //   This implementation: 5s
  
  // Compresses the data in InStream to the uz1-format and saves the result in OutStream.
  // Both InStream and OutStream will have its exceptions-flags set so that exceptions are thrown
  // for the fail and bad-bits and InStream will not be reset.
  // The uz1-signature specifies the uz1-version.
  // The PkgFilename is saved in the uz-file and should be the original filename.
  // Exceptions are thrown in case of errors (derived from std::exception).
  bool CompressToUz1(in_stream& InStream, out_stream& OutStream, const std::string& PkgFilename, 
      EUz1Signature Uz1Sig, pUz1UpdateFunc UpdateFunc = NULL, void* UserObj = NULL);
  bool CompressToUz1(in_stream& InStream, out_stream& OutStream, const std::wstring& PkgFilename, 
      EUz1Signature Uz1Sig, pUz1UpdateFunc UpdateFunc = NULL, void* UserObj = NULL);
  
  
  //==================================================
  // uz decompression
  //==================================================
  enum EFilenameType
  {
    FT_UNICODE,
    FT_ASCII
  };
  
  // Holds either a unicode or ascii filename.
  struct SFilename
  {
    // Definies which variable contains the filename.
    EFilenameType FilenameType;
    
    // One of these 2 strings contain the filename.
    std::string ASCIIStr; // Valid in case of FilenameType == FT_ASCII.
    std::wstring UnicodeStr; // Valid in case of FilenameType == FT_UNICODE.
  };
  
  
  // Speed tests:
  // - AS-HiSpeed.unr.uz (1.21MB):
  //   Standard UT (as displayed by UCC): 0.9s
  //   This implementation: 2s
  // - MPDGT-(SD)-ANUBIS_SMALL-(SW,SH,M@D)
  //   Standard UT (as displayed by UCC): 10.2s
  //   This impl: 30s


  // Decompresses the data in InStream from the uz1-format and saves the result in OutStream.
  // Both InStream and OutStream will have its exceptions-flags set so that exceptions are thrown
  // for the fail and bad-bits and InStream will not be reset.
  // Exceptions are thrown in case of errors (derived from std::exception).
  // The uz-file saves the (normally) original filename (i.e. the filename without the .uz) either in
  // unicode or ASCII format. Use the second version of this function to get it.
  bool DecompressFromUz1(in_stream& InStream, out_stream& OutStream, pUz1UpdateFunc UpdateFunc = NULL, void* UserObj = NULL);
  bool DecompressFromUz1(in_stream& InStream, out_stream& OutStream, SFilename& OrigFilename, pUz1UpdateFunc UpdateFunc = NULL, void* UserObj = NULL);
  


  //==================================================
  // Base class for all algorithms required for the uz-format.
  //==================================================
  class uz1AlgorithmBase
  {
    public:
      // Encodes the data in the input stream, beginning with the byte at position InStreamBeg.
      virtual bool Compress(in_stream& InStream, out_stream& OutStream, std::ios::pos_type InStreamBeg = 0) = 0;
      
      // Decodes the data in the input stream, beginning with the byte at position InStreamBeg.
      virtual bool Decompress(in_stream& InStream, out_stream& OutStream, std::ios::pos_type InStreamBeg = 0) = 0;
    
      // Update function getter/setter.
      pUz1UpdateFunc GetUpdateFunc()const { return m_UpdateFunc; }
      void SetUpdateFunc(pUz1UpdateFunc NewFunc) { m_UpdateFunc = NewFunc; }
      
      void* GetUserObj()const { return m_pUserObj; }
      void SetUserObj(void* UserObj) { m_pUserObj = UserObj; }
    
    protected:
      // Constructor. UpdateFunc is called in the compress/decompress process, if UpdateFunc is not NULL (UserObj is passed
      // as a parameter with the UpdateFunc-call). ThisStepNum and NumsSteps are used to amend the message of the UpdateFunc-call.
      uz1AlgorithmBase(pUz1UpdateFunc UpdateFunc, void* UserObj, int ThisStepNum, int NumSteps);
    
      // Called at the beginning of the Compress/Decompress functions. Returns the length of InStream.
      int AlgorithmPreamble(in_stream& InStream, out_stream& OutStream, std::ios::pos_type InStreamStartPos);
      
      // If m_UpdateFunc is not NULL, it is called. True is returned, in case the operation should be cancelled,
      // else false is returned.
      bool CallUpdateFunction(unsigned int CurStatus, unsigned int CompletedStatus, const std::wstring& Msg);
      
    private:
      pUz1UpdateFunc m_UpdateFunc;
      void* m_pUserObj;
      int m_ThisStepNum;
      std::wstring m_NumStepsStr;
      
    protected:
      static const int BYTE_UPDATE_INTERVALL = 8192; // In some algorithms every x bytes the update-function is called. x is this constant.
  };


  //==================================================
  // Implements the "Burrows-Wheeler inspired data compressor" (which doesn't compress anything, but only the byte-order is changed).
  //==================================================
  
  /*
  Speed tests:
  (Normal AS-HiSpeed.unr)

  STD: 416.43s
  C: 235.195s
  EXT: 1.68164s
  7Z: 1.97155s
  */

  // Which algorithm to use for the BWT?
  #define BWT_STD_SORT 1 // Uses std::stable_sort.
  #define BWT_C_SORT 2   // Uses qsort of the crt.
  #define BWT_EXT_SORT 3 // Uses http://sourceforge.net/projects/bwtcoder/files/bwtcoder/preliminary-2/
  #define BWT_7Z_SORT 4  // Uses 7-zip source; this is broken (the sorting always returned slightly different results than the above 3 ways;
                         //       doesn't matter, as BWT_EXT_SORT is faster anyway; I only used this for testing purposes).

  #define BWT_SORT_TYPE BWT_STD_SORT


  class uz1BurrowsWheelerAlgorithm : public uz1AlgorithmBase
  {
    public:
      // Constructor. UpdateFunc is called in the compress/decompress process, if UpdateFunc is not NULL.
      uz1BurrowsWheelerAlgorithm(pUz1UpdateFunc UpdateFunc = NULL, void* UserObj = NULL, int ThisStepNum = -1, int NumSteps = -1);
    
      // Encodes the data in the input stream, beginning with the byte at position InStreamBeg.
      virtual bool Compress(in_stream& InStream, out_stream& OutStream, std::ios::pos_type InStreamBeg = 0);
      
      // Decodes the data in the input stream, beginning with the byte at position InStreamBeg.
      virtual bool Decompress(in_stream& InStream, out_stream& OutStream, std::ios::pos_type InStreamBeg = 0);
      
    private:
      // Initializes the vector with numbers: { 0, 1, 2, 3, ..., CompressLength }
      static void InitCompressPositionVector(std::vector<int>& CompressPositionVect, const int CompressLength);

      // Used to sort the data.
#if (BWT_SORT_TYPE == BWT_STD_SORT)
      static bool ClampedBufferCompare(int P1, int P2);
#elif (BWT_SORT_TYPE == BWT_C_SORT)
      static int CStyle_ClampedBufferCompare(const int* P1, const int* P2);
#endif
    
      // These are only valid during the ClampedBufferCompare-call.
#if (BWT_SORT_TYPE == BWT_STD_SORT)
      static std::vector<BYTE>* Temp_CompressBuffer;
      static int Temp_CompressLength;
#elif (BWT_SORT_TYPE == BWT_C_SORT)
	    static unsigned char* Temp_CStyle_CompressBuffer;
	    static int Temp_CStyle_CompressLength;
#endif

    private:
      static const unsigned int MAX_BUFFER_SIZE = 0x40000; // Size of the used buffer.
  };


  //==================================================
  // Implements the runtime-length-encoding-compressor.
  //==================================================
  class uz1RLEAlgorithm : public uz1AlgorithmBase
  {
    public:
      // Constructor. UpdateFunc is called in the compress/decompress process, if UpdateFunc is not NULL.
      uz1RLEAlgorithm(pUz1UpdateFunc UpdateFunc = NULL, void* UserObj = NULL, int ThisStepNum = -1, int NumSteps = -1);

      // Encodes the data in the input stream, beginning with the byte at position InStreamBeg.
      virtual bool Compress(in_stream& InStream, out_stream& OutStream, std::ios::pos_type InStreamBeg = 0);
      
      // Decodes the data in the input stream, beginning with the byte at position InStreamBeg.
      virtual bool Decompress(in_stream& InStream, out_stream& OutStream, std::ios::pos_type InStreamBeg = 0);
      
    private:
      // If Count >= 5, the specified 'Char' is written 5 times and the length is appended.
      // Else only the specified number of 'Char's are written.
      // That means, a compressed block begins, if 5 identical characters appear back-to-back.
      static void EncodeEmitRun(out_stream& OutStream, BYTE Char, BYTE Count); 
      
    private:
      static const BYTE RLE_LEAD = 5;
  };


  //==================================================
  // Implements the Huffman compression.
  //==================================================
  class uz1HuffmanAlgorithm : public uz1AlgorithmBase
  {
    public:
      // Constructor. UpdateFunc is called in the compress/decompress process, if UpdateFunc is not NULL.
      uz1HuffmanAlgorithm(pUz1UpdateFunc UpdateFunc = NULL, void* UserObj = NULL, int ThisStepNum = -1, int NumSteps = -1);

      // Encodes the data in the input stream, beginning with the byte at position InStreamBeg.
      virtual bool Compress(in_stream& InStream, out_stream& OutStream, std::ios::pos_type InStreamBeg = 0);
      
      // Decodes the data in the input stream, beginning with the byte at position InStreamBeg.
      virtual bool Decompress(in_stream& InStream, out_stream& OutStream, std::ios::pos_type InStreamBeg = 0);
  };
  
  
  //==================================================
  // Implements the Move-To-Front-encoder (changes only the byte-order).
  //==================================================
  class uz1MoveToFrontAlgorithm : public uz1AlgorithmBase
  {
    public:
      // Constructor. UpdateFunc is called in the compress/decompress process, if UpdateFunc is not NULL.
      uz1MoveToFrontAlgorithm(pUz1UpdateFunc UpdateFunc = NULL, void* UserObj = NULL, int ThisStepNum = -1, int NumSteps = -1);

      // Encodes the data in the input stream, beginning with the byte at position InStreamBeg.
      virtual bool Compress(in_stream& InStream, out_stream& OutStream, std::ios::pos_type InStreamBeg = 0);
      
      // Decodes the data in the input stream, beginning with the byte at position InStreamBeg.
      virtual bool Decompress(in_stream& InStream, out_stream& OutStream, std::ios::pos_type InStreamBeg = 0);
  };
  
} // End namepsace uzLib