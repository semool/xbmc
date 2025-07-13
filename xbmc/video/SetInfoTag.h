/*
 *  Copyright (C) 2024-2025 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "VideoThumbLoader.h"

#include <string>

class TiXmlNode;
class TiXmlElement;

class CSetInfoTag : public IArchivable, public ISerializable
{
public:
  CSetInfoTag() { Reset(); }
  void Reset();
  /* \brief Load information to a setinfotag from an XML element

   \param element    the root XML element to parse.
   \param append     whether information should be added to the existing tag, or whether it should be reset first.
   \param prioritise if appending, whether additive tags should be prioritised (i.e. replace or prepend) over existing values. Defaults to false.

   \sa ParseNative
   */
  bool Load(const TiXmlElement* element, bool append = false, bool prioritise = false);
  bool IsEmpty() const;

  int GetID() const { return m_id; }
  void SetID(int id) { m_id = id; }

  void SetOverview(const std::string& overview);
  bool HasOverview() const { return !m_overview.empty(); }
  std::string GetOverview() const { return m_overview; }
  bool GetUpdateSetOverview() const { return m_updateSetOverview; }

  void SetTitle(const std::string& title);
  bool HasTitle() const { return !m_title.empty(); }
  std::string GetTitle() const { return m_title; }

  void SetOriginalTitle(const std::string& title);
  bool HasOriginalTitle() const { return !m_originalTitle.empty(); }
  std::string GetOriginalTitle() const { return m_originalTitle; }

  void SetArt(const KODI::ART::Artwork& art);
  bool HasArt() const { return !m_art.empty(); }
  KODI::ART::Artwork GetArt() const { return m_art; }

  void Merge(const CSetInfoTag& other);
  void Copy(const CSetInfoTag& other);
  bool Save(TiXmlNode* node,
            const std::string& tag,
            bool savePathInfo = true,
            const TiXmlElement* additionalNode = nullptr);
  void Archive(CArchive& ar) override;
  void Serialize(CVariant& value) const override;

  std::string m_title; // Title of the movie set
  std::string m_originalTitle; // Original title of movie set (from scraper)
  std::string m_overview; // Overview/description of the movie set
  KODI::ART::Artwork m_art; // Art information
  int m_id{-1}; // ID of movie set in database

private:
  bool m_updateSetOverview{false}; // If overview has been set

  /* \brief Parse our native XML format for video info.
   See Load for a description of the available tag types.

   \param element    the root XML element to parse.
   \param prioritise whether additive tags should be replaced (or prepended) by the content of the tags, or appended to.
   \sa Load
   */
  void ParseNative(const TiXmlElement* set, bool prioritise);
};
