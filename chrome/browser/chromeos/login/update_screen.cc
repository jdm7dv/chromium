// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/login/update_screen.h"

#include "chrome/browser/chromeos/login/screen_observer.h"
#include "chrome/browser/chromeos/login/update_view.h"

namespace {

// Update window should appear for at least kMinimalUpdateTime seconds.
const int kMinimalUpdateTime = 3;

// Progress bar increment step.
const int kUpdateCheckProgressIncrement = 20;
const int kUpdateCompleteProgressIncrement = 75;

}  // anonymous namespace

namespace chromeos {

UpdateScreen::UpdateScreen(WizardScreenDelegate* delegate)
    : DefaultViewScreen<chromeos::UpdateView>(delegate),
      update_result_(UPGRADE_STARTED),
      update_error_(GOOGLE_UPDATE_NO_ERROR) {
}

UpdateScreen::~UpdateScreen() {
  // Remove pointer to this object from view.
  if (view())
    view()->set_controller(NULL);
  // Google Updater is holding a pointer to us until it reports status,
  // so we need to remove it in case we were still listening.
  if (google_updater_.get())
    google_updater_->set_status_listener(NULL);
}

void UpdateScreen::OnReportResults(GoogleUpdateUpgradeResult result,
                                 GoogleUpdateErrorCode error_code,
                                 const std::wstring& version) {
  // Drop the last reference to the object so that it gets cleaned up here.
  google_updater_ = NULL;
  // Depending on the result decide what to do next.
  update_result_ = result;
  update_error_ = error_code;
  switch (update_result_) {
    case UPGRADE_IS_AVAILABLE:
      // Advance view progress bar.
      view()->AddProgress(kUpdateCheckProgressIncrement);
      // Create new Google Updater instance and install the update.
      google_updater_ = CreateGoogleUpdate();
      google_updater_->set_status_listener(this);
      google_updater_->CheckForUpdate(true, NULL);
      break;
    case UPGRADE_SUCCESSFUL:
      view()->AddProgress(kUpdateCompleteProgressIncrement);
      // Fall through.
    case UPGRADE_ALREADY_UP_TO_DATE:
      view()->AddProgress(kUpdateCheckProgressIncrement);
      // Fall through.
    case UPGRADE_ERROR:
      if (MinimalUpdateTimeElapsed()) {
        ExitUpdate();
      }
      break;
    default:
      NOTREACHED();
  }
}

void UpdateScreen::StartUpdate() {
  // Reset view.
  view()->Reset();
  view()->set_controller(this);

  // Start the minimal update time timer.
  minimal_update_time_timer_.Start(
      base::TimeDelta::FromSeconds(kMinimalUpdateTime),
      this,
      &UpdateScreen::OnMinimalUpdateTimeElapsed);

  // Create Google Updater object and check if there is an update available.
  google_updater_ = CreateGoogleUpdate();
  google_updater_->set_status_listener(this);
  google_updater_->CheckForUpdate(false, NULL);
}

void UpdateScreen::CancelUpdate() {
#if !defined(OFFICIAL_BUILD)
  update_result_ = UPGRADE_ALREADY_UP_TO_DATE;
  update_error_ = GOOGLE_UPDATE_NO_ERROR;
  ExitUpdate();
#endif
}

void UpdateScreen::ExitUpdate() {
  minimal_update_time_timer_.Stop();
  ScreenObserver* observer = delegate()->GetObserver(this);
  if (observer) {
    switch (update_result_) {
      case UPGRADE_ALREADY_UP_TO_DATE:
        observer->OnExit(ScreenObserver::UPDATE_NOUPDATE);
        break;
      case UPGRADE_SUCCESSFUL:
        observer->OnExit(ScreenObserver::UPDATE_INSTALLED);
        break;
      case UPGRADE_ERROR:
        if (update_error_ == GOOGLE_UPDATE_ERROR_UPDATING) {
          observer->OnExit(ScreenObserver::UPDATE_NETWORK_ERROR);
        } else {
          // TODO(denisromanov): figure out better what to do if
          // some other error has occurred.
          observer->OnExit(ScreenObserver::UPDATE_OTHER_ERROR);
        }
        break;
      default:
        NOTREACHED();
    }
  }
}

bool UpdateScreen::MinimalUpdateTimeElapsed() {
  return !minimal_update_time_timer_.IsRunning();
}

GoogleUpdate* UpdateScreen::CreateGoogleUpdate() {
  return new GoogleUpdate();
}

void UpdateScreen::OnMinimalUpdateTimeElapsed() {
  if (update_result_ == UPGRADE_SUCCESSFUL ||
      update_result_ == UPGRADE_ALREADY_UP_TO_DATE ||
      update_result_ == UPGRADE_ERROR) {
    ExitUpdate();
  }
}

}  // namespace chromeos
