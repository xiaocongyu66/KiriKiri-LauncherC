#pragma once

#include <cstddef>

void TVPSDLUIRegisterLegacyCocosStudioAssets(const char *sourceRoot,
                                             const char *runtimeRoot);
void TVPSDLUIRecordViewport(int frameWidth, int frameHeight, int sceneWidth,
                            int sceneHeight, float scale);
void TVPSDLUIRecordAndroidTouch(const char *eventName, float frameX,
                                float frameY, int pointerId, bool active);
void TVPSDLUIRecordGameMenuCreated(int sceneWidth, int sceneHeight,
                                   float uiScale, int rootWidth,
                                   int rootHeight, int handlerWidth,
                                   int handlerHeight,
                                   int handlerInactiveOpacity);
void TVPSDLUIRecordGameMenuButton(const char *id, const char *actionName,
                                  float localX, float localY, float width,
                                  float height, const char *iconPath,
                                  bool enabled);
void TVPSDLUIRecordGameMenuState(const char *eventName, bool shrinked,
                                 bool hitted, float rootX, float rootY,
                                 float rootWidth, float rootHeight,
                                 float handlerX, float handlerY,
                                 float handlerWidth, float handlerHeight,
                                 bool mouseIcon, float duration);
void TVPSDLUIQueueGameMenuAction(const char *actionName, const char *source,
                                 float sceneX, float sceneY, int pointerId);
bool TVPSDLUIPollGameMenuAction(char *actionName, size_t actionNameSize);
void TVPSDLUIRecordGameMenuAction(const char *actionName, bool shrinked,
                                  bool mouseIcon);
void TVPSDLUIRecordMessageBoxShow(const char *caption, const char *text,
                                  int buttonCount,
                                  const char *const *buttonTexts);
void TVPSDLUIRecordMessageBoxAction(int buttonIndex, const char *source);
void TVPSDLUIRecordMessageBoxClose(const char *reason);
void TVPSDLUIRecordProgressShow(const char *source);
void TVPSDLUIRecordProgressButtons(int buttonCount,
                                   const char *const *buttonTexts);
void TVPSDLUIRecordProgressText(const char *fieldName, const char *text);
void TVPSDLUIRecordProgressPercent(const char *fieldName, float percent);
void TVPSDLUIRecordProgressVisible(const char *fieldName, bool visible);
void TVPSDLUIRecordProgressClose(const char *reason);
