/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

/*!
\file guiImage.h
\brief
*/

#include "GUIControl.h"
#include "GUITexture.h"
#include "ImageSettings.h"
#include "guilib/guiinfo/GUIInfoLabel.h"

#include <vector>

/*!
 \ingroup controls
 \brief
 */

class CGUIImage : public CGUIControl
{
public:
  class CFadingTexture
  {
  public:
    CFadingTexture(const CGUITexture* texture, unsigned int fadeTime)
    {
      // create a copy of our texture, and allocate resources
      m_texture.reset(texture->Clone());
      m_texture->AllocResources();
      m_fadeTime = fadeTime;
      m_fading = false;
    };
    ~CFadingTexture()
    {
      m_texture->FreeResources();
    };

    std::unique_ptr<CGUITexture> m_texture; ///< texture to fade out
    unsigned int m_fadeTime; ///< time to fade out (ms)
    bool         m_fading;   ///< whether we're fading out

  private:
    CFadingTexture(const CFadingTexture&) = delete;
    CFadingTexture& operator=(const CFadingTexture&) = delete;
  };

  CGUIImage(int parentID, int controlID, float posX, float posY, float width, float height, const CTextureInfo& texture);
  CGUIImage(const CGUIImage &left);
  ~CGUIImage(void) override;
  CGUIImage* Clone() const override { return new CGUIImage(*this); }

  void Process(unsigned int currentTime, CDirtyRegionList &dirtyregions) override;
  void Render() override;
  void UpdateVisibility(const CGUIListItem *item = NULL) override;
  bool OnAction(const CAction &action) override ;
  bool OnMessage(CGUIMessage& message) override;
  void AllocResources() override;
  void FreeResources(bool immediately = false) override;
  void DynamicResourceAlloc(bool bOnOff) override;
  bool IsDynamicallyAllocated() override { return m_bDynamicResourceAlloc; }
  void SetInvalid() override;
  bool CanFocus() const override;
  void UpdateInfo(const CGUIListItem *item = NULL) override;

  virtual void SetInfo(const KODI::GUILIB::GUIINFO::CGUIInfoLabel &info);
  virtual void SetFileName(const std::string& strFileName, bool setConstant = false, const bool useCache = true);
  virtual void SetAspectRatio(const CAspectRatio &aspect);
  virtual void SetScalingMethod(TEXTURE_SCALING scalingMethod);
  virtual void SetDiffuseScalingMethod(TEXTURE_SCALING scalingMethod);
  void SetWidth(float width) override;
  void SetHeight(float height) override;
  void SetPosition(float posX, float posY) override;
  std::string GetDescription() const override;
  void SetCrossFade(unsigned int time);
  void SetImageFilter(const KODI::GUILIB::GUIINFO::CGUIInfoLabel& imageFilter);
  void SetDiffuseFilter(const KODI::GUILIB::GUIINFO::CGUIInfoLabel& diffuseFilter);

  const std::string& GetFileName() const;
  float GetTextureWidth() const;
  float GetTextureHeight() const;

  CRect CalcRenderRegion() const override;

#ifdef _DEBUG
  void DumpTextureUse() override;
#endif
protected:
  virtual void AllocateOnDemand();
  virtual void FreeTextures(bool immediately = false);
  void FreeResourcesButNotAnims();
  unsigned char GetFadeLevel(unsigned int time) const;
  std::string GetFallback(const std::string& currentName);
  void ProcessState();
  void ProcessAllocation();
  void ProcessNoTransition(unsigned int currentTime);
  void ProcessInstantTransition(unsigned int currentTime);
  void ProcessFadingTransition(unsigned int currentTime);

  /*!
   * \brief Update the diffuse color based on the current item infos
   * \param item the item to for info resolution
  */
  void UpdateDiffuseColor(const CGUIListItem* item);

  void UpdateImageFilter(KODI::GUILIB::IMAGE_FILTER imageFilter);
  void UpdateDiffuseFilter(KODI::GUILIB::IMAGE_FILTER diffuseFilter);

  bool m_bDynamicResourceAlloc;

  // border + conditional info
  CTextureInfo m_image;
  KODI::GUILIB::GUIINFO::CGUIInfoLabel m_info;

  bool m_isTransitioning{false};
  bool m_hasNewStagingTexture{false};

  std::unique_ptr<CGUITexture> m_textureCurrent;
  std::unique_ptr<CGUITexture> m_textureNext;

  std::string m_nameCurrent{};
  std::string m_nameNext{};
  std::string m_nameStaging{};

  std::string m_currentFallback;

  unsigned int m_crossFadeTime;
  unsigned int m_currentFadeTime;
  unsigned int m_lastRenderTime;

  KODI::GUILIB::GUIINFO::CGUIInfoLabel m_imageFilterInfo;
  KODI::GUILIB::IMAGE_FILTER m_imageFilter{KODI::GUILIB::IMAGE_FILTER::UNKNOWN};

  KODI::GUILIB::GUIINFO::CGUIInfoLabel m_diffuseFilterInfo;
  KODI::GUILIB::IMAGE_FILTER m_diffuseFilter{KODI::GUILIB::IMAGE_FILTER::UNKNOWN};
};
