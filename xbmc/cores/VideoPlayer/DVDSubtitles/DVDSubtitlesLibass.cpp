/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "DVDSubtitlesLibass.h"

#include "FileItem.h"
#include "ServiceBroker.h"
#include "Util.h"
#include "cores/VideoPlayer/Interface/TimingConstants.h"
#include "filesystem/Directory.h"
#include "filesystem/File.h"
#include "filesystem/SpecialProtocol.h"
#include "settings/Settings.h"
#include "settings/SettingsComponent.h"
#include "settings/SubtitlesSettings.h"
#include "utils/FontUtils.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
#include "utils/log.h"

#include <cstring>
#include <mutex>

using namespace KODI::SUBTITLES;
using namespace UTILS;

namespace
{
constexpr int ASS_BORDER_STYLE_OUTLINE = 1; // Outline + drop shadow
constexpr int ASS_BORDER_STYLE_BOX = 3; // Box + drop shadow
constexpr int ASS_BORDER_STYLE_SQUARE_BOX = 4; // Square box + outline

constexpr int ASS_FONT_ENCODING_AUTO = -1;

// Convert RGB/ARGB to RGBA by also applying the opacity value
COLOR::Color ConvColor(COLOR::Color argbColor, int opacity = 100)
{
  return COLOR::ConvertToRGBA(COLOR::ChangeOpacity(argbColor, (100.0f - opacity) / 100.0f));
}

} // namespace

static void libass_log(int level, const char* fmt, va_list args, void* data)
{
  if (level >= 5)
    return;
  std::string log = StringUtils::FormatV(fmt, args);
  CLog::Log(LOGDEBUG, "CDVDSubtitlesLibass: [ass] {}", log);
}

CDVDSubtitlesLibass::CDVDSubtitlesLibass()
{
  CLog::Log(LOGINFO, "CDVDSubtitlesLibass: Using libass version {0:x}", ass_library_version());
  CLog::Log(LOGINFO, "CDVDSubtitlesLibass: Creating ASS library structure");
  m_library = ass_library_init();
  if (!m_library)
    return;

  ass_set_message_cb(m_library, libass_log, this);

  CLog::Log(LOGINFO, "CDVDSubtitlesLibass: Initializing ASS Renderer");

  m_renderer = ass_renderer_init(m_library);

  if (!m_renderer)
    throw std::runtime_error("Libass render failed to initialize");
}

CDVDSubtitlesLibass::~CDVDSubtitlesLibass()
{
  if (m_track)
    ass_free_track(m_track);
  ass_renderer_done(m_renderer);
  ass_library_done(m_library);
}

