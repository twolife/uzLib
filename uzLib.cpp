/*
uzLib.cpp: Contains the implementation of the uz1, uz2 and uz3 decompression/compression classes.
The uz1-implementation delegates the main job to the stuff in uz1Impl.h

Language: C++/CLI

Created by Gugi, 2010-2011
*/

#include "stdafx.h"
#include <string>
#include <fstream>
#include <windows.h>
#include <vcclr.h>

#include "uzLib.h"
#include "uz1Impl.h"

using namespace System::Runtime::InteropServices;
using namespace std;

//===========================================================================================================
// Helper functions
//===========================================================================================================
namespace
{
  // .NET-String --> Native WString
  void NETStrToWStr(String^ ManagedStr, wstring& OutNativeStr)
  {
    if (ManagedStr == nullptr)
    {
      OutNativeStr = L"";
      return;
    }
    
    // Convert the managed string into an array of characters.
    const wchar_t* chars = (const wchar_t*)(Marshal::StringToHGlobalUni(ManagedStr)).ToPointer();
    OutNativeStr = chars;
    
    // Free the character-array.
    Marshal::FreeHGlobal(IntPtr((void*)chars));
  }
  
  // .NET-String --> Native String
  void NETStrToStr(String^ ManagedStr, string& OutNativeStr)
  {
    if (ManagedStr == nullptr)
    {
      OutNativeStr = "";
      return;
    }
    
    // Convert the managed string into an array of characters.
    const char* chars = (const char*)(Marshal::StringToHGlobalAnsi(ManagedStr)).ToPointer();
    OutNativeStr = chars;
    
    // Free the character-array.
    Marshal::FreeHGlobal(IntPtr((void*)chars));
  }

  // Native String --> .NET-String
  String^ StrToNETStr(const std::string& InNativeStr)
  {
    return Marshal::PtrToStringAnsi(static_cast<IntPtr>(const_cast<char*>(InNativeStr.c_str())));
  }
  
  // Native WString --> .NET-String
  String^ StrToNETStr(const std::wstring& InNativeStr)
  {
    return Marshal::PtrToStringUni(static_cast<IntPtr>(const_cast<wchar_t*>(InNativeStr.c_str())));
  }

  // Extracts the path from the filename and creates the directory structure.
  void CreateDirectoryFromFilename(String^ Filename)
  {
    int SlashPos = Filename->LastIndexOf('\\');
    if (SlashPos <= 0)
      return;
    
    try
    {
      System::IO::Directory::CreateDirectory(Filename->Substring(0, SlashPos));
    }
    catch(Exception^ ex)
    {
      throw gcnew System::IO::IOException("Couldn't create target directory: " + Filename + "\nMessage: " + ex->Message);
    }
  }
}


//===========================================================================================================
// Exceptions implementation
//===========================================================================================================
String^ uzLib::CompressionException::GetZlibErrorDesc(int Error)
{
  #define RETURN_AS_STRING(Code) case Code: return #Code;
  switch (Error)
  {
    RETURN_AS_STRING(Z_OK)
    RETURN_AS_STRING(Z_STREAM_END)
    RETURN_AS_STRING(Z_NEED_DICT)
    RETURN_AS_STRING(Z_ERRNO)
    RETURN_AS_STRING(Z_STREAM_ERROR)
    RETURN_AS_STRING(Z_DATA_ERROR)
    RETURN_AS_STRING(Z_MEM_ERROR)
    RETURN_AS_STRING(Z_BUF_ERROR)
    RETURN_AS_STRING(Z_VERSION_ERROR)
    default: Error.ToString();
 }
 #undef RETURN_AS_STRING
 
 return Error.ToString();
}


//===========================================================================================================
// UCompressBase implementation
//===========================================================================================================
void uzLib::UCompressBase::InitDLLHandle()
{
  if (m_hZlibDLL != NULL)
    return;
  
  m_hZlibDLL = ::LoadLibrary(L"zlib1.dll");
  
  if (m_hZlibDLL == NULL)
    throw gcnew uzLib::LoadLibraryException("Failed loading zlib1.dll");
}

