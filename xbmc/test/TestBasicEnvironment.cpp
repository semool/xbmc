/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "TestBasicEnvironment.h"

#include "AppParamParser.h"
#include "Application.h"
#include "ServiceBroker.h"
#include "TestUtils.h"
#include "filesystem/Directory.h"
#include "filesystem/File.h"
#include "filesystem/SpecialProtocol.h"
#include "messaging/ApplicationMessenger.h"
#include "platform/Filesystem.h"
#include "profiles/ProfileManager.h"
#include "settings/AdvancedSettings.h"
#include "settings/Settings.h"
#include "settings/SettingsComponent.h"
#include "utils/log.h"
#include "windowing/WinSystem.h"

#ifdef TARGET_DARWIN
#include "Util.h"
#endif

#include <cstdio>
#include <cstdlib>
#include <climits>
#include <system_error>

namespace fs = KODI::PLATFORM::FILESYSTEM;

TestBasicEnvironment::TestBasicEnvironment() = default;

void TestBasicEnvironment::SetUp()
{
  CAppParamParser params;
  params.SetPlatformDirectories(false);

  CServiceBroker::CreateLogging();

  const auto settingsComponent = std::make_shared<CSettingsComponent>();
  settingsComponent->Initialize(params);
  CServiceBroker::RegisterSettingsComponent(settingsComponent);

  CServiceBroker::RegisterAppMessenger(std::make_shared<KODI::MESSAGING::CApplicationMessenger>());

  XFILE::CFile *f;

  g_application.m_ServiceManager.reset(new CServiceManager());

  if (!CXBMCTestUtils::Instance().SetReferenceFileBasePath())
    SetUpError();
  CXBMCTestUtils::Instance().setTestFileFactoryWriteInputFile(
    XBMC_REF_FILE_PATH("xbmc/filesystem/test/reffile.txt")
  );

//for darwin set framework path - else we get assert
//in guisettings init below
#ifdef TARGET_DARWIN
  std::string frameworksPath = CUtil::GetFrameworksPath();
  CSpecialProtocol::SetXBMCFrameworksPath(frameworksPath);
#endif
  /**
   * @todo Something should be done about all the asserts in GUISettings so
   * that the initialization of these components won't be needed.
   */

  /* Create a temporary directory and set it to be used throughout the
   * test suite run.
   */

  std::error_code ec;
  m_tempPath = fs::create_temp_directory(ec);
  if (ec)
  {
    TearDown();
    SetUpError();
  }

  CSpecialProtocol::SetTempPath(m_tempPath);
  CSpecialProtocol::SetProfilePath(m_tempPath);

  /* Create and delete a tempfile to initialize the VFS (really to initialize
   * CLibcdio). This is done so that the initialization of the VFS does not
   * affect the performance results of the test cases.
   */
  /** @todo Make the initialization of the VFS here optional so it can be
   * testable in a test case.
   */
  f = XBMC_CREATETEMPFILE("");
  if (!f || !XBMC_DELETETEMPFILE(f))
  {
    TearDown();
    SetUpError();
  }

  const CProfile profile("special://temp");
  CServiceBroker::GetSettingsComponent()->GetProfileManager()->AddProfile(profile);
  CServiceBroker::GetSettingsComponent()->GetProfileManager()->CreateProfileFolders();

  if (!g_application.m_ServiceManager->InitForTesting())
    exit(1);
}

void TestBasicEnvironment::TearDown()
{
  XFILE::CDirectory::RemoveRecursive(m_tempPath);

  g_application.m_ServiceManager->DeinitTesting();

  CServiceBroker::UnregisterAppMessenger();

  CServiceBroker::GetLogging().UnregisterFromSettings();
  CServiceBroker::GetSettingsComponent()->Deinitialize();
  CServiceBroker::UnregisterSettingsComponent();
  CServiceBroker::GetLogging().Uninitialize();
  CServiceBroker::DestroyLogging();
}

void TestBasicEnvironment::SetUpError()
{
  fprintf(stderr, "Setup of basic environment failed.\n");
  exit(EXIT_FAILURE);
}
