/*
uzLib.h: Contains the uz1, uz2 and uz3 decompression/compression classes.

Language: C++/CLI

Created by Gugi, 2010-2011
*/

#pragma once

using namespace System;

#include <fstream>
#include <string>
#include <windows.h>

#include "zlib.h"

namespace uzLib 
{
  //====================================================================================================
  // Exceptions
  //====================================================================================================

  //-------------------------------------------------
  /// <summary>
  /// Exception thrown on a compression exception.
  ///</summary>
  public ref class CompressionException : public Exception
  { 
    public:
      CompressionException(int zlibError):
          Exception("zlib error code: " + GetZlibErrorDesc(zlibError))
      { }
      
      CompressionException(String^ Message):
          Exception(Message)
      { }
      
    private:
      static String^ GetZlibErrorDesc(int Error);
  };
  
  //-------------------------------------------------
  /// <summary>
  /// Exception thrown when the obtaining of the function pointers of the compression routines failed.
  ///</summary>
  public ref class LoadLibraryException : public Exception
  {
    public:
      LoadLibraryException(String^ Message):
          Exception(Message)
      { }
  };


  //====================================================================================================
  // Delegates
  //====================================================================================================
  
  /// <summary>
  /// Event handler for the uzLib::uz2Lib update events.
  /// ReadBytes: Number of bytes processed so far. TotalByteCount: Total number of bytes to process.
  /// bCancel: Set this to true, to cancel the compression.
  ///</summary>
  public delegate void uzUpdateEventHandler(unsigned long ReadBytes, unsigned long TotalByteCount, String^ Message, bool% bCancel);



  //====================================================================================================
  // Classes
  //====================================================================================================


  //-------------------------------------------------
  /// <summary>
  /// Base for all unreal-compression-classes.
  ///</summary>
  public ref class UCompressBase abstract
  {
    protected:
      // zlib compress/decompress function pointer types.
      typedef int (__cdecl *pGenericCompressFunc)(Bytef *dest, uLongf *destLen, const Bytef *source, uLong sourceLen);
      typedef pGenericCompressFunc pCompressFunc;
      typedef pGenericCompressFunc pDecompressFunc;
 		
 		
 		public:
		  UCompressBase():
		      m_CompressFunc(NULL), m_DecompressFunc(NULL), m_hZlibDLL(NULL)
		  { }
		  
		  /// <summary>
      /// Compresses the data in the specified file, and saves the result in the target file.
      /// In case of an IO error, an IOException is thrown. In case of an compression error, a CompressionException
      /// is thrown.
      /// If the compression was cancelled (from the CompressionUpdateEvent event), false is returned, else true.
      ///</summary>
		  virtual bool CompressFile(String^ SourceFilename, String^ TargetFilename) = 0;
		  
		  /// <summary>
      /// Decompresses the data in the specified file, and saves the result in the target file.
      /// In case of an IO error, an IOException is thrown. In case of an compression error, a CompressionException
      /// is thrown.
      /// If the compression was cancelled (from the DecompressionUpdateEvent event), false is returned, else true.
      ///</summary>
		  virtual bool DecompressFile(String^ SourceFilename, String^ TargetFilename) = 0;
				
		  /// <summary>
      /// Might be triggered from the CompressFile() function to broadcast the current status of the compression.
      ///</summary>
		  event uzUpdateEventHandler^ CompressionUpdateEvent;
		  
		  /// <summary>
      /// Might be triggered from the DecompressFile() function to broadcast the current status of the decompression.
      ///</summary>
		  event uzUpdateEventHandler^ DecompressionUpdateEvent;

		  
		protected:
		  /// <summary>
      /// Gets the pointer to the zlib's compress() function. Throws a LoadLibraryException on an error.
      ///</summary>
		  pCompressFunc GetCompressFunc() { InitCompressFunc(); return m_CompressFunc; }
		  
		  /// <summary>
      /// Gets the pointer to the zlib's uncompress() function. Throws a LoadLibraryException on an error.
      ///</summary>
		  pDecompressFunc GetDecompressFunc() { InitDecompressFunc(); return m_DecompressFunc; }
		  
		  /// <summary>
      /// Opens the specified 'InputStream' from the specified file.
      /// Returns the size of the openend file. In case of an error, an IOException is thrown.
      ///</summary>
		  uLongf OpenInputStream(String^ InputFilename, std::basic_ifstream<Bytef>& InputStream);
		  
		  /// <summary>
      /// Opens the specified 'OutputStream' from the specified file. An existing file is overwritten.
      /// Also creates all necessary folders.
      /// Returns the specified filename as a non-managed string. In case of an error, an IOException is thrown.
      ///</summary>
		  std::wstring OpenOutputStream(String^ OutputFilename, std::basic_ofstream<Bytef>& OutputStream);
		  
		  /// <summary>
      /// Checks if the specified stream begins with the unreal package's magic number (0x9E2A83C1).
      /// Resets afterwards the filepointer to the beginning.
      /// In case this is not a case, a CompressionException is thrown.
      /// The 'InputFilename' is only needed for the exception-message.
      ///</summary>
		  void CheckInputStreamIsUPackage(std::basic_ifstream<Bytef>& InputStream, String^ InputFilename);
		  
		  
 		private:
		  /// <summary>
      /// Initializes the compress-function-pointer (points to the zlib's compress() function afterwards).
      /// In case the zLib.dll wasn't found or some other error occured, a LoadLibraryException is thrown.
      ///</summary>
		  void InitCompressFunc();
		  
		  /// <summary>
      /// Initializes the uncompress-function-pointer (points to the zlib's uncompress() function afterwards).
      /// In case the zLib.dll wasn't found or some other error occured, a LoadLibraryException is thrown.
      ///</summary>
		  void InitDecompressFunc();
					  
		  /// <summary>
      /// Initializes the handle to the zLib-dll.
      ///</summary> 		  
		  void InitDLLHandle();

		private:
		  pCompressFunc m_CompressFunc; // Pointer the the zlib compress() function.
		  pDecompressFunc m_DecompressFunc; // Pointer the the zlib uncompress() function.
		  HINSTANCE m_hZlibDLL; // Handle of the zlib dll.
		
		private:
		  static const DWORD U_PKG_MAGIC_NUMBER = 0x9E2A83C1; // Every unreal package must begin with this number.
  };
  

