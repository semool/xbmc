/*
 *  Copyright (C) 2012-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "XBDateTime.h"
#include "addons/kodi-dev-kit/include/kodi/c-api/addon-instance/pvr/pvr_channels.h" // PVR_CHANNEL_INVALID_UID
#include "pvr/PVRConstants.h" // PVR_CLIENT_INVALID_UID
#include "pvr/settings/IPVRSettingsContainer.h"
#include "settings/SettingConditions.h"
#include "settings/dialogs/GUIDialogSettingsManualBase.h"
#include "settings/lib/SettingDependency.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

class CSetting;

struct IntegerSettingOption;
struct StringSettingOption;

namespace PVR
{
class CPVRCustomTimerSettings;
class CPVRTimerInfoTag;
class CPVRTimerType;

class CGUIDialogPVRTimerSettings : public CGUIDialogSettingsManualBase, public IPVRSettingsContainer
{
public:
  CGUIDialogPVRTimerSettings();
  ~CGUIDialogPVRTimerSettings() override;

  bool CanBeActivated() const override;

  void SetTimer(const std::shared_ptr<CPVRTimerInfoTag>& timer);

  // implementation of IPVRSettingsContainer
  void AddMultiIntSetting(const std::shared_ptr<CSettingGroup>& group,
                          const std::string& settingName,
                          int settingValue) override;
  void AddSingleIntSetting(const std::shared_ptr<CSettingGroup>& group,
                           const std::string& settingName,
                           int settingValue,
                           int minValue,
                           int step,
                           int maxValue) override;
  void AddMultiStringSetting(const std::shared_ptr<CSettingGroup>& group,
                             const std::string& settingName,
                             const std::string& settingValue) override;
  void AddSingleStringSetting(const std::shared_ptr<CSettingGroup>& group,
                              const std::string& settingName,
                              const std::string& settingValue,
                              bool allowEmptyValue) override;

protected:
  // implementation of ISettingCallback
  void OnSettingChanged(const std::shared_ptr<const CSetting>& setting) override;
  void OnSettingAction(const std::shared_ptr<const CSetting>& setting) override;

  // specialization of CGUIDialogSettingsBase
  bool AllowResettingSettings() const override { return false; }
  std::string GetSettingsLabel(const std::shared_ptr<ISetting>& setting) override;
  bool Save() override;
  void SetupView() override;

  // specialization of CGUIDialogSettingsManualBase
  void InitializeSettings() override;

private:
  bool Validate() const;
  void InitializeTypesList();
  void InitializeChannelsList();
  void SetButtonLabels();

  void FixupEndLocalTime();

  static int GetDateAsIndex(const CDateTime& datetime);
  static void SetDateFromIndex(CDateTime& datetime, int date);
  static void SetTimeFromSystemTime(CDateTime& datetime, const KODI::TIME::SystemTime& time);

  static int GetWeekdaysFromSetting(const std::shared_ptr<const CSetting>& setting);

  void TypesFiller(const std::shared_ptr<const CSetting>& setting,
                   std::vector<IntegerSettingOption>& list,
                   int& current) const;
  void ChannelsFiller(const std::shared_ptr<const CSetting>& setting,
                      std::vector<IntegerSettingOption>& list,
                      int& current);
  void DaysFiller(const std::shared_ptr<const CSetting>& setting,
                  std::vector<IntegerSettingOption>& list,
                  int& current) const;
  void DupEpisodesFiller(const std::shared_ptr<const CSetting>& setting,
                         std::vector<IntegerSettingOption>& list,
                         int& current) const;
  void WeekdaysFiller(const std::shared_ptr<const CSetting>& setting,
                      std::vector<IntegerSettingOption>& list,
                      int& current) const;
  void PrioritiesFiller(const std::shared_ptr<const CSetting>& setting,
                        std::vector<IntegerSettingOption>& list,
                        int& current) const;
  void LifetimesFiller(const std::shared_ptr<const CSetting>& setting,
                       std::vector<IntegerSettingOption>& list,
                       int& current) const;
  void MaxRecordingsFiller(const std::shared_ptr<const CSetting>& setting,
                           std::vector<IntegerSettingOption>& list,
                           int& current) const;
  void RecordingGroupFiller(const std::shared_ptr<const CSetting>& setting,
                            std::vector<IntegerSettingOption>& list,
                            int& current) const;
  void MarginTimeFiller(const std::shared_ptr<const CSetting>& setting,
                        std::vector<IntegerSettingOption>& list,
                        int& current) const;
  void CustomIntSettingDefinitionsFiller(const std::shared_ptr<const CSetting>& setting,
                                         std::vector<IntegerSettingOption>& list,
                                         int& current) const;
  void CustomStringSettingDefinitionsFiller(const std::shared_ptr<const CSetting>& setting,
                                            std::vector<StringSettingOption>& list,
                                            std::string& current) const;

  static std::string WeekdaysValueFormatter(const std::shared_ptr<const CSetting>& setting);

  void AddCondition(const std::shared_ptr<CSetting>& setting,
                    const std::string& identifier,
                    const SettingConditionCheck& condition,
                    SettingDependencyType depType,
                    const std::string& settingId) const;

  void AddTypeDependentEnableCondition(const std::shared_ptr<CSetting>& setting,
                                       const std::string& identifier) const;
  bool TypeReadOnlyCondition(const std::string& condition,
                             const std::string& value,
                             const std::shared_ptr<const CSetting>& setting) const;

  void AddTypeDependentVisibilityCondition(const std::shared_ptr<CSetting>& setting,
                                           const std::string& identifier) const;
  bool TypeSupportsCondition(const std::string& condition,
                             const std::string& value,
                             const std::shared_ptr<const CSetting>& setting) const;

  void AddStartAnytimeDependentVisibilityCondition(const std::shared_ptr<CSetting>& setting,
                                                   const std::string& identifier) const;
  bool StartAnytimeSetCondition(const std::string& condition,
                                const std::string& value,
                                const std::shared_ptr<const CSetting>& setting) const;
  void AddEndAnytimeDependentVisibilityCondition(const std::shared_ptr<CSetting>& setting,
                                                 const std::string& identifier) const;
  bool EndAnytimeSetCondition(const std::string& condition,
                              const std::string& value,
                              const std::shared_ptr<const CSetting>& setting) const;

  using TypeEntriesMap = std::map<int, std::shared_ptr<CPVRTimerType>>;

  struct ChannelDescriptor
  {
    int channelUid;
    int clientId;
    std::string description;

    ChannelDescriptor(int _channelUid = PVR_CHANNEL_INVALID_UID,
                      int _clientId = PVR_CLIENT_INVALID_UID,
                      const std::string& _description = "")
      : channelUid(_channelUid), clientId(_clientId), description(_description)
    {
    }

    bool operator==(const ChannelDescriptor& right) const = default;
  };

  using ChannelEntriesMap = std::map<int, ChannelDescriptor>;

  std::unique_ptr<CPVRCustomTimerSettings> m_customTimerSettings;

  std::shared_ptr<CPVRTimerInfoTag> m_timerInfoTag;
  TypeEntriesMap m_typeEntries;
  ChannelEntriesMap m_channelEntries;
  std::string m_timerStartTimeStr;
  std::string m_timerEndTimeStr;

  std::shared_ptr<CPVRTimerType> m_timerType;
  bool m_bIsRadio = false;
  bool m_bIsNewTimer = true;
  bool m_bTimerActive = false;
  std::string m_strTitle;
  std::string m_strEpgSearchString;
  bool m_bFullTextEpgSearch = true;
  ChannelDescriptor m_channel;
  CDateTime m_startLocalTime;
  CDateTime m_endLocalTime;
  bool m_bStartAnyTime = false;
  bool m_bEndAnyTime = false;
  unsigned int m_iWeekdays{0};
  CDateTime m_firstDayLocalTime;
  unsigned int m_iPreventDupEpisodes = 0;
  unsigned int m_iMarginStart = 0;
  unsigned int m_iMarginEnd = 0;
  int m_iPriority = 0;
  int m_iLifetime = 0;
  int m_iMaxRecordings = 0;
  std::string m_strDirectory;
  unsigned int m_iRecordingGroup = 0;
};
} // namespace PVR
