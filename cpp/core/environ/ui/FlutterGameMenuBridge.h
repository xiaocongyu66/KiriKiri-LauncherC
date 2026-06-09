#pragma once

bool TVPShowFlutterGameMainMenu();

extern "C" const char *KR2LauncherGetMainMenuJson();
extern "C" int KR2LauncherActivateMenuItem(const char *itemPathUtf8);
extern "C" int KR2LauncherLaunchGame(const char *gamePathUtf8);