void uzLib::UCompressBase::InitCompressFunc()
{
  if (m_CompressFunc != NULL)
    return;
  InitDLLHandle();
  
  m_CompressFunc = reinterpret_cast<uzLib::UCompressBase::pCompressFunc>(::GetProcAddress(m_hZlibDLL, "compress"));
  
  if (m_CompressFunc == NULL)
    throw gcnew uzLib::LoadLibraryException("Failed loading compress-function.");
}

void uzLib::UCompressBase::InitDecompressFunc()
{
  if (m_CompressFunc != NULL)
    return;
  InitDLLHandle();
  
  m_DecompressFunc = reinterpret_cast<uzLib::UCompressBase::pDecompressFunc>(::GetProcAddress(m_hZlibDLL, "uncompress"));
  
  if (m_DecompressFunc == NULL)
    throw gcnew uzLib::LoadLibraryException("Failed loading uncompress-function.");
}

uLongf uzLib::UCompressBase::OpenInputStream(String^ InputFilename, basic_ifstream<Bytef>& InputStream)
{
  // Convert the filename.
  wstring ConvSrcFilename;
  NETStrToWStr(InputFilename, ConvSrcFilename);
  
  // Open the input file.
  InputStream.open(ConvSrcFilename.c_str(), ios_base::in | ios_base::binary, SH_DENYWR);
  if (!InputStream.is_open() || !InputStream.good() || InputStream.eof())
    throw gcnew System::IO::IOException("Couldn't open input file '" + InputFilename + "'.");
  
  // Get the file size.
  const uLongf BeginInputStreamPos = InputStream.tellg();
  InputStream.seekg(0, ios_base::end);
  const uLongf EndInputStreamPos = InputStream.tellg();
  InputStream.seekg(0, ios_base::beg); // Set filepointer-position to the beginning again.
  
  return (EndInputStreamPos - BeginInputStreamPos);
}

wstring uzLib::UCompressBase::OpenOutputStream(String^ OutputFilename, basic_ofstream<Bytef>& OutputStream)
{
  // Convert the filename.
  wstring ConvTargetFilename;
  NETStrToWStr(OutputFilename, ConvTargetFilename);
  
  // Create the directory in which the file should be.
  CreateDirectoryFromFilename(OutputFilename);
  
  // Open the output file.
  OutputStream.open(ConvTargetFilename.c_str(), ios_base::out | ios_base::trunc | ios_base::binary, SH_DENYWR);
  if (!OutputStream.is_open() || !OutputStream.good())
    throw gcnew System::IO::IOException("Couldn't open output file '" + OutputFilename + "'.");
  
  return ConvTargetFilename;
}

void uzLib::UCompressBase::CheckInputStreamIsUPackage(basic_ifstream<Bytef>& InputStream, String^ InputFilename)
{
  DWORD MagicNumber = 0;
  InputStream.read(reinterpret_cast<Bytef*>(&MagicNumber), sizeof(DWORD));
  if (MagicNumber != U_PKG_MAGIC_NUMBER)
    throw gcnew uzLib::CompressionException("Input file '" + InputFilename + "' is not an unreal package.");
  
  InputStream.seekg(0, ios_base::beg); // Set filepointer-position to the beginning again.
}


//===========================================================================================================
// uz1Lib implementation
//===========================================================================================================

namespace
{
  void uz1UpdateFunc(unsigned int CurStatus, unsigned int CompletedStatus, const std::wstring& Msg, bool& bCancel, void* UserObj)
  {
    if (UserObj == NULL)
      return;
    
    // Get the uz1Lib.
    gcroot<uzLib::uz1Lib^>* TheLib = static_cast<gcroot<uzLib::uz1Lib^>* >(UserObj);
    if (static_cast<uzLib::uz1Lib^>(*TheLib) == nullptr)
      throw gcnew System::Exception("uz1Lib-reference is null in uz1UpdateFunc.");
    
    // Get the managed version of the message-string.
    String^ ManagedMsg = "";
    if (Msg.length() > 0)
      ManagedMsg = StrToNETStr(Msg);
    
    // Call the update event (can't be done directly; will result in error C3767).
    (*TheLib)->CallUzUpdateEvent(CurStatus, CompletedStatus, ManagedMsg, bCancel);
  }
}

