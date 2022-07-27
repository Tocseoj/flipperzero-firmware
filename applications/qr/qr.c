#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdlib.h>
#include "qrcodegen.h"

// Gets version number as int from a side length e.g. 21 => 1
#define QR_VERSION_FOR_SIZE(n)  (((n) - 17) / 4)
typedef enum {
    QrVersionUnknown, // library calculates smallest version on create 
    QrVersion1, // 21x21 code, 3x3 resolution
    QrVersion2, // 25x25 code, 2x2 resolution
    QrVersion3, // 29x29 code, 2x2 resolution
    QrVersion4, // 33x33 code, 1x1 resolution
    QrVersion5, // 37x37 code, 1x1 resolution
    QrVersion6, // 41x41 code, 1x1 resolution
    QrVersion7, // 45x45 code, 1x1 resolution
    QrVersion8, // 49x49 code, 1x1 resolution
    QrVersion9, // 53x53 code, 1x1 resolution
    QrVersion10,// 57x57 code, 1x1 resolution
    QrVersion11,// 61x61 code, 1x1 resolution
} QrVersion;

typedef enum {
    QrModeUnknown, // library calculates proper mode on create 
	QrModeNumeric     = 0x1, // max len 722
	QrModeAlphaNum    = 0x2, // max len 468; 0–9, A–Z (upper-case only), space, $, %, *, +, -, ., /, :
	QrModeBinary      = 0x4, // max len 321
	QrModeKanji       = 0x8, // max len 198
	QrModeECI         = 0x7, // Extended Channel Interpretation
} QrMode;

#define QR_ERROR_TYPE_COUNT 4
// Error correction level
typedef enum {
    QrEccAuto = -1, // library sets to highest level possible after minimizing version
    QrEccLow, // ~7% erroneous
    QrEccMedium, // ~15% erroneous
    QrEccQuartile, // ~25% erroneous
    QrEccHigh, // ~30% erroneous
} QrEcc;

#define QR_MASK_TYPE_COUNT 8
typedef enum {
	QrMaskAuto = -1, // automatically select an optimal mask pattern
	QrMask0,
	QrMask1,
	QrMask2,
	QrMask3,
	QrMask4,
	QrMask5,
	QrMask6,
	QrMask7,
} QrMask;

#define QR_EDITING_PARAM_COUNT 3
typedef enum {
    QrEditingParamCounter,
    QrEditingParamLevel,
    QrEditingParamMask,
} QrEditingParam;

typedef struct {
    InputKey key;
    InputType type;
} QrEvent;

#define QR_MAX_DATA_LEN 468 // Version 11, level L, AlphaNum mode
#define QR_MAX_COUNTER 999999

typedef struct {
    QrEditingParam editing; // What parameter the arrow keys modify
    char prefix[4]; // prefix of encoded value
    uint32_t counter; // integer part of encoded value
    QrEcc level; // error correction level
    QrMask mask;
} QrState;

/**
 * @brief Updates what is drawn to display when view_port_update is called
 * 
 * Some defined constants
 * #define GUI_DISPLAY_WIDTH 128
 * #define GUI_DISPLAY_HEIGHT 64
 * (0,0)---x---(128,0)
 * |
 * y
 * |
 * (0, 64)
 * 
 * @param canvas gui/Canvas instance
 * @param ctx a mutex containing the app state, use this to draw dynamically
 */
