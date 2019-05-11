/*
 * Copyright 2019 Andrew Rossignol (andrew.rossignol@gmail.com)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <linux/uinput.h>
#include <unistd.h>
#include <usbcan.h>

// A simple utility that reads CAN messages, parses them and emits uinput events
// from the steering wheel, throttle, brake and paddle shifters.
//
// This allows a car to be used as a joystick.

namespace {

// The IDs of the various inputs.
constexpr uint16_t kThrottleId = 0x01a1;
constexpr uint16_t kSteeringId = 0x01e5;
constexpr uint16_t kBrakeId = 0x00f1;
constexpr uint16_t kPaddleShifterId = 0x01f3;

// Limits for the brake, throttle and steering axes.
constexpr uint8_t kBrakeLimit = 0x4b;
constexpr uint8_t kThrottleLimit = 0xfe;
constexpr int16_t kSteeringLimit = 0x1d00;

/*
 * Handle a CAN message containing the throttle position.
 */
void HandleThrottlePos(const struct usbcan_msg* msg, int uinput_fd) {
  if (msg->frame.can_dlc == 7) {
    uint8_t value = msg->frame.data[6];

    struct input_event throttle_event = {};
    throttle_event.type = EV_ABS;
    throttle_event.code = ABS_GAS;
    throttle_event.value = value;

    if (write(uinput_fd, &throttle_event, sizeof(throttle_event)) < 0) {
      fprintf(stderr, "Failed to write throttle event: %d (%s)\n",
          errno, strerror(errno));
      exit(-1);
    }
  } else {
    fprintf(stderr, "Malformed accelerator position\n");
  }
}

/*
 * Handle a CAN message containing the brake position.
 */
void HandleBrakePos(const struct usbcan_msg* msg, int uinput_fd) {
  if (msg->frame.can_dlc == 6) {
    uint8_t value = msg->frame.data[1];

    struct input_event brake_event = {};
    brake_event.type = EV_ABS;
    brake_event.code = ABS_BRAKE;
    brake_event.value = value;

    if (write(uinput_fd, &brake_event, sizeof(brake_event)) < 0) {
      fprintf(stderr, "Failed to write brake event: %d (%s)\n",
          errno, strerror(errno));
      exit(-1);
    }
  } else {
    fprintf(stderr, "Malformed brake position\n");
  }
}

/*
 * Handle a CAN message containing the steering position.
 */
void HandleSteeringPos(const struct usbcan_msg* msg, int uinput_fd) {
  if (msg->frame.can_dlc == 8) {
    int16_t value = (static_cast<uint16_t>(msg->frame.data[1]) << 8)
        | msg->frame.data[2];
    struct input_event wheel_event = {};
    wheel_event.type = EV_ABS;
    wheel_event.code = ABS_WHEEL;
    wheel_event.value = -value;

    if (write(uinput_fd, &wheel_event, sizeof(wheel_event)) < 0) {
      fprintf(stderr, "Failed to write wheel event: %d (%s)\n",
          errno, strerror(errno));
      exit(-1);
    }
  } else {
    fprintf(stderr, "Malformed steering position\n");
  }
}

/*
 * Handle a CAN message containing the paddle shifter state.
 */
void HandlePaddleShifter(const struct usbcan_msg* msg, int uinput_fd) {
  if (msg->frame.can_dlc == 3) {
    bool up_pressed = msg->frame.data[1] & 0x01;
    bool down_pressed = msg->frame.data[1] & 0x02;

    struct input_event gear_event = {};
    gear_event.type = EV_KEY;
    gear_event.code = BTN_GEAR_UP;
    gear_event.value = up_pressed;

    if (write(uinput_fd, &gear_event, sizeof(gear_event)) < 0) {
      fprintf(stderr, "Failed to write gear up event: %d (%s)\n",
          errno, strerror(errno));
      exit(-1);
    }

    gear_event.code = BTN_GEAR_DOWN;
    gear_event.value = down_pressed;

    if (write(uinput_fd, &gear_event, sizeof(gear_event)) < 0) {
      fprintf(stderr, "Failed to write gear down event: %d (%s)\n",
          errno, strerror(errno));
      exit(-1);
    }
  } else {
    fprintf(stderr, "Malformed steering position\n");
  }

}

/*
 * Handle a CAN message and emit a uinput event if a match was found.
 *
 * Returns true if the message was handled.
 */
bool HandleCanMessage(const struct usbcan_msg *msg, int uinput_fd) {
  bool handled = true;
  switch (msg->frame.can_id) {
    case kThrottleId:
      HandleThrottlePos(msg, uinput_fd);
      break;
    case kBrakeId:
      HandleBrakePos(msg, uinput_fd);
      break;
    case kSteeringId:
      HandleSteeringPos(msg, uinput_fd);
      break;
    case kPaddleShifterId:
      HandlePaddleShifter(msg, uinput_fd);
      break;
    default:
      handled = false;
      break;
  }

  return handled;
}

/*
 * Invoked by libcanusb when a group of CAN packets are ready.
 */
