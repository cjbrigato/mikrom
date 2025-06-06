#include "rar.hpp"

File::File()
{
  hFile=FILE_BAD_HANDLE;
  NewFile=false;
  LastWrite=false;
  HandleType=FILE_HANDLENORMAL;
  LineInput=false;
  SkipClose=false;
  ErrorType=FILE_SUCCESS;
  OpenShared=false;
  AllowDelete=true;
  AllowExceptions=true;
  PreserveAtime=false;
#ifdef _WIN_ALL
  CreateMode=FMF_UNDEFINED;
#endif
  ReadErrorMode=FREM_ASK;
  TruncatedAfterReadError=false;
  CurFilePos=0;

#ifdef CHROMIUM_UNRAR
  hOpenFile=FILE_BAD_HANDLE;
#endif
}


File::~File()
{
  if (hFile!=FILE_BAD_HANDLE && !SkipClose)
    if (NewFile)
      Delete();
    else
      Close();
}


void File::operator = (File &SrcFile)
{
  hFile=SrcFile.hFile;
  NewFile=SrcFile.NewFile;
  LastWrite=SrcFile.LastWrite;
  HandleType=SrcFile.HandleType;
  TruncatedAfterReadError=SrcFile.TruncatedAfterReadError;
  FileName=SrcFile.FileName;
  SrcFile.SkipClose=true;
}


bool File::Open(const std::wstring &Name,uint Mode)
{
  ErrorType=FILE_SUCCESS;
  FileHandle hNewFile;
  bool OpenShared=File::OpenShared || (Mode & FMF_OPENSHARED)!=0;
  bool UpdateMode=(Mode & FMF_UPDATE)!=0;
  bool WriteMode=(Mode & FMF_WRITE)!=0;
#ifdef _WIN_ALL
#if defined(CHROMIUM_UNRAR)
  // Do not open a file handle since the sandbox doesn't allow it. Use the
  // handle provided by the caller.
  hNewFile = hOpenFile;
#else
  uint Access=WriteMode ? GENERIC_WRITE:GENERIC_READ;
  if (UpdateMode)
    Access|=GENERIC_WRITE;
  uint ShareMode=(Mode & FMF_OPENEXCLUSIVE) ? 0 : FILE_SHARE_READ;
  if (OpenShared)
    ShareMode|=FILE_SHARE_WRITE;
  uint Flags=FILE_FLAG_SEQUENTIAL_SCAN;
  FindData FD;
  if (PreserveAtime)
    Access|=FILE_WRITE_ATTRIBUTES; // Needed to preserve atime.
  hNewFile=CreateFile(Name.c_str(),Access,ShareMode,NULL,OPEN_EXISTING,Flags,NULL);

  DWORD LastError;
  if (hNewFile==FILE_BAD_HANDLE)
  {
    LastError=GetLastError();

    std::wstring LongName;
    if (GetWinLongPath(Name,LongName))
    {
      hNewFile=CreateFile(LongName.c_str(),Access,ShareMode,NULL,OPEN_EXISTING,Flags,NULL);

      // For archive names longer than 260 characters first CreateFile
      // (without \\?\) fails and sets LastError to 3 (access denied).
      // We need the correct "file not found" error code to decide
      // if we create a new archive or quit with "cannot create" error.
      // So we need to check the error code after \\?\ CreateFile again,
      // otherwise we'll fail to create new archives with long names.
      // But we cannot simply assign the new code to LastError,
      // because it would break "..\arcname.rar" relative names processing.
      // First CreateFile returns the correct "file not found" code for such
      // names, but "\\?\" CreateFile returns ERROR_INVALID_NAME treating
      // dots as a directory name. So we check only for "file not found"
      // error here and for other errors use the first CreateFile result.
      if (GetLastError()==ERROR_FILE_NOT_FOUND)
        LastError=ERROR_FILE_NOT_FOUND;
    }
  }
  if (hNewFile==FILE_BAD_HANDLE && LastError==ERROR_FILE_NOT_FOUND)
    ErrorType=FILE_NOTFOUND;
  if (PreserveAtime && hNewFile!=FILE_BAD_HANDLE)
  {
    FILETIME ft={0xffffffff,0xffffffff}; // This value prevents atime modification.
    SetFileTime(hNewFile,NULL,&ft,NULL);
  }
#endif  // defined(CHROMIUM_UNRAR)

#else

#if defined(CHROMIUM_UNRAR)
  // Do not open a file handle since the sandbox doesn't allow it. Use the
  // handle provided by the caller.
  int handle = hOpenFile;
#else
  int flags=UpdateMode ? O_RDWR:(WriteMode ? O_WRONLY:O_RDONLY);
#ifdef O_BINARY
  flags|=O_BINARY;
#if defined(_AIX) && defined(_LARGE_FILE_API)
  flags|=O_LARGEFILE;
#endif
#endif
  // NDK r20 has O_NOATIME, but fails to create files with it in Android 7+.
#if defined(O_NOATIME)
  if (PreserveAtime)
    flags|=O_NOATIME;
#endif
  std::string NameA;
  WideToChar(Name,NameA);

  int handle=open(NameA.c_str(),flags);
#endif  // defined(CHROMIUM_UNRAR)

#ifdef LOCK_EX

#ifdef _OSF_SOURCE
  extern "C" int flock(int, int);
#endif
  if (!OpenShared && UpdateMode && handle>=0 && flock(handle,LOCK_EX|LOCK_NB)==-1)
  {
    close(handle);
    return false;
  }

#endif
  if (handle==-1)
    hNewFile=FILE_BAD_HANDLE;
  else
  {
#ifdef FILE_USE_OPEN
    hNewFile=handle;
#else
    hNewFile=fdopen(handle,UpdateMode ? UPDATEBINARY:READBINARY);
#endif
  }
  if (hNewFile==FILE_BAD_HANDLE && errno==ENOENT)
    ErrorType=FILE_NOTFOUND;
#endif
  NewFile=false;
  HandleType=FILE_HANDLENORMAL;
  SkipClose=false;
  bool Success=hNewFile!=FILE_BAD_HANDLE;
  if (Success)
  {
    hFile=hNewFile;
    FileName=Name;
    TruncatedAfterReadError=false;
  }
  return Success;
}


