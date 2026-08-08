#include <gui/main_window.h>
#include <gui/gui.h>
#include "imgui.h"
#include <stdio.h>
#include <thread>
#include <complex>
#include <gui/widgets/waterfall.h>
#include <gui/widgets/frequency_select.h>
#include <signal_path/iq_frontend.h>
#include <gui/icons.h>
#include <gui/widgets/bandplan.h>
#include <gui/style.h>
#include <config.h>
#include <signal_path/signal_path.h>
#include <core.h>
#include <gui/menus/source.h>
#include <gui/menus/display.h>
#include <gui/menus/bandplan.h>
#include <gui/menus/sink.h>
#include <gui/menus/vfo_color.h>
#include <gui/menus/module_manager.h>
#include <gui/menus/theme.h>
#include <gui/dialogs/credits.h>
#include <filesystem>
#include <signal_path/source.h>
#include <gui/dialogs/loading_screen.h>
#include <gui/colormaps.h>
#include <gui/widgets/snr_meter.h>
#include <gui/tuner.h>
#include "Tracy.hpp"

const int narrowMenuWidth = 70.0f * style::uiScale;
const int buttonWidth_normal = 35.0f * style::uiScale;
const int buttonPadding_normal = 2.0f;
const int edgeDragMargin = 6.0f * style::uiScale;

