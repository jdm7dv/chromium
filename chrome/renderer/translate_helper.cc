// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/translate_helper.h"

#include "base/compiler_specific.h"
#include "chrome/renderer/render_view.h"
#include "third_party/WebKit/WebKit/chromium/public/WebFrame.h"
#include "third_party/WebKit/WebKit/chromium/public/WebScriptSource.h"

using WebKit::WebFrame;
using WebKit::WebScriptSource;

// The delay in millliseconds that we'll wait before checking to see if the
// translate library injected in the page is ready.
static const int kTranslateInitCheckDelayMs = 150;

// The maximum number of times we'll check to see if the translate library
// injected in the page is ready.
static const int kMaxTranslateInitCheckAttempts = 5;

// The delay we wait in milliseconds before checking whether the translation has
// finished.
static const int kTranslateStatusCheckDelayMs = 400;

// Language name passed to the Translate element for it to detect the language.
static const char* const kAutoDetectionLanguage = "auto";

////////////////////////////////////////////////////////////////////////////////
// TranslateHelper, public:
//
TranslateHelper::TranslateHelper(RenderView* render_view)
    : render_view_(render_view),
      translation_pending_(false),
      page_id_(-1),
      ALLOW_THIS_IN_INITIALIZER_LIST(method_factory_(this)) {
}

void TranslateHelper::TranslatePage(int page_id,
                                    const std::string& source_lang,
                                    const std::string& target_lang,
                                    const std::string& translate_script) {
  if (render_view_->page_id() != page_id)
    return;  // We navigated away, nothing to do.

  if (translation_pending_ && page_id == page_id_ &&
      target_lang_ == target_lang) {
    // A similar translation is already under way, nothing to do.
    return;
  }

  // Any pending translation is now irrelevant.
  CancelPendingTranslation();

  // Set our states.
  translation_pending_ = true;
  page_id_ = page_id;
  // If the source language is undetermined, we'll let the translate element
  // detect it.
  source_lang_ = (source_lang != RenderView::kUnknownLanguageCode) ?
                  source_lang : kAutoDetectionLanguage;
  target_lang_ = target_lang;

  if (!IsTranslateLibAvailable()) {
    // Evaluate the script to add the translation related method to the global
    // context of the page.
    ExecuteScript(translate_script);
    DCHECK(IsTranslateLibAvailable());
  }

  TranslatePageImpl(0);
}

void TranslateHelper::RevertTranslation(int page_id) {
  if (render_view_->page_id() != page_id)
    return;  // We navigated away, nothing to do.

  if (!IsTranslateLibAvailable()) {
    NOTREACHED();
    return;
  }

  WebFrame* main_frame = render_view_->webview()->mainFrame();
  if (!main_frame)
    return;

  CancelPendingTranslation();

  main_frame->executeScript(
      WebScriptSource(ASCIIToUTF16("cr.googleTranslate.revert()")));
}

void TranslateHelper::CancelPendingTranslation() {
  method_factory_.RevokeAll();
  translation_pending_ = false;
  page_id_ = -1;
  source_lang_.clear();
  target_lang_.clear();
}

////////////////////////////////////////////////////////////////////////////////
// TranslateHelper, protected:
//
bool TranslateHelper::IsTranslateLibAvailable() {
  bool lib_available = false;
  if (!ExecuteScriptAndGetBoolResult(
      "typeof cr != 'undefined' && typeof cr.googleTranslate != 'undefined' && "
      "typeof cr.googleTranslate.translate == 'function'", &lib_available)) {
    NOTREACHED();
    return false;
  }
  return lib_available;
}

bool TranslateHelper::IsTranslateLibReady() {
  bool lib_ready = false;
  if (!ExecuteScriptAndGetBoolResult("cr.googleTranslate.libReady",
                                     &lib_ready)) {
    NOTREACHED();
    return false;
  }
  return lib_ready;
}

bool TranslateHelper::HasTranslationFinished() {
  bool translation_finished = false;
  if (!ExecuteScriptAndGetBoolResult("cr.googleTranslate.finished",
                                     &translation_finished)) {
    NOTREACHED() << "crGoogleTranslateGetFinished returned unexpected value.";
    return true;
  }

  return translation_finished;
}

bool TranslateHelper::HasTranslationFailed() {
  bool translation_failed = false;
  if (!ExecuteScriptAndGetBoolResult("cr.googleTranslate.error",
                                     &translation_failed)) {
    NOTREACHED() << "crGoogleTranslateGetError returned unexpected value.";
    return true;
  }

  return translation_failed;
}

bool TranslateHelper::StartTranslation() {
  bool translate_success = false;
  if (!ExecuteScriptAndGetBoolResult("cr.googleTranslate.translate('" +
                                     source_lang_ + "','" + target_lang_ + "')",
                                     &translate_success)) {
    NOTREACHED();
    return false;
  }
  return translate_success;
}

