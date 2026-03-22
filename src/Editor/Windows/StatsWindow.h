#ifndef MOLGA_STATS_WINDOW_H
#define MOLGA_STATS_WINDOW_H

#include "EditorWindow.h"

class StatsWindow : public EditorWindow {
public:
    StatsWindow();
    void OnGUI() override;
};

#endif // MOLGA_STATS_WINDOW_H
