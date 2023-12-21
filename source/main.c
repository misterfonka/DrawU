#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <malloc.h>
#include <stdlib.h>
#include <coreinit/screen.h>
#include <coreinit/cache.h>
#include <whb/log_cafe.h>
#include <whb/log_udp.h>
#include <whb/log.h>
#include <whb/proc.h>
#include <vpad/input.h>

#define SCREEN_WIDTH 854
#define SCREEN_HEIGHT 480
#define MAX_POINTS 10000

/* Struct to represent a pixel with its color */
typedef struct {
    int x;
    int y;
    uint32_t color;
} Pixel;

/* Function to draw a larger pixel at the specified position */
void drawPixel(int x, int y, uint32_t color, OSScreenID screen) {
    for (int i = -1; i <= 1; ++i) {
        for (int j = -1; j <= 1; ++j) {
            OSScreenPutPixelEx(screen, x + i, y + j, color);
        }
    }
}

/* Function to draw a line between two points */
void drawLine(int x1, int y1, int x2, int y2, uint32_t color, OSScreenID screen) {
    int dx = abs(x2 - x1);
    int sx = x1 < x2 ? 1 : -1;
    int dy = -abs(y2 - y1);
    int sy = y1 < y2 ? 1 : -1;
    int err = dx + dy;

    while (true) {
        for (int i = -1; i <= 1; ++i) {
            for (int j = -1; j <= 1; ++j) {
                OSScreenPutPixelEx(screen, x1 + i, y1 + j, color);
            }
        }

        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x1 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y1 += sy;
        }
    }
}