#if !defined(SFX_MODULE)
void File::TOpen(const std::wstring &Name)
{
  if (!WOpen(Name))
    ErrHandler.Exit(RARX_OPEN);
}
#endif


bool File::WOpen(const std::wstring &Name)
{
  if (Open(Name))
    return true;
  ErrHandler.OpenErrorMsg(Name);
  return false;
}


bool File::Create(const std::wstring &Name,uint Mode)
{
#if defined(CHROMIUM_UNRAR)
  // Since the Chromium sandbox does not allow the creation of files, use the
  // provided file.
  hFile = hOpenFile;
#else
  // OpenIndiana based NAS and CIFS shares fail to set the file time if file
  // was created in read+write mode and some data was written and not flushed
  // before SetFileTime call. So we should use the write only mode if we plan
  // SetFileTime call and do not need to read from file.
  bool WriteMode=(Mode & FMF_WRITE)!=0;
  bool ShareRead=(Mode & FMF_SHAREREAD)!=0 || File::OpenShared;
#ifdef _WIN_ALL
  CreateMode=Mode;
  uint Access=WriteMode ? GENERIC_WRITE:GENERIC_READ|GENERIC_WRITE;
  DWORD ShareMode=ShareRead ? FILE_SHARE_READ:0;

  // Windows automatically removes dots and spaces in the end of file name,
  // So we detect such names and process them with \\?\ prefix.
  wchar LastChar=GetLastChar(Name);
  bool Special=LastChar=='.' || LastChar==' ';
  
  if (Special && (Mode & FMF_STANDARDNAMES)==0)
    hFile=FILE_BAD_HANDLE;
  else
    hFile=CreateFile(Name.c_str(),Access,ShareMode,NULL,CREATE_ALWAYS,0,NULL);

  if (hFile==FILE_BAD_HANDLE)
  {
    std::wstring LongName;
    if (GetWinLongPath(Name,LongName))
      hFile=CreateFile(LongName.c_str(),Access,ShareMode,NULL,CREATE_ALWAYS,0,NULL);
  }

#else
#ifdef FILE_USE_OPEN
  std::string NameA;
  WideToChar(Name,NameA);
  hFile=open(NameA.c_str(),(O_CREAT|O_TRUNC) | (WriteMode ? O_WRONLY : O_RDWR),0666);
#else
  hFile=fopen(NameA.c_str(),WriteMode ? WRITEBINARY:CREATEBINARY);
#endif
#endif
#endif  // defined(CHROMIUM_UNRAR)
  NewFile=true;
  HandleType=FILE_HANDLENORMAL;
  SkipClose=false;
  FileName=Name;
  return hFile!=FILE_BAD_HANDLE;
}