void CDVDSubtitlesLibass::Configure()
{
  CLog::Log(LOGINFO, "CDVDSubtitlesLibass: Initializing ASS library font settings");

  if (!m_renderer)
  {
    CLog::Log(LOGERROR, "CDVDSubtitlesLibass: Failed to initialize ASS font settings. ASS renderer "
                        "not initialized.");
    return;
  }

  ass_set_margins(m_renderer, 0, 0, 0, 0);
  ass_set_use_margins(m_renderer, 0);

  // Libass uses system font provider (like fontconfig) by default in some
  // platforms (e.g. linux/windows), on some other systems like android the
  // font provider is currenlty not supported, then an user can add his
  // additionals fonts only by using the user fonts folder.
  ass_set_fonts_dir(m_library,
                    CSpecialProtocol::TranslatePath(UTILS::FONT::FONTPATH::USER).c_str());

  // Load additional fonts into Libass memory
  CFileItemList items;
  // Get fonts from system directory
  if (XFILE::CDirectory::Exists(UTILS::FONT::FONTPATH::SYSTEM))
  {
    XFILE::CDirectory::GetDirectory(UTILS::FONT::FONTPATH::SYSTEM, items,
                                    UTILS::FONT::SUPPORTED_EXTENSIONS_MASK,
                                    XFILE::DIR_FLAG_NO_FILE_DIRS | XFILE::DIR_FLAG_NO_FILE_INFO);
  }
  // Get temporary fonts
  if (XFILE::CDirectory::Exists(UTILS::FONT::FONTPATH::SYSTEM, false))
  {
    XFILE::CDirectory::GetDirectory(
        UTILS::FONT::FONTPATH::TEMP, items, UTILS::FONT::SUPPORTED_EXTENSIONS_MASK,
        XFILE::DIR_FLAG_BYPASS_CACHE | XFILE::DIR_FLAG_NO_FILE_DIRS | XFILE::DIR_FLAG_NO_FILE_INFO);
  }
  for (const auto& item : items)
  {
    if (item->m_bIsFolder)
      continue;
    const std::string filepath = item->GetPath();
    const std::string fileName = item->GetLabel();
    std::vector<uint8_t> buffer;
    if (XFILE::CFile().LoadFile(filepath, buffer) <= 0)
    {
      CLog::LogF(LOGERROR, "Failed to load file {}", filepath);
      continue;
    }
#if LIBASS_VERSION >= 0x01501000
    ass_add_font(m_library, fileName.c_str(), reinterpret_cast<const char*>(buffer.data()),
                 static_cast<int>(buffer.size()));
#else
    ass_add_font(m_library, const_cast<char*>(fileName.c_str()),
                 reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()));
#endif
    if (StringUtils::CompareNoCase(fileName, FONT::FONT_DEFAULT_FILENAME) == 0)
    {
      m_defaultFontFamilyName = FONT::GetFontFamily(buffer);
    }
  }
  if (m_defaultFontFamilyName.empty())
  {
    CLog::LogF(LOGERROR,
               "The application font {} is missing. The default subtitle font cannot be set.",
               FONT::FONT_DEFAULT_FILENAME);
  }

  ass_set_fonts(m_renderer,
                UTILS::FONT::FONTPATH::GetSystemFontPath(FONT::FONT_DEFAULT_FILENAME).c_str(),
                m_defaultFontFamilyName.c_str(), ASS_FONTPROVIDER_AUTODETECT, nullptr, 1);

  ass_set_font_scale(m_renderer, 1);

  // Extract font must be set before loading ASS/SSA data,
  // after that cannot be changed
  const std::shared_ptr<CSettings> settings = CServiceBroker::GetSettingsComponent()->GetSettings();
  bool overrideFont = settings->GetBool(CSettings::SETTING_SUBTITLES_OVERRIDEFONTS);
  ass_set_extract_fonts(m_library, overrideFont ? 0 : 1);
}

bool CDVDSubtitlesLibass::DecodeHeader(char* data, int size)
{
  std::unique_lock<CCriticalSection> lock(m_section);
  if (!m_library || !data)
    return false;

  CLog::Log(LOGINFO, "CDVDSubtitlesLibass: Creating new ASS track");
  m_track = ass_new_track(m_library);

  ass_process_codec_private(m_track, data, size);
  return true;
}

bool CDVDSubtitlesLibass::DecodeDemuxPkt(const char* data, int size, double start, double duration)
{
  std::unique_lock<CCriticalSection> lock(m_section);
  if (!m_track)
  {
    CLog::Log(LOGERROR, "{} - No SSA header found.", __FUNCTION__);
    return false;
  }

  //! @bug libass isn't const correct
  ass_process_chunk(m_track, const_cast<char*>(data), size, DVD_TIME_TO_MSEC(start),
                    DVD_TIME_TO_MSEC(duration));
  return true;
}

bool CDVDSubtitlesLibass::CreateTrack()
{
  std::unique_lock<CCriticalSection> lock(m_section);
  if (!m_library)
  {
    CLog::Log(LOGERROR, "{} - Failed to create ASS track, library not initialized.", __FUNCTION__);
    return false;
  }

  CLog::Log(LOGINFO, "CDVDSubtitlesLibass: Creating new ASS track");
  m_track = ass_new_track(m_library);
  if (m_track == NULL)
  {
    CLog::Log(LOGERROR, "{} - Failed to allocate ASS track.", __FUNCTION__);
    return false;
  }

  m_track->track_type = m_track->TRACK_TYPE_ASS;
  m_track->Timer = 100.;
  // Set fixed values to PlayRes to allow the use of style override code for positioning
  m_track->PlayResX = (int)VIEWPORT_WIDTH;
  m_track->PlayResY = (int)VIEWPORT_HEIGHT;
  m_track->Kerning = true; // Font kerning improves the letterspacing
  m_track->WrapStyle = 1; // The line feed \n doesn't break but wraps (instead \N breaks)

  return true;
}