void uzLib::uz1Lib::CallUzUpdateEvent(unsigned long CurStatus, unsigned long CompletedStatus, String^ Message, bool% bCancel)
{
  if (m_bInCompressionMode)
    CompressionUpdateEvent(CurStatus, CompletedStatus, Message, bCancel);
  else  
    DecompressionUpdateEvent(CurStatus, CompletedStatus, Message, bCancel);
}

//-----------------------------------------------------------
// CompressFile
//-----------------------------------------------------------
bool uzLib::uz1Lib::CompressFile(String^ SourceFilename, String^ TargetFilename, EMUz1Signature Uz1Version)
{
  m_bInCompressionMode = true;

  // Open the input file.
  basic_ifstream<Bytef> InputStream;
  /*const uLongf InputFileSize = */ OpenInputStream(SourceFilename, InputStream);
  System::Diagnostics::Debug::Assert(InputStream.is_open() && InputStream.good() && !InputStream.eof());
  InputStream.exceptions(std::ios::failbit | std::ios::badbit);
  
  // Create the ouput file.
  basic_ofstream<Bytef> OutputStream;
  const wstring ConvTargetFilename = OpenOutputStream(TargetFilename, OutputStream);
  System::Diagnostics::Debug::Assert(OutputStream.is_open() && OutputStream.good());
  OutputStream.exceptions(std::ios::failbit | std::ios::badbit);
  
  bool bDeleteBuiltFile = false;
  bool bCancelled = false;
  gcroot<uzLib::uz1Lib^>* ThisLib = NULL;

  try
  {   
    // Get the filename to save in the uz-file. We simply use the filename with which we opened the file;
    // UT99 seems to get the package-name (because if you compress a file called EDITOR.U for example,
    // Editor.u is actually saved).
    std::wstring SrcFilename;
    NETStrToWStr(System::IO::Path::GetFileName(SourceFilename), SrcFilename);
    
    // Compress the file.
    ThisLib = new gcroot<uzLib::uz1Lib^>(this); // You can't get the address of gcroot by the * operator :/ So we need to create it using new...
    if (!uzLib::CompressToUz1(InputStream, OutputStream, SrcFilename, 
        static_cast<uzLib::EUz1Signature>(Uz1Version), &uz1UpdateFunc, static_cast<void*>(ThisLib)))
    {
      bDeleteBuiltFile = true;
      bCancelled = true; // Do not use return here. The file won't be deleted, because ConvTargetFilename-destructor is called to early (Same bug as: http://connect.microsoft.com/VisualStudio/feedback/details/101422/inconsistent-order-of-execution-of-destructors-and-finally-blocks-during-stack-unwinding)
    }
  }
  catch (std::exception& ex)
  {
    bDeleteBuiltFile = true;
    throw gcnew uzLib::CompressionException("A native exception was caught in '" + __FUNCTION__ + "':\n" + StrToNETStr(ex.what()));
  }
  catch (System::Exception^)
  {
    bDeleteBuiltFile = true;
    throw;
  }
  catch (...)
  {
    bDeleteBuiltFile = true;
    throw gcnew System::Exception("An unknown exception was caught in '" + __FUNCTION__ + "'.");
  }
  finally
  {
    if (ThisLib != NULL)
    {
      delete ThisLib;
      ThisLib = NULL;
    }
    
    if (InputStream.is_open())
      InputStream.close();

    // Close the output file. In case of an error, delete the already written output file.
    if (OutputStream.is_open())
      OutputStream.close();
    if (bDeleteBuiltFile)
      ::DeleteFile(ConvTargetFilename.c_str());
  }
  
  return (!bCancelled);
}