#if !defined(SFX_MODULE)
void File::TCreate(const std::wstring &Name,uint Mode)
{
  if (!WCreate(Name,Mode))
    ErrHandler.Exit(RARX_FATAL);
}
#endif


bool File::WCreate(const std::wstring &Name,uint Mode)
{
  if (Create(Name,Mode))
    return true;
  ErrHandler.CreateErrorMsg(Name);
  return false;
}


bool File::Close()
{
  bool Success=true;

  if (hFile!=FILE_BAD_HANDLE)
  {
    if (!SkipClose)
    {
#if !defined(CHROMIUM_UNRAR)
// unrar should not close the file handle since it wasn't opened by unrar.
#ifdef _WIN_ALL
      // We use the standard system handle for stdout in Windows
      // and it must not be closed here.
      if (HandleType==FILE_HANDLENORMAL)
        Success=CloseHandle(hFile)!=FALSE;
#else
#ifdef FILE_USE_OPEN
      Success=close(hFile)!=-1;
#else
      Success=fclose(hFile)!=EOF;
#endif
#endif
#endif  // defined(CHROMIUM_UNRAR)
    }
    hFile=FILE_BAD_HANDLE;
  }
  HandleType=FILE_HANDLENORMAL;
  if (!Success && AllowExceptions)
    ErrHandler.CloseError(FileName);
  return Success;
}


bool File::Delete()
{
  if (HandleType!=FILE_HANDLENORMAL)
    return false;
  if (hFile!=FILE_BAD_HANDLE)
    Close();
  if (!AllowDelete)
    return false;
  return DelFile(FileName);
}


bool File::Rename(const std::wstring &NewName)
{
  // No need to rename if names are already same.
  bool Success=(NewName==FileName);

  if (!Success)
    Success=RenameFile(FileName,NewName);

  if (Success)
    FileName=NewName;

  return Success;
}


bool File::Write(const void *Data,size_t Size)
{
  if (Size==0)
    return true;
  if (HandleType==FILE_HANDLESTD)
  {
#ifdef _WIN_ALL
    hFile=GetStdHandle(STD_OUTPUT_HANDLE);
#else
    // Cannot use the standard stdout here, because it already has wide orientation.
    if (hFile==FILE_BAD_HANDLE)
    {
#ifdef FILE_USE_OPEN
      hFile=dup(STDOUT_FILENO); // Open new stdout stream.
#else
      hFile=fdopen(dup(STDOUT_FILENO),"w"); // Open new stdout stream.
#endif
    }
#endif
  }
  bool Success;
  while (1)
  {
    Success=false;
#ifdef _WIN_ALL
    DWORD Written=0;
    if (HandleType!=FILE_HANDLENORMAL)
    {
      // writing to stdout can fail in old Windows if data block is too large
      const size_t MaxSize=0x4000;
      for (size_t I=0;I<Size;I+=MaxSize)
      {
        Success=WriteFile(hFile,(byte *)Data+I,(DWORD)Min(Size-I,MaxSize),&Written,NULL)!=FALSE;
        if (!Success)
          break;
      }
    }
    else
      Success=WriteFile(hFile,Data,(DWORD)Size,&Written,NULL)!=FALSE;
#else
#ifdef FILE_USE_OPEN
    ssize_t Written=write(hFile,Data,Size);
    Success=Written==Size;
#else
    int Written=fwrite(Data,1,Size,hFile);
    Success=Written==Size && !ferror(hFile);
#endif
#endif
    if (!Success && AllowExceptions && HandleType==FILE_HANDLENORMAL)
    {
#if defined(_WIN_ALL) && !defined(SFX_MODULE) && !defined(RARDLL)
      int ErrCode=GetLastError();
      int64 FilePos=Tell();
      uint64 FreeSize=GetFreeDisk(FileName);
      SetLastError(ErrCode);
      if (FreeSize>Size && FilePos-Size<=0xffffffff && FilePos+Size>0xffffffff)
        ErrHandler.WriteErrorFAT(FileName);
#endif
      if (ErrHandler.AskRepeatWrite(FileName,false))
      {
#if !defined(_WIN_ALL) && !defined(FILE_USE_OPEN)
        clearerr(hFile);
#endif
        if (Written<Size && Written>0)
          Seek(Tell()-Written,SEEK_SET);
        continue;
      }
      ErrHandler.WriteError(L"",FileName);
    }
    break;
  }
  LastWrite=true;
  return Success; // It can return false only if AllowExceptions is disabled.
}