std::string TranslateHelper::GetOriginalPageLanguage() {
  std::string lang;
  ExecuteScriptAndGetStringResult("cr.googleTranslate.sourceLang", &lang);
  return lang;
}

////////////////////////////////////////////////////////////////////////////////
// TranslateHelper, private:
//
void TranslateHelper::CheckTranslateStatus() {
  if (page_id_ != render_view_->page_id())
    return;  // This is not the same page, the translation has been canceled.

  // First check if there was an error.
  if (HasTranslationFailed()) {
    NotifyBrowserTranslationFailed(TranslateErrors::TRANSLATION_ERROR);
    return;  // There was an error.
  }

  if (HasTranslationFinished()) {
    std::string actual_source_lang;
    // Translation was successfull, if it was auto, retrieve the source
    // language the Translate Element detected.
    if (source_lang_ == kAutoDetectionLanguage) {
      actual_source_lang = GetOriginalPageLanguage();
      if (actual_source_lang.empty()) {
        NotifyBrowserTranslationFailed(TranslateErrors::TRANSLATION_ERROR);
        return;
      }
    } else {
      actual_source_lang = source_lang_;
    }

    if (!translation_pending_) {
      NOTREACHED();
      return;
    }

    translation_pending_ = false;

    // Notify the browser we are done.
    render_view_->Send(new ViewHostMsg_PageTranslated(
        render_view_->routing_id(), render_view_->page_id(),
        actual_source_lang, target_lang_, TranslateErrors::NONE));
    return;
  }

  // The translation is still pending, check again later.
  MessageLoop::current()->PostDelayedTask(FROM_HERE,
      method_factory_.NewRunnableMethod(&TranslateHelper::CheckTranslateStatus),
      DontDelayTasks() ? 0 : kTranslateStatusCheckDelayMs);
}

bool TranslateHelper::ExecuteScript(const std::string& script) {
  WebFrame* main_frame = render_view_->webview()->mainFrame();
  if (!main_frame)
    return false;
  main_frame->executeScript(WebScriptSource(ASCIIToUTF16(script)));
  return true;
}

bool TranslateHelper::ExecuteScriptAndGetBoolResult(const std::string& script,
                                                    bool* value) {
  DCHECK(value);
  WebFrame* main_frame = render_view_->webview()->mainFrame();
  if (!main_frame)
    return false;

  v8::Handle<v8::Value> v = main_frame->executeScriptAndReturnValue(
      WebScriptSource(ASCIIToUTF16(script)));
  if (v.IsEmpty() || !v->IsBoolean())
    return false;

  *value = v->BooleanValue();
  return true;
}

bool TranslateHelper::ExecuteScriptAndGetStringResult(const std::string& script,
                                                      std::string* value) {
  DCHECK(value);
  WebFrame* main_frame = render_view_->webview()->mainFrame();
  if (!main_frame)
    return false;

  v8::Handle<v8::Value> v = main_frame->executeScriptAndReturnValue(
      WebScriptSource(ASCIIToUTF16(script)));
  if (v.IsEmpty() || !v->IsString())
    return false;

  v8::Local<v8::String> v8_str = v->ToString();
  int length = v8_str->Utf8Length() + 1;
  scoped_array<char> str(new char[length]);
  v8_str->WriteUtf8(str.get(), length);
  *value = str.get();
  return true;
}

void TranslateHelper::TranslatePageImpl(int count) {
  DCHECK_LT(count, kMaxTranslateInitCheckAttempts);
  if (page_id_ != render_view_->page_id())
    return;

  if (!IsTranslateLibReady()) {
    // The library is not ready, try again later, unless we have tried several
    // times unsucessfully already.
    if (++count >= kMaxTranslateInitCheckAttempts) {
      NotifyBrowserTranslationFailed(TranslateErrors::INITIALIZATION_ERROR);
      return;
    }
    MessageLoop::current()->PostDelayedTask(FROM_HERE,
        method_factory_.NewRunnableMethod(&TranslateHelper::TranslatePageImpl,
                                          count),
        DontDelayTasks() ? 0 : count * kTranslateInitCheckDelayMs);
    return;
  }

  if (!StartTranslation()) {
    NotifyBrowserTranslationFailed(TranslateErrors::TRANSLATION_ERROR);
    return;
  }
  // Check the status of the translation.
  MessageLoop::current()->PostDelayedTask(FROM_HERE,
      method_factory_.NewRunnableMethod(&TranslateHelper::CheckTranslateStatus),
      DontDelayTasks() ? 0 : kTranslateStatusCheckDelayMs);
}

void TranslateHelper::NotifyBrowserTranslationFailed(
    TranslateErrors::Type error) {
  translation_pending_ = false;
  // Notify the browser there was an error.
  render_view_->Send(new ViewHostMsg_PageTranslated(
      render_view_->routing_id(), page_id_, source_lang_, target_lang_, error));
}