int main(int argc, char** argv) {
    /* Init logging modules */
    WHBLogCafeInit();
    WHBLogUdpInit();
    WHBLogPrint("Logging initialized.");
    /* Init the process and screen */
    WHBProcInit();
    /* Make sure to not access the OSScreen API after the while loop below.
    This caused me to be stuck on the WiiU loading screen once closing the
    software. */
    OSScreenInit();
    VPADInit();

    VPADStatus status;
    VPADReadError error;
    bool vpad_fatal = false;

    /* Get buffer sizes for TV and DRC screens */
    size_t tvBufferSize = OSScreenGetBufferSizeEx(SCREEN_TV);
    size_t drcBufferSize = OSScreenGetBufferSizeEx(SCREEN_DRC);
    WHBLogPrintf("Allocating 0x%X bytes for the TV, and 0x%X bytes for the DRC.", tvBufferSize, drcBufferSize);

    /* Allocate memory for screen buffers */
    void* tvBuffer = memalign(0x100, tvBufferSize);
    void* drcBuffer = memalign(0x100, drcBufferSize);

    if (!tvBuffer || !drcBuffer) {
        WHBLogPrint("Out of memory!");

        if (tvBuffer) free(tvBuffer);
        if (drcBuffer) free(drcBuffer);

        /* Deinit everything and exit */
        VPADShutdown();
        OSScreenShutdown();
        WHBProcShutdown();
        WHBLogPrint("Quitting.");
        WHBLogCafeDeinit();
        WHBLogUdpDeinit();

        return 1;
    }

    /* Set screen buffers */
    OSScreenSetBufferEx(SCREEN_TV, tvBuffer);
    OSScreenSetBufferEx(SCREEN_DRC, drcBuffer);

    /* Enable screens */
    OSScreenEnableEx(SCREEN_TV, true);
    OSScreenEnableEx(SCREEN_DRC, true);

    int pixelX = SCREEN_WIDTH / 2;  /* Initial X position of the pixel */
    int pixelY = SCREEN_HEIGHT / 2; /* Initial Y position of the pixel */

    Pixel* pixels = (Pixel*)malloc(MAX_POINTS * sizeof(Pixel));
    int numPixels = 0;

    /* Define set colors */
    uint32_t colors[] = {
        0xFFFFFF00,  /* White */
        0xFF000000,  /* Red */
        0x00FF0000,  /* Green */
        0x0000FF00,  /* Blue */
    };

    /* Define corresponding color names */
    const char* colorNames[] = {
        "WHITE",
        "RED",
        "GREEN",
        "BLUE",
    };

    int currentColorIndex = 0;

    while (WHBProcIsRunning()) {

        VPADRead(VPAD_CHAN_0, &status, 1, &error);

        /* Check for any errors on the DRC */
        switch (error) {
            case VPAD_READ_SUCCESS: {
                break;
            }
            case VPAD_READ_NO_SAMPLES: {
                continue;
            }
            case VPAD_READ_INVALID_CONTROLLER: {
                WHBLogPrint("Gamepad disconnected!");
                vpad_fatal = true;
                break;
            }
            default: {
                WHBLogPrintf("Unknown VPAD error! %08X", error);
                vpad_fatal = true;
                break;
            }
        }

        if (vpad_fatal) break;

        /* Clear each buffer */
        OSScreenClearBufferEx(SCREEN_TV, 0x00000000);
        OSScreenClearBufferEx(SCREEN_DRC, 0x00000000);

        /* Print text */
        OSScreenPutFontEx(SCREEN_TV, 0, 0, "DrawU");
        OSScreenPutFontEx(SCREEN_TV, 0, 2, "Use the d-pad to draw pixels to the screen.");
        char displayText[128];
        snprintf(displayText, sizeof(displayText), "Press A to cycle through different colors. Current color: %s", colorNames[currentColorIndex]);
        OSScreenPutFontEx(SCREEN_TV, 0, 3, displayText);
        OSScreenPutFontEx(SCREEN_TV, 0, 4, "Press HOME to quit.");
        OSScreenPutFontEx(SCREEN_TV, 0, 26, "Created by misterfonka on Github.");

        /* Logic for drawing the pixels to the screen
            For the up and down buttons on the d-pad */
        if (status.hold & VPAD_BUTTON_UP && pixelY > 0) {
            pixelY -= 5;
        } else if (status.hold & VPAD_BUTTON_DOWN && pixelY < SCREEN_HEIGHT - 1) {
            pixelY += 5;
        }

        /* For the left and right button on the D-pad */
        if (status.hold & VPAD_BUTTON_LEFT && pixelX > 0) {
            pixelX -= 5;
        } else if (status.hold & VPAD_BUTTON_RIGHT && pixelX < SCREEN_WIDTH - 1) {
            pixelX += 5;
        }

        /* Change color on A button press */
        if (status.trigger & VPAD_BUTTON_A) {
            currentColorIndex = (currentColorIndex + 1) % (sizeof(colors) / sizeof(colors[0]));
        }

        /* Record the pixel position and color */
        if (numPixels < MAX_POINTS) {
            pixels[numPixels].x = pixelX;
            pixels[numPixels].y = pixelY;
            pixels[numPixels].color = colors[currentColorIndex];
            numPixels++;
        }

        /* Flush caches */
        DCFlushRange(tvBuffer, tvBufferSize);
        DCFlushRange(drcBuffer, drcBufferSize);

        /* Draw the pixels */
        for (int i = 0; i < numPixels; ++i) {
            drawPixel(pixels[i].x, pixels[i].y, pixels[i].color, SCREEN_DRC);
        }

        /* Draw lines between consecutive points */
        for (int i = 1; i < numPixels; ++i) {
            drawLine(pixels[i - 1].x, pixels[i - 1].y, pixels[i].x, pixels[i].y, pixels[i].color, SCREEN_DRC);
        }

        /* Flip the buffers */
        OSScreenFlipBuffersEx(SCREEN_TV);
        OSScreenFlipBuffersEx(SCREEN_DRC);
    }

    WHBLogPrint("Shutdown requested!");

    if (tvBuffer) free(tvBuffer);
    if (drcBuffer) free(drcBuffer);
    if (pixels) free(pixels);

    /* Deinit everything and quit */
    VPADShutdown();
    WHBProcShutdown();
    WHBLogPrint("Quitting.");
    WHBLogCafeDeinit();
    WHBLogUdpDeinit();

    return 1;
}