int File::Read(void *Data,size_t Size)
{
  if (TruncatedAfterReadError)
    return 0;

  int64 FilePos=0; // Initialized only to suppress some compilers warning.

  if (ReadErrorMode==FREM_IGNORE)
    FilePos=Tell();
  int TotalRead=0;
  while (true)
  {
    int ReadSize=DirectRead(Data,Size);

    if (ReadSize==-1)
    {
      ErrorType=FILE_READERROR;
      if (AllowExceptions)
        if (ReadErrorMode==FREM_IGNORE)
        {
          ReadSize=0;
          for (size_t I=0;I<Size;I+=512)
          {
            Seek(FilePos+I,SEEK_SET);
            size_t SizeToRead=Min(Size-I,512);
            int ReadCode=DirectRead(Data,SizeToRead);
            ReadSize+=(ReadCode==-1) ? 512:ReadCode;
            if (ReadSize!=-1)
              TotalRead+=ReadSize;
          }
        }
        else
        {
          bool Ignore=false,Retry=false,Quit=false;
          if (ReadErrorMode==FREM_ASK && HandleType==FILE_HANDLENORMAL && IsOpened())
          {
            ErrHandler.AskRepeatRead(FileName,Ignore,Retry,Quit);
            if (Retry)
              continue;
          }
          if (Ignore || ReadErrorMode==FREM_TRUNCATE)
          {
            TruncatedAfterReadError=true;
            return 0;
          }
          ErrHandler.ReadError(FileName);
        }
    }
    TotalRead+=ReadSize; // If ReadSize is -1, TotalRead is also set to -1 here.

    if (HandleType==FILE_HANDLESTD && !LineInput && ReadSize>0 && (uint)ReadSize<Size)
    {
      // Unlike regular files, for pipe we can read only as much as was
      // written at the other end of pipe. We had seen data coming in small
      // ~80 byte chunks when piping from 'type arc.rar'. Extraction code
      // would fail if we read an incomplete archive header from stdin.
      // So here we ensure that requested size is completely read.
      // But we return the available data immediately in "line input" mode,
      // when processing user's input in console prompts. Otherwise apps
      // piping user responses to multiple Ask() prompts can hang if no more
      // data is available yet and pipe isn't closed.
      Data=(byte*)Data+ReadSize;
      Size-=ReadSize;
      continue;
    }
    break;
  }
  if (TotalRead>0) // Can be -1 for error and AllowExceptions disabled.
    CurFilePos+=TotalRead;
  return TotalRead; // It can return -1 only if AllowExceptions is disabled.
}


// Returns -1 in case of error.
int File::DirectRead(void *Data,size_t Size)
{
#ifdef _WIN_ALL
  const size_t MaxDeviceRead=20000;
  const size_t MaxLockedRead=32768;
#endif
  if (HandleType==FILE_HANDLESTD)
  {
#ifdef _WIN_ALL
//    if (Size>MaxDeviceRead)
//      Size=MaxDeviceRead;
    hFile=GetStdHandle(STD_INPUT_HANDLE);
#else
#ifdef FILE_USE_OPEN
    hFile=STDIN_FILENO;
#else
    hFile=stdin;
#endif
#endif
  }
#ifdef _WIN_ALL
  // For pipes like 'type file.txt | rar -si arcname' ReadFile may return
  // data in small ~4KB blocks. It may slightly reduce the compression ratio.
  DWORD Read;
  if (!ReadFile(hFile,Data,(DWORD)Size,&Read,NULL))
  {
    if (IsDevice() && Size>MaxDeviceRead)
      return DirectRead(Data,MaxDeviceRead);
    if (HandleType==FILE_HANDLESTD && GetLastError()==ERROR_BROKEN_PIPE)
      return 0;

    // We had a bug report about failure to archive 1C database lock file
    // 1Cv8tmp.1CL, which is a zero length file with a region above 200 KB
    // permanently locked. If our first read request uses too large buffer
    // and if we are in -dh mode, so we were able to open the file,
    // we'll fail with "Read error". So now we use try a smaller buffer size
    // in case of lock error.
    if (HandleType==FILE_HANDLENORMAL && Size>MaxLockedRead &&
        GetLastError()==ERROR_LOCK_VIOLATION)
      return DirectRead(Data,MaxLockedRead);

    return -1;
  }
  return Read;
#else
#ifdef FILE_USE_OPEN
  ssize_t ReadSize=read(hFile,Data,Size);
  if (ReadSize==-1)
    return -1;
  return (int)ReadSize;
#else
  if (LastWrite)
  {
    fflush(hFile);
    LastWrite=false;
  }
  clearerr(hFile);
  size_t ReadSize=fread(Data,1,Size,hFile);
  if (ferror(hFile))
    return -1;
  return (int)ReadSize;
#endif
#endif
}


