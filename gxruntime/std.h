#ifndef STD_H
#define STD_H

#include "../config/config.h"
#include "../stdutil/stdutil.h"
#include "../bbruntime/constants.h"

#pragma warning( disable:4786 )

#define DIRECTSOUND_VERSION 0x0700 // TODO: Not sure if we need make any changes to this version, likely not /shrugs
#define DIRECT3D_VERSION 0x0900 // Likely don't need to define it, but sometimes it's for the best
#define NOMINMAX // stupid microsoft

#include <set>
#include <map>
#include <list>
#include <string>
#include <vector>
#include <fstream>
#include <iostream>

#include <math.h>
#include <Windows.h>
//#include <ddraw.h> // Why are we still here? Just to suffer?
#include <d3d9.h>
#include <d3dx9.h>

#endif