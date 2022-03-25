/*
 *      Initial code sponsored by: Voddler Inc (voddler.com)
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "OverlayRenderer.h"

#include "Application.h"
#include "OverlayRendererUtil.h"
#include "ServiceBroker.h"
#include "cores/VideoPlayer/DVDCodecs/Overlay/DVDOverlay.h"
#include "cores/VideoPlayer/DVDCodecs/Overlay/DVDOverlayImage.h"
#include "cores/VideoPlayer/DVDCodecs/Overlay/DVDOverlayLibass.h"
#include "cores/VideoPlayer/DVDCodecs/Overlay/DVDOverlaySSA.h"
#include "cores/VideoPlayer/DVDCodecs/Overlay/DVDOverlaySpu.h"
#include "cores/VideoPlayer/DVDCodecs/Overlay/DVDOverlayText.h"
#include "cores/VideoPlayer/VideoRenderers/RenderManager.h"
#include "guilib/GUIFont.h"
#include "settings/AdvancedSettings.h"
#include "settings/Settings.h"
#include "settings/SettingsComponent.h"
#include "settings/SubtitlesSettings.h"
#include "utils/ColorUtils.h"
#include "windowing/GraphicContext.h"

#include <mutex>
#if defined(HAS_GL) || defined(HAS_GLES)
#include "OverlayRendererGL.h"
#elif defined(HAS_DX)
#include "OverlayRendererDX.h"
#endif

#include <algorithm>

using namespace OVERLAY;

COverlay::COverlay()
{
  m_x = 0.0f;
  m_y = 0.0f;
  m_width = 0.0f;
  m_height = 0.0f;
  m_type = TYPE_NONE;
  m_align = ALIGN_SCREEN;
  m_pos = POSITION_RELATIVE;
}

COverlay::~COverlay() = default;

unsigned int CRenderer::m_textureid = 1;

CRenderer::CRenderer()
{
  KODI::SUBTITLES::CSubtitlesSettings::GetInstance().RegisterObserver(this);
}

CRenderer::~CRenderer()
{
  KODI::SUBTITLES::CSubtitlesSettings::GetInstance().UnregisterObserver(this);
  Flush();
}

void CRenderer::AddOverlay(CDVDOverlay* o, double pts, int index)
{
  std::unique_lock<CCriticalSection> lock(m_section);

  SElement   e;
  e.pts = pts;
  e.overlay_dvd = o->Acquire();
  m_buffers[index].push_back(e);
}

void CRenderer::Release(std::vector<SElement>& list)
{
  std::vector<SElement> l = list;
  list.clear();

  for (auto &elem : l)
  {
    if (elem.overlay_dvd)
      elem.overlay_dvd->Release();
  }
}

void CRenderer::Flush()
{
  std::unique_lock<CCriticalSection> lock(m_section);

  for(std::vector<SElement>& buffer : m_buffers)
    Release(buffer);

  ReleaseCache();
}

void CRenderer::Release(int idx)
{
  std::unique_lock<CCriticalSection> lock(m_section);
  Release(m_buffers[idx]);
}

void CRenderer::ReleaseCache()
{
  for (auto& overlay : m_textureCache)
  {
    delete overlay.second;
  }
  m_textureCache.clear();
  m_textureid++;
}

void CRenderer::ReleaseUnused()
{
  for (auto it = m_textureCache.begin(); it != m_textureCache.end(); )
  {
    bool found = false;
    for (auto& buffer : m_buffers)
    {
      for (auto& dvdoverlay : buffer)
      {
        if (dvdoverlay.overlay_dvd && dvdoverlay.overlay_dvd->m_textureid == it->first)
        {
          found = true;
          break;
        }
      }
      if (found)
        break;
    }
    if (!found)
    {
      delete it->second;
      it = m_textureCache.erase(it);
    }
    else
      ++it;
  }
}

void CRenderer::Render(int idx)
{
  std::unique_lock<CCriticalSection> lock(m_section);

  std::vector<SElement>& list = m_buffers[idx];
  for(std::vector<SElement>::iterator it = list.begin(); it != list.end(); ++it)
  {
    if (it->overlay_dvd)
    {
      COverlay* o = Convert(it->overlay_dvd, it->pts);

      if (o)
        Render(o);
    }
  }

  ReleaseUnused();
}

void CRenderer::Render(COverlay* o)
{
  SRenderState state;
  state.x = o->m_x;
  state.y = o->m_y;
  state.width = o->m_width;
  state.height = o->m_height;

  COverlay::EPosition pos = o->m_pos;
  COverlay::EAlign align = o->m_align;

  if (pos == COverlay::POSITION_RELATIVE)
  {
    float scale_x = 1.0;
    float scale_y = 1.0;
    float scale_w = 1.0;
    float scale_h = 1.0;

    if (align == COverlay::ALIGN_SCREEN || align == COverlay::ALIGN_SUBTITLE)
    {
      scale_x = m_rv.Width();
      scale_y = m_rv.Height();
      scale_w = scale_x;
      scale_h = scale_y;
    }
    else if (align == COverlay::ALIGN_SCREEN_AR)
    {
      // Align to screen by keeping aspect ratio to fit into the screen area
      float source_width = o->m_source_width > 0 ? o->m_source_width : m_rs.Width();
      float source_height = o->m_source_height > 0 ? o->m_source_height : m_rs.Height();
      float ratio = std::min<float>(m_rv.Width() / source_width, m_rv.Height() / source_height);
      scale_x = m_rv.Width();
      scale_y = m_rv.Height();
      scale_w = ratio;
      scale_h = ratio;
    }
    else if (align == COverlay::ALIGN_VIDEO)
    {
      scale_x = m_rs.Width();
      scale_y = m_rs.Height();
      scale_w = scale_x;
      scale_h = scale_y;
    }

    state.x *= scale_x;
    state.y *= scale_y;
    state.width *= scale_w;
    state.height *= scale_h;

    pos = COverlay::POSITION_ABSOLUTE;
  }

  if (pos == COverlay::POSITION_ABSOLUTE)
  {
    if (align == COverlay::ALIGN_SCREEN || align == COverlay::ALIGN_SCREEN_AR ||
        align == COverlay::ALIGN_SUBTITLE)
    {
      if (align == COverlay::ALIGN_SUBTITLE)
      {
        RESOLUTION_INFO res = CServiceBroker::GetWinSystem()->GetGfxContext().GetResInfo(
            CServiceBroker::GetWinSystem()->GetGfxContext().GetVideoResolution());
        state.x += m_rv.x1 + m_rv.Width() * 0.5f;
        state.y += m_rv.y1 + (res.iSubtitles - res.Overscan.top);
      }
      else
      {
        state.x += m_rv.x1;
        state.y += m_rv.y1;
      }
    }
    else if (align == COverlay::ALIGN_VIDEO)
    {
      float scale_x = m_rd.Width() / m_rs.Width();
      float scale_y = m_rd.Height() / m_rs.Height();

      state.x *= scale_x;
      state.y *= scale_y;
      state.width *= scale_x;
      state.height *= scale_y;

      state.x += m_rd.x1;
      state.y += m_rd.y1;
    }
  }

  state.x += GetStereoscopicDepth();

  o->Render(state);
}

bool CRenderer::HasOverlay(int idx)
{
  bool hasOverlay = false;

  std::unique_lock<CCriticalSection> lock(m_section);

  std::vector<SElement>& list = m_buffers[idx];
  for(std::vector<SElement>::iterator it = list.begin(); it != list.end(); ++it)
  {
    if (it->overlay_dvd)
    {
      hasOverlay = true;
      break;
    }
  }
  return hasOverlay;
}

void CRenderer::SetVideoRect(CRect &source, CRect &dest, CRect &view)
{
  m_rs = source;
  m_rd = dest;
  m_rv = view;
}

void CRenderer::SetStereoMode(const std::string &stereomode)
{
  m_stereomode = stereomode;
}

void CRenderer::CreateSubtitlesStyle()
{
  m_overlayStyle = std::make_shared<KODI::SUBTITLES::style>();
  const std::shared_ptr<CSettings> settings = CServiceBroker::GetSettingsComponent()->GetSettings();

  m_overlayStyle->fontName = settings->GetString(CSettings::SETTING_SUBTITLES_FONTNAME);
  m_overlayStyle->fontSize = (double)settings->GetInt(CSettings::SETTING_SUBTITLES_FONTSIZE);

  uint32_t fontStyleMask = settings->GetInt(CSettings::SETTING_SUBTITLES_STYLE) & FONT_STYLE_MASK;
  if ((fontStyleMask & FONT_STYLE_BOLD) && (fontStyleMask & FONT_STYLE_ITALICS))
    m_overlayStyle->fontStyle = KODI::SUBTITLES::FontStyle::BOLD_ITALIC;
  else if (fontStyleMask & FONT_STYLE_BOLD)
    m_overlayStyle->fontStyle = KODI::SUBTITLES::FontStyle::BOLD;
  else if (fontStyleMask & FONT_STYLE_ITALICS)
    m_overlayStyle->fontStyle = KODI::SUBTITLES::FontStyle::ITALIC;

  m_overlayStyle->fontColor =
      UTILS::COLOR::ConvertHexToColor(settings->GetString(CSettings::SETTING_SUBTITLES_COLOR));
  m_overlayStyle->fontBorderSize = settings->GetInt(CSettings::SETTING_SUBTITLES_BORDERSIZE);
  m_overlayStyle->fontBorderColor = UTILS::COLOR::ConvertHexToColor(
      settings->GetString(CSettings::SETTING_SUBTITLES_BORDERCOLOR));
  m_overlayStyle->fontOpacity = settings->GetInt(CSettings::SETTING_SUBTITLES_OPACITY);

  int backgroundType = CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt(
      CSettings::SETTING_SUBTITLES_BACKGROUNDTYPE);
  if (backgroundType == SUBTITLE_BACKGROUNDTYPE_NONE)
    m_overlayStyle->borderStyle = KODI::SUBTITLES::BorderStyle::OUTLINE_NO_SHADOW;
  else if (backgroundType == SUBTITLE_BACKGROUNDTYPE_SHADOW)
    m_overlayStyle->borderStyle = KODI::SUBTITLES::BorderStyle::OUTLINE;
  else if (backgroundType == SUBTITLE_BACKGROUNDTYPE_BOX)
    m_overlayStyle->borderStyle = KODI::SUBTITLES::BorderStyle::BOX;
  else if (backgroundType == SUBTITLE_BACKGROUNDTYPE_SQUAREBOX)
    m_overlayStyle->borderStyle = KODI::SUBTITLES::BorderStyle::SQUARE_BOX;

  m_overlayStyle->backgroundColor =
      UTILS::COLOR::ConvertHexToColor(settings->GetString(CSettings::SETTING_SUBTITLES_BGCOLOR));
  m_overlayStyle->backgroundOpacity = settings->GetInt(CSettings::SETTING_SUBTITLES_BGOPACITY);

  m_overlayStyle->shadowColor = UTILS::COLOR::ConvertHexToColor(
      settings->GetString(CSettings::SETTING_SUBTITLES_SHADOWCOLOR));
  m_overlayStyle->shadowOpacity = settings->GetInt(CSettings::SETTING_SUBTITLES_SHADOWOPACITY);
  m_overlayStyle->shadowSize = settings->GetInt(CSettings::SETTING_SUBTITLES_SHADOWSIZE);

  int subAlign = CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt(
      CSettings::SETTING_SUBTITLES_ALIGN);
  if (subAlign == SUBTITLE_ALIGN_TOP_INSIDE || subAlign == SUBTITLE_ALIGN_TOP_OUTSIDE)
    m_overlayStyle->alignment = KODI::SUBTITLES::FontAlignment::TOP_CENTER;
  else
    m_overlayStyle->alignment = KODI::SUBTITLES::FontAlignment::SUB_CENTER;

  if (subAlign == SUBTITLE_ALIGN_BOTTOM_OUTSIDE || subAlign == SUBTITLE_ALIGN_TOP_OUTSIDE)
    m_overlayStyle->drawWithinBlackBars = true;

  m_overlayStyle->assOverrideFont = settings->GetBool(CSettings::SETTING_SUBTITLES_OVERRIDEFONTS);

  int overrideStyles = settings->GetInt(CSettings::SETTING_SUBTITLES_OVERRIDESTYLES);
  if (overrideStyles == (int)KODI::SUBTITLES::OverrideStyles::POSITIONS ||
      overrideStyles == (int)KODI::SUBTITLES::OverrideStyles::STYLES ||
      overrideStyles == (int)KODI::SUBTITLES::OverrideStyles::STYLES_POSITIONS)
    m_overlayStyle->assOverrideStyles =
        static_cast<KODI::SUBTITLES::OverrideStyles>(overrideStyles);
  else
    m_overlayStyle->assOverrideStyles = KODI::SUBTITLES::OverrideStyles::DISABLED;

  int overrideMerginVertical =
      CServiceBroker::GetSettingsComponent()->GetAdvancedSettings()->m_videoSubtitleVerticalMargin;
  if (overrideMerginVertical >= 0 && overrideMerginVertical < m_rv.Height())
    m_overlayStyle->marginVertical = overrideMerginVertical;

  m_overlayStyle->blur = settings->GetInt(CSettings::SETTING_SUBTITLES_BLUR);
}

COverlay* CRenderer::ConvertLibass(
    CDVDOverlayLibass* o,
    double pts,
    bool updateStyle,
    const std::shared_ptr<struct KODI::SUBTITLES::style>& overlayStyle)
{
  KODI::SUBTITLES::renderOpts rOpts;

  // libass render in a target area which named as frame. the frame size may bigger than video size,
  // and including margins between video to frame edge. libass allow to render subtitles into the margins.
  // this has been used to show subtitles in the top or bottom "black bar" between video to frame border.
  rOpts.sourceWidth = m_rs.Width();
  rOpts.sourceHeight = m_rs.Height();
  rOpts.videoWidth = m_rd.Width();
  rOpts.videoHeight = m_rd.Height();
  rOpts.frameWidth = m_rv.Width();
  rOpts.frameHeight = m_rv.Height();

  // Render subtitle of half-sbs and half-ou video in full screen, not in half screen
  if (m_stereomode == "left_right" || m_stereomode == "right_left")
  {
    // only half-sbs video, sbs video don't need to change source size
    if (rOpts.sourceWidth / rOpts.sourceHeight < 1.2f)
      rOpts.sourceWidth = m_rs.Width() * 2;
  }
  else if (m_stereomode == "top_bottom" || m_stereomode == "bottom_top")
  {
    // only half-ou video, ou video don't need to change source size
    if (rOpts.sourceWidth / rOpts.sourceHeight > 2.5f)
      rOpts.sourceHeight = m_rs.Height() * 2;
  }

  int subAlign = CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt(
      CSettings::SETTING_SUBTITLES_ALIGN);

  // Set position of subtitles based on video calibration settings
  if (subAlign == SUBTITLE_ALIGN_MANUAL)
  {
    if (o->IsForcedMargins())
    {
      // rOpts.position can invalidate the text positions to subtitles that make
      // use of margins to position text on the screen then in this case
      // we allow to set it only when position overrides are enabled
      int overrideStyles = CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt(
          CSettings::SETTING_SUBTITLES_OVERRIDESTYLES);
      if (overrideStyles == (int)KODI::SUBTITLES::OverrideStyles::POSITIONS ||
          overrideStyles == (int)KODI::SUBTITLES::OverrideStyles::STYLES_POSITIONS)
        rOpts.usePosition = true;
    }
    else
    {
      rOpts.usePosition = true;
    }
    if (rOpts.usePosition)
    {
      RESOLUTION_INFO res;
      res = CServiceBroker::GetWinSystem()->GetGfxContext().GetResInfo(
          CServiceBroker::GetWinSystem()->GetGfxContext().GetVideoResolution());
      rOpts.position =
          100.0 - static_cast<double>(res.iSubtitles - res.Overscan.top) * 100 / res.iHeight;
    }
  }

  // Set the horizontal text alignment (currently used to improve readability on CC subtitles only)
  // This setting influence style->alignment property
  if (o->IsTextAlignEnabled())
  {
    int subTextAlign = CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt(
        CSettings::SETTING_SUBTITLES_CAPTIONSALIGN);
    if (subTextAlign == SUBTITLE_HORIZONTAL_ALIGN_LEFT)
      rOpts.horizontalAlignment = KODI::SUBTITLES::HorizontalAlignment::LEFT;
    else if (subTextAlign == SUBTITLE_HORIZONTAL_ALIGN_RIGHT)
      rOpts.horizontalAlignment = KODI::SUBTITLES::HorizontalAlignment::RIGHT;
    else
      rOpts.horizontalAlignment = KODI::SUBTITLES::HorizontalAlignment::CENTER;
  }

  // changes: Detect changes from previously rendered images, if > 0 they are changed
  int changes = 0;
  ASS_Image* images =
      o->GetLibassHandler()->RenderImage(pts, rOpts, updateStyle, overlayStyle, &changes);

  // If no images not execute the renderer
  if (!images)
    return nullptr;

  if (o->m_textureid)
  {
    if (changes == 0)
    {
      std::map<unsigned int, COverlay*>::iterator it = m_textureCache.find(o->m_textureid);
      if (it != m_textureCache.end())
        return it->second;
    }
  }

  COverlay* overlay = NULL;
#if defined(HAS_GL) || defined(HAS_GLES)
  overlay = new COverlayGlyphGL(images, rOpts.frameWidth, rOpts.frameHeight);
#elif defined(HAS_DX)
  overlay = new COverlayQuadsDX(images, rOpts.frameWidth, rOpts.frameHeight);
#endif

  // scale to video dimensions
  if (overlay)
  {
    overlay->m_width = rOpts.frameWidth / rOpts.videoWidth;
    overlay->m_height = rOpts.frameHeight / rOpts.videoHeight;
    overlay->m_x = (rOpts.videoWidth - rOpts.frameWidth) / 2 / rOpts.videoWidth;
    overlay->m_y = (rOpts.videoHeight - rOpts.frameHeight) / 2 / rOpts.videoHeight;
  }
  m_textureCache[m_textureid] = overlay;
  o->m_textureid = m_textureid;
  m_textureid++;
  return overlay;
}

COverlay* CRenderer::Convert(CDVDOverlay* o, double pts)
{
  COverlay* r = NULL;

  if (o->IsOverlayType(DVDOVERLAY_TYPE_TEXT) || o->IsOverlayType(DVDOVERLAY_TYPE_SSA))
  {
    CDVDOverlayLibass* ovAss = static_cast<CDVDOverlayLibass*>(o);
    if (!ovAss || !ovAss->GetLibassHandler())
      return nullptr;
    bool updateStyle = !m_overlayStyle || m_forceUpdateOverlayStyle;
    if (updateStyle)
    {
      m_forceUpdateOverlayStyle = false;
      CreateSubtitlesStyle();
    }

    r = ConvertLibass(ovAss, pts, updateStyle, m_overlayStyle);

    if (!r)
      return nullptr;
  }
  else if (o->m_textureid)
  {
    std::map<unsigned int, COverlay*>::iterator it = m_textureCache.find(o->m_textureid);
    if (it != m_textureCache.end())
      r = it->second;
  }

  if (r)
  {
    return r;
  }

#if defined(HAS_GL) || defined(HAS_GLES)
  if (o->IsOverlayType(DVDOVERLAY_TYPE_IMAGE))
    r = new COverlayTextureGL(static_cast<CDVDOverlayImage*>(o), m_rs);
  else if (o->IsOverlayType(DVDOVERLAY_TYPE_SPU))
    r = new COverlayTextureGL(static_cast<CDVDOverlaySpu*>(o));
#elif defined(HAS_DX)
  if (o->IsOverlayType(DVDOVERLAY_TYPE_IMAGE))
    r = new COverlayImageDX(static_cast<CDVDOverlayImage*>(o), m_rs);
  else if (o->IsOverlayType(DVDOVERLAY_TYPE_SPU))
    r = new COverlayImageDX(static_cast<CDVDOverlaySpu*>(o));
#endif

  m_textureCache[m_textureid] = r;
  o->m_textureid = m_textureid;
  m_textureid++;

  return r;
}

void CRenderer::Notify(const Observable& obs, const ObservableMessage msg)
{
  switch (msg)
  {
    case ObservableMessageSettingsChanged:
    {
      m_forceUpdateOverlayStyle = true;
      break;
    }
    default:
      break;
  }
}
