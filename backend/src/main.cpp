#include <libevdev/libevdev.h>
#include <linux/input.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>
#include <cstring>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

// ==========================================
// GESTURE DEFINITIONS
// ==========================================

enum class GestureMode {
    NONE,
    SINGLE_FINGER,
    THREE_FINGER
};

enum class Zone {
    NONE,
    LEFT,
    RIGHT
};

struct Finger {
    int tracking_id = -1;
    int x = 0;
    int y = 0;
    bool has_x = false;
    bool has_y = false;
};

// ==========================================
// WAIT FOR CHILD PROCESS
// ==========================================

bool waitForChild(pid_t pid, int& status) {
    while (waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR)
            continue;

        perror("waitpid");
        return false;
    }

    return true;
}

// ==========================================
// READ CURRENT PERCENTAGE
// ==========================================

int getPercentage(const char* command) {
    FILE* pipe = popen(command, "r");

    if (!pipe) {
        std::cerr << "Failed to execute: " << command << "\n";
        return 0;
    }

    char buffer[512];
    std::string output;

    while (fgets(buffer, sizeof(buffer), pipe))
        output += buffer;

    pclose(pipe);

    const size_t pos = output.find('%');

    if (pos == std::string::npos)
        return 0;

    size_t start = pos;

    while (start > 0 &&
           output[start - 1] >= '0' &&
           output[start - 1] <= '9') {
        --start;
    }

    try {
        return std::stoi(output.substr(start, pos - start));
    } catch (...) {
        return 0;
    }
}

// ==========================================
// SEND OSD UPDATE TO GNOME SHELL
// ==========================================

void sendOSD(const std::string& type, int percentage) {
    percentage = std::clamp(percentage, 0, 100);

    // gdbus expects a GVariant string argument, e.g. 'volume'.
    const std::string typeArg = "'" + type + "'";
    const std::string value = std::to_string(percentage);

    const pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return;
    }

    if (pid == 0) {
        execlp(
            "gdbus",
            "gdbus",
            "emit",
            "--session",
            "--object-path",
            "/org/touchflow/OSD",
            "--signal",
            "org.touchflow.OSD.Update",
            typeArg.c_str(),
            value.c_str(),
            nullptr
        );

        _exit(127);
    }

    int status = 0;

    if (waitForChild(pid, status)) {
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            std::cerr << "OSD notification failed\n";
    }
}

// ==========================================
// BRIGHTNESS CONTROL
// ==========================================

void changeBrightness(bool increase) {
    const char* command = increase
        ? "brightnessctl set 1%+"
        : "brightnessctl set 1%-";

    if (std::system(command) != 0) {
        std::cerr << "Brightness control failed\n";
        return;
    }

    const int level = getPercentage("brightnessctl -m");
    sendOSD("brightness", level);
}

// ==========================================
// VOLUME CONTROL
// ==========================================

void changeVolume(bool increase) {
    const char* command = increase
        ? "pactl set-sink-volume @DEFAULT_SINK@ +1%"
        : "pactl set-sink-volume @DEFAULT_SINK@ -1%";

    if (std::system(command) != 0) {
        std::cerr << "Volume control failed\n";
        return;
    }

    const int level = getPercentage(
        "pactl get-sink-volume @DEFAULT_SINK@"
    );

    sendOSD("volume", level);
}

// ==========================================
// SCREENSHOT
// ==========================================

void takeScreenshot() {
    const char* home = std::getenv("HOME");

    if (!home) {
        std::cerr << "HOME environment variable unavailable\n";
        return;
    }

    const std::string directory =
        std::string(home) + "/Pictures/Screenshots";

    std::error_code error;
    std::filesystem::create_directories(directory, error);

    if (error) {
        std::cerr << "Cannot create screenshot directory: "
                  << error.message() << "\n";
        return;
    }

    const auto now = std::chrono::system_clock::now();
    const auto timestamp =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()
        ).count();

    const std::string filename =
        directory + "/TouchFlow-" +
        std::to_string(timestamp) + ".png";

    const pid_t pid = fork();

    if (pid < 0) {
        perror("Screenshot fork failed");
        return;
    }

    if (pid == 0) {
        execlp(
            "gnome-screenshot",
            "gnome-screenshot",
            "-f",
            filename.c_str(),
            nullptr
        );

        _exit(127);
    }

    int status = 0;

    if (!waitForChild(pid, status))
        return;

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        std::cout << "Screenshot saved: " << filename << "\n";
    } else {
        std::cerr << "Screenshot failed\n";
    }
}

