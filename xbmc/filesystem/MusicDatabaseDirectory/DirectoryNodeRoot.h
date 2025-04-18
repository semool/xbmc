/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "DirectoryNode.h"

namespace XFILE
{
  namespace MUSICDATABASEDIRECTORY
  {
    class CDirectoryNodeRoot : public CDirectoryNode
    {
    public:
      CDirectoryNodeRoot(const std::string& strName, CDirectoryNode* pParent);
    protected:
      NodeType GetChildType() const override;
    };
  }
}


