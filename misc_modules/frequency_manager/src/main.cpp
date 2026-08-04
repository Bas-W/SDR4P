#include <imgui.h>
#include <utils/flog.h>
#include <module.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <core.h>
#include <thread>
#include <radio_interface.h>
#include <signal_path/signal_path.h>
#include <vector>
#include <gui/tuner.h>
#include <gui/file_dialogs.h>
#include <utils/freq_formatting.h>
#include <gui/dialogs/dialog_box.h>
#include <fstream>
#include "Tracy.hpp"

//TODO: Add bookmark list color (Displayed as ribbon on bookmark label)
//TODO: Improve frequency manager menu UI
//TODO: Handle group size exceeded

SDRPP_MOD_INFO{
    /* Name:            */ "frequency_manager",
    /* Description:     */ "Frequency manager module for SDR++",
    /* Author:          */ "Ryzerth;Zimm;Bas-W",
    /* Version:         */ 0, 3, 1,
    /* Max instances    */ 1
};

struct FrequencyBookmark {
    double frequency;
    double bandwidth;
    int mode;
    float color[3];
    bool selected;
};

struct WaterfallBookmark {
    std::string listName;
    std::string bookmarkName;
    FrequencyBookmark bookmark;
};

ConfigManager config;

const char* demodModeList[] = {
    "NFM",
    "WFM",
    "AM",
    "DSB",
    "USB",
    "CW",
    "LSB",
    "RAW"
};

const char* demodModeListTxt = "NFM\0WFM\0AM\0DSB\0USB\0CW\0LSB\0RAW\0";

enum {
    BOOKMARK_DISP_MODE_OFF,
    BOOKMARK_DISP_MODE_TOP,
    BOOKMARK_DISP_MODE_BOTTOM,
    _BOOKMARK_DISP_MODE_COUNT
};

const char* bookmarkDisplayModesTxt = "Off\0Top\0Bottom\0";
constexpr float bookmarkDefaultColor[3] = {1.0f, 1.0f, 0.0f};
constexpr ImU32 bookmarkTextColor = IM_COL32(25, 25, 25, 255);
constexpr ImU32 bookmarkHoveredHighlightColor = IM_COL32(220, 220, 220, 200);
constexpr float bookmarkNameTextPadding = 5.0f;
constexpr float bookmarkLabelRounding = 5.0f;
constexpr float bookmarkHoveredOutlineThickness = 2.0f;

constexpr float bookmarkGroupPadding = 1.0f;

class FrequencyManagerModule : public ModuleManager::Instance {
public:
    bool anyLabelHovered = false;
    WaterfallBookmark hoveredBookmark;
    std::string hoveredBookmarkName;

    FrequencyManagerModule(std::string name) {
        this->name = name;

        config.acquire();
        std::string selList = config.conf["selectedList"];
        bookmarkDisplayMode = config.conf["bookmarkDisplayMode"];
        config.release();

        refreshLists();
        loadByName(selList);
        refreshWaterfallBookmarks();

        fftRedrawHandler.ctx = this;
        fftRedrawHandler.handler = fftRedraw;
        inputHandler.ctx = this;
        inputHandler.handler = fftInput;

        gui::menu.registerEntry(name, menuHandler, this, NULL);
        gui::waterfall.onFFTRedraw.bindHandler(&fftRedrawHandler);
        gui::waterfall.onInputProcess.bindHandler(&inputHandler);
    }

    ~FrequencyManagerModule() {
        gui::menu.removeEntry(name);
        gui::waterfall.onFFTRedraw.unbindHandler(&fftRedrawHandler);
        gui::waterfall.onInputProcess.unbindHandler(&inputHandler);
    }

    void postInit() {}

    void enable() {
        ZoneScoped;
        enabled = true;
    }

    void disable() {
        ZoneScoped;
        enabled = false;
    }

    bool isEnabled() {
        ZoneScoped;
        return enabled;
    }

private:
    static void applyBookmark(FrequencyBookmark bm, std::string vfoName) {
        ZoneScoped;
        if (vfoName == "") {
            // TODO: Replace with proper tune call
            gui::waterfall.setCenterFrequency(bm.frequency);
            gui::waterfall.centerFreqMoved = true;
        }
        else {
            if (core::modComManager.interfaceExists(vfoName)) {
                if (core::modComManager.getModuleName(vfoName) == "radio") {
                    int mode = bm.mode;
                    float bandwidth = bm.bandwidth;
                    core::modComManager.callInterface(vfoName, RADIO_IFACE_CMD_SET_MODE, &mode, NULL);
                    core::modComManager.callInterface(vfoName, RADIO_IFACE_CMD_SET_BANDWIDTH, &bandwidth, NULL);
                }
            }
            tuner::tune(tuner::TUNER_MODE_NORMAL, vfoName, bm.frequency);
        }
    }