// ==========================================
// MAIN
// ==========================================

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: ./touchflow /dev/input/eventX\n";
        return 1;
    }

    // Open touchpad.
    const int fd = open(argv[1], O_RDONLY | O_CLOEXEC);

    if (fd < 0) {
        perror("Cannot open touchpad");
        return 1;
    }

    libevdev* dev = nullptr;
    const int initResult = libevdev_new_from_fd(fd, &dev);

    if (initResult < 0) {
        std::cerr << "Cannot initialize libevdev: "
                  << strerror(-initResult) << "\n";
        close(fd);
        return 1;
    }

    // Validate multitouch support.
    if (!libevdev_has_event_code(dev, EV_ABS, ABS_MT_SLOT) ||
        !libevdev_has_event_code(dev, EV_ABS, ABS_MT_TRACKING_ID) ||
        !libevdev_has_event_code(dev, EV_ABS, ABS_MT_POSITION_X) ||
        !libevdev_has_event_code(dev, EV_ABS, ABS_MT_POSITION_Y)) {

        std::cerr << "Multitouch coordinates unavailable\n";
        libevdev_free(dev);
        close(fd);
        return 1;
    }

    const input_absinfo* slotInfo =
        libevdev_get_abs_info(dev, ABS_MT_SLOT);
    const input_absinfo* xInfo =
        libevdev_get_abs_info(dev, ABS_MT_POSITION_X);
    const input_absinfo* yInfo =
        libevdev_get_abs_info(dev, ABS_MT_POSITION_Y);

    if (!slotInfo || !xInfo || !yInfo) {
        std::cerr << "Cannot read touchpad configuration\n";
        libevdev_free(dev);
        close(fd);
        return 1;
    }

    const int slotMin = slotInfo->minimum;
    const int slotCount = slotInfo->maximum - slotMin + 1;

    if (slotCount <= 0) {
        std::cerr << "Invalid touchpad slot configuration\n";
        libevdev_free(dev);
        close(fd);
        return 1;
    }

    std::vector<Finger> fingers(slotCount);
    int currentSlot = 0;

    // Touchpad coordinate boundaries.
    const int minX = xInfo->minimum;
    const int maxX = xInfo->maximum;
    const int minY = yInfo->minimum;
    const int maxY = yInfo->maximum;

    const int touchpadWidth = maxX - minX;
    const int touchpadHeight = maxY - minY;

    if (touchpadWidth <= 0 || touchpadHeight <= 0) {
        std::cerr << "Invalid touchpad dimensions\n";
        libevdev_free(dev);
        close(fd);
        return 1;
    }

    // Configure edge zones: 8% on each side.
    constexpr double EDGE_PERCENT = 0.06;

    const int edgeWidth = std::max(
        1,
        static_cast<int>(touchpadWidth * EDGE_PERCENT)
    );

    const int leftEdge = minX + edgeWidth;
    const int rightEdge = maxX - edgeWidth;

    // Movement thresholds.
    const int threshold = std::max(1, touchpadHeight / 100);
    const int screenshotThreshold =
        std::max(1, touchpadHeight / 4);

    // Gesture state.
    GestureMode gestureMode = GestureMode::NONE;
    Zone activeZone = Zone::NONE;

    bool gestureActive = false;
    bool screenshotTriggered = false;
    bool ignoreUntilAllReleased = false;

    // True after a second finger joins a one-finger gesture.
    // While true, wait for the third finger instead of ignoring
    // the whole contact sequence.
    bool multiTouchCandidate = false;

    int previousY = 0;
    int accumulatedMovement = 0;

    std::cout << "\nTouchFlow running on: "
              << libevdev_get_name(dev) << "\n";
    std::cout << "Left edge: Brightness\n";
    std::cout << "Right edge: Volume\n";
    std::cout << "Three-finger swipe DOWN: Screenshot\n";
    std::cout << "Edge width: 8%\n\n";

    // Main event loop.
    while (true) {
        input_event ev{};

        const int rc = libevdev_next_event(
            dev,
            LIBEVDEV_READ_FLAG_NORMAL,
            &ev
        );

        if (rc < 0) {
            if (rc == -EAGAIN)
                continue;

            std::cerr << "Input error: " << rc << "\n";
            break;
        }

        if (rc != LIBEVDEV_READ_STATUS_SUCCESS)
            continue;

        // Process raw touchpad events.
        if (ev.type == EV_ABS) {
            if (ev.code == ABS_MT_SLOT) {
                currentSlot = ev.value - slotMin;
            } else if (currentSlot >= 0 &&
                       currentSlot < slotCount) {

                Finger& finger = fingers[currentSlot];

                if (ev.code == ABS_MT_TRACKING_ID) {
                    finger.tracking_id = ev.value;
                    finger.has_x = false;
                    finger.has_y = false;
                } else if (ev.code == ABS_MT_POSITION_X) {
                    finger.x = ev.value;
                    finger.has_x = true;
                } else if (ev.code == ABS_MT_POSITION_Y) {
                    finger.y = ev.value;
                    finger.has_y = true;
                }
            }
        }

        // Process only completed event frames.
        if (ev.type != EV_SYN || ev.code != SYN_REPORT)
            continue;

        int activeCount = 0;
        int activeYSum = 0;
        Finger* activeFinger = nullptr;
        bool allHaveY = true;

        // Collect active finger information.
        for (auto& finger : fingers) {
            if (finger.tracking_id >= 0) {
                ++activeCount;
                activeFinger = &finger;

                if (finger.has_y)
                    activeYSum += finger.y;
                else
                    allHaveY = false;
            }
        }

        // All fingers released: reset everything.
        if (activeCount == 0) {
            gestureActive = false;
            gestureMode = GestureMode::NONE;
            activeZone = Zone::NONE;
            accumulatedMovement = 0;
            screenshotTriggered = false;
            multiTouchCandidate = false;
            ignoreUntilAllReleased = false;
            continue;
        }

        // Ignore the rest of a cancelled gesture until all
        // contacts have been released.
        if (ignoreUntilAllReleased)
            continue;

        // ==========================================
        // HANDLE FINGER-COUNT CHANGES
        // ==========================================

        if (gestureActive &&
            gestureMode == GestureMode::SINGLE_FINGER &&
            activeCount != 1) {

            // A second/third finger has joined. Cancel the
            // one-finger action, but allow the three-finger
            // gesture to become active when finger three lands.
            gestureActive = false;
            gestureMode = GestureMode::NONE;
            activeZone = Zone::NONE;
            accumulatedMovement = 0;

            if (activeCount >= 2 && activeCount <= 3) {
                multiTouchCandidate = true;
            } else {
                ignoreUntilAllReleased = true;
            }

            continue;
        }

        if (gestureActive &&
            gestureMode == GestureMode::THREE_FINGER &&
            activeCount != 3) {

            // Once a three-finger gesture has started, a finger
            // leaving cancels that gesture until all are released.
            gestureActive = false;
            gestureMode = GestureMode::NONE;
            activeZone = Zone::NONE;
            accumulatedMovement = 0;
            screenshotTriggered = false;
            multiTouchCandidate = false;
            ignoreUntilAllReleased = true;

            std::cout << "Three-finger gesture cancelled\n";
            continue;
        }

        // ==========================================
        // START A NEW GESTURE
        // ==========================================

        if (!gestureActive) {
            if (multiTouchCandidate) {
                // Two fingers are down: wait for finger three.
                if (activeCount == 2)
                    continue;

                // Third finger has landed: start tracking now.
                if (activeCount == 3) {
                    if (!allHaveY)
                        continue;

                    gestureActive = true;
                    gestureMode = GestureMode::THREE_FINGER;
                    previousY = activeYSum / 3;
                    accumulatedMovement = 0;
                    screenshotTriggered = false;
                    activeZone = Zone::NONE;

                    std::cout << "Three-finger gesture activated\n";
                    continue;
                }

                // The contact sequence fell back to one finger,
                // or an unsupported number of fingers is present.
                ignoreUntilAllReleased = true;
                continue;
            }

            // Start a normal one-finger gesture.
            if (activeCount == 1) {
                if (activeFinger == nullptr ||
                    !activeFinger->has_x ||
                    !activeFinger->has_y) {
                    continue;
                }

                gestureActive = true;
                gestureMode = GestureMode::SINGLE_FINGER;
                previousY = activeFinger->y;
                accumulatedMovement = 0;

                if (activeFinger->x <= leftEdge) {
                    activeZone = Zone::LEFT;
                    std::cout << "Brightness zone activated\n";
                } else if (activeFinger->x >= rightEdge) {
                    activeZone = Zone::RIGHT;
                    std::cout << "Volume zone activated\n";
                } else {
                    activeZone = Zone::NONE;
                }

                continue;
            }

            // Two fingers: wait to see whether a third finger lands.
            if (activeCount == 2) {
                multiTouchCandidate = true;
                continue;
            }

            // Three fingers may also arrive in the same event frame.
            if (activeCount == 3) {
                if (!allHaveY)
                    continue;

                gestureActive = true;
                gestureMode = GestureMode::THREE_FINGER;
                previousY = activeYSum / 3;
                accumulatedMovement = 0;
                screenshotTriggered = false;
                activeZone = Zone::NONE;

                std::cout << "Three-finger gesture activated\n";
                continue;
            }

            // Ignore unsupported finger counts until release.
            ignoreUntilAllReleased = true;
            continue;
        }

        // ==========================================
        // THREE-FINGER SWIPE DOWN
        // ==========================================

        if (gestureMode == GestureMode::THREE_FINGER) {
            if (!allHaveY)
                continue;

            const int currentAverageY = activeYSum / 3;
            const int deltaY = currentAverageY - previousY;

            previousY = currentAverageY;
            accumulatedMovement += deltaY;

            // Linux touchpad Y coordinates increase downward.
            if (!screenshotTriggered &&
                accumulatedMovement >= screenshotThreshold) {

                std::cout << "\nThree-finger swipe DOWN detected\n";
                takeScreenshot();
                screenshotTriggered = true;
            }

            continue;
        }

        // ==========================================
        // SINGLE-FINGER BRIGHTNESS / VOLUME
        // ==========================================

        if (gestureMode == GestureMode::SINGLE_FINGER) {
            if (activeFinger == nullptr ||
                !activeFinger->has_x ||
                !activeFinger->has_y) {
                continue;
            }

            // Middle area: no action.
            if (activeZone == Zone::NONE)
                continue;

            const int deltaY = activeFinger->y - previousY;
            previousY = activeFinger->y;
            accumulatedMovement += deltaY;

            // Process movement in increments.
            while (std::abs(accumulatedMovement) >= threshold) {
                const bool movingUp = accumulatedMovement < 0;

                if (activeZone == Zone::LEFT) {
                    changeBrightness(movingUp);

                    std::cout << "Brightness "
                              << (movingUp ? "UP" : "DOWN")
                              << "\n";
                } else if (activeZone == Zone::RIGHT) {
                    changeVolume(movingUp);

                    std::cout << "Volume "
                              << (movingUp ? "UP" : "DOWN")
                              << "\n";
                }

                accumulatedMovement += movingUp
                    ? threshold
                    : -threshold;
            }
        }
    }

    // Cleanup.
    libevdev_free(dev);
    close(fd);

    std::cout << "TouchFlow stopped\n";
    return 0;
}