void CanPacketCallback(uint32_t device_id, uint32_t bus_id,
                       struct usbcan_msg *msgs, uint32_t num_msgs,
                       void *arg) {
  int uinput_fd = *reinterpret_cast<int*>(arg);

  bool handled = false;
  for (uint32_t i = 0; i < num_msgs; i++) {
    handled |= HandleCanMessage(&msgs[i], uinput_fd);
  }

  if (handled) {
    // If the message was handled, emit a SYN event to uinput.
    struct input_event sync_event = {};
    sync_event.type = EV_SYN;
    sync_event.code = SYN_REPORT;
    sync_event.value = 0;

    if (write(uinput_fd, &sync_event, sizeof(sync_event)) < 0) {
      fprintf(stderr, "Failed to write sync event: %d (%s)\n",
          errno, strerror(errno));
    }
  }
}

// Initialize the uinput device for the joystick.
bool InitUinputDevice(int uinput_fd) {
  if (ioctl(uinput_fd, UI_SET_EVBIT, EV_ABS) < 0) {
    fprintf(stderr, "Failed to set abs attribute: %d (%s)\n",
        errno, strerror(errno));
    return false;
  }

  if (ioctl(uinput_fd, UI_SET_ABSBIT, ABS_WHEEL) < 0) {
    fprintf(stderr, "Failed to set wheel attribute: %d (%s)\n",
        errno, strerror(errno));
    return false;
  }

  if (ioctl(uinput_fd, UI_SET_ABSBIT, ABS_GAS) < 0) {
    fprintf(stderr, "Failed to set gas attribute: %d (%s)\n",
        errno, strerror(errno));
    return false;
  }

  if (ioctl(uinput_fd, UI_SET_ABSBIT, ABS_BRAKE) < 0) {
    fprintf(stderr, "Failed to set brake attribute: %d (%s)\n",
        errno, strerror(errno));
    return false;
  }

  if (ioctl(uinput_fd, UI_SET_EVBIT, EV_KEY) < 0) {
    fprintf(stderr, "Failed to set key attribute: %d (%s)\n",
        errno, strerror(errno));
    return false;
  }

  if (ioctl(uinput_fd, UI_SET_KEYBIT, BTN_GEAR_UP) < 0) {
    fprintf(stderr, "Failed to set gear up attribute: %d (%s)\n",
        errno, strerror(errno));
    return false;
  }

  if (ioctl(uinput_fd, UI_SET_KEYBIT, BTN_GEAR_DOWN) < 0) {
    fprintf(stderr, "Failed to set gear down attribute: %d (%s)\n",
        errno, strerror(errno));
    return false;
  }

  struct uinput_user_dev uidev = {};
  snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "Cadillac ELR");

  uidev.id.bustype = BUS_VIRTUAL;
  uidev.id.vendor = 1;
  uidev.id.product = 1;
  uidev.id.version = 1;

  uidev.absmax[ABS_WHEEL] = kSteeringLimit;
  uidev.absmin[ABS_WHEEL] = -kSteeringLimit;
  uidev.absfuzz[ABS_WHEEL] = 0;
  uidev.absflat[ABS_WHEEL] = 0;

  uidev.absmax[ABS_GAS] = kThrottleLimit;
  uidev.absmin[ABS_GAS] = 0;
  uidev.absfuzz[ABS_GAS] = 0;
  uidev.absflat[ABS_GAS] = 0;

  uidev.absmax[ABS_BRAKE] = kBrakeLimit;
  uidev.absmin[ABS_BRAKE] = 0;
  uidev.absfuzz[ABS_BRAKE] = 0;
  uidev.absflat[ABS_BRAKE] = 0;

  if (write(uinput_fd, &uidev, sizeof(uidev)) < 0) {
    fprintf(stderr, "Failed to write uinput descriptor: %d (%s)\n",
        errno, strerror(errno));
    return false;
  }

  if (ioctl(uinput_fd, UI_DEV_CREATE) < 0) {
    fprintf(stderr, "Failed to create uinput device: %d (%s)\n",
        errno, strerror(errno));
    return false;
  }

  return true;
}

}  // namespace

int main(int argc, char** argv) {
  fprintf(stderr, "can-joystick\n");

  int uinput_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
  if (uinput_fd < 0) {
    fprintf(stderr, "Failed to open uinput: %d (%s)\n", errno, strerror(errno));
    return -1;
  }

  if (!InitUinputDevice(uinput_fd)) {
    return -1;
  }

  bool success = usbcan_library_init();
  if (!success) {
    fprintf(stderr, "Failed to init usbcan\n");
    return -1;
  }

  struct usbcan_bus_config config = {
    .speed = CAN_SPEED_500KBPS,
    .filters = nullptr,
    .num_filters = 0,
    .cb = CanPacketCallback,
    .arg = &uinput_fd,
  };

  success = usbcan_init(/*device_id=*/ 0, /*bus_id=*/ 0, &config);
  if (!success) {
    fprintf(stderr, "Failed to init listener on bus 0\n");
    return -1;
  }

  success = usbcan_start(/*device_id=*/ 0, /*bus_id=*/ 0);
  if (!success) {
    fprintf(stderr, "Failed to start listening on bus 0\n");
    return -1;
  }

  pause();
  usbcan_library_close();
  return 0;
}
