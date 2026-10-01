#include "progman.hpp"
#include <format>

void ProgmanWindow::create_control_panel() {
  ControlPanelWindow *sub = new ControlPanelWindow();
  sub->win = this;

  sub->subwindow = MwVaCreateWidget(MwSubWindowClass, NULL, mWindow, 25, 25,
                                    580, 300, MwNtitle, "Settings", NULL);
  MwAddUserHandler(sub->subwindow, MwNcloseHandler, ControlPanelWindow::remove,
                   sub);
  MwAddUserHandler(sub->subwindow, MwNresizeHandler, ControlPanelWindow::resize,
                   sub);

  sub->refresh_items();

  ctrl_panel_windows.push_back(sub);
};

void ProgmanWindow::ControlPanelWindow::refresh_items() {
  if (items) {
    MwDestroyWidget(items);
  }

  items = MwVaCreateWidget(MwListBoxClass, NULL, MwSubWindowGetFrame(subwindow),
                           5, 5, 580 - 20, 300 - 40, NULL);

  int index = 0;

  index = MwListBoxSet(items, -1, 0, "Setting");
  index = MwListBoxSet(items, index, -1, "Description");
  index = MwListBoxSet(items, index, -1, "Value");

  auto keys = this->win->mRegistry->ListKeys();
  if (keys.has_value()) {
    for (auto item : keys.value()) {
      index = -1;
      index = MwListBoxSet(items, -1, 0, item.name.c_str());
      index = MwListBoxSet(items, index, -1, item.description.c_str());
      std::string val;
      switch (item.type) {
      case TCCRegistryType::Bool:
        val = std::format(
            "{}", this->win->mRegistry->GetValue<bool>(item.name).value());
        break;
      case TCCRegistryType::Int:
        val = std::format(
            "{}", this->win->mRegistry->GetValue<int64_t>(item.name).value());
        break;
      case TCCRegistryType::Float:
        val = std::format(
            "{}", this->win->mRegistry->GetValue<double>(item.name).value());
        break;
      case TCCRegistryType::String:
        val = this->win->mRegistry->GetValue<std::string>(item.name).value();
        break;
      }
      index = MwListBoxSet(items, index, -1, val.c_str());
    }
  }
  MwVaApply(items, MwNhasHeading, 1, NULL);
  MwAddUserHandler(items, MwNlistBoxActivateHandler,
                   ControlPanelWindow::activate, this);

  MwListBoxSetWidth(items, 0, 100);
  MwListBoxSetWidth(items, 1, 350);
  MwListBoxSetWidth(items, 2, -(350 + 100));
}

void MWAPI ProgmanWindow::ControlPanelWindow::remove(MwWidget handle,
                                                     void *user, void *client) {
  ControlPanelWindow *sub = (ControlPanelWindow *)user;

  std::erase(sub->win->ctrl_panel_windows, sub);
  MwShow(sub->subwindow, MwFALSE);

  MwDestroyWidget(sub->items);
  MwDestroyWidget(sub->subwindow);
};
void MWAPI ProgmanWindow::ControlPanelWindow::resize(MwWidget handle,
                                                     void *user, void *client) {
  ControlPanelWindow *sub = (ControlPanelWindow *)user;

  auto folder_height = 160;
  auto keys = sub->win->mRegistry->ListKeys();
  if (keys.has_value()) {
    folder_height = keys.value().size() * 32;
  }
  auto width = MwGetInteger(handle, MwNwidth) - 20;
  auto height = MwGetInteger(handle, MwNheight) - 20;
  MwViewportSetSize(sub->items, width, height);
};