//-----------------------------------------------------------
// DecompressFile
//-----------------------------------------------------------
bool uzLib::uz1Lib::DecompressFile(String^ SourceFilename, String^ TargetFilename)
{
  m_bInCompressionMode = false;

  // Open the input file.
  basic_ifstream<Bytef> InputStream;
  /*const uLongf InputFileSize = */ OpenInputStream(SourceFilename, InputStream);
  System::Diagnostics::Debug::Assert(InputStream.is_open() && InputStream.good() && !InputStream.eof());
  InputStream.exceptions(std::ios::failbit | std::ios::badbit);
  
  // Create the ouput file.
  basic_ofstream<Bytef> OutputStream;
  const wstring ConvTargetFilename = OpenOutputStream(TargetFilename, OutputStream);
  System::Diagnostics::Debug::Assert(OutputStream.is_open() && OutputStream.good());
  OutputStream.exceptions(std::ios::failbit | std::ios::badbit);
  
  bool bDeleteBuiltFile = false;
  bool bCancelled = false;
  gcroot<uzLib::uz1Lib^>* ThisLib = NULL;

  try
  {
    // Decompress the file.
    ThisLib = new gcroot<uzLib::uz1Lib^>(this); // You can't get the address of gcroot by the * operator :/ So we need to create it using new...
    if (!uzLib::DecompressFromUz1(InputStream, OutputStream, &uz1UpdateFunc, static_cast<void*>(ThisLib)))
    {
      bDeleteBuiltFile = true;
      bCancelled = true; // Do not use return here. The file won't be deleted, because ConvTargetFilename-destructor is called to early (Same bug as: http://connect.microsoft.com/VisualStudio/feedback/details/101422/inconsistent-order-of-execution-of-destructors-and-finally-blocks-during-stack-unwinding)
    }
  }
  catch (std::exception& ex)
  {
    bDeleteBuiltFile = true;
    throw gcnew uzLib::CompressionException("A native exception was caught in '" + __FUNCTION__ + "':\n" + StrToNETStr(ex.what()));
  }
  catch (System::Exception^)
  {
    bDeleteBuiltFile = true;
    throw;
  }
  catch (...)
  {
    bDeleteBuiltFile = true;
    throw gcnew System::Exception("An unknown exception was caught in '" + __FUNCTION__ + "'.");
  }
  finally
  {
    if (ThisLib != NULL)
    {
      delete ThisLib;
      ThisLib = NULL;
    }

    if (InputStream.is_open())
      InputStream.close();

    // Close the output file. In case of an error, delete the already written output file.
    if (OutputStream.is_open())
      OutputStream.close();
    if (bDeleteBuiltFile)
      ::DeleteFile(ConvTargetFilename.c_str());
  }
  
  return (!bCancelled);
}


//===========================================================================================================
// uz2Lib implementation: Very close to the tinyuz2-sourcecode.
//===========================================================================================================