void MainWindow::init() {
    ZoneScoped;
    LoadingScreen::show("Initializing UI");
    gui::waterfall.init();
    gui::waterfall.setRawFFTSize(fftSize);

    credits::init();

    core::configManager.acquire();
    json menuElements = core::configManager.conf["menuElements"];
    std::string modulesDir = core::configManager.conf["modulesDirectory"];
    std::string resourcesDir = core::configManager.conf["resourcesDirectory"];
    core::configManager.release();

    // Assert that directories are absolute
    modulesDir = std::filesystem::absolute(modulesDir).string();
    resourcesDir = std::filesystem::absolute(resourcesDir).string();

    // Load menu elements
    gui::menu.order.clear();
    for (auto& elem : menuElements) {
        if (!elem.contains("name")) {
            flog::error("Menu element is missing 'name' key");
            continue;
        }
        if (!elem["name"].is_string()) {
            flog::error("Menu element 'name' key isn't a string");
            continue;
        }
        if (!elem.contains("open")) {
            flog::error("Menu element is missing 'open' key");
            continue;
        }
        if (!elem["open"].is_boolean()) {
            flog::error("Menu element 'open' key isn't a string");
            continue;
        }

        if (!elem.contains("location")) {
            flog::error("Menu element is missing 'location' key. Setting to default (left column)");
            elem["location"] = Menu::MenuOption_Location::left;
            goto endLocationKeyChecks;
        }
        if (!elem["location"].is_number_integer()) {
            flog::error("Menu element 'location' key isn't an integer. Setting to default (left column)");
            elem["location"] = Menu::MenuOption_Location::left;
            goto endLocationKeyChecks;
        }
        endLocationKeyChecks:

        Menu::MenuOption_t opt;
        opt.name = elem["name"];
        opt.open = elem["open"];
        opt.location = elem["location"];
        gui::menu.order.push_back(opt);
    }

    gui::menu.registerEntry("Source", sourcemenu::draw, NULL);
    gui::menu.registerEntry("Sinks", sinkmenu::draw, NULL);
    gui::menu.registerEntry("Band Plan", bandplanmenu::draw, NULL);
    gui::menu.registerEntry("Display", displaymenu::draw, NULL);
    gui::menu.registerEntry("Theme", thememenu::draw, NULL);
    gui::menu.registerEntry("VFO Color", vfo_color_menu::draw, NULL);
    gui::menu.registerEntry("Module Manager", module_manager_menu::draw, NULL);

    gui::freqSelect.init();

    // Set default values for waterfall in case no source init's it
    gui::waterfall.setBandwidth(8000000);
    gui::waterfall.setViewBandwidth(8000000);

    fft_in = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * fftSize);
    fft_out = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * fftSize);
    fftwPlan = fftwf_plan_dft_1d(fftSize, fft_in, fft_out, FFTW_FORWARD, FFTW_ESTIMATE);

    sigpath::iqFrontEnd.init(&dummyStream, 8000000, true, 1, false, 1024, 20.0, IQFrontEnd::FFTWindow::NUTTALL, acquireFFTBuffer, releaseFFTBuffer, this);
    sigpath::iqFrontEnd.start();

    vfoCreatedHandler.handler = vfoAddedHandler;
    vfoCreatedHandler.ctx = this;
    sigpath::vfoManager.onVfoCreated.bindHandler(&vfoCreatedHandler);

    flog::info("Loading modules");

    // Load modules from /module directory
    if (std::filesystem::is_directory(modulesDir)) {
        for (const auto& file : std::filesystem::directory_iterator(modulesDir)) {
            std::string path = file.path().generic_string();
            if (file.path().extension().generic_string() != SDRPP_MOD_EXTENTSION) {
                continue;
            }
            if (!file.is_regular_file()) { continue; }
            flog::info("Loading {0}", path);
            LoadingScreen::show("Loading " + file.path().filename().string());
            core::moduleManager.loadModule(path);
        }
    }
    else {
        flog::warn("Module directory {0} does not exist, not loading modules from directory", modulesDir);
    }

    // Read module config
    core::configManager.acquire();
    std::vector<std::string> modules = core::configManager.conf["modules"];
    auto modList = core::configManager.conf["moduleInstances"].items();
    core::configManager.release();

    // Load additional modules specified through config
    for (auto const& path : modules) {
#ifndef __ANDROID__
        std::string apath = std::filesystem::absolute(path).string();
        flog::info("Loading {0}", apath);
        LoadingScreen::show("Loading " + std::filesystem::path(path).filename().string());
        core::moduleManager.loadModule(apath);
#else
        core::moduleManager.loadModule(path);
#endif
    }

    // Create module instances
    for (auto const& [name, _module] : modList) {
        std::string mod = _module["module"];
        bool enabled = _module["enabled"];
        flog::info("Initializing {0} ({1})", name, mod);
        LoadingScreen::show("Initializing " + name + " (" + mod + ")");
        core::moduleManager.createInstance(name, mod);
        if (!enabled) { core::moduleManager.disableInstance(name); }
    }

    // Load color maps
    LoadingScreen::show("Loading color maps");
    flog::info("Loading color maps");
    if (std::filesystem::is_directory(resourcesDir + "/colormaps")) {
        for (const auto& file : std::filesystem::directory_iterator(resourcesDir + "/colormaps")) {
            std::string path = file.path().generic_string();
            LoadingScreen::show("Loading " + file.path().filename().string());
            flog::info("Loading {0}", path);
            if (file.path().extension().generic_string() != ".json") {
                continue;
            }
            if (!file.is_regular_file()) { continue; }
            colormaps::loadMap(path);
        }
    }
    else {
        flog::warn("Color map directory {0} does not exist, not loading modules from directory", modulesDir);
    }

    gui::waterfall.updatePalletteFromArray(colormaps::maps["Turbo"].map, colormaps::maps["Turbo"].entryCount);

    sourcemenu::init();
    sinkmenu::init();
    bandplanmenu::init();
    displaymenu::init();
    vfo_color_menu::init();
    module_manager_menu::init();

    // TODO for 0.2.5
    // Fix gain not updated on startup, soapysdr

    // Update UI settings
    LoadingScreen::show("Loading configuration");
    core::configManager.acquire();
    fftMin = core::configManager.conf["min"];
    fftMax = core::configManager.conf["max"];
    gui::waterfall.setFFTMin(fftMin);
    gui::waterfall.setWaterfallMin(fftMin);
    gui::waterfall.setFFTMax(fftMax);
    gui::waterfall.setWaterfallMax(fftMax);

    double frequency = core::configManager.conf["frequency"];

    showMenu = core::configManager.conf["showMenu"];
    showRightMenu = core::configManager.conf["showRightMenu"];

    startedWithMenuClosed = !showMenu;

    gui::freqSelect.setFrequency(frequency);
    gui::freqSelect.frequencyChanged = false;
    sigpath::sourceManager.tune(frequency);
    gui::waterfall.setCenterFrequency(frequency);
    bw = 1.0;
    gui::waterfall.vfoFreqChanged = false;
    gui::waterfall.centerFreqMoved = false;
    gui::waterfall.selectFirstVFO();

    menuWidth = core::configManager.conf["menuWidth"].is_number() ? static_cast<int>(core::configManager.conf["menuWidth"]) : menuWidth;
    newWidth = menuWidth;
    menuWidthRight = core::configManager.conf["menuWidthRight"].is_number() ? static_cast<int>(core::configManager.conf["menuWidthRight"]) : menuWidthRight;
    newWidthRight = menuWidthRight;

    fftHeight = core::configManager.conf["fftHeight"];
    gui::waterfall.setFFTHeight(fftHeight);

    tuningMode = core::configManager.conf["centerTuning"] ? tuner::TUNER_MODE_CENTER : tuner::TUNER_MODE_NORMAL;
    gui::waterfall.VFOMoveSingleClick = (tuningMode == tuner::TUNER_MODE_CENTER);

    core::configManager.release();

    // Correct the offset of all VFOs so that they fit on the screen
    float finalBwHalf = gui::waterfall.getBandwidth() / 2.0;
    for (auto& [_name, _vfo] : gui::waterfall.vfos) {
        if (_vfo->lowerOffset < -finalBwHalf) {
            sigpath::vfoManager.setCenterOffset(_name, (_vfo->bandwidth / 2) - finalBwHalf);
            continue;
        }
        if (_vfo->upperOffset > finalBwHalf) {
            sigpath::vfoManager.setCenterOffset(_name, finalBwHalf - (_vfo->bandwidth / 2));
            continue;
        }
    }

    autostart = core::args["autostart"].b();
    initComplete = true;

    core::moduleManager.doPostInitAll();
    gui::audioAnalyzer.doPostInit();
}

