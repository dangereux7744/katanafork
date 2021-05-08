/*
  File: cxxutil.cpp
  Author: James Oakley
  Copyright (C): 2010 James Oakley
  License: Katana is free software: you may redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation, either version 2 of the
  License, or (at your option) any later version.

  This file was not written while under employment by Dartmouth
  College and the attribution requirements on the rest of Katana do
  not apply to code taken from this file.
  Project:  katana
  Date: December 2010
  Description: Utility methods that need to be implemented in C++
*/

extern "C"
{
#include "util.h"
}

#include "cxxutil.h"
#include <cstdlib>
#include <cxxabi.h>

//C++ name demangling
//the returned string should be free'd
char* demangleName(char* mangledName)
{
  int status;
  char* demangled=abi::__cxa_demangle(mangledName,NULL,NULL,&status);
  if(status)
  {
    death("c++ name demangling failed\n");
  }
  return demangled;

}