//-----------------------------------------------------------
// CompressFile
//-----------------------------------------------------------
bool uzLib::uz2Lib::CompressFile(String^ SourceFilename, String^ TargetFilename)
{
  // Open the input file.
  basic_ifstream<Bytef> InputStream;
  const uLongf InputFileSize = OpenInputStream(SourceFilename, InputStream);
  System::Diagnostics::Debug::Assert(InputStream.is_open() && InputStream.good() && !InputStream.eof());
 
  CheckInputStreamIsUPackage(InputStream, SourceFilename);
  
  const uLongf BeginInputStreamPos = InputStream.tellg(); // Should be 0.
  
  // Create the ouput file.
  basic_ofstream<Bytef> OutputStream;
  const wstring ConvTargetFilename = OpenOutputStream(TargetFilename, OutputStream);
  System::Diagnostics::Debug::Assert(OutputStream.is_open() && OutputStream.good());
 
  bool bDeleteBuiltFile = false;
  bool bCancelled = false;

  Bytef* InBuffer = NULL;
  Bytef* ComprBuffer = NULL;
  
  try
  {
    // Get the compress function.
    const pCompressFunc CompressFunc = GetCompressFunc();

    // Create the buffers.
    InBuffer = new Bytef[UNCOMPR_BLOCK_SIZE];
    ComprBuffer = new Bytef[COMPR_BLOCK_SIZE];
            
    // Loop through the input-file and compress each chunk, as long bytes can be read (and written).
    while (InputStream.good() && OutputStream.good())
    {
      // Read some bytes.
      InputStream.read(InBuffer, UNCOMPR_BLOCK_SIZE);
      const uLongf ReadCount = InputStream.gcount();
      if (ReadCount == 0)
        break;
      
      // Compress the bytes.
      uLongf ComprBufferCount = COMPR_BLOCK_SIZE;
      const int StatusCode = (*CompressFunc)(ComprBuffer, &ComprBufferCount, InBuffer, ReadCount);
      if (StatusCode != Z_OK)
        throw gcnew uzLib::CompressionException(StatusCode);
      
      // Write the count of the compressed bytes.
      OutputStream.write(reinterpret_cast<Bytef*>(&ComprBufferCount), sizeof(uLongf));
      
      // Write the count of the UNcompressed bytes.
      OutputStream.write(reinterpret_cast<const Bytef*>(&ReadCount), sizeof(uLongf));
      
      // Write the compressed data.
      OutputStream.write(ComprBuffer, ComprBufferCount);
      
      // Send an update.
      bool bCancel = false;
      CompressionUpdateEvent(static_cast<uLongf>(InputStream.tellg())-BeginInputStreamPos, InputFileSize, String::Empty, bCancel);
      if (bCancel)
      {
        bDeleteBuiltFile = true; // Delete the already written data.
        bCancelled = true;
        break;
      }
    }
    
    // In case of an IO error, throw an exception.
    if (InputStream.bad() || (!InputStream.eof() && !bCancelled) || OutputStream.bad())
    {
      if (OutputStream.bad())
        throw gcnew System::IO::IOException("Bad output stream '" + TargetFilename + "'.");
      else
        throw gcnew System::IO::IOException("Bad input stream '" + SourceFilename + "'.");
    }
  }
  catch(...)
  {
    bDeleteBuiltFile = true;
    throw; // rethrow
  }
  finally
  {
    if (InputStream.is_open())
      InputStream.close();

    // Close the output file. In case of an error, delete the already written output file.
    if (OutputStream.is_open())
      OutputStream.close();
    if (bDeleteBuiltFile)
      ::DeleteFile(ConvTargetFilename.c_str());
      
    // Cleanup the buffers.
    if (InBuffer != NULL)
    {
      delete[] InBuffer;
      InBuffer = NULL;
    }
    
    if (ComprBuffer != NULL)
    {
      delete[] ComprBuffer;
      ComprBuffer = NULL;
    }
  }
  
  return (!bCancelled);
}


