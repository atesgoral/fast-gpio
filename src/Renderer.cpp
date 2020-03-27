#include "Renderer.h"

#include "../lib/fastgpio/fastgpioomega2.h"
#include "../lib/libnewgpio/hdr/TimeHelper.h"

#define GPIO_INPUT 0
#define GPIO_OUTPUT 1

#define PIN_LATCH 2
#define PIN_CLOCK 1
#define PIN_DATA 0

#define PIN_RED_BUTTON 19
#define PIN_YELLOW_BUTTON 18

#define TARGET_FPS 60

void Renderer::gpioLoop(void *pArg) {
  Renderer &renderer = *reinterpret_cast<Renderer *>(pArg);
  SafeBuffer &safeBuffer = renderer.m_safeBuffer;

  renderer.m_switchEventHandle.data = &renderer;

  FastGpioOmega2 gpio;

  gpio.SetDirection(PIN_LATCH, GPIO_OUTPUT);
  gpio.SetDirection(PIN_CLOCK, GPIO_OUTPUT);
  gpio.SetDirection(PIN_DATA, GPIO_OUTPUT);

  gpio.SetDirection(PIN_RED_BUTTON, GPIO_INPUT);
  gpio.SetDirection(PIN_YELLOW_BUTTON, GPIO_INPUT);

  gpio.Set(PIN_LATCH, 0);
  gpio.Set(PIN_CLOCK, 0);
  gpio.Set(PIN_DATA, 0);

  int prevRedButtonDown = 0;
  int prevYellowButtonDown = 0;

  long long maxFrameInterval = 1000000000LL / TARGET_FPS;

  while (1) {
    long long start = timeNowNS();

    const char *pBuffer = safeBuffer.acquire();

    for (int half = 0; half < 2; half++) {
      for (int row = 0; row < 16; row++) {
        for (int col = 0; col < 8; col++) {
          gpio.Set(PIN_DATA, pBuffer[row * 16 + col + half * 8]);

          gpio.Set(PIN_CLOCK, 1);
          gpio.Set(PIN_CLOCK, 0);
        }
      }
    }

    safeBuffer.release();

    gpio.Set(PIN_LATCH, 1);
    gpio.Set(PIN_LATCH, 0);

    if (!renderer.m_isRunning) {
      break;
    }

    int redButtonDown;
    int yellowButtonDown;

    gpio.Read(PIN_RED_BUTTON, redButtonDown);
    gpio.Read(PIN_YELLOW_BUTTON, yellowButtonDown);

    if (redButtonDown != prevRedButtonDown) {
      prevRedButtonDown = redButtonDown;

      if (redButtonDown) {
        renderer.postEvent("RED_DOWN");
      } else {
        renderer.postEvent("RED_UP");
      }
    }

    if (yellowButtonDown != prevYellowButtonDown) {
      prevYellowButtonDown = yellowButtonDown;

      if (yellowButtonDown) {
        renderer.postEvent("YELLOW_DOWN");
      } else {
        renderer.postEvent("YELLOW_UP");
      }
    }

    long long elapsed = timeNowNS() - start;
    long long toGo = maxFrameInterval - elapsed;

    if (toGo > 0) {
      sleepNano(toGo);
    } else {
      printf("!");
    }
  }
}

void Renderer::postEvent(const char *szEventName) {
  m_switchEventQueue.enqueue(szEventName);
  uv_async_send(&m_switchEventHandle);
}

void Renderer::start(const SwitchEventCallback switchEventCallback) {
  m_switchEventCallback = switchEventCallback;
  m_isRunning = true;

  uv_async_init(uv_default_loop(), &m_switchEventHandle, [](uv_async_t *pHandle) -> void {
    Renderer &renderer = *reinterpret_cast<Renderer *>(pHandle->data);

    vector<string> &queue = renderer.m_switchEventQueue.acquire();

    for (vector<string>::const_iterator it = queue.begin(); it != queue.end(); ++it) {
      renderer.m_switchEventCallback((*it).c_str());
    }

    renderer.m_switchEventQueue.release();
  });

  uv_thread_create(&m_thread, gpioLoop, this);
}

void Renderer::render(const char *pBuffer) {
  m_safeBuffer.set(pBuffer);
}

void Renderer::stop() {
  m_safeBuffer.clear();
  m_isRunning = false; // @todo set atomically?
  uv_thread_join(&m_thread);
}