void File::Seek(int64 Offset,int Method)
{
  if (!RawSeek(Offset,Method) && AllowExceptions)
    ErrHandler.SeekError(FileName);
}


bool File::RawSeek(int64 Offset,int Method)
{
  if (hFile==FILE_BAD_HANDLE)
    return true;
  if (!IsSeekable()) // To extract archives from stdin with -si.
  {
    // We tried to dynamically allocate 32 KB buffer here, but it improved
    // speed in Windows 10 by mere ~1.5%.
    byte Buf[4096];
    if (Method==SEEK_CUR || Method==SEEK_SET && Offset>=CurFilePos)
    {
      uint64 SkipSize=Method==SEEK_CUR ? Offset:Offset-CurFilePos;
      while (SkipSize>0) // Reading to emulate seek forward.
      {
        int ReadSize=Read(Buf,(size_t)Min(SkipSize,ASIZE(Buf)));
        if (ReadSize<=0)
          return false;
        SkipSize-=ReadSize;
        CurFilePos+=ReadSize;
      }
      return true;
    }
    // May need it in FileLength() in Archive::UnexpEndArcMsg() when unpacking
    // RAR 4.x archives without the end of archive block created with -en.
    if (Method==SEEK_END)
    {
      int ReadSize;
      while ((ReadSize=Read(Buf,ASIZE(Buf)))>0)
        CurFilePos+=ReadSize;
      return true;
    }

    return false; // Backward seek on unseekable file.
  }
  if (Offset<0 && Method!=SEEK_SET)
  {
    Offset=(Method==SEEK_CUR ? Tell():FileLength())+Offset;
    Method=SEEK_SET;
  }
#ifdef _WIN_ALL
  LONG HighDist=(LONG)(Offset>>32);
  if (SetFilePointer(hFile,(LONG)Offset,&HighDist,Method)==0xffffffff &&
      GetLastError()!=NO_ERROR)
    return false;
#else
  LastWrite=false;
#ifdef FILE_USE_OPEN
  if (lseek(hFile,(off_t)Offset,Method)==-1)
    return false;
#elif defined(_LARGEFILE_SOURCE) && !defined(_OSF_SOURCE) && !defined(__VMS)
  if (fseeko(hFile,Offset,Method)!=0)
    return false;
#else
  if (fseek(hFile,(long)Offset,Method)!=0)
    return false;
#endif
#endif
  return true;
}


int64 File::Tell()
{
  if (hFile==FILE_BAD_HANDLE)
    if (AllowExceptions)
      ErrHandler.SeekError(FileName);
    else
      return -1;
  if (!IsSeekable())
    return CurFilePos;
#ifdef _WIN_ALL
  LONG HighDist=0;
  uint LowDist=SetFilePointer(hFile,0,&HighDist,FILE_CURRENT);
  if (LowDist==0xffffffff && GetLastError()!=NO_ERROR)
    if (AllowExceptions)
      ErrHandler.SeekError(FileName);
    else
      return -1;
  return INT32TO64(HighDist,LowDist);
#else
#ifdef FILE_USE_OPEN
  return lseek(hFile,0,SEEK_CUR);
#elif defined(_LARGEFILE_SOURCE) && !defined(_OSF_SOURCE)
  return ftello(hFile);
#else
  return ftell(hFile);
#endif
#endif
}


