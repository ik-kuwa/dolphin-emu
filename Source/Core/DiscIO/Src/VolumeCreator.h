// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#ifndef _VOLUME_CREATOR_H
#define _VOLUME_CREATOR_H

#include "Volume.h"
#include "UtilityFuncs.h"

namespace DiscIO
{
std::unique_ptr<IVolume> CreateVolumeFromFilename(const std::string& _rFilename, u32 _PartitionGroup = 0, u32 _VolumeNum = -1);
std::unique_ptr<IVolume> CreateVolumeFromDirectory(const std::string& _rDirectory, bool _bIsWii, const std::string& _rApploader = "", const std::string& _rDOL = "");
bool IsVolumeWiiDisc(const IVolume& _rVolume);
bool IsVolumeWadFile(const IVolume& _rVolume);
} // namespace

#endif

