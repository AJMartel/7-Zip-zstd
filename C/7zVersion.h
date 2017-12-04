#define MY_VER_MAJOR 17
#define MY_VER_MINOR 01
#define MY_VER_BUILD 0
#define MY_VERSION_NUMBERS "17.01 ZS v1.3.2 R1"
#define MY_VERSION MY_VERSION_NUMBERS

#ifdef MY_CPU_NAME
  #define MY_VERSION_CPU MY_VERSION " (" MY_CPU_NAME ")"
#else
  #define MY_VERSION_CPU MY_VERSION
#endif

#define MY_DATE "2017-10-31"
#undef MY_COPYRIGHT
#undef MY_VERSION_COPYRIGHT_DATE
#define MY_AUTHOR_NAME "Igor Pavlov, Tino Reichardt"
#define MY_COPYRIGHT_PD "Igor Pavlov : Public domain"
#define MY_COPYRIGHT_CR "Copyright (c) 1999-2017 Igor Pavlov"

#ifdef USE_COPYRIGHT_CR
  #define MY_COPYRIGHT MY_COPYRIGHT_CR
#else
  #define MY_COPYRIGHT MY_COPYRIGHT_PD
#endif

#define MY_COPYRIGHT_DATE MY_COPYRIGHT " : " MY_DATE
#define MY_VERSION_COPYRIGHT_DATE MY_VERSION_CPU " : " MY_COPYRIGHT " : " MY_DATE