  //-------------------------------------------------
  
  /// <summary>
  /// Used to specify the uz1-version. The difference between the two is an additional RLE-step in 5678.
  /// The uz1-file begins with the signature.
  ///</summary>
  public enum struct EMUz1Signature
  {
    /// <summary>
    /// E.g.: UT99
    ///</summary>
    UMSIG_UT99 = 1234, 
    /// <summary>
    /// E.g.: Postal
    ///</summary>
    UMSIG_5678 = 5678
  };
  
  /// <summary>
  /// Compression/Decompression lib for the uz format.
  /// Compression: It is necessary to specify a version of the uz1-file for compression.
	///     So either you use the overloaded method (which accepts a version) or the uz1Lib_UT99/uz1Lib_5678 classes.
  ///</summary>
	public ref class uz1Lib : UCompressBase
	{
		public:
		  uz1Lib() : UCompressBase(), m_bInCompressionMode(false)
		  { }
		
		  // Compression. It is necessary to specify a version of the uz1-file for compression.
		  // So either you use the overloaded method (which accepts a version) or the uz1Lib_UT99/uz1Lib_5678 classes.
		  
		  [System::Obsolete("Use the overloaded method (which accepts a version) or the uz1Lib_UT99/uz1Lib_5678 classes.", true)]
		  virtual bool CompressFile(String^ /*SourceFilename*/, String^ /*TargetFilename*/) override
		      { throw gcnew System::NotImplementedException("Use the overloaded method (which accepts a version) or the uz1Lib_UT99/uz1Lib_5678 classes."); }
		  
		  bool CompressFile(String^ SourceFilename, String^ TargetFilename, EMUz1Signature Uz1Version);
		      
		  // Decompression. No signature is required as it is saved in the uz-file.
		  virtual bool DecompressFile(String^ SourceFilename, String^ TargetFilename) override;
	  
	  internal:
		  // Called to call the correct (as specified by m_bInCompressionMode) update event.
		  void CallUzUpdateEvent(unsigned long CurStatus, unsigned long CompletedStatus, String^ Message, bool% bCancel);
		
		private:
		  bool m_bInCompressionMode;
  };
  
