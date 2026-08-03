#pragma once

namespace bandplanmenu {
    void init();
    void draw(void* ctx);

    void drawExplorer();
    void openExplorer();
    bool isExplorerOpen();
    bool isExplorerHovered();
    bool wasExplorerOpen();
    bool wasExplorerHovered();
};