bool CDVDSubtitlesLibass::CreateStyle()
{
  std::unique_lock<CCriticalSection> lock(m_section);
  if (!m_library)
  {
    CLog::Log(LOGERROR, "{} - Failed to create ASS style, library not initialized.", __FUNCTION__);
    return false;
  }

  if (!m_track)
  {
    CLog::Log(LOGERROR, "{} - Failed to create ASS style, track not initialized.", __FUNCTION__);
    return false;
  }

  m_defaultKodiStyleId = ass_alloc_style(m_track);
  return true;
}

bool CDVDSubtitlesLibass::CreateTrack(char* buf, size_t size)
{
  std::unique_lock<CCriticalSection> lock(m_section);
  if (!m_library)
  {
    CLog::Log(LOGERROR, "{} - No ASS library struct (m_library)", __FUNCTION__);
    return false;
  }

  CLog::Log(LOGINFO, "CDVDSubtitlesLibass: Creating m_track from SSA buffer");

  m_track = ass_read_memory(m_library, buf, size, 0);
  if (m_track == NULL)
    return false;

  return true;
}

ASS_Image* CDVDSubtitlesLibass::RenderImage(
    double pts,
    renderOpts opts,
    bool updateStyle,
    const std::shared_ptr<struct KODI::SUBTITLES::style>& subStyle,
    int* changes)
{
  std::unique_lock<CCriticalSection> lock(m_section);
  if (!m_renderer || !m_track)
  {
    CLog::Log(LOGERROR, "{} - ASS renderer/ASS track not initialized.", __FUNCTION__);
    return nullptr;
  }

  if (!subStyle)
  {
    CLog::Log(LOGERROR, "{} - The subtitle overlay style is not set.", __FUNCTION__);
    return nullptr;
  }

  if (updateStyle || m_currentDefaultStyleId == ASS_NO_ID)
  {
    ApplyStyle(subStyle, opts);
    m_drawWithinBlackBars = subStyle->drawWithinBlackBars;
  }

  double sar = static_cast<double>(opts.sourceWidth) / static_cast<double>(opts.sourceHeight);
  double dar = static_cast<double>(opts.videoWidth) / static_cast<double>(opts.videoHeight);

  ass_set_frame_size(m_renderer, static_cast<int>(opts.frameWidth),
                     static_cast<int>(opts.frameHeight));
  ass_set_storage_size(m_renderer, static_cast<int>(opts.sourceWidth),
                       static_cast<int>(opts.sourceHeight));
  int marginTop{0};
  int marginLeft{0};
  if (m_drawWithinBlackBars)
  {
    marginTop = static_cast<int>((opts.frameHeight - opts.videoHeight) / 2);
    marginLeft = static_cast<int>((opts.frameWidth - opts.videoWidth) / 2);
  }
  ass_set_margins(m_renderer, marginTop, marginTop, marginLeft, marginLeft);

  ass_set_use_margins(m_renderer, m_drawWithinBlackBars);

  // Vertical text position in percent (if 0 do nothing)
  ass_set_line_position(m_renderer, opts.position);

  ass_set_pixel_aspect(m_renderer, dar / sar);

  // For posterity ass_render_frame have an inconsistent rendering for overlapped subtitles cases,
  // if the playback occurs in sequence (without seeks) the overlapped subtitles lines will be rendered in right order
  // if you seek forward/backward the video, the overlapped subtitles lines could be rendered in the wrong order
  // this is a known side effect from libass devs and not a bug from our part
  return ass_render_frame(m_renderer, m_track, DVD_TIME_TO_MSEC(pts), changes);
}