    static FrequencyBookmark parseBookmark(const json& bm) {
        ZoneScoped;
        FrequencyBookmark fbm;

        try {
            fbm.frequency = bm.value("frequency", 0.0);
            fbm.bandwidth = bm.value("bandwidth", 0.0);
            fbm.mode = bm.value("mode", 0);
        } catch (json::exception& e) {
            flog::error("Error loading bookmark data: {}", e.what());
        }

        memcpy(fbm.color, bookmarkDefaultColor, sizeof(bookmarkDefaultColor));

        try {
            if (bm.contains("color") && bm["color"].is_object()) {
                const auto& c = bm["color"];
                fbm.color[0] = c.value("r", bookmarkDefaultColor[0]);
                fbm.color[1] = c.value("g", bookmarkDefaultColor[1]);
                fbm.color[2] = c.value("b", bookmarkDefaultColor[2]);
            }
        } catch (json::exception& e) {
            flog::error("Error loading bookmark color: {}", e.what());
        }

        fbm.selected = false;
        return fbm;
    }

    bool bookmarkEditDialog() {
        ZoneScoped;
        bool open = true;
        gui::mainWindow.lockWaterfallControls = true;

        std::string id = "Edit##freq_manager_edit_popup_" + name;
        ImGui::OpenPopup(id.c_str());

        char nameBuf[1024];
        strcpy(nameBuf, editedBookmarkName.c_str());

        if (ImGui::BeginPopup(id.c_str(), ImGuiWindowFlags_NoResize)) {
            ImGui::BeginTable(("freq_manager_edit_table" + name).c_str(), 2);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::LeftLabel("Name");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(200);
            if (ImGui::InputText(("##freq_manager_edit_name" + name).c_str(), nameBuf, 1023)) {
                editedBookmarkName = nameBuf;
            }

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::LeftLabel("Frequency");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(200);
            ImGui::InputDouble(("##freq_manager_edit_freq" + name).c_str(), &editedBookmark.frequency);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::LeftLabel("Bandwidth");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(200);
            ImGui::InputDouble(("##freq_manager_edit_bw" + name).c_str(), &editedBookmark.bandwidth);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::LeftLabel("Mode");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(200);

            ImGui::Combo(("##freq_manager_edit_mode" + name).c_str(), &editedBookmark.mode, demodModeListTxt);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::LeftLabel("Color");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(200);
            ImGui::ColorEdit3("##freq_manager_edit_color", editedBookmark.color, ImGuiColorEditFlags_DisplayRGB);

            ImGui::EndTable();

            bool applyDisabled = (strlen(nameBuf) == 0) || (bookmarks.find(editedBookmarkName) != bookmarks.end() && editedBookmarkName != firstEditedBookmarkName);
            if (applyDisabled) { style::beginDisabled(); }
            if (ImGui::Button("Apply")) {
                open = false;

                // If editing, delete the original one
                if (editOpen) {
                    bookmarks.erase(firstEditedBookmarkName);
                }
                bookmarks[editedBookmarkName] = editedBookmark;

                saveByName(selectedListName);
            }
            if (applyDisabled) { style::endDisabled(); }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                open = false;
            }
            ImGui::EndPopup();
        }
        return open;
    }

    bool newListDialog() {
        ZoneScoped;
        bool open = true;
        gui::mainWindow.lockWaterfallControls = true;

        float menuWidth = ImGui::GetContentRegionAvail().x;

        std::string id = "New##freq_manager_new_popup_" + name;
        ImGui::OpenPopup(id.c_str());

        char nameBuf[1024];
        strcpy(nameBuf, editedListName.c_str());

        if (ImGui::BeginPopup(id.c_str(), ImGuiWindowFlags_NoResize)) {
            ImGui::LeftLabel("Name");
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::InputText(("##freq_manager_edit_name" + name).c_str(), nameBuf, 1023)) {
                editedListName = nameBuf;
            }

            bool alreadyExists = (std::find(listNames.begin(), listNames.end(), editedListName) != listNames.end());

            if (strlen(nameBuf) == 0 || alreadyExists) { style::beginDisabled(); }
            if (ImGui::Button("Apply")) {
                open = false;

                config.acquire();
                if (renameListOpen) {
                    config.conf["lists"][editedListName] = config.conf["lists"][firstEditedListName];
                    config.conf["lists"].erase(firstEditedListName);
                }
                else {
                    config.conf["lists"][editedListName]["showOnWaterfall"] = true;
                    config.conf["lists"][editedListName]["bookmarks"] = json::object();
                }
                refreshWaterfallBookmarks(false);
                config.release(true);
                refreshLists();
                loadByName(editedListName);
            }
            if (strlen(nameBuf) == 0 || alreadyExists) { style::endDisabled(); }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                open = false;
            }
            ImGui::EndPopup();
        }
        return open;
    }

    bool selectListsDialog() {
        ZoneScoped;
        gui::mainWindow.lockWaterfallControls = true;

        float menuWidth = ImGui::GetContentRegionAvail().x;

        std::string id = "Select lists##freq_manager_sel_popup_" + name;
        ImGui::OpenPopup(id.c_str());

        bool open = true;

        if (ImGui::BeginPopup(id.c_str(), ImGuiWindowFlags_NoResize)) {
            // No need to lock config since we're not modifying anything and there's only one instance
            for (auto [listName, list] : config.conf["lists"].items()) {
                bool shown = list["showOnWaterfall"];
                if (ImGui::Checkbox((listName + "##freq_manager_sel_list_").c_str(), &shown)) {
                    config.acquire();
                    config.conf["lists"][listName]["showOnWaterfall"] = shown;
                    refreshWaterfallBookmarks(false);
                    config.release(true);
                }
            }

            if (ImGui::Button("Ok")) {
                open = false;
            }
            ImGui::EndPopup();
        }
        return open;
    }

    void refreshLists() {
        ZoneScoped;
        listNames.clear();
        listNamesTxt = "";

        config.acquire();
        for (auto [_name, list] : config.conf["lists"].items()) {
            ZoneScopedN("refreshLists_loadItem");
            listNames.push_back(_name);
            listNamesTxt += _name;
            listNamesTxt += '\0';
        }
        config.release();
    }

    void refreshWaterfallBookmarks(bool lockConfig = true) {
        ZoneScoped;
        if (lockConfig) { config.acquire(); }
        waterfallBookmarks.clear();
        for (auto [listName, list] : config.conf["lists"].items()) {
            ZoneScopedN("refreshWaterfallBookmarks_loadItem");
            if (!((bool)list["showOnWaterfall"])) { continue; }
            WaterfallBookmark wbm;
            wbm.listName = listName;
            for (auto [bookmarkName, bm] : config.conf["lists"][listName]["bookmarks"].items()) {
                wbm.bookmarkName = bookmarkName;
                wbm.bookmark = parseBookmark(bm);
                waterfallBookmarks.push_back(wbm);
            }
        }
        if (lockConfig) { config.release(); }
    }

    void loadFirst() {
        ZoneScoped;
        if (listNames.size() > 0) {
            loadByName(listNames[0]);
            return;
        }
        selectedListName = "";
        selectedListId = 0;
    }

    void loadByName(std::string listName) {
        ZoneScoped;
        bookmarks.clear();
        if (std::find(listNames.begin(), listNames.end(), listName) == listNames.end()) {
            selectedListName = "";
            selectedListId = 0;
            loadFirst();
            return;
        }
        selectedListId = std::distance(listNames.begin(), std::find(listNames.begin(), listNames.end(), listName));
        selectedListName = listName;
        config.acquire();
        for (const auto& [bookmarkName, bm] : config.conf["lists"][listName]["bookmarks"].items()) {
            bookmarks[bookmarkName] = parseBookmark(bm);
        }
        config.release();
    }

    void saveByName(std::string listName) {
        ZoneScoped;
        config.acquire();
        config.conf["lists"][listName]["bookmarks"] = json::object();
        for (auto [bmName, bm] : bookmarks) {
            ZoneScopedN("saveByName_saveItem");
            config.conf["lists"][listName]["bookmarks"][bmName]["frequency"] = bm.frequency;
            config.conf["lists"][listName]["bookmarks"][bmName]["bandwidth"] = bm.bandwidth;
            config.conf["lists"][listName]["bookmarks"][bmName]["mode"] = bm.mode;
            config.conf["lists"][listName]["bookmarks"][bmName]["color"]["r"] = bm.color[0];
            config.conf["lists"][listName]["bookmarks"][bmName]["color"]["g"] = bm.color[1];
            config.conf["lists"][listName]["bookmarks"][bmName]["color"]["b"] = bm.color[2];
        }
        refreshWaterfallBookmarks(false);
        config.release(true);
    }

    static void menuHandler(void* ctx) {
        ZoneScoped;
        FrequencyManagerModule* _this = (FrequencyManagerModule*)ctx;
        float menuWidth = ImGui::GetContentRegionAvail().x;

        // TODO: Replace with something that won't iterate every frame
        std::vector<std::string> selectedNames;
        for (auto& [name, bm] : _this->bookmarks) {
            if (bm.selected) { selectedNames.push_back(name); }
        }

        float lineHeight = ImGui::GetTextLineHeightWithSpacing();

        float btnSize = ImGui::CalcTextSize("Rename").x + 8;
        ImGui::SetNextItemWidth(menuWidth - 24 - (2 * lineHeight) - btnSize);
        if (ImGui::Combo(("##freq_manager_list_sel" + _this->name).c_str(), &_this->selectedListId, _this->listNamesTxt.c_str())) {
            _this->loadByName(_this->listNames[_this->selectedListId]);
            config.acquire();
            config.conf["selectedList"] = _this->selectedListName;
            config.release(true);
        }
        ImGui::SameLine();
        if (_this->listNames.size() == 0) { style::beginDisabled(); }
        if (ImGui::Button(("Rename##_freq_mgr_ren_lst_" + _this->name).c_str(), ImVec2(btnSize, 0))) {
            _this->firstEditedListName = _this->listNames[_this->selectedListId];
            _this->editedListName = _this->firstEditedListName;
            _this->renameListOpen = true;
        }
        if (_this->listNames.size() == 0) { style::endDisabled(); }
        ImGui::SameLine();
        if (ImGui::Button(("+##_freq_mgr_add_lst_" + _this->name).c_str(), ImVec2(lineHeight, 0))) {
            // Find new unique default name
            if (std::find(_this->listNames.begin(), _this->listNames.end(), "New List") == _this->listNames.end()) {
                _this->editedListName = "New List";
            }
            else {
                char buf[64];
                for (int i = 1; i < 1000; i++) {
                    sprintf(buf, "New List (%d)", i);
                    if (std::find(_this->listNames.begin(), _this->listNames.end(), buf) == _this->listNames.end()) { break; }
                }
                _this->editedListName = buf;
            }
            _this->newListOpen = true;
        }
        ImGui::SameLine();
        if (_this->selectedListName == "") { style::beginDisabled(); }
        if (ImGui::Button(("-##_freq_mgr_del_lst_" + _this->name).c_str(), ImVec2(lineHeight, 0))) {
            _this->deleteListOpen = true;
        }
        if (_this->selectedListName == "") { style::endDisabled(); }

        // List delete confirmation
        if (ImGui::GenericDialog(("freq_manager_del_list_confirm" + _this->name).c_str(), _this->deleteListOpen, GENERIC_DIALOG_BUTTONS_YES_NO, [_this]() {
                ImGui::Text("Deleting list named \"%s\". Are you sure?", _this->selectedListName.c_str());
            }) == GENERIC_DIALOG_BUTTON_YES) {
            config.acquire();
            config.conf["lists"].erase(_this->selectedListName);
            _this->refreshWaterfallBookmarks(false);
            config.release(true);
            _this->refreshLists();
            _this->selectedListId = std::clamp<int>(_this->selectedListId, 0, _this->listNames.size());
            if (_this->listNames.size() > 0) {
                _this->loadByName(_this->listNames[_this->selectedListId]);
            }
            else {
                _this->selectedListName = "";
            }
        }

        if (_this->selectedListName == "") { style::beginDisabled(); }
        //Draw buttons on top of the list
        ImGui::BeginTable(("freq_manager_btn_table" + _this->name).c_str(), 3);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        if (ImGui::Button(("Add##_freq_mgr_add_" + _this->name).c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
            // If there's no VFO selected, just save the center freq
            if (gui::waterfall.selectedVFO == "") {
                _this->editedBookmark.frequency = gui::waterfall.getCenterFrequency();
                _this->editedBookmark.bandwidth = 0;
                _this->editedBookmark.mode = 7;
            }
            else {
                _this->editedBookmark.frequency = gui::waterfall.getCenterFrequency() + sigpath::vfoManager.getOffset(gui::waterfall.selectedVFO);
                _this->editedBookmark.bandwidth = sigpath::vfoManager.getBandwidth(gui::waterfall.selectedVFO);
                _this->editedBookmark.mode = 7;
                if (core::modComManager.getModuleName(gui::waterfall.selectedVFO) == "radio") {
                    int mode;
                    core::modComManager.callInterface(gui::waterfall.selectedVFO, RADIO_IFACE_CMD_GET_MODE, NULL, &mode);
                    _this->editedBookmark.mode = mode;
                }
            }

             memcpy(_this->editedBookmark.color, bookmarkDefaultColor, sizeof(bookmarkDefaultColor));

            _this->editedBookmark.selected = false;

            _this->createOpen = true;

            // Find new unique default name
            if (_this->bookmarks.find("New Bookmark") == _this->bookmarks.end()) {
                _this->editedBookmarkName = "New Bookmark";
            }
            else {
                char buf[64];
                for (int i = 1; i < 1000; i++) {
                    sprintf(buf, "New Bookmark (%d)", i);
                    if (_this->bookmarks.find(buf) == _this->bookmarks.end()) { break; }
                }
                _this->editedBookmarkName = buf;
            }
        }

        ImGui::TableSetColumnIndex(1);
        if (selectedNames.size() == 0 && _this->selectedListName != "") { style::beginDisabled(); }
        if (ImGui::Button(("Remove##_freq_mgr_rem_" + _this->name).c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
            _this->deleteBookmarksOpen = true;
        }
        if (selectedNames.size() == 0 && _this->selectedListName != "") { style::endDisabled(); }
        ImGui::TableSetColumnIndex(2);
        if (selectedNames.size() != 1 && _this->selectedListName != "") { style::beginDisabled(); }
        if (ImGui::Button(("Edit##_freq_mgr_edt_" + _this->name).c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
            _this->editOpen = true;
            _this->editedBookmark = _this->bookmarks[selectedNames[0]];
            _this->editedBookmarkName = selectedNames[0];
            _this->firstEditedBookmarkName = selectedNames[0];
        }
        if (selectedNames.size() != 1 && _this->selectedListName != "") { style::endDisabled(); }

        ImGui::EndTable();

        // Bookmark delete confirm dialog
        // List delete confirmation
        if (ImGui::GenericDialog(("freq_manager_del_list_confirm" + _this->name).c_str(), _this->deleteBookmarksOpen, GENERIC_DIALOG_BUTTONS_YES_NO, [_this]() {
                ImGui::TextUnformatted("Deleting selected bookmaks. Are you sure?");
            }) == GENERIC_DIALOG_BUTTON_YES) {
            for (auto& _name : selectedNames) { _this->bookmarks.erase(_name); }
            _this->saveByName(_this->selectedListName);
        }

        // Bookmark list
        if (ImGui::BeginTable(("freq_manager_bkm_table" + _this->name).c_str(), 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, 200.0f * style::uiScale))) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 3.0f);
            ImGui::TableSetupColumn("Frequency", ImGuiTableColumnFlags_WidthStretch, 2.0f);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupScrollFreeze(3, 1);
            ImGui::TableHeadersRow();
            for (auto& [name, bm] : _this->bookmarks) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImVec2 min = ImGui::GetCursorPos();

                if (ImGui::Selectable((name + "##_freq_mgr_bkm_name_" + _this->name).c_str(), &bm.selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_SelectOnClick)) {
                    // if shift or control isn't pressed, deselect all others
                    if (!ImGui::GetIO().KeyShift && !ImGui::GetIO().KeyCtrl) {
                        for (auto& [_name, _bm] : _this->bookmarks) {
                            if (name == _name) { continue; }
                            _bm.selected = false;
                        }
                    }
                }
                if (ImGui::TableGetHoveredColumn() >= 0 && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    applyBookmark(bm, gui::waterfall.selectedVFO);
                }

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", utils::formatFreq(bm.frequency).c_str());

                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%s", demodModeList[bm.mode]);
                ImVec2 max = ImGui::GetCursorPos();
            }
            ImGui::EndTable();
        }


        if (selectedNames.size() != 1 && _this->selectedListName != "") { style::beginDisabled(); }
        if (ImGui::Button(("Apply##_freq_mgr_apply_" + _this->name).c_str(), ImVec2(menuWidth, 0))) {
            FrequencyBookmark& bm = _this->bookmarks[selectedNames[0]];
            applyBookmark(bm, gui::waterfall.selectedVFO);
            bm.selected = false;
        }
        if (selectedNames.size() != 1 && _this->selectedListName != "") { style::endDisabled(); }

        //Draw import and export buttons
        ImGui::BeginTable(("freq_manager_bottom_btn_table" + _this->name).c_str(), 2);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        if (ImGui::Button(("Import##_freq_mgr_imp_" + _this->name).c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0)) && !_this->importOpen) {
            _this->importOpen = true;
            _this->importDialog = new pfd::open_file("Import bookmarks", "", { "JSON Files (*.json)", "*.json", "All Files", "*" }, pfd::opt::multiselect);
        }

        ImGui::TableSetColumnIndex(1);
        if (selectedNames.size() == 0 && _this->selectedListName != "") { style::beginDisabled(); }
        if (ImGui::Button(("Export##_freq_mgr_exp_" + _this->name).c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0)) && !_this->exportOpen) {
            _this->exportedBookmarks = json::object();
            config.acquire();
            for (auto& _name : selectedNames) {
                _this->exportedBookmarks["bookmarks"][_name] = config.conf["lists"][_this->selectedListName]["bookmarks"][_name];
            }
            config.release();
            _this->exportOpen = true;
            _this->exportDialog = new pfd::save_file("Export bookmarks", "", { "JSON Files (*.json)", "*.json", "All Files", "*" });
        }
        if (selectedNames.size() == 0 && _this->selectedListName != "") { style::endDisabled(); }
        ImGui::EndTable();

        if (ImGui::Button(("Select displayed lists##_freq_mgr_exp_" + _this->name).c_str(), ImVec2(menuWidth, 0))) {
            _this->selectListsOpen = true;
        }

        ImGui::LeftLabel("Bookmark display mode");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::Combo(("##_freq_mgr_dms_" + _this->name).c_str(), &_this->bookmarkDisplayMode, bookmarkDisplayModesTxt)) {
            config.acquire();
            config.conf["bookmarkDisplayMode"] = _this->bookmarkDisplayMode;
            config.release(true);
        }

        if (_this->selectedListName == "") { style::endDisabled(); }

        if (_this->createOpen) {
            _this->createOpen = _this->bookmarkEditDialog();
        }

        if (_this->editOpen) {
            _this->editOpen = _this->bookmarkEditDialog();
        }

        if (_this->newListOpen) {
            _this->newListOpen = _this->newListDialog();
        }

        if (_this->renameListOpen) {
            _this->renameListOpen = _this->newListDialog();
        }

        if (_this->selectListsOpen) {
            _this->selectListsOpen = _this->selectListsDialog();
        }

        // Handle import and export
        if (_this->importOpen && _this->importDialog->ready()) {
            _this->importOpen = false;
            std::vector<std::string> paths = _this->importDialog->result();
            if (paths.size() > 0 && _this->listNames.size() > 0) {
                _this->importBookmarks(paths[0]);
            }
            delete _this->importDialog;
        }
        if (_this->exportOpen && _this->exportDialog->ready()) {
            _this->exportOpen = false;
            std::string path = _this->exportDialog->result();
            if (path != "") {
                _this->exportBookmarks(path);
            }
            delete _this->exportDialog;
        }
    }

    struct BookmarkLabel {
        WaterfallBookmark* ptr_bm;
        float minX;
        float maxX;
    };

    struct BookmarkGroup {
        std::vector<BookmarkLabel> vec_labels;
        float minX;
        float maxX;
    };

    // TODO: ImGui color alpha channel overrides may be incorrect
    // TODO: Dont draw indicator of selected (tuned) bookmark
    static void fftRedraw(ImGui::WaterFall::FFTRedrawArgs args, void* ctx) {
        ZoneScoped;
        FrequencyManagerModule* _this = (FrequencyManagerModule*)ctx;
        if (_this->bookmarkDisplayMode == BOOKMARK_DISP_MODE_OFF) { return; }
        if (_this->waterfallBookmarks.empty()) { return; }

        std::vector<BookmarkLabel> visibleLabels;

        // Filter visible bookmarks
        for (auto& bm : _this->waterfallBookmarks) {
            double centerXpos = args.min.x + std::round((bm.bookmark.frequency - args.lowFreq) * args.freqToPixelRatio);

            ImVec2 nameSize = ImGui::CalcTextSize(bm.bookmarkName.c_str());
            float rectMinX = centerXpos - (nameSize.x / 2) - bookmarkNameTextPadding * style::uiScale;
            float rectMaxX = centerXpos + (nameSize.x / 2) + bookmarkNameTextPadding * style::uiScale;

            if (rectMaxX > args.min.x && rectMinX < args.max.x) {
                visibleLabels.push_back({ &bm, rectMinX, rectMaxX });
            }
        }

        if (visibleLabels.empty()) { return; }

        // Sort left to right
        std::sort(visibleLabels.begin(), visibleLabels.end(), [](const auto& a, const auto& b) {
            return a.minX < b.minX;
        });

        std::vector<BookmarkGroup> groups;
        groups.push_back({ { visibleLabels[0] }, visibleLabels[0].minX, visibleLabels[0].maxX });

        // Sort overlapping bookmarks into groups
        if (visibleLabels.size() > 1) {
            for (auto it = visibleLabels.begin() + 1; it != visibleLabels.end(); ++it) {
                auto& label = *it;

                if (label.minX <= groups.back().maxX + bookmarkGroupPadding * style::uiScale) {
                    groups.back().vec_labels.push_back(label);
                    groups.back().maxX = std::max(groups.back().maxX, label.maxX);
                }
                else {
                    groups.push_back({ { label }, label.minX, label.maxX });
                }
            }
        }

        _this->anyLabelHovered = false;

        bool bookmarksAlignTop = _this->bookmarkDisplayMode == BOOKMARK_DISP_MODE_TOP;

        // Draw bookmarks
        for (auto& group : groups) {
            for (int i = 0; i < group.vec_labels.size(); i++) {
                auto& label = group.vec_labels.at(i);

                ImVec4 bookMarkColor = ImVec4(label.ptr_bm->bookmark.color[0], label.ptr_bm->bookmark.color[1], label.ptr_bm->bookmark.color[2], 1.0f);

                float labelSizeX = label.maxX - label.minX;
                float centerXpos = label.minX + (labelSizeX) / 2.0f;

                float nameSizeY = ImGui::GetTextLineHeight();

                ImVec2 rectMin;
                ImVec2 rectMax;

                if (bookmarksAlignTop) {
                    float vfoZoneOffset = bookmarkGroupPadding * style::uiScale * 5.0f;
                    rectMin = ImVec2(label.minX, gui::waterfall.vfoZoneMax.y + vfoZoneOffset + (nameSizeY + bookmarkGroupPadding * style::uiScale) * i);
                    rectMax = ImVec2(label.maxX, gui::waterfall.vfoZoneMax.y + vfoZoneOffset + nameSizeY * (i + 1) + bookmarkGroupPadding * style::uiScale * i);
                }
                else {
                    rectMin = ImVec2(label.minX, args.max.y - nameSizeY * (i + 1) - bookmarkGroupPadding * style::uiScale * i);
                    rectMax = ImVec2(label.maxX, args.max.y - (nameSizeY + bookmarkGroupPadding * style::uiScale) * i);
                }

                if (rectMax.y > args.max.y) {break;}

                ImVec2 clampedRectMin = ImVec2(std::clamp<double>(rectMin.x, args.min.x, args.max.x), rectMin.y);
                ImVec2 clampedRectMax = ImVec2(std::clamp<double>(rectMax.x, args.min.x, args.max.x), rectMax.y);

                ImVec2 lineMarkerMin = ImVec2(centerXpos, bookmarksAlignTop ? clampedRectMax.y : args.min.y);
                ImVec2 lineMarkerMax = ImVec2(centerXpos, bookmarksAlignTop ? args.max.y : clampedRectMin.y);

                // Draw label rect
                args.window->DrawList->AddRectFilled(clampedRectMin, clampedRectMax, ImGui::ColorConvertFloat4ToU32(bookMarkColor), bookmarkLabelRounding);

                // Draw outline on hover
                if (ImGui::IsMouseHoveringRect(clampedRectMin, clampedRectMax)) {
                    _this->anyLabelHovered = true;
                    _this->hoveredBookmark = *label.ptr_bm;
                    _this->hoveredBookmarkName = label.ptr_bm->bookmarkName.c_str();

                    // Line marker outline
                    args.window->DrawList->AddRectFilled(
                        ImVec2(std::clamp<double>(centerXpos - bookmarkHoveredOutlineThickness, args.min.x, args.max.x), lineMarkerMin.y),
                        ImVec2(std::clamp<double>(centerXpos + bookmarkHoveredOutlineThickness + 1, args.min.x, args.max.x), lineMarkerMax.y),
                        bookmarkHoveredHighlightColor);

                    // Label rect highlight
                    args.window->DrawList->AddRectFilled(
                        ImVec2(std::clamp<double>(rectMin.x, args.min.x, args.max.x), rectMin.y),
                        ImVec2(std::clamp<double>(rectMax.x, args.min.x, args.max.x), rectMax.y),
                        bookmarkHoveredHighlightColor,
                        bookmarkLabelRounding);
                }
                else if (label.ptr_bm->bookmark.selected) {
                    args.window->DrawList->AddRectFilled(
                        ImVec2(std::clamp<double>(rectMin.x, args.min.x, args.max.x), rectMin.y),
                        ImVec2(std::clamp<double>(rectMax.x, args.min.x, args.max.x), rectMax.y),
                        (bookmarkHoveredHighlightColor ^ 0xff) | static_cast<int>((bookmarkHoveredHighlightColor & 0xff) * 0.7f),
                        bookmarkLabelRounding);
                }

                // Draw center line
                args.window->DrawList->AddLine(lineMarkerMin, lineMarkerMax, ImGui::ColorConvertFloat4ToU32(bookMarkColor));

                // Draw text
                if (rectMin.x >= args.min.x && rectMax.x <= args.max.x) {
                    args.window->DrawList->AddText(ImVec2(rectMin.x + bookmarkNameTextPadding * style::uiScale, rectMin.y), bookmarkTextColor, label.ptr_bm->bookmarkName.c_str());
                }
            }
        }
    }

    bool mouseAlreadyDown = false;
    bool mouseClickedInLabel = false;
    static void fftInput(ImGui::WaterFall::InputHandlerArgs args, void* ctx) {
        ZoneScoped;
        FrequencyManagerModule* _this = (FrequencyManagerModule*)ctx;
        if (_this->bookmarkDisplayMode == BOOKMARK_DISP_MODE_OFF) { return; }

        if (_this->mouseClickedInLabel) {
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                _this->mouseClickedInLabel = false;
            }
            gui::waterfall.inputHandled = true;
            return;
        }

        // Check if mouse was already down
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !_this->anyLabelHovered) {
            _this->mouseAlreadyDown = true;
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            _this->mouseAlreadyDown = false;
            _this->mouseClickedInLabel = false;
        }

        // If yes, cancel
        if (_this->mouseAlreadyDown || !_this->anyLabelHovered) { return; }

        gui::waterfall.inputHandled = true;

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            _this->mouseClickedInLabel = true;
            applyBookmark(_this->hoveredBookmark.bookmark, gui::waterfall.selectedVFO);
        }

        ImGui::BeginTooltip();
        ImGui::TextUnformatted(_this->hoveredBookmarkName.c_str());
        ImGui::Separator();
        ImGui::Text("List: %s", _this->hoveredBookmark.listName.c_str());
        ImGui::Text("Frequency: %s", utils::formatFreq(_this->hoveredBookmark.bookmark.frequency).c_str());
        ImGui::Text("Bandwidth: %s", utils::formatFreq(_this->hoveredBookmark.bookmark.bandwidth).c_str());
        ImGui::Text("Mode: %s", demodModeList[_this->hoveredBookmark.bookmark.mode]);
        ImGui::EndTooltip();
    }

    json exportedBookmarks;
    bool importOpen = false;
    bool exportOpen = false;
    pfd::open_file* importDialog;
    pfd::save_file* exportDialog;

    void importBookmarks(std::string path) {
        ZoneScoped;
        std::ifstream fs(path);
        json importBookmarks;
        fs >> importBookmarks;

        if (!importBookmarks.contains("bookmarks")) {
            flog::error("File does not contains any bookmarks");
            return;
        }

        if (!importBookmarks["bookmarks"].is_object()) {
            flog::error("Bookmark attribute is invalid");
            return;
        }

        // Load every bookmark
        for (auto const [_name, bm] : importBookmarks["bookmarks"].items()) {
            ZoneScopedN("importBookmarks_loadItem");
            if (bookmarks.find(_name) != bookmarks.end()) {
                flog::warn("Bookmark with the name '{0}' already exists in list, skipping", _name);
                continue;
            }
            bookmarks[_name] = parseBookmark(bm);
        }
        saveByName(selectedListName);

        fs.close();
    }

    void exportBookmarks(std::string path) {
        ZoneScoped;
        std::ofstream fs(path);
        fs << exportedBookmarks;
        fs.close();
    }

    std::string name;
    bool enabled = true;
    bool createOpen = false;
    bool editOpen = false;
    bool newListOpen = false;
    bool renameListOpen = false;
    bool selectListsOpen = false;

    bool deleteListOpen = false;
    bool deleteBookmarksOpen = false;

    EventHandler<ImGui::WaterFall::FFTRedrawArgs> fftRedrawHandler;
    EventHandler<ImGui::WaterFall::InputHandlerArgs> inputHandler;

    std::map<std::string, FrequencyBookmark> bookmarks;

    std::string editedBookmarkName = "";
    std::string firstEditedBookmarkName = "";
    FrequencyBookmark editedBookmark;

    std::vector<std::string> listNames;
    std::string listNamesTxt = "";
    std::string selectedListName = "";
    int selectedListId = 0;

    std::string editedListName;
    std::string firstEditedListName;

    std::vector<WaterfallBookmark> waterfallBookmarks;

    int bookmarkDisplayMode = 0;
};

