// Copyright by Contributors
#define _CRT_SECURE_NO_WARNINGS

#include <dmlc/logging.h>
#include <errno.h>
extern "C" {
#include <sys/stat.h>
}
#ifndef _MSC_VER
extern "C" {
#include <sys/types.h>
#include <dirent.h>
}
#else
#include <Windows.h>
#define stat _stat64
#endif

#include "./local_filesys.h"

namespace dmlc {
namespace io {
/*! \brief implementation of file i/o stream */
class FileStream : public SeekStream {
 public:
  explicit FileStream(FILE *fp, bool use_stdio)
      : fp_(fp), use_stdio_(use_stdio) {}
  virtual ~FileStream(void) {
    this->Close();
  }
  virtual size_t Read(void *ptr, size_t size) {
    return std::fread(ptr, 1, size, fp_);
  }
  virtual void Write(const void *ptr, size_t size) {
    CHECK(std::fwrite(ptr, 1, size, fp_) == size)
        << "FileStream.Write incomplete";
  }
  virtual void Seek(size_t pos) {
    std::fseek(fp_, static_cast<long>(pos), SEEK_SET);  // NOLINT(*)
  }
  virtual size_t Tell(void) {
    return std::ftell(fp_);
  }
  virtual bool AtEnd(void) const {
    return std::feof(fp_) != 0;
  }
  inline void Close(void) {
    if (fp_ != NULL && !use_stdio_) {
      std::fclose(fp_); fp_ = NULL;
    }
  }

 private:
  std::FILE *fp_;
  bool use_stdio_;
};

int LocalFileSystem::CreateDirectory(const URI &path) {
  struct stat st;
  int status = 0;
  //umask(S_IWGRP | S_IWOTH);
  if (stat(path.name.c_str(), &st) != 0)
  {
    int err = errno;
    LOG(INFO) << "LocalFileSystem.CreateDirectory " 
              << "Stat says: " << path.name << strerror(err);
  	//Directory does not exist. EEXIST for race condition.	
  	if (mkdir(path.name.c_str(), ACCESSPERMS) != 0 && errno != EEXIST) { //0666 hard coded.
  	  status = -1;
  	  int errsv = errno;
  	  LOG(FATAL) << "LocalFileSystem.CreateDirectory " 
  	               << " Error, Race Condition: " << path.name << strerror(errsv);
  	}
    else if (errno == EEXIST)
    {
      int errsv = errno;
      LOG(FATAL) << "LocalFileSystem.CreateDirectory " 
                    << " Error, the thing is therr: " << path.name << strerror(errsv);
    }
    else {
      LOG(INFO) << "CreateDirectory " 
              << " Succeeded." << path.name; 
      //chmod(path.name.c_str(), DEFFILEMODE);
    }
  }
  else if (!S_ISDIR(st.st_mode))
  {
  	errno = ENOTDIR;
	  status = -1;
    int errsv = errno;
    LOG(FATAL) << "LocalFileSystem.CreateDirectory " << path.name
               << " Error, Not a Directory: " << strerror(errsv);
  }
  return (status);
}

int LocalFileSystem::DeleteDirectory(const URI &path) {
  std::string command = "rm -rf ";
  std::string dir = path.name;
  command += dir;
  return system( command.c_str() );
}


FileInfo LocalFileSystem::GetPathInfo(const URI &path) {
  struct stat sb;
  if (stat(path.name.c_str(), &sb) == -1) {
    int errsv = errno;
//    LOG(INFO) << "LocalFileSystem.GetPathInfo " << path.name
//               << ":" << strerror(errsv);
    FileInfo ret;
    ret.path = path;
    ret.size = 0;
    ret.type = kNonExist;
    return ret;
  }
  FileInfo ret;
  ret.path = path;
  ret.size = sb.st_size;

  if ((sb.st_mode & S_IFMT) == S_IFDIR) {
    ret.type = kDirectory;
  } else {
    ret.type = kFile;
  }
  return ret;
}

void LocalFileSystem::ListDirectory(const URI &path, std::vector<FileInfo> *out_list) {
#ifndef _MSC_VER
  DIR *dir = opendir(path.name.c_str());
  if (dir == NULL) {
    int errsv = errno;
    LOG(FATAL) << "LocalFileSystem.ListDirectory " << path.str()
               <<" error: " << strerror(errsv);
  }
  out_list->clear();
  struct dirent *ent;
  /* print all the files and directories within directory */
  while ((ent = readdir(dir)) != NULL) {
    if (!strcmp(ent->d_name, ".")) continue;
    if (!strcmp(ent->d_name, "..")) continue;
    URI pp = path;
    if (pp.name[pp.name.length() - 1] != '/') {
      pp.name += '/';
    }
    pp.name += ent->d_name;
    out_list->push_back(GetPathInfo(pp));
  }
  closedir(dir);
#else
  WIN32_FIND_DATA fd;
  std::string pattern = path.name + "/*";
  HANDLE handle = FindFirstFile(pattern.c_str(), &fd);
  if (handle == INVALID_HANDLE_VALUE) {
    int errsv = GetLastError();
    LOG(FATAL) << "LocalFileSystem.ListDirectory " << path.str()
               << " error: " << strerror(errsv);
  }
  do {
    if (strcmp(fd.cFileName, ".") && strcmp(fd.cFileName, "..")) {
      URI pp = path;
      char clast = pp.name[pp.name.length() - 1];
      if (pp.name == ".") {
        pp.name = fd.cFileName;
      } else if (clast != '/' && clast != '\\') {
        pp.name += '/';
        pp.name += fd.cFileName;
      }
      out_list->push_back(GetPathInfo(pp));
    }
  }  while (FindNextFile(handle, &fd));
  FindClose(handle);
#endif
}

SeekStream *LocalFileSystem::Open(const URI &path,
                                  const char* const mode,
                                  bool allow_null) {
  bool use_stdio = false;
  FILE *fp = NULL;
  const char *fname = path.name.c_str();
  using namespace std;
#ifndef DMLC_STRICT_CXX98_
  if (!strcmp(fname, "stdin")) {
    use_stdio = true; fp = stdin;
  }
  if (!strcmp(fname, "stdout")) {
    use_stdio = true; fp = stdout;
  }
#endif
  if (!strncmp(fname, "file://", 7)) fname += 7;
  if (!use_stdio) {
    std::string flag = mode;
    if (flag == "w") flag = "wb";
    if (flag == "r") flag = "rb";
    fp = fopen64(fname, flag.c_str());
  }
  if (fp != NULL) {
    return new FileStream(fp, use_stdio);
  } else {
    CHECK(allow_null) << " LocalFileSystem: fail to open " << path.str();
    return NULL;
  }
}
SeekStream *LocalFileSystem::OpenForRead(const URI &path, bool allow_null) {
  return Open(path, "r", allow_null);
}
}  // namespace io
}  // namespace dmlc