float* MainWindow::acquireFFTBuffer(void* ctx) {
    ZoneScoped;
    return gui::waterfall.getFFTBuffer();
}

void MainWindow::releaseFFTBuffer(void* ctx) {
    ZoneScoped;
    gui::waterfall.pushFFT();
}

void MainWindow::vfoAddedHandler(VFOManager::VFO* vfo, void* ctx) {
    ZoneScoped;
    MainWindow* _this = (MainWindow*)ctx;
    std::string name = vfo->getName();
    core::configManager.acquire();
    if (!core::configManager.conf["vfoOffsets"].contains(name)) {
        core::configManager.release();
        return;
    }
    double offset = core::configManager.conf["vfoOffsets"][name];
    core::configManager.release();

    double viewBW = gui::waterfall.getViewBandwidth();
    double viewOffset = gui::waterfall.getViewOffset();

    double viewLower = viewOffset - (viewBW / 2.0);
    double viewUpper = viewOffset + (viewBW / 2.0);

    double newOffset = std::clamp<double>(offset, viewLower, viewUpper);

    sigpath::vfoManager.setCenterOffset(name, _this->initComplete ? newOffset : offset);
}

void MainWindow::draw() {
    ZoneScoped;
    ImGui::Begin("Main", NULL, WINDOW_FLAGS);
    ImVec4 textCol = ImGui::GetStyleColorVec4(ImGuiCol_Text);

    ImGui::WaterfallVFO* vfo = NULL;
    if (gui::waterfall.selectedVFO != "") {
        vfo = gui::waterfall.vfos[gui::waterfall.selectedVFO];
    }

    // Handle VFO movement
    if (vfo != NULL) {
        if (vfo->centerOffsetChanged) {
            ZoneScopedN("draw_vfoMoved");
            if (tuningMode == tuner::TUNER_MODE_CENTER) {
                tuner::tune(tuner::TUNER_MODE_CENTER, gui::waterfall.selectedVFO, gui::waterfall.getCenterFrequency() + vfo->generalOffset);
            }
            gui::freqSelect.setFrequency(gui::waterfall.getCenterFrequency() + vfo->generalOffset);
            gui::freqSelect.frequencyChanged = false;
            core::configManager.acquire();
            core::configManager.conf["vfoOffsets"][gui::waterfall.selectedVFO] = vfo->generalOffset;
            core::configManager.release(true);
        }
    }

    sigpath::vfoManager.updateFromWaterfall(&gui::waterfall);

    // Handle selection of another VFO
    if (gui::waterfall.selectedVFOChanged) {
        ZoneScopedN("draw_vfoChanged");
        gui::freqSelect.setFrequency((vfo != NULL) ? (vfo->generalOffset + gui::waterfall.getCenterFrequency()) : gui::waterfall.getCenterFrequency());
        gui::waterfall.selectedVFOChanged = false;
        gui::freqSelect.frequencyChanged = false;
    }

    // Handle change in selected frequency
    if (gui::freqSelect.frequencyChanged) {
        ZoneScopedN("draw_freqChanged");
        gui::freqSelect.frequencyChanged = false;
        tuner::tune(tuningMode, gui::waterfall.selectedVFO, gui::freqSelect.frequency);
        if (vfo != NULL) {
            vfo->centerOffsetChanged = false;
            vfo->lowerOffsetChanged = false;
            vfo->upperOffsetChanged = false;
        }
        core::configManager.acquire();
        core::configManager.conf["frequency"] = gui::waterfall.getCenterFrequency();
        if (vfo != NULL) {
            core::configManager.conf["vfoOffsets"][gui::waterfall.selectedVFO] = vfo->generalOffset;
        }
        core::configManager.release(true);
    }

    // Handle dragging the frequency scale
    if (gui::waterfall.centerFreqMoved) {
        ZoneScopedN("draw_centerFreqChanged");
        gui::waterfall.centerFreqMoved = false;
        sigpath::sourceManager.tune(gui::waterfall.getCenterFrequency());
        if (vfo != NULL) {
            gui::freqSelect.setFrequency(gui::waterfall.getCenterFrequency() + vfo->generalOffset);
        }
        else {
            gui::freqSelect.setFrequency(gui::waterfall.getCenterFrequency());
        }
        core::configManager.acquire();
        core::configManager.conf["frequency"] = gui::waterfall.getCenterFrequency();
        core::configManager.release(true);
    }

    int _fftHeight = gui::waterfall.getFFTHeight();
    if (fftHeight != _fftHeight) {
        fftHeight = _fftHeight;
        core::configManager.acquire();
        core::configManager.conf["fftHeight"] = fftHeight;
        core::configManager.release(true);
    }

    ImVec2 btnSize(buttonWidth_normal - buttonPadding_normal * 2, buttonWidth_normal - buttonPadding_normal * 2);

    bool tmpPlaySate = playing;
    if (playButtonLocked && !tmpPlaySate) { style::beginDisabled(); }
    if (playing) {
        ImGui::PushID(ImGui::GetID("sdrpp_stop_btn"));
        if (ImGui::ImageButton(icons::STOP, btnSize, ImVec2(0, 0), ImVec2(1, 1), buttonPadding_normal, ImVec4(0, 0, 0, 0), textCol) || ImGui::IsKeyPressed(ImGuiKey_End, false)) {
            setPlayState(false);
        }
        ImGui::PopID();
    }
    else { // TODO: Might need to check if there even is a device
        ImGui::PushID(ImGui::GetID("sdrpp_play_btn"));
        if (ImGui::ImageButton(icons::PLAY, btnSize, ImVec2(0, 0), ImVec2(1, 1), buttonPadding_normal, ImVec4(0, 0, 0, 0), textCol) || ImGui::IsKeyPressed(ImGuiKey_End, false)) {
            setPlayState(true);
        }
        ImGui::PopID();
    }
    if (playButtonLocked && !tmpPlaySate) { style::endDisabled(); }

    // Handle auto-start
    if (autostart) {
        autostart = false;
        setPlayState(true);
    }

    ImGui::SameLine();
    float origY = ImGui::GetCursorPosY();

    sigpath::sinkManager.showVolumeSlider(gui::waterfall.selectedVFO, "##_sdrpp_main_volume_", 200 * style::uiScale, buttonWidth_normal - buttonPadding_normal * 2, buttonPadding_normal, true);

    ImGui::SameLine();

    ImGui::SetCursorPosY(origY);
    gui::freqSelect.draw();

    ImGui::SameLine();

    ImGui::SetCursorPosY(origY);
    if (tuningMode == tuner::TUNER_MODE_CENTER) {
        ImGui::PushID(ImGui::GetID("sdrpp_ena_st_btn"));
        if (ImGui::ImageButton(icons::CENTER_TUNING, btnSize, ImVec2(0, 0), ImVec2(1, 1), buttonPadding_normal, ImVec4(0, 0, 0, 0), textCol)) {
            tuningMode = tuner::TUNER_MODE_NORMAL;
            gui::waterfall.VFOMoveSingleClick = false;
            core::configManager.acquire();
            core::configManager.conf["centerTuning"] = false;
            core::configManager.release(true);
        }
        ImGui::PopID();
    }
    else { // TODO: Might need to check if there even is a device
        ImGui::PushID(ImGui::GetID("sdrpp_dis_st_btn"));
        if (ImGui::ImageButton(icons::NORMAL_TUNING, btnSize, ImVec2(0, 0), ImVec2(1, 1), buttonPadding_normal, ImVec4(0, 0, 0, 0), textCol)) {
            tuningMode = tuner::TUNER_MODE_CENTER;
            gui::waterfall.VFOMoveSingleClick = true;
            tuner::tune(tuner::TUNER_MODE_CENTER, gui::waterfall.selectedVFO, gui::freqSelect.frequency);
            core::configManager.acquire();
            core::configManager.conf["centerTuning"] = true;
            core::configManager.release(true);
        }
        ImGui::PopID();
    }

    ImGui::SameLine();

    int snrOffset = 87.0f * style::uiScale;
    int snrWidth = std::clamp<int>(ImGui::GetWindowSize().x - ImGui::GetCursorPosX() - snrOffset, 100.0f * style::uiScale, 300.0f * style::uiScale);
    int snrPos = std::max<int>(ImGui::GetWindowSize().x - (snrWidth + snrOffset), ImGui::GetCursorPosX());

    ImGui::SetCursorPosX(snrPos);
    ImGui::SetCursorPosY(origY + (5.0f * style::uiScale));
    ImGui::SetNextItemWidth(snrWidth);
    ImGui::SNRMeter((vfo != NULL) ? gui::waterfall.selectedVFOSNR : 0);

    // Note: this is what makes the vertical size correct, needs to be fixed
    ImGui::SameLine();

    // ImGui::EndChild();

    // Logo button
    ImGui::SetCursorPosX(ImGui::GetWindowSize().x - (48 * style::uiScale));
    ImGui::SetCursorPosY(10.0f * style::uiScale);
    if (ImGui::ImageButton(icons::LOGO, ImVec2(buttonWidth_normal, buttonWidth_normal), ImVec2(0, 0), ImVec2(1, 1), 0)) {
        showCredits = true;
    }
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        showCredits = false;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        showCredits = false;
    }

    // Handle waterfall lock (may still be locked later)
    if (showCredits) {
        goto waterfallLocked;
    }
    if (bandplanmenu::isExplorerOpen() && bandplanmenu::isExplorerHovered()) {
        goto waterfallLocked;
    }
    if (bandplanmenu::wasExplorerOpen() && bandplanmenu::wasExplorerHovered()) {
        goto waterfallLocked;
    }
    lockWaterfallControls = false;
    goto waterfallUnlocked;
    waterfallLocked:
    lockWaterfallControls = true;
    waterfallUnlocked:

    // Handle menu resize
    ImVec2 mousePos = ImGui::GetMousePos();
    ImGuiStyle& style = ImGui::GetStyle();
    if (!lockWaterfallControls && showMenu) {
        float curY = ImGui::GetCursorPosY();
        bool click = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        bool down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        if (grabbingMenu) {
            newWidth = mousePos.x - style.CellPadding.x * 2;
            newWidth = std::clamp<float>(newWidth, 250, ImGui::GetWindowContentRegionMax().x - 250);
            ImGui::GetForegroundDrawList()->AddLine(ImVec2(newWidth + style.CellPadding.x * 2, curY), ImVec2(newWidth + style.CellPadding.x * 2, ImGui::GetWindowContentRegionMax().y), ImGui::GetColorU32(ImGuiCol_SeparatorActive));
        }
        if (mousePos.x >= newWidth + style.CellPadding.x * 2 - edgeDragMargin && mousePos.x <= newWidth + style.CellPadding.x * 2 + edgeDragMargin && mousePos.y > curY) {
            hoveringMenu = true;
            if (click) {
                grabbingMenu = true;
            }
        }
        else {
            hoveringMenu = false;
        }
        if (!down && grabbingMenu) {
            grabbingMenu = false;
            menuWidth = newWidth;
            core::configManager.acquire();
            core::configManager.conf["menuWidth"] = menuWidth;
            core::configManager.release(true);
        }
    }
    if (!lockWaterfallControls && showRightMenu) {
        float curY = ImGui::GetCursorPosY();
        bool click = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        bool down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        if (grabbingMenuRight) {
            newWidthRight = ImGui::GetWindowContentRegionMax().x - mousePos.x - narrowMenuWidth - style.CellPadding.x * 2;
            newWidthRight = std::clamp<float>(newWidthRight, 250, ImGui::GetWindowContentRegionMax().x - 250);
            ImGui::GetForegroundDrawList()->AddLine(ImVec2(ImGui::GetWindowContentRegionMax().x - newWidthRight - narrowMenuWidth - style.CellPadding.x * 2, curY),
                ImVec2(ImGui::GetWindowContentRegionMax().x - newWidthRight - narrowMenuWidth - style.CellPadding.x * 2, ImGui::GetWindowContentRegionMax().y), ImGui::GetColorU32(ImGuiCol_SeparatorActive));
        }
        if (mousePos.x >= ImGui::GetWindowContentRegionMax().x - newWidthRight - narrowMenuWidth - style.CellPadding.x * 2 - edgeDragMargin && mousePos.x <= ImGui::GetWindowContentRegionMax().x - newWidthRight - narrowMenuWidth - style.CellPadding.x * 2 + edgeDragMargin && mousePos.y > curY) {
            hoveringMenuRight = true;
            if (click) {
                grabbingMenuRight = true;
            }
        }
        else {
            hoveringMenuRight = false;
        }
        if (!down && grabbingMenuRight) {
            grabbingMenuRight = false;
            menuWidthRight = newWidthRight;
            core::configManager.acquire();
            core::configManager.conf["menuWidthRight"] = menuWidthRight;
            core::configManager.release(true);
        }
    }

    if (hoveringMenu || hoveringMenuRight) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    } else {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
    }

    // Process menu keybinds
    displaymenu::checkKeybinds();

    // Columns scaling
    float leftPanelWidth = showMenu ? menuWidth : (buttonWidth_normal + buttonPadding_normal * 2) * 1.2f;
    float rightPanelWidth = showRightMenu ? menuWidthRight : (buttonWidth_normal + buttonPadding_normal * 2) * 1.2f;
    float centerPanelWidth = std::max<int>(ImGui::GetWindowContentRegionMax().x - leftPanelWidth - rightPanelWidth - narrowMenuWidth, 100.0f * style::uiScale);

    ImGui::Columns(4, "WindowColumns", false);
    ImGui::SetColumnWidth(0, leftPanelWidth);
    ImGui::SetColumnWidth(1, centerPanelWidth);
    ImGui::SetColumnWidth(2, narrowMenuWidth);
    ImGui::SetColumnWidth(3, rightPanelWidth);

    // Left menu Column
    {
        ImGui::PushID(ImGui::GetID("sdrpp_leftMenuToggle_btn"));
        int buttonPadding = buttonPadding_normal;
        float buttonWidth = buttonWidth_normal - buttonPadding * 2;
        if (showMenu) {
            ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x - buttonWidth + ImGui::GetStyle().CellPadding.x);
        }
        if (ImGui::ImageButton(showMenu ? icons::LEFT_PANEL_CLOSE : icons::LEFT_PANEL_OPEN, ImVec2(buttonWidth, buttonWidth), ImVec2(0, 0), ImVec2(1, 1), buttonPadding, ImVec4(0, 0, 0, 0), textCol)) {
            showMenu = !showMenu;
            core::configManager.acquire();
            core::configManager.conf["showMenu"] = showMenu;
            core::configManager.release(true);
        }
        ImGui::PopID();

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s Side Panel", showMenu ? "Close" : "Open");
        }
    }

    if (ImGui::BeginChild("Left Column", ImVec2(0, 0), showMenu, ImGuiWindowFlags_None)) {
        if (showMenu) {
            if (gui::menu.draw(firstMenuRender, Menu::MenuOption_Location::left)) {
                core::configManager.acquire();
                json arr = json::array();
                for (int i = 0; i < gui::menu.order.size(); i++) {
                    arr[i]["name"] = gui::menu.order[i].name;
                    arr[i]["open"] = gui::menu.order[i].open;
                    arr[i]["location"] = gui::menu.order[i].location;
                }
                core::configManager.conf["menuElements"] = arr;

                // Update enabled and disabled modules
                for (auto [_name, inst] : core::moduleManager.instances) {
                    if (!core::configManager.conf["moduleInstances"].contains(_name)) { continue; }
                    core::configManager.conf["moduleInstances"][_name]["enabled"] = inst.instance->isEnabled();
                }

                core::configManager.release(true);
            }

            if (ImGui::CollapsingHeader("Debug")) {
                ImGui::Text("Frame time: %.3f ms/frame", ImGui::GetIO().DeltaTime * 1000.0f);
                ImGui::Text("Framerate: %.1f FPS", ImGui::GetIO().Framerate);
                ImGui::Text("Center Frequency: %.0f Hz", gui::waterfall.getCenterFrequency());
                ImGui::Text("Source name: %s", sourceName.c_str());
                ImGui::Checkbox("Show demo window", &demoWindow);
                ImGui::Text("ImGui version: %s", ImGui::GetVersion());

                // ImGui::Checkbox("Bypass buffering", &sigpath::iqFrontEnd.inputBuffer.bypass);

                // ImGui::Text("Buffering: %d", (sigpath::iqFrontEnd.inputBuffer.writeCur - sigpath::iqFrontEnd.inputBuffer.readCur + 32) % 32);

                if (ImGui::Button("Test Bug")) {
                    flog::error("Will this make the software crash?");
                }

                if (ImGui::Button("Testing something")) {
                    gui::menu.order[0].open = true;
                    firstMenuRender = true;
                }

                ImGui::Checkbox("WF Single Click", &gui::waterfall.VFOMoveSingleClick);
                ImGui::Checkbox("Lock Menu Order", &gui::menu.locked);

                if (ImGui::Button("Reset side panel sizes")) {
                    menuWidth = mainWindow_defaultMenuWidth;
                    menuWidthRight = mainWindow_defaultMenuWidth;
                    newWidth = menuWidth;
                    newWidthRight = menuWidthRight;
                    core::configManager.acquire();
                    core::configManager.conf["menuWidth"] = menuWidth;
                    core::configManager.conf["menuWidthRight"] = menuWidthRight;
                    core::configManager.release(true);
                }

                ImGui::Spacing();
            }
        }
    }

    ImGui::EndChild();

    // Center Column
    ImGui::NextColumn();

    ImGui::BeginChild("Waterfall", ImVec2(0, -400 * style::uiScale));

    // @TODO: add bottom toolbar?
    gui::waterfall.draw();

    ImGui::EndChild();

    ImGui::BeginChild("Audio Analyzer");

    gui::audioAnalyzer.draw();

    ImGui::EndChild();

    if (!lockWaterfallControls) {
        // Handle arrow keys
        if (vfo != NULL && (gui::waterfall.mouseInFFT || gui::waterfall.mouseInWaterfall)) {
            bool freqChanged = false;
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) && !gui::freqSelect.digitHovered) {
                double nfreq = gui::waterfall.getCenterFrequency() + vfo->generalOffset - vfo->snapInterval;
                nfreq = roundl(nfreq / vfo->snapInterval) * vfo->snapInterval;
                tuner::tune(tuningMode, gui::waterfall.selectedVFO, nfreq);
                freqChanged = true;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) && !gui::freqSelect.digitHovered) {
                double nfreq = gui::waterfall.getCenterFrequency() + vfo->generalOffset + vfo->snapInterval;
                nfreq = roundl(nfreq / vfo->snapInterval) * vfo->snapInterval;
                tuner::tune(tuningMode, gui::waterfall.selectedVFO, nfreq);
                freqChanged = true;
            }
            if (freqChanged) {
                core::configManager.acquire();
                core::configManager.conf["frequency"] = gui::waterfall.getCenterFrequency();
                if (vfo != NULL) {
                    core::configManager.conf["vfoOffsets"][gui::waterfall.selectedVFO] = vfo->generalOffset;
                }
                core::configManager.release(true);
            }
        }

        // Handle scrollwheel
        int wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0 && (gui::waterfall.mouseInFFT || gui::waterfall.mouseInWaterfall)) {
            if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl)) {
                bw = std::clamp<double>(bw - wheel / 10.0, 0.0, 1.0);
                double factor = bw * bw;

                // Map 0.0 -> 1.0 to 1000.0 -> bandwidth
                double wfBw = gui::waterfall.getBandwidth();
                double delta = wfBw - 1000.0;
                double finalBw = std::min<double>(1000.0 + (factor * delta), wfBw);

                gui::waterfall.setViewBandwidth(finalBw);
                if (vfo != NULL) {
                    gui::waterfall.setViewOffset(vfo->centerOffset); // center vfo on screen
                }
            }
            else {
                double nfreq;
                if (vfo != NULL) {
                    // Select factor depending on modifier keys
                    double interval;
                    if (ImGui::IsKeyDown(ImGuiKey_LeftShift)) {
                        interval = vfo->snapInterval * 10.0;
                    }
                    else if (ImGui::IsKeyDown(ImGuiKey_LeftAlt)) {
                        interval = vfo->snapInterval * 0.1;
                    }
                    else {
                        interval = vfo->snapInterval;
                    }

                    nfreq = gui::waterfall.getCenterFrequency() + vfo->generalOffset + (interval * wheel);
                    nfreq = roundl(nfreq / interval) * interval;
                }
                else {
                    nfreq = gui::waterfall.getCenterFrequency() - (gui::waterfall.getViewBandwidth() * wheel / 20.0);
                }
                tuner::tune(tuningMode, gui::waterfall.selectedVFO, nfreq);
                gui::freqSelect.setFrequency(nfreq);
                core::configManager.acquire();
                core::configManager.conf["frequency"] = gui::waterfall.getCenterFrequency();
                if (vfo != NULL) {
                    core::configManager.conf["vfoOffsets"][gui::waterfall.selectedVFO] = vfo->generalOffset;
                }
                core::configManager.release(true);
            }
        }
    }

    // Waterfall controls
    ImGui::NextColumn();
    if (ImGui::BeginChild("WaterfallControls", ImVec2(0, 0), true, ImGuiWindowFlags_NoScrollbar)) {
        ImVec2 windowSize = ImGui::GetWindowSize();

        ImGui::PushID(ImGui::GetID("sdrpp_bandplanExpl_btn"));
        int buttonPadding = buttonPadding_normal;
        float buttonWidth = buttonWidth_normal - buttonPadding * 2;
        ImGui::SetCursorPosX(windowSize.x / 2.0f - buttonWidth / 2.0f - buttonPadding);
        if (ImGui::ImageButton(icons::EXPLORE, ImVec2(buttonWidth, buttonWidth), ImVec2(0, 0), ImVec2(1, 1), buttonPadding, ImVec4(0, 0, 0, 0), textCol)) {
            bandplanmenu::openExplorer();
        }
        ImGui::PopID();

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Bandplan Explorer");
        }

        ImGui::NewLine();

        ImGuiStyle& style = ImGui::GetStyle();

        // Shrink sliders to fit
        float sliderHeight = std::clamp((ImGui::GetContentRegionAvail().y - style.WindowPadding.y - ImGui::GetTextLineHeight() * 6 - style.ItemSpacing.y * 2) / 3, 20.0f * style::uiScale, 150.0f * style::uiScale);
        ImVec2 wfSliderSize(20.0 * style::uiScale, sliderHeight);

        ImGui::SetCursorPosX((ImGui::GetWindowSize().x / 2.0) - (ImGui::CalcTextSize("Zoom").x / 2.0));
        ImGui::TextUnformatted("Zoom");
        ImGui::SetCursorPosX((ImGui::GetWindowSize().x / 2.0) - 10 * style::uiScale);

        if (ImGui::VSliderFloat("##_7_", wfSliderSize, &bw, 1.0, 0.0, "")) {
            double factor = (double)bw * (double)bw;

            // Map 0.0 -> 1.0 to 1000.0 -> bandwidth
            double wfBw = gui::waterfall.getBandwidth();
            double delta = wfBw - 1000.0;
            double finalBw = std::min<double>(1000.0 + (factor * delta), wfBw);

            gui::waterfall.setViewBandwidth(finalBw);
            if (vfo != NULL) {
                gui::waterfall.setViewOffset(vfo->centerOffset); // center vfo on screen
            }
        }

        ImGui::NewLine();

        ImGui::SetCursorPosX((ImGui::GetWindowSize().x / 2.0) - (ImGui::CalcTextSize("Max").x / 2.0));
        ImGui::TextUnformatted("Max");
        ImGui::SetCursorPosX((ImGui::GetWindowSize().x / 2.0) - 10 * style::uiScale);
        if (ImGui::VSliderFloat("##_8_", wfSliderSize, &fftMax, 0.0, -160.0f, "")) {
            fftMax = std::max<float>(fftMax, fftMin + 10);
            core::configManager.acquire();
            core::configManager.conf["max"] = fftMax;
            core::configManager.release(true);
        }

        ImGui::NewLine();

        ImGui::SetCursorPosX((ImGui::GetWindowSize().x / 2.0) - (ImGui::CalcTextSize("Min").x / 2.0));
        ImGui::TextUnformatted("Min");
        ImGui::SetCursorPosX((ImGui::GetWindowSize().x / 2.0) - 10 * style::uiScale);
        ImGui::SetItemUsingMouseWheel();
        if (ImGui::VSliderFloat("##_9_", wfSliderSize, &fftMin, 0.0, -160.0f, "")) {
            fftMin = std::min<float>(fftMax - 10, fftMin);
            core::configManager.acquire();
            core::configManager.conf["min"] = fftMin;
            core::configManager.release(true);
        }
    }
    ImGui::EndChild();

    // Right menu column
    ImGui::NextColumn();
    {
        ImGui::PushID(ImGui::GetID("sdrpp_rightMenuToggle_btn"));
        int buttonPadding = buttonPadding_normal;
        float buttonWidth = buttonWidth_normal - buttonPadding * 2;
        if (!showRightMenu) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - buttonWidth);
        }
        if (ImGui::ImageButton(showRightMenu ? icons::RIGHT_PANEL_CLOSE : icons::RIGHT_PANEL_OPEN, ImVec2(buttonWidth, buttonWidth), ImVec2(0, 0), ImVec2(1, 1), buttonPadding, ImVec4(0, 0, 0, 0), textCol)) {
            showRightMenu = !showRightMenu;
            core::configManager.acquire();
            core::configManager.conf["showRightMenu"] = showRightMenu;
            core::configManager.release(true);
        }
        ImGui::PopID();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s Side Panel", showRightMenu ? "Close" : "Open");
    }

    if (ImGui::BeginChild("RightMenu", ImVec2(0, 0), showRightMenu, ImGuiWindowFlags_None)) {
        if (showRightMenu) {
            ImGui::Spacing();

            if (gui::menu.draw(firstMenuRender, Menu::MenuOption_Location::right)) {
                core::configManager.acquire();
                json arr = json::array();
                for (int i = 0; i < gui::menu.order.size(); i++) {
                    arr[i]["name"] = gui::menu.order[i].name;
                    arr[i]["open"] = gui::menu.order[i].open;
                    arr[i]["location"] = gui::menu.order[i].location;
                }
                core::configManager.conf["menuElements"] = arr;

                // Update enabled and disabled modules
                for (auto [_name, inst] : core::moduleManager.instances) {
                    if (!core::configManager.conf["moduleInstances"].contains(_name)) { continue; }
                    core::configManager.conf["moduleInstances"][_name]["enabled"] = inst.instance->isEnabled();
                }

                core::configManager.release(true);
            }
        }
    }
    ImGui::EndChild();

    if (startedWithMenuClosed) {
        startedWithMenuClosed = false;
    }
    else {
        firstMenuRender = false;
    }

    gui::waterfall.setFFTMin(fftMin);
    gui::waterfall.setFFTMax(fftMax);
    gui::waterfall.setWaterfallMin(fftMin);
    gui::waterfall.setWaterfallMax(fftMax);

    // Popups
    bandplanmenu::drawExplorer();

    ImGui::End();

    if (showCredits) {
        credits::show();
    }

    if (demoWindow) {
        ImGui::ShowDemoWindow();
    }
}

void MainWindow::setPlayState(bool _playing) {
    ZoneScoped;
    if (_playing == playing) { return; }
    if (_playing) {
        sigpath::iqFrontEnd.flushInputBuffer();
        sigpath::sourceManager.start();
        sigpath::sourceManager.tune(gui::waterfall.getCenterFrequency());
        playing = true;
        onPlayStateChange.emit(true);
    }
    else {
        playing = false;
        onPlayStateChange.emit(false);
        sigpath::sourceManager.stop();
        sigpath::iqFrontEnd.flushInputBuffer();
    }
}

void MainWindow::setViewBandwidthSlider(float bandwidth) {
    ZoneScoped;
    bw = bandwidth;
}

bool MainWindow::sdrIsRunning() {
    ZoneScoped;
    return playing;
}

bool MainWindow::isPlaying() {
    ZoneScoped;
    return playing;
}
int MainWindow::getTuningMode() {
    ZoneScoped;
    return tuningMode;
}

void MainWindow::setFirstMenuRender() {
    ZoneScoped;
    firstMenuRender = true;
}