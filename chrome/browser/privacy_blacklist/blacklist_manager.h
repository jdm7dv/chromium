// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PRIVACY_BLACKLIST_BLACKLIST_MANAGER_H_
#define CHROME_BROWSER_PRIVACY_BLACKLIST_BLACKLIST_MANAGER_H_

#include <vector>

#include "base/basictypes.h"
#include "base/file_path.h"
#include "base/ref_counted.h"
#include "base/scoped_ptr.h"
#include "chrome/browser/chrome_thread.h"
#include "chrome/common/notification_registrar.h"

class Blacklist;
class Profile;

class BlacklistPathProvider {
 public:
  virtual ~BlacklistPathProvider();

  // The methods below will be invoked on the UI thread.
  virtual std::vector<FilePath> GetPersistentBlacklistPaths() = 0;
  virtual std::vector<FilePath> GetTransientBlacklistPaths() = 0;
};

// Updates one compiled binary blacklist based on a list of plaintext
// blacklists.
class BlacklistManager
    : public NotificationObserver,
      public base::RefCountedThreadSafe<BlacklistManager,
                                        ChromeThread::DeleteOnUIThread> {
 public:
  BlacklistManager();
  void Initialize(Profile* profile, BlacklistPathProvider* path_provider);

  const Blacklist* GetCompiledBlacklist() const {
    // TODO(phajdan.jr): Determine on which thread this should be invoked (IO?).
    return compiled_blacklist_.get();
  }

  // NotificationObserver
  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

#ifdef UNIT_TEST
  const FilePath& compiled_blacklist_path() { return compiled_blacklist_path_; }
#endif  // UNIT_TEST

 private:
  friend class ChromeThread;
  friend class DeleteTask<BlacklistManager>;

  ~BlacklistManager() {}

  // Compile all persistent blacklists to one binary blacklist stored on disk.
  void CompileBlacklist();
  void DoCompileBlacklist(const std::vector<FilePath>& source_blacklists);
  void OnBlacklistCompilationFinished(bool success);

  // Read all blacklists from disk (the compiled one and also the transient
  // blacklists).
  void ReadBlacklist();
  void DoReadBlacklist(const std::vector<FilePath>& transient_blacklists);
  void ReportBlacklistReadResult(Blacklist* blacklist);
  void OnBlacklistReadFinished(Blacklist* blacklist);

  // True after the first blacklist read has finished (regardless of success).
  // Used to avoid an infinite loop.
  bool first_read_finished_;

  Profile* profile_;

  // Path where we store the compiled blacklist.
  FilePath compiled_blacklist_path_;

  // Keep the compiled blacklist object in memory.
  scoped_ptr<Blacklist> compiled_blacklist_;

  BlacklistPathProvider* path_provider_;

  NotificationRegistrar registrar_;

  DISALLOW_COPY_AND_ASSIGN(BlacklistManager);
};

#endif  // CHROME_BROWSER_PRIVACY_BLACKLIST_BLACKLIST_MANAGER_H_
