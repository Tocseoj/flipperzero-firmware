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

// Error correction level
typedef enum {
    QrEccAuto = -1, // library sets to highest level possible after minimizing version
    QrEccLow, // ~7% erroneous
    QrEccMedium, // ~15% erroneous
    QrEccQuartile, // ~25% erroneous
    QrEccHigh, // ~30% erroneous
} QrEcc;

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

typedef enum {
    QrParamCounter,
    QrParamEcc,
    QrParamMask,
} QrParam;

typedef enum {
    QrEventTriggerTimer,
    QrEventTriggerInput,
} QrEventTrigger;

typedef struct {
    QrEventTrigger trigger;
    InputType type;
    InputKey key;
} QrEvent;

#define QR_MAX_DATA_LEN 468 // Version 11, level L, AlphaNum mode

typedef struct {
    char prefix[4]; // prefix of encoded value (TODO: currently a constant)
    
    QrParam selected; // What parameter the arrow keys modify
    // Parameters
    uint32_t counter; // integer part of encoded value
    QrEcc ecc; // error correction level
    QrMask mask;

    uint32_t delay; // timestamp of how long since last input

    // Saved drawing variables
    bool dirty;
    bool grid[61][61]; // array of grid
    uint8_t size; // length of one side of the grid
    uint8_t resolution; // how many pixels each box is 
    uint8_t offset_x;
    uint8_t offset_y;
} QrState;


static void qr_select_previous_parameter(QrState* qr_state) {
    switch (qr_state->selected)
    {
    case QrParamCounter:
        qr_state->selected = QrParamMask;
        break;
    case QrParamEcc:
        qr_state->selected = QrParamCounter;
        break;
    case QrParamMask:
        qr_state->selected = QrParamEcc;
        break;
    }
}

static void qr_select_next_parameter(QrState* qr_state) {
    switch (qr_state->selected)
    {
    case QrParamCounter:
        qr_state->selected = QrParamEcc;
        break;
    case QrParamEcc:
        qr_state->selected = QrParamMask;
        break;
    case QrParamMask:
        qr_state->selected = QrParamCounter;
        break;
    }
}

static void qr_decrease_selected_parameter(QrState* qr_state) {
    switch (qr_state->selected)
    {
    case QrParamCounter:
        qr_state->counter--;
        break;
    case QrParamEcc:
        qr_state->ecc == QrEccAuto ? (qr_state->ecc = QrEccHigh) : (qr_state->ecc--);
        break;
    case QrParamMask:
        qr_state->mask == QrMaskAuto ? (qr_state->mask = QrMask7) : (qr_state->mask--);
        break;
    }
    qr_state->dirty = true;
}

static void qr_increase_selected_parameter(QrState* qr_state) {
    switch (qr_state->selected)
    {
    case QrParamCounter:
        qr_state->counter++;
        break;
    case QrParamEcc:
        qr_state->ecc == QrEccHigh ? (qr_state->ecc = QrEccAuto) : (qr_state->ecc++);
        break;
    case QrParamMask:
        qr_state->mask == QrMask7 ? (qr_state->mask = QrMaskAuto) : (qr_state->mask++);
        break;
    }
    qr_state->dirty = true;
}

