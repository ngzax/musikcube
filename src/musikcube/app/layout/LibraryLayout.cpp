//////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2007-2017 musikcube team
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//    * Redistributions of source code must retain the above copyright notice,
//      this list of conditions and the following disclaimer.
//
//    * Redistributions in binary form must reproduce the above copyright
//      notice, this list of conditions and the following disclaimer in the
//      documentation and/or other materials provided with the distribution.
//
//    * Neither the name of the author nor the names of other contributors may
//      be used to endorse or promote products derived from this software
//      without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
//////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"

#include <cursespp/App.h>
#include <cursespp/Colors.h>
#include <cursespp/Screen.h>

#include <core/library/LocalLibraryConstants.h>
#include <core/runtime/Message.h>

#include <app/overlay/PlayQueueOverlays.h>
#include <app/util/Hotkeys.h>
#include <app/util/Messages.h>

#include <glue/util/Playback.h>

#include "LibraryLayout.h"

using namespace musik::core::library::constants;
using namespace musik::core::runtime;

#define TRANSPORT_HEIGHT 3

#define SHOULD_REFOCUS(target) \
    (this->visibleLayout == target) && \
    (this->shortcuts && !this->shortcuts->IsFocused())

using namespace musik::core;
using namespace musik::core::audio;
using namespace musik::core::library;
using namespace musik::cube;
using namespace musik::core::runtime;
using namespace cursespp;

LibraryLayout::LibraryLayout(musik::core::audio::PlaybackService& playback, ILibraryPtr library)
: LayoutBase()
, playback(playback)
, shortcuts(nullptr)
, transport(playback.GetTransport()) {
    this->library = library;
    this->InitializeWindows();
}

LibraryLayout::~LibraryLayout() {
}

void LibraryLayout::OnLayout() {
    int x = 0, y = 0;
    int cx = this->GetWidth(), cy = this->GetHeight();

    int mainHeight = cy - TRANSPORT_HEIGHT;

    this->transportView->MoveAndResize(
        1,
        mainHeight,
        cx - 2,
        TRANSPORT_HEIGHT);

    if (this->visibleLayout) {
        this->visibleLayout->MoveAndResize(x, y, cx, mainHeight);
        this->visibleLayout->Show();
    }
}

void LibraryLayout::ChangeMainLayout(std::shared_ptr<cursespp::LayoutBase> newLayout) {
    if (this->visibleLayout != newLayout) {
        this->transportView->SetFocus(TransportWindow::FocusNone);

        if (this->visibleLayout) {
           this->RemoveWindow(this->visibleLayout);
           this->visibleLayout->FocusTerminated.disconnect(this);
           this->visibleLayout->Hide();
        }

        this->visibleLayout = newLayout;

        this->visibleLayout->FocusTerminated.connect(
            this, &LibraryLayout::OnMainLayoutFocusTerminated);

        this->AddWindow(this->visibleLayout);
        this->Layout();

        /* ask the visible layout to terminate focusing, not do it
        in a circular fashion. when we hit the end, we'll focus
        the transport! see FocusNext() and FocusPrev(). */
        this->visibleLayout->SetFocusMode(ILayout::FocusModeTerminating);

        if (this->IsVisible()) {
            this->visibleLayout->BringToTop();
        }

        this->OnLayoutChanged();
    }
}

void LibraryLayout::OnLayoutChanged() {
    this->UpdateShortcutsWindow();
}

void LibraryLayout::ShowNowPlaying() {
    this->ChangeMainLayout(this->nowPlayingLayout);
}

void LibraryLayout::ShowBrowse() {
    this->ChangeMainLayout(this->browseLayout);
}

void LibraryLayout::ShowSearch() {
    SHOULD_REFOCUS(this->searchLayout)
        ? this->searchLayout->FocusInput()
        : this->ChangeMainLayout(this->searchLayout);
}