//-----------------------------------------------------------
// DecompressFile
//-----------------------------------------------------------
bool uzLib::uz2Lib::DecompressFile(String^ SourceFilename, String^ TargetFilename)
{
  // Open the input file.
  basic_ifstream<Bytef> InputStream;
  const uLongf InputFileSize = OpenInputStream(SourceFilename, InputStream);
  System::Diagnostics::Debug::Assert(InputStream.is_open() && InputStream.good() && !InputStream.eof());
  
  const uLongf BeginInputStreamPos = InputStream.tellg(); // Should be 0.
  
  // Create the ouput file.
  basic_ofstream<Bytef> OutputStream;
  const wstring ConvTargetFilename = OpenOutputStream(TargetFilename, OutputStream);
  System::Diagnostics::Debug::Assert(OutputStream.is_open() && OutputStream.good());
 
  bool bDeleteBuiltFile = false;
  bool bCancelled = false;

  Bytef* ComprBuffer = NULL;
  Bytef* UnComprBuffer = NULL;

  try
  {
    // Get the decompress function.
    const pDecompressFunc DecompressFunc = GetDecompressFunc();

    // Create the buffers.
    ComprBuffer = new Bytef[COMPR_BLOCK_SIZE];
    UnComprBuffer = new Bytef[UNCOMPR_BLOCK_SIZE];
        
    // Loop through the input-file and decompress each chunk, as long characters can be read (and written).
    while (InputStream.good() && OutputStream.good())
    {
      // Read the compressed size.
      uLongf ComprSize = 0;
      InputStream.read(reinterpret_cast<Bytef*>(&ComprSize), sizeof(uLongf));
      if (InputStream.eof())
      {
        // In case some value has been read, we got an error. Else all bytes have already been read.
        if (ComprSize != 0 || InputStream.gcount() > 0)
          throw gcnew uzLib::CompressionException("Input file ends after a compressed-size-value.");
        else
          break;
      }
      else if (ComprSize == 0)
        throw gcnew uzLib::CompressionException("Saved compressed-size is 0");
      else if (ComprSize > COMPR_BLOCK_SIZE)
        throw gcnew uzLib::CompressionException("File is not a uz2 file (compressed-size > max-compressed-size)");
      
      // Read the uncompressed size.
      uLongf UnComprSize = 0;
      InputStream.read(reinterpret_cast<Bytef*>(&UnComprSize), sizeof(uLongf));
      if (InputStream.eof())
        throw gcnew uzLib::CompressionException("Input file ends after a uncompressed-size-value.");
      else if (UnComprSize == 0)
        throw gcnew uzLib::CompressionException("Saved uncompressed-size is 0");
      else if (UnComprSize > UNCOMPR_BLOCK_SIZE)
        throw gcnew uzLib::CompressionException("File is not a uz2 file (uncompressed-size > max-uncompressed-size)");
        
      // Read the whole compressed chunk.
      InputStream.read(ComprBuffer, ComprSize);
      const uLongf NumReadBytes = InputStream.gcount();
      if (NumReadBytes != ComprSize)
        throw gcnew uzLib::CompressionException("Couldn't read complete compressed-data chunk (or the file is damaged).");
      
      // Decompress the data.
      uLongf RealUnComprSize = UNCOMPR_BLOCK_SIZE;
      const int StatusCode = (*DecompressFunc)(UnComprBuffer, &RealUnComprSize, ComprBuffer, ComprSize);
      if (StatusCode != Z_OK)
        throw gcnew uzLib::CompressionException(StatusCode);
      else if (RealUnComprSize != UnComprSize)
        throw gcnew uzLib::CompressionException("The decompressed chunk has a different size than the saved value. Damaged file?");
      
      // Write the uncompressed data.
      OutputStream.write(UnComprBuffer, RealUnComprSize);
      
      // Send an update.
      bool bCancel = false;
      DecompressionUpdateEvent(static_cast<uLongf>(InputStream.tellg())-BeginInputStreamPos, InputFileSize, String::Empty, bCancel);
      if (bCancel)
      {
        bDeleteBuiltFile = true; // Delete the already written data.
        bCancelled = true;
        break;
      }
    }
    
    // In case of an IO error, throw an exception.
    if (InputStream.bad() || (!InputStream.eof() && !bCancelled) || OutputStream.bad())
    {
      if (OutputStream.bad())
        throw gcnew System::IO::IOException("Bad output stream.");
      else
        throw gcnew System::IO::IOException("Bad input stream.");
    }
  }
  catch(...)
  {
    bDeleteBuiltFile = true;
    throw; // rethrow
  }
  finally
  {
    if (InputStream.is_open())
      InputStream.close();

    // Close the output file. In case of an error, delete the already written output file.
    if (OutputStream.is_open())
      OutputStream.close();
    if (bDeleteBuiltFile)
      ::DeleteFile(ConvTargetFilename.c_str());
      
    // Cleanup the buffers.
    if (ComprBuffer != NULL)
    {
      delete[] ComprBuffer;
      ComprBuffer = NULL;
    }
    
    if (UnComprBuffer != NULL)
    {
      delete[] UnComprBuffer;
      UnComprBuffer = NULL;
    }
  }
  
  return (!bCancelled);
}