static void qr_calculate_grid(QrState* qr_state) {
    // this should already be handled however it is important
    if(!qr_state->dirty) 
        return;

    char encoded_value[255];
    sprintf(encoded_value, "%.3s%06lu", qr_state->prefix, qr_state->counter);

    // QrEcc specified_error_level = qr_state->ecc;
    // bool auto_increase_error_level = false;
    // if(specified_error_level == QrEccAuto) {
    //     specified_error_level = QrEccLow;
    //     auto_increase_error_level = true;
    // }
    
    uint8_t qr_buffer[qrcodegen_BUFFER_LEN_MAX];
    uint8_t temp_buffer[qrcodegen_BUFFER_LEN_MAX];
    bool ok = qrcodegen_encodeText(encoded_value,
        temp_buffer, qr_buffer, 
        (enum qrcodegen_Ecc)qr_state->ecc,
        qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
        (enum qrcodegen_Mask)qr_state->mask,
        true);

    if(ok) {
        uint8_t size = qrcodegen_getSize(qr_buffer);
        qr_state->size = size;

        for(uint8_t y = 0; y < size; y++) {
            for(uint8_t x = 0; x < size; x++) {
                qr_state->grid[x][y] = qrcodegen_getModule(qr_buffer, x, y);
            }
        }
    } else {
        qr_state->size = 0;
    }
    qr_state->dirty = false;
}

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

    char encoded_value[10];
    snprintf(encoded_value, 10, "%.3s%06lu", qr_state->prefix, qr_state->counter);

    char buffer[9];
    // UI Design
    if(qr_state->selected == QrParamCounter) canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 128, 0, AlignRight, AlignTop, "Value");
    if(qr_state->selected == QrParamCounter) canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 128, 10, AlignRight, AlignTop, encoded_value);
    
    if(qr_state->selected == QrParamEcc) canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 128, 22, AlignRight, AlignTop, "Level");
    if(qr_state->selected == QrParamEcc) canvas_set_font(canvas, FontSecondary);   
    snprintf(buffer, 9, "%8d", (int)qr_state->ecc);
    canvas_draw_str_aligned(canvas, 128, 32, AlignRight, AlignTop, buffer);
    
    if(qr_state->selected == QrParamMask) canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 128, 44, AlignRight, AlignTop, "Mask");
    if(qr_state->selected == QrParamMask) canvas_set_font(canvas, FontSecondary);
    snprintf(buffer, 9, "%8d", (int)qr_state->mask);
    canvas_draw_str_aligned(canvas, 128, 54, AlignRight, AlignTop, buffer);

    // Draw QR Code
    if(qr_state->dirty) {
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_box(canvas, 0, 0, 64, 64);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_rframe(canvas, 0, 0, 64, 64, 4);
        canvas_draw_rframe(canvas, 4, 4, 56, 56, 8);
        canvas_draw_str_aligned(canvas, 32, 24, AlignCenter, AlignCenter, "Processing...");
        snprintf(buffer, 9, "%8lu", qr_state->delay);
        canvas_draw_str_aligned(canvas, 32, 36, AlignCenter, AlignCenter, buffer);
    } else if(qr_state->size > 0) {
        uint8_t size = qr_state->size;
        uint8_t resolution = qr_state->resolution;
        uint8_t offset_x = qr_state->offset_x;
        uint8_t offset_y = qr_state->offset_y;
        for(uint8_t y = 0; y < size; y++) {
            for(uint8_t x = 0; x < size; x++) {
                if(qr_state->grid[x][y]) {
                    canvas_draw_box(canvas, x*resolution + offset_x, y*resolution + offset_y, resolution, resolution);
                }
            }
        }
    } else {
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_box(canvas, 0, 0, 64, 64);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_frame(canvas, 0, 0, 64, 64);
        canvas_draw_frame(canvas, 2, 2, 60, 60);
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

    QrEvent event = {.trigger = QrEventTriggerInput, .type = input_event->type, .key = input_event->key};
    // TODO: what happens when queue is full (currently fits 8 events)
    osMessageQueuePut(event_queue, &event, 0, osWaitForever);
}

static void qr_timer(osMessageQueueId_t event_queue) {
    furi_assert(event_queue);

    QrEvent event = {.trigger = QrEventTriggerTimer};
    osMessageQueuePut(event_queue, &event, 0, 0);
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
    qr_state->selected = QrParamEcc;
    strlcpy(qr_state->prefix, "MIP", 4);
    qr_state->counter = 100609;
    qr_state->ecc = QrEccMedium;
    qr_state->mask = QrMaskAuto;
    qr_state->size = 0;
    qr_state->resolution = 3;
    qr_state->offset_x = 0;
    qr_state->offset_y = 0;
    qr_state->dirty = true;
    qr_state->delay = 0;
    // qr_calculate_grid(qr_state);
    
    // setup mutex for locking state in callbacks
    ValueMutex state_mutex;
    if(!init_mutex(&state_mutex, qr_state, sizeof(QrState))) {
        FURI_LOG_E("QrCodeDisplayer", "cannot create mutex\r\n");
        free(qr_state);
        return 255; // osError
    }

    // setup view port with functions called on draw and hardware inputs
    ViewPort* view_port = view_port_alloc();
    // view_port_set_orientation(view_port, ViewPortOrientationVertical);
    view_port_draw_callback_set(view_port, qr_draw, &state_mutex);
    view_port_input_callback_set(view_port, qr_input, event_queue);

    osTimerId_t timer =
        osTimerNew(qr_timer, osTimerPeriodic, event_queue, NULL);
    osTimerStart(timer, osKernelGetTickFreq() / 4);

    // open GUI and register view_port
    Gui* gui = furi_record_open("gui");
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    QrEvent event;
    for(bool processing = true; processing;) {
        // Can't use osWaitForever here or it can freeze the entire device
        osStatus_t event_status = osMessageQueueGet(event_queue, &event, NULL, 100);

        // get state mutex here
        QrState* qr_state = (QrState*)acquire_mutex_block(&state_mutex);

        if(event_status == osOK) {
            if (event.trigger == QrEventTriggerTimer) {
                if (qr_state->dirty) {
                    qr_state->delay++;
                    if(qr_state->delay >= 2) {
                        qr_calculate_grid(qr_state);
                    }
                }
            } else if (event.trigger == QrEventTriggerInput && event.type == InputTypePress) {
                qr_state->delay = 0;
                switch(event.key)
                {
                case InputKeyLeft:
                    qr_decrease_selected_parameter(qr_state);
                    break;
                case InputKeyRight:
                    qr_increase_selected_parameter(qr_state);
                    break;
                case InputKeyUp:
                    qr_select_previous_parameter(qr_state);
                    break;
                case InputKeyDown:
                    qr_select_next_parameter(qr_state);
                    break;
                case InputKeyOk:
                    // TODO: allow saving the encoded value (this would open a menu)
                    break;
                case InputKeyBack:
                    processing = false;
                    break;
                }
            }
        } else {
            // event timeout
        }

        view_port_update(view_port);
        release_mutex(&state_mutex, qr_state);
    }

    osTimerDelete(timer);
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