void LibraryLayout::ShowTrackSearch() {
    SHOULD_REFOCUS(this->trackSearch)
        ? this->trackSearch->FocusInput()
        : this->ChangeMainLayout(this->trackSearch);
}

void LibraryLayout::InitializeWindows() {
    this->browseLayout.reset(new BrowseLayout(this->playback, this->library));

    this->nowPlayingLayout.reset(new NowPlayingLayout(this->playback, this->library));

    this->searchLayout.reset(new SearchLayout(this->playback, this->library));
    this->searchLayout->SearchResultSelected.connect(this, &LibraryLayout::OnSearchResultSelected);

    this->trackSearch.reset(new TrackSearchLayout(this->playback, this->library));

    this->transportView.reset(new TransportWindow(this->playback));

    this->AddWindow(this->transportView);
    this->ShowBrowse();
}

void LibraryLayout::SetShortcutsWindow(ShortcutsWindow* shortcuts) {
    this->shortcuts = shortcuts;

    if (this->shortcuts) {
        this->shortcuts->AddShortcut(Hotkeys::Get(Hotkeys::NavigateLibraryBrowse), _TSTR("shortcuts_browse"));
        this->shortcuts->AddShortcut(Hotkeys::Get(Hotkeys::NavigateLibraryFilter), _TSTR("shortcuts_filter"));
        this->shortcuts->AddShortcut(Hotkeys::Get(Hotkeys::NavigateLibraryTracks), _TSTR("shortcuts_tracks"));
        this->shortcuts->AddShortcut(Hotkeys::Get(Hotkeys::NavigateLibraryPlayQueue), _TSTR("shortcuts_play_queue"));
        this->shortcuts->AddShortcut(Hotkeys::Get(Hotkeys::NavigateSettings), _TSTR("shortcuts_settings"));
        this->shortcuts->AddShortcut("^D", _TSTR("shortcuts_quit"));

        this->shortcuts->SetChangedCallback([this](std::string key) {
            if (Hotkeys::Is(Hotkeys::NavigateSettings, key)) {
                this->BroadcastMessage(message::JumpToSettings);
            }
            else if (key == "^D") {
                App::Instance().Quit();
            }
            else {
                this->KeyPress(key);
            }
        });

        this->UpdateShortcutsWindow();
    }
}

void LibraryLayout::UpdateShortcutsWindow() {
    if (this->shortcuts) {
        if (this->shortcuts->IsFocused() && this->visibleLayout) {
            this->visibleLayout->SetFocus(IWindowPtr());
        }

        if (this->visibleLayout == this->browseLayout) {
            this->shortcuts->SetActive(Hotkeys::Get(Hotkeys::NavigateLibraryBrowse));
        }
        else if (this->visibleLayout == nowPlayingLayout) {
            this->shortcuts->SetActive(Hotkeys::Get(Hotkeys::NavigateLibraryPlayQueue));
        }
        else if (this->visibleLayout == searchLayout) {
            this->shortcuts->SetActive(Hotkeys::Get(Hotkeys::NavigateLibraryFilter));
        }
        else if (this->visibleLayout == trackSearch) {
            this->shortcuts->SetActive(Hotkeys::Get(Hotkeys::NavigateLibraryTracks));
        }
    }
}

void LibraryLayout::OnAddedToParent(IWindow* parent) {
#if (__clang_major__ == 7 && __clang_minor__ == 3)
    std::enable_shared_from_this<LayoutBase>* receiver =
        (std::enable_shared_from_this<LayoutBase>*) this;
#else
    auto receiver = this;
#endif
    MessageQueue().RegisterForBroadcasts(receiver->shared_from_this());
}

void LibraryLayout::OnRemovedFromParent(IWindow* parent) {
    MessageQueue().UnregisterForBroadcasts(this);
}

void LibraryLayout::OnSearchResultSelected(
    SearchLayout* layout, std::string fieldType, int64_t fieldId)
{
    this->ShowBrowse();
    this->browseLayout->ScrollTo(fieldType, fieldId);
}