static void qr_draw(Canvas* const canvas, void* ctx) {
    // get state mutex
    const QrState* qr_state = (QrState*)acquire_mutex_block((ValueMutex*)ctx);

    char encoded_value[QR_MAX_DATA_LEN + 1];
    sprintf(encoded_value, "%s%lu", qr_state->prefix, qr_state->counter); // TODO: not safe

    char buffer[9];
    // UI Design
    if(qr_state->editing == QrEditingParamCounter) canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 128, 0, AlignRight, AlignTop, "Rendered Value");
    if(qr_state->editing == QrEditingParamCounter) canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 128, 10, AlignRight, AlignTop, encoded_value);
    
    if(qr_state->editing == QrEditingParamLevel) canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 128, 22, AlignRight, AlignTop, "Correction Level");
    if(qr_state->editing == QrEditingParamLevel) canvas_set_font(canvas, FontSecondary);   
    sprintf(buffer, "%u", qr_state->level); // TODO: not safe
    canvas_draw_str_aligned(canvas, 128, 32, AlignRight, AlignTop, buffer);
    
    if(qr_state->editing == QrEditingParamMask) canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 128, 44, AlignRight, AlignTop, "Mask Type");
    if(qr_state->editing == QrEditingParamMask) canvas_set_font(canvas, FontSecondary);
    sprintf(buffer, "%u", qr_state->mask); // TODO: not safe
    canvas_draw_str_aligned(canvas, 128, 54, AlignRight, AlignTop, buffer);


    // Text data to qr using nayuki/QR-Code-generator 
    QrEcc correctionLevel = qr_state->level;
    bool boostLevel = false;
    if(correctionLevel == QrEccAuto) {
        correctionLevel = QrEccLow;
        boostLevel = true;
    }
    uint8_t tempBuffer[qrcodegen_BUFFER_LEN_FOR_VERSION(11)];
    uint8_t pixels[qrcodegen_BUFFER_LEN_FOR_VERSION(11)];
    bool ok = qrcodegen_encodeText(encoded_value, 
        tempBuffer, pixels, (enum qrcodegen_Ecc)correctionLevel,
        QrVersion1, QrVersion11, (enum qrcodegen_Mask)qr_state->mask, boostLevel);
    
    if(ok) {
        // Draw QR Code
        uint8_t size = qrcodegen_getSize(pixels);
        QrVersion version = QR_VERSION_FOR_SIZE(size); // Version is auto calculated
        // QrMode mode = QrModeAlphaNum; // TODO: or 'QrModeNumeric' or 'QrModeBinary' calculated by qrcodegen_encodeText
        
        uint8_t resolution;
        if(version == QrVersion1) {
            resolution = 3;
        } else if (version == QrVersion2 || version == QrVersion3) {
            resolution = 2;
        } else {
            resolution = 1;
        }
        for (uint16_t y = 0; y < size; y++) {
            for (uint16_t x = 0; x < size; x++) {
                if(qrcodegen_getModule(pixels, x, y)) {
                    canvas_draw_box(canvas, x*resolution, y*resolution, resolution, resolution);
                }
            }
        }
    } else {
        // canvas_set_color(canvas, ColorWhite);
        // canvas_draw_box(canvas, 0, 0, 64, 64);
        canvas_draw_frame(canvas, 0, 0, 64, 64);
        canvas_draw_str_aligned(canvas, 32, 32, AlignCenter, AlignCenter, "Error");
    }

    // release state mutex
    release_mutex((ValueMutex*)ctx, qr_state);
}

/**
 * @brief Handles hardware button events (pressing a key)
 * 
 * @param input_event interrupt event from os 
 * @param event_queue context object that will handle the input
 */
static void qr_input(InputEvent* input_event, osMessageQueueId_t event_queue) {
    furi_assert(event_queue);

    QrEvent qr_event = {.type = input_event->type, .key = input_event->key};
    osMessageQueuePut(event_queue, &qr_event, 0, osWaitForever);
}