//===========================================================================================================
// uz3Lib implementation
//===========================================================================================================
void uzLib::uz3Lib::CheckInputStreamIsUZ3(std::basic_ifstream<Bytef>& InputStream, String^ InputFilename)
{
  DWORD MagicNumber = 0;
  InputStream.read(reinterpret_cast<Bytef*>(&MagicNumber), sizeof(DWORD));
  if (MagicNumber != UZ3_MAGIC_NUMBER)
    throw gcnew uzLib::CompressionException("Input file '" + InputFilename + "' is not a valid uz3 file.");
}


//-----------------------------------------------------------
// CompressFile
//-----------------------------------------------------------
bool uzLib::uz3Lib::CompressFile(String^ SourceFilename, String^ TargetFilename)
{
  // Open the input file.
  basic_ifstream<Bytef> InputStream;
  const uLongf InputFileSize = OpenInputStream(SourceFilename, InputStream);
  System::Diagnostics::Debug::Assert(InputStream.is_open() && InputStream.good() && !InputStream.eof());
 
  CheckInputStreamIsUPackage(InputStream, SourceFilename);
    
  // Create the ouput file.
  basic_ofstream<Bytef> OutputStream;
  const wstring ConvTargetFilename = OpenOutputStream(TargetFilename, OutputStream);
  System::Diagnostics::Debug::Assert(OutputStream.is_open() && OutputStream.good());
  
  bool bDeleteBuiltFile = false;
  
  Bytef* InBuffer = NULL;
  Bytef* ComprBuffer = NULL;
  
  try
  {
    // Get the compress function.
    const pCompressFunc CompressFunc = GetCompressFunc();
    
    // Create the input buffer.
    InBuffer = new Bytef[InputFileSize];
    
    // Read the input-file into the buffer.
    InputStream.read(InBuffer, InputFileSize);
    const uLongf ReadCount = InputStream.gcount();
    if (ReadCount != InputFileSize)
    {
      throw gcnew System::IO::IOException("Failed reading complete input file '" + SourceFilename + "' into a buffer. " +
          "Read-count: " + ReadCount.ToString() + "; Expected: " + InputFileSize.ToString());
    }
    
    // Compress everything.
    ComprBuffer = new Bytef[2 * InputFileSize];
    uLongf ComprBufferCount = 2 * InputFileSize;
    const int StatusCode = (*CompressFunc)(ComprBuffer, &ComprBufferCount, InBuffer, InputFileSize);
    if (StatusCode != Z_OK)
      throw gcnew uzLib::CompressionException(StatusCode);
    
    // Write the uz3-magic-number.
    const DWORD Temp_UZ3_MagicNumber = UZ3_MAGIC_NUMBER; // Prevents: error C2440: 'reinterpret_cast': 'cli::interior_ptr<Type>' kann nicht in 'Bytef *' konvertiert werden
    OutputStream.write(reinterpret_cast<const Bytef*>(&Temp_UZ3_MagicNumber), sizeof(DWORD));
    
    // Write the original file-size.
    OutputStream.write(reinterpret_cast<const Bytef*>(&InputFileSize), sizeof(uLongf));
    
    // Write the compressed data.
    OutputStream.write(ComprBuffer, ComprBufferCount);
  }
  catch(...)
  {
    bDeleteBuiltFile = true;
    throw; // rethrow
  }
  finally
  {
    if (InputStream.is_open())
      InputStream.close();

    // Close the output file. In case of an error, delete the already written output file.
    if (OutputStream.is_open())
      OutputStream.close();
    if (bDeleteBuiltFile)
      ::DeleteFile(ConvTargetFilename.c_str());
      
    // Cleanup the buffers.
    if (InBuffer != NULL)
    {
      delete[] InBuffer;
      InBuffer = NULL;
    }
    
    if (ComprBuffer != NULL)
    {
      delete[] ComprBuffer;
      ComprBuffer = NULL;
    }
  }
  
  return true;
}

