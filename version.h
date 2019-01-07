#define STRINGIZE2(s) #s
#define STRINGIZE(s) STRINGIZE2(s)

#define VERSION_MAJOR 1
#define VERSION_MINOR 2
#define VERSION_REVISION 9
#define VERSION_BUILD 0

#define VERSION_NUMERIC                                                        \
  ((VERSION_MAJOR << 24) | (VERSION_MINOR << 16) | (VERSION_REVISION << 8) |   \
   (VERSION_BUILD << 0))

#define VER_TAGLINE_STR "...it's too windy in here"
#define VER_FILE_DESCRIPTION_STR "Windy " VER_TAGLINE_STR
#define VER_COMPANY_NAME_STR "Wade Brainerd"

#define VER_FILE_VERSION                                                       \
  VERSION_MAJOR, VERSION_MINOR, VERSION_REVISION, VERSION_BUILD
#define VER_FILE_VERSION_STR                                                   \
  STRINGIZE(VERSION_MAJOR)                                                     \
  "." STRINGIZE(VERSION_MINOR) "." STRINGIZE(VERSION_REVISION) "." STRINGIZE(  \
      VERSION_BUILD)

#define VER_PRODUCTNAME_STR "Windy"
#define VER_PRODUCT_VERSION VER_FILE_VERSION
#define VER_PRODUCT_VERSION_STR VER_FILE_VERSION_STR
#define VER_COPYRIGHT_STR "Copyright 2018 by Wade Brainerd"

#ifdef _DEBUG
#define VER_VER_DEBUG VS_FF_DEBUG
#else
#define VER_VER_DEBUG 0
#endif

#define VER_FILEOS VOS_NT_WINDOWS32
#define VER_FILEFLAGS VER_VER_DEBUG
#define VER_FILETYPE VFT_APP