void MWAPI ProgmanWindow::ControlPanelWindow::activate(MwWidget handle,
                                                       void *user, void *call) {
  ControlPanelWindow *c = (ControlPanelWindow *)user;
  MwSizeHints hints = {0};
  hints.min_width = hints.max_width = 320;
  hints.min_height = hints.max_height = 400;

  c->ty_name = MwListBoxGet(handle, *(int *)call);

  auto registry = c->win->mRegistry;
  auto value = registry->GetValue(c->ty_name);

  c->popup = MwVaCreateWidget(
      MwSubWindowClass, NULL, MwSubWindowGetFrame(c->subwindow), 15, 15, 320,
      200, MwNtitle, "Value Set", MwNsizeHints, &hints, NULL);
  MwWidget frame = MwSubWindowGetFrame(c->popup);
  c->popup_box = MwVaCreateWidget(
      MwBoxClass, NULL, frame, 25, 25, MwGetInteger(frame, MwNwidth) - 50,
      MwGetInteger(frame, MwNheight) - 50, MwNorientation, MwVERTICAL, NULL);
  c->popup_text = MwVaCreateWidget(
      MwLabelClass, NULL, c->popup_box, MwDEFAULT, MwDEFAULT, 320, 200, MwNtext,
      std::format("Setting value of {}", c->ty_name).c_str(), MwNratio, 3,
      NULL);
  c->popup_sep1 = MwVaCreateWidget(MwSeparatorClass, NULL, c->popup_box,
                                   MwDEFAULT, MwDEFAULT, 320, 200, NULL);

  if (value.has_value()) {
    auto val = value.value();

    c->ty = tcc_registry_type_of(val);

    switch (c->ty) {
    case TCCRegistryType::Bool:
      c->popup_entry = MwVaCreateWidget(
          MwBoxClass, NULL, c->popup_box, MwDEFAULT, MwDEFAULT, 320, 200,
          MwNratio, 1, MwNorientation, MwHORIZONTAL, NULL);
      c->popup_checkbox_checkbox = MwVaCreateWidget(
          MwCheckBoxClass, NULL, c->popup_entry, MwDEFAULT, MwDEFAULT, 320, 200,
          MwNratio, 1, MwNchecked, std::get<bool>(val), NULL);
      c->popup_checkbox_text = MwVaCreateWidget(
          MwLabelClass, NULL, c->popup_entry, MwDEFAULT, MwDEFAULT, 320, 200,
          MwNratio, 1, MwNtext, "Enabled", NULL);
      break;
    case TCCRegistryType::Int:
    case TCCRegistryType::Float:
    case TCCRegistryType::String:
      c->popup_entry =
          MwVaCreateWidget(MwEntryClass, NULL, c->popup_box, MwDEFAULT,
                           MwDEFAULT, 320, 200, MwNratio, 1, NULL);
      switch (tcc_registry_type_of(val)) {
      case TCCRegistryType::Int:
        MwVaApply(c->popup_entry, MwNtext,
                  std::format("{}", std::get<int64_t>(val)).c_str(), NULL);
        break;
      case TCCRegistryType::Float:
        MwVaApply(c->popup_entry, MwNtext,
                  std::format("{}", std::get<double>(val)).c_str(), NULL);
        break;
      case TCCRegistryType::String:
        MwVaApply(c->popup_entry, MwNtext, std::get<std::string>(val).c_str(),
                  NULL);
        break;
      default:
        break;
      }
      break;
    }
  }
  c->popup_sep2 = MwVaCreateWidget(MwSeparatorClass, NULL, c->popup_box,
                                   MwDEFAULT, MwDEFAULT, 320, 200, NULL);
  c->popup_submit =
      MwVaCreateWidget(MwButtonClass, NULL, c->popup_box, MwDEFAULT, MwDEFAULT,
                       320, 200, MwNtext, "Change", MwNratio, 1, NULL);
  MwAddUserHandler(c->popup_submit, MwNactivateHandler,
                   ControlPanelWindow::submit, c);
  // MwAddUserHandler(MwMessageBoxGetChild(msgbox, MwMB_BUTTONOK),
  //                  MwNactivateHandler, destroy, msgbox);
}
void MWAPI ProgmanWindow::ControlPanelWindow::submit(MwWidget handle,
                                                     void *user, void *call) {
  ControlPanelWindow *c = (ControlPanelWindow *)user;

  MwWidget err = nullptr;

  switch (c->ty) {
  case TCCRegistryType::Bool: {
    bool val = MwGetInteger(c->popup_checkbox_checkbox, MwNchecked);
    c->win->mRegistry->SetValue(c->ty_name, TCCRegistryValue(val));
  } break;
  case TCCRegistryType::Int: {
    int64_t val = 0;
    try {
      val = std::stoll(MwGetString(c->popup_entry, MwNtext));
    } catch (std::invalid_argument ex) {
      err = MwMessageBox(c->popup, "input was not a number", "error",
                         MwMB_BUTTONOK);
    } catch (std::out_of_range ex) {
      err = MwMessageBox(c->popup, "input outside of range of an int64_t",
                         "error", MwMB_BUTTONOK);
    };
    if (!err) {
      c->win->mRegistry->SetValue(c->ty_name, TCCRegistryValue(val));
    }
  } break;
  case TCCRegistryType::Float: {
    double val = 0;
    try {
      val = std::stod(MwGetString(c->popup_entry, MwNtext));
    } catch (std::invalid_argument ex) {
      err = MwMessageBox(c->popup, "input was not a number", "error",
                         MwMB_BUTTONOK);
    } catch (std::out_of_range ex) {
      err = MwMessageBox(c->popup, "input outside of range of an number",
                         "error", MwMB_BUTTONOK);
    };
    if (!err) {
      c->win->mRegistry->SetValue(c->ty_name, TCCRegistryValue(val));
    }
  } break;
  case TCCRegistryType::String: {
    std::string val = MwGetString(c->popup_entry, MwNtext);
    c->win->mRegistry->SetValue(c->ty_name, val);
    break;
  }
  }
  if (!err) {
    MwDestroyWidget(c->popup);
    c->popup = nullptr;

    c->refresh_items();
  } else {
    auto ok = +[](MwWidget handle, void *user, void *call) {
      MwDestroyWidget(MwGetParent(handle));
    };

    MwAddUserHandler(MwMessageBoxGetChild(err, MwMB_BUTTONOK),
                     MwNactivateHandler, ok, NULL);
    MwAddUserHandler(err, MwNcloseHandler, ok, NULL);
  }
}