IWindowPtr LibraryLayout::FocusNext() {
    if (this->transportView->IsFocused()) {
        if (this->transportView->FocusNext()) {
            return this->transportView;
        }

        this->transportView->Blur();
        return this->visibleLayout->FocusFirst();
    }

    return this->visibleLayout->FocusNext();
}

IWindowPtr LibraryLayout::FocusPrev() {
    if (this->transportView->IsFocused()) {
        if (this->transportView->FocusPrev()) {
            return this->transportView;
        }

        this->transportView->Blur();
        return this->visibleLayout->FocusLast();
    }

    return this->visibleLayout->FocusPrev();
}

void LibraryLayout::OnMainLayoutFocusTerminated(
    LayoutBase::FocusDirection direction)
{
    if (direction == LayoutBase::FocusForward) {
        this->transportView->FocusFirst();
    }
    else {
        this->transportView->FocusLast();
    }
}

IWindowPtr LibraryLayout::GetFocus() {
    if (this->transportView->IsFocused()) {
        return this->transportView;
    }

    return this->visibleLayout->GetFocus();
}

bool LibraryLayout::SetFocus(cursespp::IWindowPtr window) {
    if (window == this->transportView) {
        this->transportView->RestoreFocus();
        return true;
    }

    return this->visibleLayout->SetFocus(window);
}

void LibraryLayout::ProcessMessage(musik::core::runtime::IMessage &message) {
    switch (message.Type()) {
        case message::JumpToCategory: {
            static std::map<int, const char*> JUMP_TYPE_TO_COLUMN = {
                { message::category::Album, constants::Track::ALBUM },
                { message::category::Artist, constants::Track::ARTIST },
                { message::category::AlbumArtist, constants::Track::ALBUM_ARTIST },
                { message::category::Genre, constants::Track::GENRE }
            };

            auto type = JUMP_TYPE_TO_COLUMN[(int)message.UserData1()];
            auto id = message.UserData2();
            this->OnSearchResultSelected(nullptr, type, id);
        }
        break;

        case message::TracksAddedToPlaylist:
        case message::PlaylistCreated: {
            MessageQueue().Post(Message::Create(
                this->browseLayout.get(),
                message.Type(),
                message.UserData1(),
                message.UserData2()));
        }
        break;
    }

    LayoutBase::ProcessMessage(message);
}

bool LibraryLayout::KeyPress(const std::string& key) {
    if (key == "^[") { /* switches between browse/now playing */
        if (this->visibleLayout != this->browseLayout) {
            this->ShowBrowse();
        }
        else {
            this->ShowNowPlaying();
        }
        return true;
    }
    else if (Hotkeys::Is(Hotkeys::NavigateLibraryPlayQueue, key)) {
        this->ShowNowPlaying();
        return true;
    }
    else if (Hotkeys::Is(Hotkeys::NavigateLibraryBrowse, key)) {
        this->ShowBrowse();
        return true;
    }
    else if (Hotkeys::Is(Hotkeys::NavigateLibraryFilter, key)) {
        this->ShowSearch();
        return true;
    }
    else if (Hotkeys::Is(Hotkeys::NavigateLibraryTracks, key)) {
        this->ShowTrackSearch();
        return true;
    }
    else if (this->GetFocus() == this->transportView && key == "KEY_UP") {
        this->transportView->Blur();
        this->visibleLayout->FocusLast();
        return true;
    }
    else if (this->GetFocus() == this->transportView && key == "KEY_DOWN") {
        this->transportView->Blur();
        this->visibleLayout->FocusFirst();
        return true;
    }
    /* forward to the visible layout */
    else if (this->visibleLayout && this->visibleLayout->KeyPress(key)) {
        return true;
    }
    else if (key == " " || key == "M- ") {
        musik::glue::playback::PauseOrResume(this->transport);
        return true;
    }

    return LayoutBase::KeyPress(key);
}