void CDVDSubtitlesLibass::ApplyStyle(const std::shared_ptr<struct KODI::SUBTITLES::style>& subStyle,
                                     renderOpts opts)
{
  CLog::Log(LOGDEBUG, "{} - Start setting up the LibAss style", __FUNCTION__);

  if (!subStyle)
  {
    CLog::Log(LOGERROR, "{} - The subtitle overlay style is not set.", __FUNCTION__);
    return;
  }

  // ASS_Style is a POD struct need to be initialized with {}
  ASS_Style defaultStyle{};
  ASS_Style* style = nullptr;

  if (m_subtitleType == ADAPTED ||
      (m_subtitleType == NATIVE &&
       (subStyle->assOverrideStyles != OverrideStyles::DISABLED || subStyle->assOverrideFont)))
  {
    m_currentDefaultStyleId = m_defaultKodiStyleId;

    if (m_subtitleType == NATIVE)
    {
      style = &defaultStyle;
    }
    else
    {
      style = &m_track->styles[m_currentDefaultStyleId];
    }

    free(style->Name);
    style->Name = strdup("KodiDefault");

    // Calculate the scale (influence ASS style properties)
    double scale = 1.0;
    int playResY;
    if (m_subtitleType == NATIVE &&
        (subStyle->assOverrideStyles == OverrideStyles::STYLES ||
         subStyle->assOverrideStyles == OverrideStyles::STYLES_POSITIONS ||
         subStyle->assOverrideFont))
    {
      // With styles overridden the PlayResY will be changed to 288
      playResY = 288;
      scale = 288. / 720;
    }
    else
    {
      playResY = m_track->PlayResY;
    }

    // It is mandatory set the FontName, the text is case sensitive
    free(style->FontName);
    if (subStyle->fontName == FONT_DEFAULT_FAMILYNAME)
      style->FontName = strdup(m_defaultFontFamilyName.c_str());
    else
      style->FontName = strdup(subStyle->fontName.c_str());

    // Configure the font properties
    // FIXME: The font size need to be scaled to be shown in right PT size
    style->FontSize = (subStyle->fontSize / 720) * playResY;
    // Modifies the width/height of the font (1 = 100%)
    style->ScaleX = 1.0;
    style->ScaleY = 1.0;
    // Extra space between characters causes the underlined
    // text line to become more discontinuous (test on LibAss 15.1)
    style->Spacing = 0;

    // Set automatic paragraph direction (not VSFilter-compatible)
    // to fix wrong RTL text direction when there are unicode chars
    style->Encoding = ASS_FONT_ENCODING_AUTO;

    bool isFontBold =
        (subStyle->fontStyle == FontStyle::BOLD || subStyle->fontStyle == FontStyle::BOLD_ITALIC);
    bool isFontItalic =
        (subStyle->fontStyle == FontStyle::ITALIC || subStyle->fontStyle == FontStyle::BOLD_ITALIC);
    style->Bold = isFontBold * -1;
    style->Italic = isFontItalic * -1;

    // Compute the font color, depending on the opacity
    COLOR::Color subColor = ConvColor(subStyle->fontColor, subStyle->fontOpacity);
    // Set default subtitles color
    style->PrimaryColour = subColor;
    // Set SecondaryColour may be used to prevent an onscreen collision
    style->SecondaryColour = subColor;

    // Configure the effects
    double lineSpacing = 0.0;
    if (subStyle->borderStyle == BorderStyle::OUTLINE ||
        subStyle->borderStyle == BorderStyle::OUTLINE_NO_SHADOW)
    {
      style->BorderStyle = ASS_BORDER_STYLE_OUTLINE;
      style->Outline = (10.00 / 100 * subStyle->fontBorderSize) * scale;
      style->OutlineColour = ConvColor(subStyle->fontBorderColor, subStyle->fontOpacity);
      if (subStyle->borderStyle == BorderStyle::OUTLINE_NO_SHADOW)
      {
        style->BackColour = ConvColor(COLOR::NONE, 0); // Set the shadow color
        style->Shadow = 0; // Set the shadow size
      }
      else
      {
        style->BackColour =
            ConvColor(subStyle->shadowColor, subStyle->shadowOpacity); // Set the shadow color
        style->Shadow = (10.00 / 100 * subStyle->shadowSize) * scale; // Set the shadow size
      }
    }
    else if (subStyle->borderStyle == BorderStyle::BOX)
    {
      // This BorderStyle not support outline color/size
      style->BorderStyle = ASS_BORDER_STYLE_BOX;
      style->Outline = 4 * scale; // Space between the text and the box edges
      style->OutlineColour =
          ConvColor(subStyle->backgroundColor,
                    subStyle->backgroundOpacity); // Set the background border color
      style->BackColour =
          ConvColor(subStyle->shadowColor, subStyle->shadowOpacity); // Set the box shadow color
      style->Shadow = (10.00 / 100 * subStyle->shadowSize) * scale; // Set the box shadow size
      // By default a box overlaps the other, then we increase a bit the line spacing
      lineSpacing = 6.0;
    }
    else if (subStyle->borderStyle == BorderStyle::SQUARE_BOX)
    {
      // This BorderStyle not support shadow color/size
      style->BorderStyle = ASS_BORDER_STYLE_SQUARE_BOX;
      style->Outline = (10.00 / 100 * subStyle->fontBorderSize) * scale;
      style->OutlineColour = ConvColor(subStyle->fontBorderColor, subStyle->fontOpacity);
      style->BackColour = ConvColor(subStyle->backgroundColor, subStyle->backgroundOpacity);
      style->Shadow = 4 * scale; // Space between the text and the box edges
    }

    ass_set_line_spacing(m_renderer, lineSpacing);

    style->Blur = (10.00 / 100 * subStyle->blur);

    double marginLR = 20;
    if (opts.horizontalAlignment != HorizontalAlignment::DISABLED)
    {
      // If the subtitle text is aligned on the left or right
      // of the screen, we set an extra left/right margin
      marginLR += static_cast<double>(opts.frameWidth) / 10;
    }

    // Set the margins (in pixel)
    style->MarginL = static_cast<int>(marginLR * scale);
    style->MarginR = static_cast<int>(marginLR * scale);
    if (opts.disableVerticalMargin)
      style->MarginV = 0;
    else
      style->MarginV = static_cast<int>(subStyle->marginVertical * scale);

    // Set the vertical alignment
    if (subStyle->alignment == FontAlignment::TOP_LEFT ||
        subStyle->alignment == FontAlignment::TOP_CENTER ||
        subStyle->alignment == FontAlignment::TOP_RIGHT)
      style->Alignment = VALIGN_TOP;
    else if (subStyle->alignment == FontAlignment::MIDDLE_LEFT ||
             subStyle->alignment == FontAlignment::MIDDLE_CENTER ||
             subStyle->alignment == FontAlignment::MIDDLE_RIGHT)
      style->Alignment = VALIGN_CENTER;
    else if (subStyle->alignment == FontAlignment::SUB_LEFT ||
             subStyle->alignment == FontAlignment::SUB_CENTER ||
             subStyle->alignment == FontAlignment::SUB_RIGHT)
      style->Alignment = VALIGN_SUB;

    // Set the horizontal alignment, giving priority to horizontalFontAlign property when set
    if (opts.horizontalAlignment == HorizontalAlignment::LEFT)
      style->Alignment |= HALIGN_LEFT;
    else if (opts.horizontalAlignment == HorizontalAlignment::CENTER)
      style->Alignment |= HALIGN_CENTER;
    else if (opts.horizontalAlignment == HorizontalAlignment::RIGHT)
      style->Alignment |= HALIGN_RIGHT;
    else if (subStyle->alignment == FontAlignment::TOP_LEFT ||
             subStyle->alignment == FontAlignment::MIDDLE_LEFT ||
             subStyle->alignment == FontAlignment::SUB_LEFT)
      style->Alignment |= HALIGN_LEFT;
    else if (subStyle->alignment == FontAlignment::TOP_CENTER ||
             subStyle->alignment == FontAlignment::MIDDLE_CENTER ||
             subStyle->alignment == FontAlignment::SUB_CENTER)
      style->Alignment |= HALIGN_CENTER;
    else if (subStyle->alignment == FontAlignment::TOP_RIGHT ||
             subStyle->alignment == FontAlignment::MIDDLE_RIGHT ||
             subStyle->alignment == FontAlignment::SUB_RIGHT)
      style->Alignment |= HALIGN_RIGHT;
  }

  if (m_subtitleType == NATIVE)
  {
    ConfigureAssOverride(subStyle, style);
    m_currentDefaultStyleId = m_track->default_style;
  }
}

