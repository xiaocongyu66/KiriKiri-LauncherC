/**
 * @file alphamovie.cpp
 * @brief Minimal AlphaMovie stub plugin for KiriKiri2.
 *
 * Provides enough of the AlphaMovie interface so that games which
 * reference AlphaMovie.dll do not crash.  Actual video playback is
 * not implemented — calls are silently ignored.
 */

#include "ncbind.hpp"

#define NCB_MODULE_NAME TJS_W("AlphaMovie.dll")

class AlphaMovie {
public:
  AlphaMovie() {}
  virtual ~AlphaMovie() {}

  void open(tTJSVariant) { m_opened = true; }
  void clear() {
    m_opened = false;
    m_playing = false;
    m_frame = 0;
    m_numOfFrame = 0;
  }
  void play() { m_playing = true; }
  void stop() {
    m_playing = false;
    m_frame = 0;
  }
  void pause() {}
  void close() {}
  void rewind() {}

  void setPosition(int x, int y) {
    m_left = x;
    m_top = y;
    m_position = x;
  }
  void setNextMovieFile(tTJSVariant) {}

  bool get_loop() const { return m_loop; }
  void set_loop(bool v) { m_loop = v; }

  bool get_nextLoop() const { return m_nextLoop; }
  void set_nextLoop(bool v) { m_nextLoop = v; }

  bool get_visible() const { return m_visible; }
  void set_visible(bool v) { m_visible = v; }

  int get_frame() const { return m_frame; }
  void set_frame(int v) { m_frame = v; }

  double get_fps() const { return m_fps; }
  void set_fps(double v) { m_fps = v; }

  int get_position() const { return m_position; }
  void set_position(int v) { m_position = v; }

  int get_preloadSamples() const { return m_preloadSamples; }
  void set_preloadSamples(int v) { m_preloadSamples = v; }

  int get_left() const { return m_left; }
  void set_left(int v) { m_left = v; }

  int get_top() const { return m_top; }
  void set_top(int v) { m_top = v; }

  int get_width() const { return 0; }
  int get_height() const { return 0; }

  bool get_opened() const { return m_opened; }
  bool get_isPlaying() const { return m_playing; }
  bool isPlaying() const { return m_playing; }

  int get_totalTime() const { return 0; }
  int get_numberOfFrame() const { return 0; }
  int get_numOfFrame() const { return m_numOfFrame; }
  void set_numOfFrame(int v) { m_numOfFrame = v; }

  double get_FPSRate() const { return m_FPSRate; }
  void set_FPSRate(double v) { m_FPSRate = v; }
  double get_FPSScale() const { return m_FPSScale; }
  void set_FPSScale(double v) { m_FPSScale = v; }

  int get_screenWidth() const { return m_screenWidth; }
  void set_screenWidth(int v) { m_screenWidth = v; }
  int get_screenHeight() const { return m_screenHeight; }
  void set_screenHeight(int v) { m_screenHeight = v; }

  int showNextImage(iTJSDispatch2 *) { return 0; }

private:
  bool m_loop = false;
  bool m_nextLoop = false;
  bool m_visible = false;
  bool m_opened = false;
  bool m_playing = false;
  int m_frame = 0;
  int m_numOfFrame = 0;
  double m_fps = 30.0;
  double m_FPSRate = 30.0;
  double m_FPSScale = 1.0;
  int m_position = 0;
  int m_preloadSamples = 0;
  int m_left = 0;
  int m_top = 0;
  int m_screenWidth = 1280;
  int m_screenHeight = 720;
};

NCB_REGISTER_CLASS(AlphaMovie) {
  Constructor();

  NCB_METHOD(open);
  NCB_METHOD(clear);
  NCB_METHOD(play);
  NCB_METHOD(stop);
  NCB_METHOD(pause);
  NCB_METHOD(close);
  NCB_METHOD(rewind);
  NCB_METHOD(isPlaying);
  NCB_METHOD(setPosition);
  NCB_METHOD(setNextMovieFile);

  NCB_PROPERTY(loop, get_loop, set_loop);
  NCB_PROPERTY(nextLoop, get_nextLoop, set_nextLoop);
  NCB_PROPERTY(visible, get_visible, set_visible);
  NCB_PROPERTY(frame, get_frame, set_frame);
  NCB_PROPERTY(fps, get_fps, set_fps);
  NCB_PROPERTY(position, get_position, set_position);
  NCB_PROPERTY(preloadSamples, get_preloadSamples, set_preloadSamples);
  NCB_PROPERTY(left, get_left, set_left);
  NCB_PROPERTY(top, get_top, set_top);
  NCB_PROPERTY_RO(width, get_width);
  NCB_PROPERTY_RO(height, get_height);
  NCB_PROPERTY_RO(opened, get_opened);
  NCB_PROPERTY_RO(totalTime, get_totalTime);
  NCB_PROPERTY_RO(numberOfFrame, get_numberOfFrame);
  NCB_PROPERTY(numOfFrame, get_numOfFrame, set_numOfFrame);
  NCB_PROPERTY(FPSRate, get_FPSRate, set_FPSRate);
  NCB_PROPERTY(FPSScale, get_FPSScale, set_FPSScale);
  NCB_PROPERTY(screenWidth, get_screenWidth, set_screenWidth);
  NCB_PROPERTY(screenHeight, get_screenHeight, set_screenHeight);
  NCB_METHOD(showNextImage);
};
