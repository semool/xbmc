/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "BlurayFile.h"

#include "URL.h"
#include "utils/URIUtils.h"

#include <assert.h>

using namespace XFILE;

CBlurayFile::CBlurayFile(void) : COverrideFile(false)
{
}

CBlurayFile::~CBlurayFile(void) = default;

std::string CBlurayFile::TranslatePath(const CURL& url)
{
  assert(url.IsProtocol("bluray"));

  std::string host = url.GetHostName();
  const std::string& filename = url.GetFileName();
  if (host.empty() || filename.empty())
    return "";

  return host.append(filename);
}

bool CBlurayFile::Exists(const CURL& url)
{
  if (url.GetFileName() == "menu")
    return CFile::Exists(URIUtils::GetBlurayFile(url.Get()));
  return CFile::Exists(TranslatePath(url));
}