void CDVDSubtitlesLibass::ConfigureAssOverride(
    const std::shared_ptr<struct KODI::SUBTITLES::style>& subStyle, ASS_Style* style)
{
  if (!subStyle)
  {
    CLog::Log(LOGERROR, "{} - The subtitle overlay style is not set.", __FUNCTION__);
    return;
  }

  // Default behaviour, disable ASS embedded styles override (if has been changed)
  int stylesFlags{ASS_OVERRIDE_DEFAULT};
  if (style)
  {
    // Manage override cases with ASS embedded styles
    if (subStyle->assOverrideStyles == OverrideStyles::STYLES)
    {
      stylesFlags = ASS_OVERRIDE_BIT_COLORS | ASS_OVERRIDE_BIT_ATTRIBUTES |
                    ASS_OVERRIDE_BIT_BORDER | ASS_OVERRIDE_BIT_MARGINS;
    }
    else if (subStyle->assOverrideStyles == OverrideStyles::STYLES_POSITIONS)
    {
      stylesFlags = ASS_OVERRIDE_BIT_COLORS | ASS_OVERRIDE_BIT_ATTRIBUTES |
                    ASS_OVERRIDE_BIT_BORDER | ASS_OVERRIDE_BIT_MARGINS | ASS_OVERRIDE_BIT_ALIGNMENT;
    }
    else if (subStyle->assOverrideStyles == OverrideStyles::POSITIONS)
    {
      stylesFlags = ASS_OVERRIDE_BIT_ALIGNMENT | ASS_OVERRIDE_BIT_MARGINS;
    }
    if (subStyle->assOverrideFont)
    {
      stylesFlags |= ASS_OVERRIDE_BIT_FONT_SIZE_FIELDS | ASS_OVERRIDE_BIT_FONT_NAME;
    }
    ass_set_selective_style_override(m_renderer, style);
  }

  ass_set_selective_style_override_enabled(m_renderer, stylesFlags);
}

