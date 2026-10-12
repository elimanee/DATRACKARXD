#include <imgui.h>

#include "app.h"
#include "i18n.h"

void App::manualWindow() {
  if (!ImGui::Begin(TW("Manual").c_str(), &showManual_)) {
    ImGui::End();
    return;
  }
  ImGui::End();
}
