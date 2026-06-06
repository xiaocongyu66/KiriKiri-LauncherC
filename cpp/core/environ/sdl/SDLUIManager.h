#pragma once

void TVPSDLUIRegisterLegacyCocosStudioAssets(const char *sourceRoot,
                                             const char *runtimeRoot);
void TVPSDLUIRecordViewport(int frameWidth, int frameHeight, int sceneWidth,
                            int sceneHeight, float scale);
void TVPSDLUIRecordGameMenuCreated(int sceneWidth, int sceneHeight,
                                   float uiScale, int rootWidth,
                                   int rootHeight, int handlerWidth,
                                   int handlerHeight,
                                   int handlerInactiveOpacity);
void TVPSDLUIRecordGameMenuState(const char *eventName, bool shrinked,
                                 bool hitted, float rootX, float rootY,
                                 float rootWidth, float rootHeight,
                                 float handlerX, float handlerY,
                                 float handlerWidth, float handlerHeight,
                                 bool mouseIcon, float duration);
void TVPSDLUIRecordGameMenuAction(const char *actionName, bool shrinked,
                                  bool mouseIcon);