MOD_EXPORT void _INIT_() {
    ZoneScoped;
    json def = json({});
    def["selectedList"] = "General";
    def["bookmarkDisplayMode"] = BOOKMARK_DISP_MODE_TOP;
    def["lists"]["General"]["showOnWaterfall"] = true;
    def["lists"]["General"]["bookmarks"] = json::object();

    config.setPath(core::args["root"].s() + "/frequency_manager_config.json");
    config.load(def);
    config.enableAutoSave();

    // Check if of list and convert if they're the old type
    config.acquire();
    if (!config.conf.contains("bookmarkDisplayMode")) {
        config.conf["bookmarkDisplayMode"] = BOOKMARK_DISP_MODE_TOP;
    }
    for (auto [listName, list] : config.conf["lists"].items()) {
        if (list.contains("bookmarks") && list.contains("showOnWaterfall") && list["showOnWaterfall"].is_boolean()) { continue; }
        json newList;
        newList = json::object();
        newList["showOnWaterfall"] = true;
        newList["bookmarks"] = list;
        config.conf["lists"][listName] = newList;
    }
    config.release(true);
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    ZoneScoped;
    return new FrequencyManagerModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    ZoneScoped;
    delete (FrequencyManagerModule*)instance;
}

MOD_EXPORT void _END_() {
    ZoneScoped;
    config.disableAutoSave();
    config.save();
}