void File::Prealloc(int64 Size)
{
#ifdef _WIN_ALL
  if (RawSeek(Size,SEEK_SET))
  {
    Truncate();
    Seek(0,SEEK_SET);
  }
#endif

#if defined(_UNIX) && defined(USE_FALLOCATE)
  // fallocate is rather new call. Only latest kernels support it.
  // So we are not using it by default yet.
  int fd = GetFD();
  if (fd >= 0)
    fallocate(fd, 0, 0, Size);
#endif
}


byte File::GetByte()
{
  byte Byte=0;
  Read(&Byte,1);
  return Byte;
}


void File::PutByte(byte Byte)
{
  Write(&Byte,1);
}


bool File::Truncate()
{
#ifdef _WIN_ALL
  return SetEndOfFile(hFile)!=FALSE;
#else
  return ftruncate(GetFD(),(off_t)Tell())==0;
#endif
}


void File::Flush()
{
#ifdef _WIN_ALL
  FlushFileBuffers(hFile);
#else
#ifndef FILE_USE_OPEN
  fflush(hFile);
#endif
  fsync(GetFD());
#endif
}


void File::SetOpenFileTime(RarTime *ftm,RarTime *ftc,RarTime *fta)
{
#ifdef _WIN_ALL
  // Workaround for OpenIndiana NAS time bug. If we cannot create a file
  // in write only mode, we need to flush the write buffer before calling
  // SetFileTime or file time will not be changed.
  if (CreateMode!=FMF_UNDEFINED && (CreateMode & FMF_WRITE)==0)
    FlushFileBuffers(hFile);

  bool sm=ftm!=NULL && ftm->IsSet();
  bool sc=ftc!=NULL && ftc->IsSet();
  bool sa=fta!=NULL && fta->IsSet();
  FILETIME fm,fc,fa;
  if (sm)
    ftm->GetWinFT(&fm);
  if (sc)
    ftc->GetWinFT(&fc);
  if (sa)
    fta->GetWinFT(&fa);
  SetFileTime(hFile,sc ? &fc:NULL,sa ? &fa:NULL,sm ? &fm:NULL);
#endif
}


void File::SetCloseFileTime(RarTime *ftm,RarTime *fta)
{
// Android APP_PLATFORM := android-14 does not support futimens and futimes.
// Newer platforms support futimens, but fail on Android 4.2.
// We have to use utime for Android.
// Also we noticed futimens fail to set timestamps on NTFS partition
// mounted to virtual Linux x86 machine, but utimensat worked correctly.
// So we set timestamps for already closed files in Unix.
#ifdef _UNIX
  SetCloseFileTimeByName(FileName,ftm,fta);
#endif
}


void File::SetCloseFileTimeByName(const std::wstring &Name,RarTime *ftm,RarTime *fta)
{
#ifdef _UNIX
  bool setm=ftm!=NULL && ftm->IsSet();
  bool seta=fta!=NULL && fta->IsSet();
  if (setm || seta)
  {
    std::string NameA;
    WideToChar(Name,NameA);

#ifdef UNIX_TIME_NS
    timespec times[2];
    times[0].tv_sec=seta ? fta->GetUnix() : 0;
    times[0].tv_nsec=seta ? long(fta->GetUnixNS()%1000000000) : UTIME_NOW;
    times[1].tv_sec=setm ? ftm->GetUnix() : 0;
    times[1].tv_nsec=setm ? long(ftm->GetUnixNS()%1000000000) : UTIME_NOW;
    utimensat(AT_FDCWD,NameA.c_str(),times,0);
#else
    utimbuf ut;
    if (setm)
      ut.modtime=ftm->GetUnix();
    else
      ut.modtime=fta->GetUnix(); // Need to set something, cannot left it 0.
    if (seta)
      ut.actime=fta->GetUnix();
    else
      ut.actime=ut.modtime; // Need to set something, cannot left it 0.
    utime(NameA.c_str(),&ut);
#endif
  }
#endif
}


