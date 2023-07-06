# ESP32 DSHOT

RMT implementation of the DSHOT protocol for ESP-IDF.

## Installation

* Make sure to enable C++ support in your IDF project.

* Download the repository and place it in your `<project_dir>/components/` folder.

## Usage

* Include the header file in your project: `#include "DShotRMT.h"`

* Initialize the `DShotRMT` instance:

    ```cpp
    DShotRMT esc;
    esc.install(DSHOT_GPIO, DSHOT_RMT_CHANNEL));
    esc.init();
    ```

* Send velocity commands:

    ```cpp
    for (;;) {
        esc.sendThrottle(throttle);
        vTaskDelay(1);
    }
    ```

    > ! The ESC will disarm if you don't periodically send a throttle command !

* Reverse the motor direction:

    ```cpp
    esc.setReversed(true);
    ```

ESCs are very picky about arming/disarming. If you have trouble arming your ESCs, try the basic [example](examples/basic.cpp).