  /// <summary>
  /// Compression/Decompression lib for the uz format (Compression: UT99 version, i.e. the signature is 1234).
  ///</summary>
  public ref class uz1Lib_UT99 : uz1Lib
  {
    public:
		  virtual bool CompressFile(String^ SourceFilename, String^ TargetFilename) override
		      { return CompressFile(SourceFilename, TargetFilename, EMUz1Signature::UMSIG_UT99); }
  };
   
  /// <summary>
  /// Compression/Decompression lib for the uz format (Compression: 5678 version (e.g. Postal), i.e. the signature is 1234).
  ///</summary>
  public ref class uz1Lib_5678 : uz1Lib
  {
    public:
		  virtual bool CompressFile(String^ SourceFilename, String^ TargetFilename) override
		      { return CompressFile(SourceFilename, TargetFilename, EMUz1Signature::UMSIG_5678); }
  };

  //-------------------------------------------------
  /// <summary>
  /// Compression/Decompression lib for the uz2 format.
  ///</summary>
	public ref class uz2Lib : UCompressBase
	{
		public:
		  uz2Lib() : UCompressBase()
		  { }
		
		  /// <summary>
      /// Compresses the data in the specified file into the uz2-format, and saves the result in the target file.
      /// It is not ensured that the target filename ends with .uz2.
      /// In case the source file is not an unreal package, a CompressionException is thrown.
      /// In case of an IO error, an IOException is thrown. In case of an compression error, a CompressionException
      /// is thrown.
      /// If the compression was cancelled (from the CompressionUpdateEvent event), false is returned, else true.
      ///</summary>
		  virtual bool CompressFile(String^ SourceFilename, String^ TargetFilename) override;
		  
		  /// <summary>
      /// Decompresses the data in the specified file, and saves the result in the target file.
      /// It is not ensured that the source filename ends with .uz2, nor that the output file is an unreal package.
      /// In case of an IO error, an IOException is thrown. In case of an compression error, a CompressionException
      /// is thrown.
      /// If the compression was cancelled (from the DecompressionUpdateEvent event), false is returned, else true.
      ///</summary>
		  virtual bool DecompressFile(String^ SourceFilename, String^ TargetFilename) override;
				
				
		private:
		  static const std::streamsize UNCOMPR_BLOCK_SIZE = 32768; // The chunk-size which is read from the input-file.
      static const std::streamsize COMPR_BLOCK_SIZE = 33096; // The chunk-size for the compressed-buffer.
	};
	
	
  //-------------------------------------------------
  /// <summary>
  /// Compression/Decompression lib for the uz3 format.
  ///</summary>
	public ref class uz3Lib : UCompressBase
	{
		public:
		  uz3Lib() : UCompressBase()
		  { }
		
		  /// <summary>
      /// Compresses the data in the specified file into the uz3-format, and saves the result in the target file.
      /// It is not ensured that the target filename ends with .uz3.
      /// In case the source file is not an unreal package, a CompressionException is thrown.
      /// In case of an IO error, an IOException is thrown. In case of an compression error, a CompressionException
      /// is thrown.
      /// Always returns true. The CompressionUpdateEvent event is never triggered.
      ///</summary>
		  virtual bool CompressFile(String^ SourceFilename, String^ TargetFilename) override;
		  
		  /// <summary>
      /// Decompresses the data in the specified file, and saves the result in the target file.
      /// It is not ensured that the source filename ends with .uz3.
      /// In case the source file is not a uz3 file, a CompressionException is thrown.
      /// (the source file may have a another extension than uz3, though).
      /// In case of an IO error, an IOException is thrown. In case of an decompression error, a CompressionException
      /// is thrown.
      /// Always returns true. The DecompressionUpdateEvent event is never triggered.
      ///</summary>
		  virtual bool DecompressFile(String^ SourceFilename, String^ TargetFilename) override;
		
		
		private:
		  /// <summary>
      /// Checks if the specified stream begins with the uz3's magic number (0x0000162E).
      /// Does NOT reset the filepointer afterwards to the beginning.
      /// In case this is not a case, a CompressionException is thrown.
      /// The 'InputFilename' is only needed for the exception-message.
      ///</summary>
		  void CheckInputStreamIsUZ3(std::basic_ifstream<Bytef>& InputStream, String^ InputFilename);

		private:
		  static const uLongf UZ3_MAGIC_NUMBER = 0x0000162E; // Every uz3 package begins with this number.
	};
	
} // End namepsace uzLib