int32_t qr_code_displayer(void* p) {
    // unknown
    UNUSED(p);

    // setup event queue
    osMessageQueueId_t event_queue = osMessageQueueNew(8, sizeof(QrEvent), NULL);

    // variable to track plugin state
    QrState* qr_state = (QrState*)malloc(sizeof(QrState));
    if(qr_state == NULL) {
        // Memory allocation failed!
        return 255; // should be osMemoryError
    }

    // Initialize state
    qr_state->editing = QrEditingParamCounter;
    strcpy(qr_state->prefix, "MIP"); // TODO: not safe
    qr_state->counter = 100609;
    qr_state->level = QrEccHigh;
    qr_state->mask = QrMask0;
    
    // setup mutex for locking state in callbacks
    ValueMutex state_mutex;
    if(!init_mutex(&state_mutex, qr_state, sizeof(QrState))) {
        FURI_LOG_E("QrCodeDisplayer", "cannot create mutex\r\n");
        free(qr_state);
        return 255; // osError
    }

    // setup view port with functions called on draw and hardware inputs
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, qr_draw, &state_mutex);
    view_port_input_callback_set(view_port, qr_input, event_queue);

    // open GUI and register view_port
    Gui* gui = furi_record_open("gui");
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    QrEvent event;
    for(bool processing = true; processing;) {
        // We can wait forever since our app only changes on input
        osStatus_t event_status = osMessageQueueGet(event_queue, &event, NULL, osWaitForever);

        if(event_status == osOK) {
            if(event.key == InputKeyBack) {
                // Quit if back is pressed
                processing = false;
            } else {
                // get state mutex here
                QrState* qr_state = (QrState*)acquire_mutex_block(&state_mutex);

                if(event.type == InputTypePress) {
                    int8_t direction = 0;
                    switch (event.key)
                    {
                    case InputKeyLeft:
                        direction = -1;
                        break;
                    case InputKeyRight:
                        direction = 1;
                        break;
                    case InputKeyUp:
                        if(qr_state->editing == 0) {
                            qr_state->editing = QR_EDITING_PARAM_COUNT - 1;
                        } else {
                            qr_state->editing--;
                        }
                        break;
                    case InputKeyDown:
                        if(qr_state->editing + 1 == QR_EDITING_PARAM_COUNT) {
                            qr_state->editing = 0;
                        } else {
                            qr_state->editing++;
                        }
                        break;
                    case InputKeyOk:
                        break;
                    case InputKeyBack:
                        break;
                    }

                    if(direction != 0) {
                        switch (qr_state->editing)
                        {
                        case QrEditingParamCounter:
                            if(qr_state->counter == 0 && direction < 0) {
                                qr_state->counter = QR_MAX_COUNTER;
                            } else if(qr_state->counter == QR_MAX_COUNTER && direction > 0) {
                                qr_state->counter = 0;
                            } else {
                                qr_state->counter += direction;
                            }
                            break;
                        case QrEditingParamLevel:
                            if(qr_state->level == -1 && direction < 0) {
                                qr_state->level = QR_ERROR_TYPE_COUNT - 1;
                            } else if(qr_state->level + 1 == QR_ERROR_TYPE_COUNT && direction > 0) {
                                qr_state->level = -1;
                            } else {
                                qr_state->level += direction;
                            }
                            break;
                        case QrEditingParamMask:
                            if(qr_state->mask == -1 && direction < 0) {
                                qr_state->mask = QR_MASK_TYPE_COUNT - 1;
                            } else if(qr_state->mask + 1 == QR_MASK_TYPE_COUNT && direction > 0) {
                                qr_state->mask = -1;
                            } else {
                                qr_state->mask += direction;
                            }
                            break;
                        }
                    }
                }

                // release state mutex
                release_mutex(&state_mutex, qr_state);

                // Redraw since state changed
                view_port_update(view_port);
            }
        } else if (event_status == osErrorTimeout) {
            // shouldn't timeout because we have no reason to not wait forever
        } else {
            FURI_LOG_E("QrCodeDisplayer", "got osError(%lu) from osMessageQueueGet\r\n", event_status);
            processing = false;
        }
    }

    view_port_enabled_set(view_port, false);
    gui_remove_view_port(gui, view_port);
    furi_record_close("gui");
    view_port_free(view_port);
    osMessageQueueDelete(event_queue);
    // delete_mutex and free malloc'd state variable
    delete_mutex(&state_mutex);
    free(qr_state);

    return 0; // osOk
}