#ifdef _UNIX
void File::StatToRarTime(struct stat &st,RarTime *ftm,RarTime *ftc,RarTime *fta)
{
#ifdef UNIX_TIME_NS
#if defined(_APPLE)
  if (ftm!=NULL) ftm->SetUnixNS(st.st_mtimespec.tv_sec*(uint64)1000000000+st.st_mtimespec.tv_nsec);
  if (ftc!=NULL) ftc->SetUnixNS(st.st_ctimespec.tv_sec*(uint64)1000000000+st.st_ctimespec.tv_nsec);
  if (fta!=NULL) fta->SetUnixNS(st.st_atimespec.tv_sec*(uint64)1000000000+st.st_atimespec.tv_nsec);
#else
  if (ftm!=NULL) ftm->SetUnixNS(st.st_mtim.tv_sec*(uint64)1000000000+st.st_mtim.tv_nsec);
  if (ftc!=NULL) ftc->SetUnixNS(st.st_ctim.tv_sec*(uint64)1000000000+st.st_ctim.tv_nsec);
  if (fta!=NULL) fta->SetUnixNS(st.st_atim.tv_sec*(uint64)1000000000+st.st_atim.tv_nsec);
#endif
#else
  if (ftm!=NULL) ftm->SetUnix(st.st_mtime);
  if (ftc!=NULL) ftc->SetUnix(st.st_ctime);
  if (fta!=NULL) fta->SetUnix(st.st_atime);
#endif
}
#endif


void File::GetOpenFileTime(RarTime *ftm,RarTime *ftc,RarTime *fta)
{
#ifdef _WIN_ALL
  FILETIME ctime,atime,mtime;
  GetFileTime(hFile,&ctime,&atime,&mtime);
  if (ftm!=NULL) ftm->SetWinFT(&mtime);
  if (ftc!=NULL) ftc->SetWinFT(&ctime);
  if (fta!=NULL) fta->SetWinFT(&atime);
#elif defined(_UNIX)
  struct stat st;
  fstat(GetFD(),&st);
  StatToRarTime(st,ftm,ftc,fta);
#endif
}


int64 File::FileLength()
{
  int64 SavePos=Tell();
  Seek(0,SEEK_END);
  int64 Length=Tell();
  Seek(SavePos,SEEK_SET);
  return Length;
}


bool File::IsDevice()
{
  if (hFile==FILE_BAD_HANDLE)
    return false;
#ifdef _WIN_ALL
  uint Type=GetFileType(hFile);
  return Type==FILE_TYPE_CHAR || Type==FILE_TYPE_PIPE;
#else
  return isatty(GetFD());
#endif
}


#ifndef SFX_MODULE
int64 File::Copy(File &Dest,int64 Length)
{
  bool CopyAll=(Length==INT64NDF);

  // Adjust the buffer size to data size. So we do not waste too much time
  // to vector initialization when copying many small data blocks like
  // when updating an archive with many small files.
  size_t BufSize=File::CopyBufferSize();
  if (!CopyAll && Length<(int64)BufSize)
    BufSize=(size_t)Length;

  std::vector<byte> Buffer(BufSize);
  int64 CopySize=0;

  while (CopyAll || Length>0)
  {
    Wait();
    size_t SizeToRead=(!CopyAll && Length<(int64)Buffer.size()) ? (size_t)Length:Buffer.size();
    byte *Buf=Buffer.data();
    int ReadSize=Read(Buf,SizeToRead);
    if (ReadSize==0)
      break;
    size_t WriteSize=ReadSize;
#ifdef _WIN_ALL
    // For FAT32 USB flash drives in Windows if first write is 4 KB or more,
    // write caching is disabled and "write through" is enabled, resulting
    // in bad performance, especially for many small files. It happens when
    // we create SFX archive on USB drive, because SFX module is written first.
    // So we split the first write to small 1 KB followed by rest of data.
    if (CopySize==0 && WriteSize>=4096)
    {
      const size_t FirstWrite=1024;
      Dest.Write(Buf,FirstWrite);
      Buf+=FirstWrite;
      WriteSize-=FirstWrite;
    }
#endif
    Dest.Write(Buf,WriteSize);
    CopySize+=ReadSize;
    if (!CopyAll)
      Length-=ReadSize;
  }
  return CopySize;
}
#endif

#if defined(CHROMIUM_UNRAR)
void File::SetFileHandle(FileHandle hF) {
  hOpenFile = hF;
}
#endif  // defined(CHROMIUM_UNRAR)
