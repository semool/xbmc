/*
 *  Copyright (C) 2012-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "guilib/guiinfo/PlayerGUIInfo.h"

#include "Application.h"
#include "FileItem.h"
#include "PlayListPlayer.h"
#include "ServiceBroker.h"
#include "URL.h"
#include "cores/AudioEngine/Utils/AEUtil.h"
#include "cores/DataCacheCore.h"
#include "cores/EdlEdit.h"
#include "guilib/GUIComponent.h"
#include "guilib/GUIDialog.h"
#include "guilib/GUIWindowManager.h"
#include "guilib/guiinfo/GUIInfo.h"
#include "guilib/guiinfo/GUIInfoHelper.h"
#include "guilib/guiinfo/GUIInfoLabels.h"
#include "utils/StringUtils.h"
#include "utils/TimeUtils.h"
#include "utils/URIUtils.h"
#include "utils/Variant.h"
#include "utils/log.h"

#include <charconv>
#include <cmath>

using namespace KODI::GUILIB::GUIINFO;

CPlayerGUIInfo::CPlayerGUIInfo()
: m_playerShowTime(false),
  m_playerShowInfo(false)
{
}

CPlayerGUIInfo::~CPlayerGUIInfo() = default;

int CPlayerGUIInfo::GetTotalPlayTime() const
{
  return std::lrint(g_application.GetTotalTime());
}

int CPlayerGUIInfo::GetPlayTime() const
{
  return std::lrint(g_application.GetTime());
}

int CPlayerGUIInfo::GetPlayTimeRemaining() const
{
  int iReverse = GetTotalPlayTime() - std::lrint(g_application.GetTime());
  return iReverse > 0 ? iReverse : 0;
}

float CPlayerGUIInfo::GetSeekPercent() const
{
  int iTotal = GetTotalPlayTime();
  if (iTotal == 0)
    return 0.0f;

  float fPercentPlayTime = static_cast<float>(GetPlayTime() * 1000) / iTotal * 0.1f;
  float fPercentPerSecond = 100.0f / static_cast<float>(iTotal);
  float fPercent = fPercentPlayTime + fPercentPerSecond * g_application.GetAppPlayer().GetSeekHandler().GetSeekSize();
  fPercent = std::max(0.0f, std::min(fPercent, 100.0f));
  return fPercent;
}

std::string CPlayerGUIInfo::GetCurrentPlayTime(TIME_FORMAT format) const
{
  if (format == TIME_FORMAT_GUESS && GetTotalPlayTime() >= 3600)
    format = TIME_FORMAT_HH_MM_SS;

  return StringUtils::SecondsToTimeString(std::lrint(GetPlayTime()), format);
}

std::string CPlayerGUIInfo::GetCurrentPlayTimeRemaining(TIME_FORMAT format) const
{
  if (format == TIME_FORMAT_GUESS && GetTotalPlayTime() >= 3600)
    format = TIME_FORMAT_HH_MM_SS;

  int iTimeRemaining = GetPlayTimeRemaining();
  if (iTimeRemaining)
    return StringUtils::SecondsToTimeString(iTimeRemaining, format);

  return std::string();
}

std::string CPlayerGUIInfo::GetDuration(TIME_FORMAT format) const
{
  int iTotal = GetTotalPlayTime();
  if (iTotal > 0)
  {
    if (format == TIME_FORMAT_GUESS && iTotal >= 3600)
      format = TIME_FORMAT_HH_MM_SS;
    return StringUtils::SecondsToTimeString(iTotal, format);
  }
  return std::string();
}

std::string CPlayerGUIInfo::GetCurrentSeekTime(TIME_FORMAT format) const
{
  if (format == TIME_FORMAT_GUESS && GetTotalPlayTime() >= 3600)
    format = TIME_FORMAT_HH_MM_SS;

  return StringUtils::SecondsToTimeString(g_application.GetTime() + g_application.GetAppPlayer().GetSeekHandler().GetSeekSize(), format);
}

std::string CPlayerGUIInfo::GetSeekTime(TIME_FORMAT format) const
{
  if (!g_application.GetAppPlayer().GetSeekHandler().HasTimeCode())
    return std::string();

  int iSeekTimeCode = g_application.GetAppPlayer().GetSeekHandler().GetTimeCodeSeconds();
  if (format == TIME_FORMAT_GUESS && iSeekTimeCode >= 3600)
    format = TIME_FORMAT_HH_MM_SS;

  return StringUtils::SecondsToTimeString(iSeekTimeCode, format);
}

void CPlayerGUIInfo::SetShowInfo(bool showinfo)
{
  m_playerShowInfo = showinfo;
}

bool CPlayerGUIInfo::ToggleShowInfo()
{
  SetShowInfo(!m_playerShowInfo);
  return m_playerShowInfo;
}

bool CPlayerGUIInfo::InitCurrentItem(CFileItem *item)
{
  if (item && g_application.GetAppPlayer().IsPlaying())
  {
    CLog::Log(LOGDEBUG, "CPlayerGUIInfo::InitCurrentItem({})", CURL::GetRedacted(item->GetPath()));
    m_currentItem.reset(new CFileItem(*item));
  }
  else
  {
    m_currentItem.reset();
  }
  return false;
}

bool CPlayerGUIInfo::GetLabel(std::string& value, const CFileItem *item, int contextWindow, const CGUIInfo &info, std::string *fallback) const
{
  switch (info.m_info)
  {
    ///////////////////////////////////////////////////////////////////////////////////////////////
    // PLAYER_*
    ///////////////////////////////////////////////////////////////////////////////////////////////
    case PLAYER_SEEKOFFSET:
    {
      int lastSeekOffset = CServiceBroker::GetDataCacheCore().GetSeekOffSet();
      std::string seekOffset = StringUtils::SecondsToTimeString(
          std::abs(lastSeekOffset / 1000), static_cast<TIME_FORMAT>(info.GetData1()));
      if (lastSeekOffset < 0)
        value = "-" + seekOffset;
      else if (lastSeekOffset > 0)
        value = "+" + seekOffset;
      return true;
    }
    case PLAYER_PROGRESS:
      value = std::to_string(std::lrintf(g_application.GetPercentage()));
      return true;
    case PLAYER_PROGRESS_CACHE:
      value = std::to_string(std::lrintf(g_application.GetCachePercentage()));
      return true;
    case PLAYER_VOLUME:
      value =
          StringUtils::Format("{:2.1f} dB", CAEUtil::PercentToGain(g_application.GetVolumeRatio()));
      return true;
    case PLAYER_SUBTITLE_DELAY:
      value = StringUtils::Format("{:2.3f} s",
                                  g_application.GetAppPlayer().GetVideoSettings().m_SubtitleDelay);
      return true;
    case PLAYER_AUDIO_DELAY:
      value = StringUtils::Format("{:2.3f} s",
                                  g_application.GetAppPlayer().GetVideoSettings().m_AudioDelay);
      return true;
    case PLAYER_CHAPTER:
      value = StringUtils::Format("{:02}", g_application.GetAppPlayer().GetChapter());
      return true;
    case PLAYER_CHAPTERCOUNT:
      value = StringUtils::Format("{:02}", g_application.GetAppPlayer().GetChapterCount());
      return true;
    case PLAYER_CHAPTERNAME:
      g_application.GetAppPlayer().GetChapterName(value);
      return true;
    case PLAYER_PATH:
    case PLAYER_FILENAME:
    case PLAYER_FILEPATH:
      value = GUIINFO::GetFileInfoLabelValueFromPath(info.m_info, item->GetPath());
      return true;
    case PLAYER_TITLE:
      // use label or drop down to title from path
      value = item->GetLabel();
      if (value.empty())
        value = CUtil::GetTitleFromPath(item->GetPath());
      return true;
    case PLAYER_PLAYSPEED:
    {
      float speed = g_application.GetAppPlayer().GetPlaySpeed();
      if (speed == 1.0f)
        speed = g_application.GetAppPlayer().GetPlayTempo();
      value = StringUtils::Format("{:.2f}", speed);
      return true;
    }
    case PLAYER_TIME:
      value = GetCurrentPlayTime(static_cast<TIME_FORMAT>(info.GetData1()));
      return true;
    case PLAYER_START_TIME:
    {
      const CDateTime time(g_application.GetAppPlayer().GetStartTime());
      value = time.GetAsLocalizedTime(static_cast<TIME_FORMAT>(info.GetData1()));
      return true;
    }
    case PLAYER_DURATION:
      value = GetDuration(static_cast<TIME_FORMAT>(info.GetData1()));
      return true;
    case PLAYER_TIME_REMAINING:
      value = GetCurrentPlayTimeRemaining(static_cast<TIME_FORMAT>(info.GetData1()));
      return true;
    case PLAYER_FINISH_TIME:
    {
      CDateTime time(CDateTime::GetCurrentDateTime());
      int playTimeRemaining = GetPlayTimeRemaining();
      float speed = g_application.GetAppPlayer().GetPlaySpeed();
      float tempo = g_application.GetAppPlayer().GetPlayTempo();
      if (speed == 1.0f)
        playTimeRemaining /= tempo;
      time += CDateTimeSpan(0, 0, 0, playTimeRemaining);
      value = time.GetAsLocalizedTime(static_cast<TIME_FORMAT>(info.GetData1()));
      return true;
    }
    case PLAYER_TIME_SPEED:
    {
      float speed = g_application.GetAppPlayer().GetPlaySpeed();
      if (speed != 1.0f)
        value = StringUtils::Format("{} ({}x)",
                                    GetCurrentPlayTime(static_cast<TIME_FORMAT>(info.GetData1())),
                                    static_cast<int>(speed));
      else
        value = GetCurrentPlayTime(TIME_FORMAT_GUESS);
      return true;
    }
    case PLAYER_SEEKTIME:
      value = GetCurrentSeekTime(static_cast<TIME_FORMAT>(info.GetData1()));
      return true;
    case PLAYER_SEEKSTEPSIZE:
    {
      int seekSize = g_application.GetAppPlayer().GetSeekHandler().GetSeekSize();
      std::string strSeekSize = StringUtils::SecondsToTimeString(abs(seekSize), static_cast<TIME_FORMAT>(info.GetData1()));
      if (seekSize < 0)
        value = "-" + strSeekSize;
      if (seekSize > 0)
        value = "+" + strSeekSize;
      return true;
    }
    case PLAYER_SEEKNUMERIC:
      value = GetSeekTime(static_cast<TIME_FORMAT>(info.GetData1()));
      return !value.empty();
    case PLAYER_CACHELEVEL:
    {
      int iLevel = g_application.GetAppPlayer().GetCacheLevel();
      if (iLevel >= 0)
      {
        value = std::to_string(iLevel);
        return true;
      }
      break;
    }
    case PLAYER_ITEM_ART:
      value = item->GetArt(info.GetData3());
      return true;
    case PLAYER_ICON:
      value = item->GetArt("thumb");
      if (value.empty())
        value = item->GetArt("icon");
      if (fallback)
        *fallback = item->GetArt("icon");
      return true;
    case PLAYER_EDITLIST:
    case PLAYER_CUTS:
    case PLAYER_SCENE_MARKERS:
    case PLAYER_CUTLIST:
    case PLAYER_CHAPTERS:
      value = GetContentRanges(info.m_info);
      return true;

    ///////////////////////////////////////////////////////////////////////////////////////////////
    // PLAYER_PROCESS_*
    ///////////////////////////////////////////////////////////////////////////////////////////////
    case PLAYER_PROCESS_VIDEODECODER:
      value = CServiceBroker::GetDataCacheCore().GetVideoDecoderName();
      return true;
    case PLAYER_PROCESS_DEINTMETHOD:
      value = CServiceBroker::GetDataCacheCore().GetVideoDeintMethod();
      return true;
    case PLAYER_PROCESS_PIXELFORMAT:
      value = CServiceBroker::GetDataCacheCore().GetVideoPixelFormat();
      return true;
    case PLAYER_PROCESS_VIDEOFPS:
      value = StringUtils::Format("{:.3f}", CServiceBroker::GetDataCacheCore().GetVideoFps());
      return true;
    case PLAYER_PROCESS_VIDEODAR:
      value = StringUtils::Format("{:.2f}", CServiceBroker::GetDataCacheCore().GetVideoDAR());
      return true;
    case PLAYER_PROCESS_VIDEOWIDTH:
      value = StringUtils::FormatNumber(CServiceBroker::GetDataCacheCore().GetVideoWidth());
      return true;
    case PLAYER_PROCESS_VIDEOHEIGHT:
      value = StringUtils::FormatNumber(CServiceBroker::GetDataCacheCore().GetVideoHeight());
      return true;
    case PLAYER_PROCESS_VIDEOSCANTYPE:
      value = CServiceBroker::GetDataCacheCore().IsVideoInterlaced() ? "i" : "p";
      return true;
    case PLAYER_PROCESS_AUDIODECODER:
      value = CServiceBroker::GetDataCacheCore().GetAudioDecoderName();
      return true;
    case PLAYER_PROCESS_AUDIOCHANNELS:
      value = CServiceBroker::GetDataCacheCore().GetAudioChannels();
      return true;
    case PLAYER_PROCESS_AUDIOSAMPLERATE:
      value = StringUtils::FormatNumber(CServiceBroker::GetDataCacheCore().GetAudioSampleRate());
      return true;
    case PLAYER_PROCESS_AUDIOBITSPERSAMPLE:
      value = StringUtils::FormatNumber(CServiceBroker::GetDataCacheCore().GetAudioBitsPerSample());
      return true;

    ///////////////////////////////////////////////////////////////////////////////////////////////
    // PLAYLIST_*
    ///////////////////////////////////////////////////////////////////////////////////////////////
    case PLAYLIST_LENGTH:
    case PLAYLIST_POSITION:
    case PLAYLIST_RANDOM:
    case PLAYLIST_REPEAT:
      value = GUIINFO::GetPlaylistLabel(info.m_info, info.GetData1());
      return true;
  }

  return false;
}

bool CPlayerGUIInfo::GetInt(int& value, const CGUIListItem *gitem, int contextWindow, const CGUIInfo &info) const
{
  switch (info.m_info)
  {
    ///////////////////////////////////////////////////////////////////////////////////////////////
    // PLAYER_*
    ///////////////////////////////////////////////////////////////////////////////////////////////
    case PLAYER_VOLUME:
      value = static_cast<int>(g_application.GetVolumePercent());
      return true;
    case PLAYER_SUBTITLE_DELAY:
      value = g_application.GetSubtitleDelay();
      return true;
    case PLAYER_AUDIO_DELAY:
      value = g_application.GetAudioDelay();
      return true;
    case PLAYER_PROGRESS:
      value = std::lrintf(g_application.GetPercentage());
      return true;
    case PLAYER_PROGRESS_CACHE:
      value = std::lrintf(g_application.GetCachePercentage());
      return true;
    case PLAYER_CACHELEVEL:
      value = g_application.GetAppPlayer().GetCacheLevel();
      return true;
    case PLAYER_CHAPTER:
      value = g_application.GetAppPlayer().GetChapter();
      return true;
    case PLAYER_CHAPTERCOUNT:
      value = g_application.GetAppPlayer().GetChapterCount();
      return true;
    case PLAYER_SEEKBAR:
      value = std::lrintf(GetSeekPercent());
      return true;
  }

  return false;
}

bool CPlayerGUIInfo::GetBool(bool& value, const CGUIListItem *gitem, int contextWindow, const CGUIInfo &info) const
{
  const CFileItem *item = nullptr;
  if (gitem->IsFileItem())
    item = static_cast<const CFileItem*>(gitem);

  switch (info.m_info)
  {
    ///////////////////////////////////////////////////////////////////////////////////////////////
    // PLAYER_*
    ///////////////////////////////////////////////////////////////////////////////////////////////
    case PLAYER_SHOWINFO:
      value = m_playerShowInfo;
      return true;
    case PLAYER_SHOWTIME:
      value = m_playerShowTime;
      return true;
    case PLAYER_MUTED:
      value = (g_application.IsMuted() ||
               g_application.GetVolumeRatio() <= CApplicationVolumeHandling::VOLUME_MINIMUM);
      return true;
    case PLAYER_HAS_MEDIA:
      value = g_application.GetAppPlayer().IsPlaying();
      return true;
    case PLAYER_HAS_AUDIO:
      value = g_application.GetAppPlayer().IsPlayingAudio();
      return true;
    case PLAYER_HAS_VIDEO:
      value = g_application.GetAppPlayer().IsPlayingVideo();
      return true;
    case PLAYER_HAS_GAME:
      value = g_application.GetAppPlayer().IsPlayingGame();
      return true;
    case PLAYER_PLAYING:
      value = g_application.GetAppPlayer().GetPlaySpeed() == 1.0f;
      return true;
    case PLAYER_PAUSED:
      value = g_application.GetAppPlayer().IsPausedPlayback();
      return true;
    case PLAYER_REWINDING:
      value = g_application.GetAppPlayer().GetPlaySpeed() < 0.0f;
      return true;
    case PLAYER_FORWARDING:
      value = g_application.GetAppPlayer().GetPlaySpeed() > 1.5f;
      return true;
    case PLAYER_REWINDING_2x:
      value = g_application.GetAppPlayer().GetPlaySpeed() == -2;
      return true;
    case PLAYER_REWINDING_4x:
      value = g_application.GetAppPlayer().GetPlaySpeed() == -4;
      return true;
    case PLAYER_REWINDING_8x:
      value = g_application.GetAppPlayer().GetPlaySpeed() == -8;
      return true;
    case PLAYER_REWINDING_16x:
      value = g_application.GetAppPlayer().GetPlaySpeed() == -16;
      return true;
    case PLAYER_REWINDING_32x:
      value = g_application.GetAppPlayer().GetPlaySpeed() == -32;
      return true;
    case PLAYER_FORWARDING_2x:
      value = g_application.GetAppPlayer().GetPlaySpeed() == 2;
      return true;
    case PLAYER_FORWARDING_4x:
      value = g_application.GetAppPlayer().GetPlaySpeed() == 4;
      return true;
    case PLAYER_FORWARDING_8x:
      value = g_application.GetAppPlayer().GetPlaySpeed() == 8;
      return true;
    case PLAYER_FORWARDING_16x:
      value = g_application.GetAppPlayer().GetPlaySpeed() == 16;
      return true;
    case PLAYER_FORWARDING_32x:
      value = g_application.GetAppPlayer().GetPlaySpeed() == 32;
      return true;
    case PLAYER_CAN_PAUSE:
      value = g_application.GetAppPlayer().CanPause();
      return true;
    case PLAYER_CAN_SEEK:
      value = g_application.GetAppPlayer().CanSeek();
      return true;
    case PLAYER_SUPPORTS_TEMPO:
      value = g_application.GetAppPlayer().SupportsTempo();
      return true;
    case PLAYER_IS_TEMPO:
    {
      value = (g_application.GetAppPlayer().GetPlayTempo() != 1.0f &&
               g_application.GetAppPlayer().GetPlaySpeed() == 1.0f);
      return true;
    }
    case PLAYER_CACHING:
      value = g_application.GetAppPlayer().IsCaching();
      return true;
    case PLAYER_SEEKBAR:
    {
      CGUIDialog *seekBar = CServiceBroker::GetGUI()->GetWindowManager().GetDialog(WINDOW_DIALOG_SEEK_BAR);
      value = seekBar ? seekBar->IsDialogRunning() : false;
      return true;
    }
    case PLAYER_SEEKING:
      value = g_application.GetAppPlayer().GetSeekHandler().InProgress();
      return true;
    case PLAYER_HASPERFORMEDSEEK:
    {
      int requestedLastSecondInterval{0};
      std::from_chars_result result =
          std::from_chars(info.GetData3().data(), info.GetData3().data() + info.GetData3().size(),
                          requestedLastSecondInterval);
      if (result.ec == std::errc::invalid_argument)
      {
        value = false;
        return false;
      }

      value = CServiceBroker::GetDataCacheCore().HasPerformedSeek(requestedLastSecondInterval);
      return true;
    }
    case PLAYER_PASSTHROUGH:
      value = g_application.GetAppPlayer().IsPassthrough();
      return true;
    case PLAYER_ISINTERNETSTREAM:
      if (item)
      {
        value = URIUtils::IsInternetStream(item->GetDynPath());
        return true;
      }
      break;
    case PLAYER_HAS_PROGRAMS:
      value = (g_application.GetAppPlayer().GetProgramsCount() > 1) ? true : false;
      return true;
    case PLAYER_HAS_RESOLUTIONS:
      value = CServiceBroker::GetWinSystem()->GetGfxContext().IsFullScreenRoot() &&
              CResolutionUtils::HasWhitelist();
      return true;
    case PLAYER_HASDURATION:
      value = g_application.GetTotalTime() > 0;
      return true;
    case PLAYER_FRAMEADVANCE:
      value = CServiceBroker::GetDataCacheCore().IsFrameAdvance();
      return true;
    case PLAYER_HAS_SCENE_MARKERS:
      value = !CServiceBroker::GetDataCacheCore().GetSceneMarkers().empty();
      return true;

    ///////////////////////////////////////////////////////////////////////////////////////////////
    // PLAYLIST_*
    ///////////////////////////////////////////////////////////////////////////////////////////////
    case PLAYLIST_ISRANDOM:
    {
      PLAYLIST::CPlayListPlayer& player = CServiceBroker::GetPlaylistPlayer();
      int playlistid = info.GetData1();
      if (info.GetData2() > 0 && playlistid > PLAYLIST_NONE)
        value = player.IsShuffled(playlistid);
      else
        value = player.IsShuffled(player.GetCurrentPlaylist());
      return true;
    }
    case PLAYLIST_ISREPEAT:
    {
      PLAYLIST::CPlayListPlayer& player = CServiceBroker::GetPlaylistPlayer();
      int playlistid = info.GetData1();
      if (info.GetData2() > 0 && playlistid > PLAYLIST_NONE)
        value = (player.GetRepeat(playlistid) == PLAYLIST::REPEAT_ALL);
      else
        value = player.GetRepeat(player.GetCurrentPlaylist()) == PLAYLIST::REPEAT_ALL;
      return true;
    }
    case PLAYLIST_ISREPEATONE:
    {
      PLAYLIST::CPlayListPlayer& player = CServiceBroker::GetPlaylistPlayer();
      int playlistid = info.GetData1();
      if (info.GetData2() > 0 && playlistid > PLAYLIST_NONE)
        value = (player.GetRepeat(playlistid) == PLAYLIST::REPEAT_ONE);
      else
        value = player.GetRepeat(player.GetCurrentPlaylist()) == PLAYLIST::REPEAT_ONE;
      return true;
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////
    // PLAYER_PROCESS_*
    ///////////////////////////////////////////////////////////////////////////////////////////////
    case PLAYER_PROCESS_VIDEOHWDECODER:
      value = CServiceBroker::GetDataCacheCore().IsVideoHwDecoder();
      return true;

    ///////////////////////////////////////////////////////////////////////////////////////////////
    // LISTITEM_*
    ///////////////////////////////////////////////////////////////////////////////////////////////
    case LISTITEM_ISPLAYING:
    {
      if (item)
      {
        if (item->HasProperty("playlistposition"))
        {
          value = static_cast<int>(item->GetProperty("playlisttype").asInteger()) == CServiceBroker::GetPlaylistPlayer().GetCurrentPlaylist() &&
                  static_cast<int>(item->GetProperty("playlistposition").asInteger()) == CServiceBroker::GetPlaylistPlayer().GetCurrentSong();
          return true;
        }
        else if (m_currentItem && !m_currentItem->GetPath().empty())
        {
          if (!g_application.m_strPlayListFile.empty())
          {
            //playlist file that is currently playing or the playlistitem that is currently playing.
            value = item->IsPath(g_application.m_strPlayListFile) || m_currentItem->IsSamePath(item);
          }
          else
          {
            value = m_currentItem->IsSamePath(item);
          }
          return true;
        }
      }
      break;
    }
  }

  return false;
}

std::string CPlayerGUIInfo::GetContentRanges(int iInfo) const
{
  std::string values;

  CDataCacheCore& data = CServiceBroker::GetDataCacheCore();
  std::vector<std::pair<float, float>> ranges;

  time_t start;
  int64_t current;
  int64_t min;
  int64_t max;
  data.GetPlayTimes(start, current, min, max);

  time_t duration = max - start * 1000;
  if (duration > 0)
  {
    switch (iInfo)
    {
      case PLAYER_EDITLIST:
      case PLAYER_CUTLIST:
        ranges = GetEditList(data, duration);
        break;
      case PLAYER_CUTS:
        ranges = GetCuts(data, duration);
        break;
      case PLAYER_SCENE_MARKERS:
        ranges = GetSceneMarkers(data, duration);
        break;
      case PLAYER_CHAPTERS:
        ranges = GetChapters(data, duration);
        break;
      default:
        CLog::Log(LOGERROR, "CPlayerGUIInfo::GetContentRanges({}) - unhandled guiinfo", iInfo);
        break;
    }

    // create csv string from ranges
    for (const auto& range : ranges)
      values += StringUtils::Format("{:.5f},{:.5f},", range.first, range.second);

    if (!values.empty())
      values.pop_back(); // remove trailing comma
  }

  return values;
}

std::vector<std::pair<float, float>> CPlayerGUIInfo::GetEditList(const CDataCacheCore& data,
                                                                 time_t duration) const
{
  std::vector<std::pair<float, float>> ranges;

  const std::vector<EDL::Edit>& edits = data.GetEditList();
  for (const auto& edit : edits)
  {
    float editStart = edit.start * 100.0f / duration;
    float editEnd = edit.end * 100.0f / duration;
    ranges.emplace_back(std::make_pair(editStart, editEnd));
  }
  return ranges;
}

std::vector<std::pair<float, float>> CPlayerGUIInfo::GetCuts(const CDataCacheCore& data,
                                                             time_t duration) const
{
  std::vector<std::pair<float, float>> ranges;

  const std::vector<int64_t>& cuts = data.GetCuts();
  float lastMarker = 0.0f;
  for (const auto& cut : cuts)
  {
    float marker = cut * 100.0f / duration;
    if (marker != 0)
      ranges.emplace_back(std::make_pair(lastMarker, marker));

    lastMarker = marker;
  }
  return ranges;
}

std::vector<std::pair<float, float>> CPlayerGUIInfo::GetSceneMarkers(const CDataCacheCore& data,
                                                                     time_t duration) const
{
  std::vector<std::pair<float, float>> ranges;

  const std::vector<int64_t>& scenes = data.GetSceneMarkers();
  float lastMarker = 0.0f;
  for (const auto& scene : scenes)
  {
    float marker = scene * 100.0f / duration;
    if (marker != 0)
      ranges.emplace_back(std::make_pair(lastMarker, marker));

    lastMarker = marker;
  }
  return ranges;
}

std::vector<std::pair<float, float>> CPlayerGUIInfo::GetChapters(const CDataCacheCore& data,
                                                                 time_t duration) const
{
  std::vector<std::pair<float, float>> ranges;

  const std::vector<std::pair<std::string, int64_t>>& chapters = data.GetChapters();
  float lastMarker = 0.0f;
  for (const auto& chapter : chapters)
  {
    float marker = chapter.second * 1000 * 100.0f / duration;
    if (marker != 0)
      ranges.emplace_back(std::make_pair(lastMarker, marker));

    lastMarker = marker;
  }
  return ranges;
}