ASS_Event* CDVDSubtitlesLibass::GetEvents()
{
  std::unique_lock<CCriticalSection> lock(m_section);
  if (!m_track)
  {
    CLog::Log(LOGERROR, "{} -  Missing ASS structs (m_track)", __FUNCTION__);
    return NULL;
  }
  return m_track->events;
}

int CDVDSubtitlesLibass::GetNrOfEvents() const
{
  std::unique_lock<CCriticalSection> lock(m_section);
  if (!m_track)
    return 0;
  return m_track->n_events;
}

int CDVDSubtitlesLibass::AddEvent(const char* text, double startTime, double stopTime)
{
  return AddEvent(text, startTime, stopTime, nullptr);
}

int CDVDSubtitlesLibass::AddEvent(const char* text,
                                  double startTime,
                                  double stopTime,
                                  subtitleOpts* opts)
{
  if (text == NULL || text[0] == '\0')
  {
    CLog::Log(LOGDEBUG,
              "{} - Add event skipped due to empty text (with start time: {}, stop time {})",
              __FUNCTION__, startTime, stopTime);
    return ASS_NO_ID;
  }

  std::unique_lock<CCriticalSection> lock(m_section);
  if (!m_library || !m_track)
  {
    CLog::Log(LOGERROR, "{} - Missing ASS structs (m_library or m_track)", __FUNCTION__);
    return ASS_NO_ID;
  }

  int eventId = ass_alloc_event(m_track);
  if (eventId >= 0)
  {
    ASS_Event* event = m_track->events + eventId;
    event->Start = DVD_TIME_TO_MSEC(startTime);
    event->Duration = DVD_TIME_TO_MSEC(stopTime - startTime);
    event->Style = m_defaultKodiStyleId;
    event->ReadOrder = eventId;
    event->Text = strdup(text);
    if (opts && opts->useMargins)
    {
      event->MarginL = opts->marginLeft;
      event->MarginR = opts->marginRight;
      event->MarginV = opts->marginVertical;
    }
    return eventId;
  }
  else
    CLog::Log(LOGERROR, "{} - Cannot allocate a new event", __FUNCTION__);
  return ASS_NO_ID;
}

