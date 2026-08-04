#include <gui/menus/bandplan.h>
#include <gui/widgets/bandplan.h>
#include <gui/gui.h>
#include <core.h>
#include <gui/style.h>
#include <utils/freq_formatting.h>

namespace bandplanmenu {
    int bandplanId;
    bool bandPlanEnabled;
    int bandPlanPos = 0;
    bool explorerOpen = false;
    bool explorerWasOpen = false;
    bool explorerHovered = false;
    bool explorerWasHovered = false;
    const char* bandPlanExplorerId = "Explorer##bandplan_explorer_popup";

    const char* bandPlanPosTxt = "Bottom\0Top\0";

    void init() {
        // todo: check if the bandplan wasn't removed
        if (bandplan::bandplanNames.size() == 0) {
            gui::waterfall.hideBandplan();
            return;
        }

        if (bandplan::bandplans.find(core::configManager.conf["bandPlan"]) != bandplan::bandplans.end()) {
            std::string name = core::configManager.conf["bandPlan"];
            bandplanId = std::distance(bandplan::bandplanNames.begin(), std::find(bandplan::bandplanNames.begin(),
                                                                                  bandplan::bandplanNames.end(), name));
            gui::waterfall.bandplan = &bandplan::bandplans[name];
        }
        else {
            gui::waterfall.bandplan = &bandplan::bandplans[bandplan::bandplanNames[0]];
        }

        bandPlanEnabled = core::configManager.conf["bandPlanEnabled"];
        bandPlanEnabled ? gui::waterfall.showBandplan() : gui::waterfall.hideBandplan();
        bandPlanPos = core::configManager.conf["bandPlanPos"];
        gui::waterfall.setBandPlanPos(bandPlanPos);
    }

    void draw(void* ctx) {
        float menuColumnWidth = ImGui::GetContentRegionAvail().x;
        ImGui::PushItemWidth(menuColumnWidth);
        if (ImGui::Combo("##_bandplan_name_", &bandplanId, bandplan::bandplanNameTxt.c_str())) {
            gui::waterfall.bandplan = &bandplan::bandplans[bandplan::bandplanNames[bandplanId]];
            core::configManager.acquire();
            core::configManager.conf["bandPlan"] = bandplan::bandplanNames[bandplanId];
            core::configManager.release(true);
        }
        ImGui::PopItemWidth();

        ImGui::LeftLabel("Position");
        ImGui::SetNextItemWidth(menuColumnWidth - ImGui::GetCursorPosX());
        if (ImGui::Combo("##_bandplan_pos_", &bandPlanPos, bandPlanPosTxt)) {
            gui::waterfall.setBandPlanPos(bandPlanPos);
            core::configManager.acquire();
            core::configManager.conf["bandPlanPos"] = bandPlanPos;
            core::configManager.release(true);
        }

        if (ImGui::Checkbox("Enabled", &bandPlanEnabled)) {
            bandPlanEnabled ? gui::waterfall.showBandplan() : gui::waterfall.hideBandplan();
            core::configManager.acquire();
            core::configManager.conf["bandPlanEnabled"] = bandPlanEnabled;
            core::configManager.release(true);
        }

        if (ImGui::Button("Explore")) {
            openExplorer();
        }

        bandplan::BandPlan_t plan = bandplan::bandplans[bandplan::bandplanNames[bandplanId]];
        ImGui::Text("Country: %s (%s)", plan.countryName.c_str(), plan.countryCode.c_str());
        ImGui::Text("Author: %s", plan.authorName.c_str());
    }

    void drawExplorer() {
        explorerWasOpen = explorerOpen || explorerWasOpen && ImGui::IsMouseDown(ImGuiMouseButton_Left);
        explorerWasHovered = explorerHovered || explorerWasHovered && ImGui::IsMouseDown(ImGuiMouseButton_Left);
        if (!explorerOpen) {
            explorerHovered = false;
            return;
        }

        ImVec2 viewportSize = ImGui::GetMainViewport()->Size;
        ImGui::SetNextWindowSize(ImVec2(0, 400.0f * style::uiScale), ImGuiCond_Appearing);
        ImGui::SetNextWindowPos(ImVec2(viewportSize.x / 2.0f, viewportSize.y / 2.0f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (ImGui::Begin("Bandplan explorer##bandplanExplorerWindow", &explorerOpen, ImGuiWindowFlags_None)) {

            explorerHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);

            if (ImGui::BeginTable("bandplan_explorer_popup_table", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit)) {
                bool tunableHovered = false;
                bool tunableClicked = false;
                double tuneToFreq = 0;
                ImDrawList* draw_list = ImGui::GetWindowDrawList();

                ImGui::TableSetupScrollFreeze(4, 1);
                ImGui::TableSetupColumn("Name");
                ImGui::TableSetupColumn("Type");
                ImGui::TableSetupColumn("Freq Start");
                ImGui::TableSetupColumn("Freq End");
                ImGui::TableHeadersRow();

                for (auto& band : gui::waterfall.bandplan->bands) {
                    ImU32 color;
                    if (bandplan::colorTable.find(band.type.c_str()) != bandplan::colorTable.end()) {
                        color = bandplan::colorTable[band.type].colorValue;
                    }
                    else {
                        color = IM_COL32(255, 255, 255, 255);
                    }

                    ImGui::TableNextRow();

                    // Name
                    ImGui::TableNextColumn();
                    ImVec2 cursorPos = ImGui::GetCursorScreenPos();
                    ImVec2 rectSize = ImGui::CalcTextSize("0");
                    draw_list->AddRectFilled(cursorPos, ImVec2(cursorPos.x + (rectSize.y / 2), cursorPos.y + rectSize.y), color, 0.0f);
                    ImGui::SetCursorPosX(ImGui::GetCursorPos().x + rectSize.y + ImGui::CalcTextSize(" ").x);
                    ImGui::TextUnformatted(band.name.c_str());

                    // Type
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(band.type.c_str());

                    // Start freq
                    ImGui::TableNextColumn();

                    if (ImGui::Selectable(utils::formatFreq(band.start).c_str(), false, ImGuiSelectableFlags_None)) {
                        tunableClicked = true;
                        tuneToFreq = band.start;
                    }

                    if (ImGui::IsItemHovered()) {
                        tunableHovered = true;
                    }

                    // End freq
                    ImGui::TableNextColumn();

                    if (ImGui::Selectable(utils::formatFreq(band.end).c_str(), false, ImGuiSelectableFlags_None)) {
                        tunableClicked = true;
                        tuneToFreq = band.end;
                    }

                    if (ImGui::IsItemHovered()) {
                        tunableHovered = true;
                    }
                }

                ImGui::EndTable();

                if (tunableHovered) {
                    ImGui::SetTooltip("Tune to");
                }

                if (tunableClicked) {
                    tuner::tune(gui::mainWindow.getTuningMode(), gui::waterfall.selectedVFO, tuneToFreq);
                }
            }
        }
        ImGui::End();
    }

    void openExplorer() {
        explorerOpen = true;
    }
    bool isExplorerOpen() {
        return explorerOpen;
    }
    bool isExplorerHovered() {
        return explorerHovered;
    }
    bool wasExplorerOpen() {
        return explorerWasOpen;
    }
    bool wasExplorerHovered() {
        return explorerWasHovered;
    }
};