//-----------------------------------------------------------
// DecompressFile
//-----------------------------------------------------------
bool uzLib::uz3Lib::DecompressFile(String^ SourceFilename, String^ TargetFilename)
{
  // Open the input file.
  basic_ifstream<Bytef> InputStream;
  const uLongf InputFileSize = OpenInputStream(SourceFilename, InputStream);
  System::Diagnostics::Debug::Assert(InputStream.is_open() && InputStream.good() && !InputStream.eof());
  
  CheckInputStreamIsUZ3(InputStream, SourceFilename);
    
  // Create the ouput file.
  basic_ofstream<Bytef> OutputStream;
  const wstring ConvTargetFilename = OpenOutputStream(TargetFilename, OutputStream);
  System::Diagnostics::Debug::Assert(OutputStream.is_open() && OutputStream.good());
 
  bool bDeleteBuiltFile = false;

  Bytef* ComprBuffer = NULL;
  Bytef* UnComprBuffer = NULL;

  try
  {
    // Get the decompress function.
    const pDecompressFunc DecompressFunc = GetDecompressFunc();

    // Read the original filesize (the magic number has already been read with the call to CheckInputStreamIsUZ3()).
    uLongf SavedUnComprFileSize = 0;
    InputStream.read(reinterpret_cast<Bytef*>(&SavedUnComprFileSize), sizeof(uLongf));
    if (SavedUnComprFileSize == 0)
      throw gcnew uzLib::CompressionException("The read value for the uncompressed filesize is 0.");

    // Create the input buffer (holds the compressed data).
    const uLongf CompressedFileSize = InputFileSize - 8; // -8: 4 bytes for the magic number, 4 for the saved file size.
    ComprBuffer = new Bytef[CompressedFileSize];
    
     // Read the input-file into the buffer.
    InputStream.read(ComprBuffer, CompressedFileSize);
    const uLongf ReadCount = InputStream.gcount();
    if (ReadCount != CompressedFileSize)
    {
      throw gcnew System::IO::IOException("Failed reading complete input file '" + SourceFilename + "' into a buffer. " +
          "Read-count: " + ReadCount.ToString() + "; Expected: " + CompressedFileSize.ToString());
    }
   
    // Decompress everything.
    UnComprBuffer = new Bytef[SavedUnComprFileSize];
    uLongf UnComprSize = SavedUnComprFileSize;
    const int StatusCode = (*DecompressFunc)(UnComprBuffer, &UnComprSize, ComprBuffer, CompressedFileSize);
    if (StatusCode != Z_OK)
      throw gcnew uzLib::CompressionException(StatusCode);
    else if (SavedUnComprFileSize != UnComprSize)
      throw gcnew uzLib::CompressionException("The decompressed file has a different size than the saved filesize. Damaged file?");
      
    // Write the uncompressed data.
    OutputStream.write(UnComprBuffer, UnComprSize);
  }
  catch(...)
  {
    bDeleteBuiltFile = true;
    throw; // rethrow
  }
  finally
  {
    if (InputStream.is_open())
      InputStream.close();

    // Close the output file. In case of an error, delete the already written output file.
    if (OutputStream.is_open())
      OutputStream.close();
    if (bDeleteBuiltFile)
      ::DeleteFile(ConvTargetFilename.c_str());
      
    // Cleanup the buffers.
    if (ComprBuffer != NULL)
    {
      delete[] ComprBuffer;
      ComprBuffer = NULL;
    }
    
    if (UnComprBuffer != NULL)
    {
      delete[] UnComprBuffer;
      UnComprBuffer = NULL;
    }
  }
  
  return true;
}