void CDVDSubtitlesLibass::AppendTextToEvent(int eventId, const char* text)
{
  std::unique_lock<CCriticalSection> lock(m_section);
  if (eventId == ASS_NO_ID || text == NULL || text[0] == '\0')
    return;
  if (!m_track)
  {
    CLog::Log(LOGERROR, "{} -  Missing ASS structs (m_track)", __FUNCTION__);
    return;
  }

  ASS_Event* assEvents = m_track->events;
  if (!assEvents)
  {
    CLog::Log(LOGERROR, "{} -  Failed append text to Event ID {}, there are no Events",
              __FUNCTION__, eventId);
    return;
  }

  ASS_Event* assEvent = (assEvents + eventId);
  if (assEvent)
  {
    size_t buffSize = strlen(assEvent->Text) + strlen(text) + 1;
    char* appendedText = new char[buffSize];
    strcpy(appendedText, assEvent->Text);
    strcat(appendedText, text);
    free(assEvent->Text);
    assEvent->Text = strdup(appendedText);
    delete[] appendedText;
  }
}

void CDVDSubtitlesLibass::ChangeEventStopTime(int eventId, double stopTime)
{
  std::unique_lock<CCriticalSection> lock(m_section);
  if (eventId == ASS_NO_ID)
    return;
  if (!m_track)
  {
    CLog::Log(LOGERROR, "{} -  Missing ASS structs (m_track)", __FUNCTION__);
    return;
  }

  ASS_Event* assEvents = m_track->events;
  if (!assEvents)
  {
    CLog::Log(LOGERROR, "{} -  Failed change stop time to Event ID {}, there are no Events",
              __FUNCTION__, eventId);
    return;
  }

  ASS_Event* assEvent = (assEvents + eventId);
  if (assEvent)
    assEvent->Duration = (DVD_TIME_TO_MSEC(stopTime) - assEvent->Start);
}

void CDVDSubtitlesLibass::FlushEvents()
{
  std::unique_lock<CCriticalSection> lock(m_section);
  if (!m_library || !m_track)
  {
    CLog::Log(LOGERROR, "{} - Missing ASS structs (m_library or m_track)", __FUNCTION__);
    return;
  }

  ass_flush_events(m_track);
}

int CDVDSubtitlesLibass::DeleteEvents(int nEvents, int threshold)
{
  std::unique_lock<CCriticalSection> lock(m_section);
  if (!m_library || !m_track)
  {
    CLog::Log(LOGERROR, "{} - Missing ASS structs (m_library or m_track)", __FUNCTION__);
    return ASS_NO_ID;
  }

  if (m_track->n_events == 0)
    return ASS_NO_ID;
  if (m_track->n_events < (threshold - nEvents))
    return m_track->n_events - 1;

  // Currently LibAss do not have delete event method we have to free the events
  // and reassign all events starting with the first empty position
  int n = 0;
  for (; n < nEvents; n++)
  {
    ass_free_event(m_track, n);
    m_track->n_events--;
  }
  for (int i = 0; n > 0 && i < threshold; i++)
  {
    m_track->events[i] = m_track->events[i + n];
  }
  return m_track->n_events - 1;
}
