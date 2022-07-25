#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdlib.h>

typedef enum {
    QrVersionUnspecified,
    QrVersion1, // 21x21
    QrVersion2, // unsupported
    QrVersion3, // unsupported
    QrVersion4, // unsupported
    QrVersion5, // unsupported
    QrVersion6, // unsupported
    QrVersion7, // unsupported
    QrVersion8, // unsupported
    QrVersion9, // unsupported
    QrVersion10, // unsupported
    QrVersion11, // unsupported
} QrVersion;

// Error correction level
typedef enum {
    QrCorrectionLevelUnspecified,
    QrCorrectionLevelL, // unsupported
    QrCorrectionLevelM, // unsupported
    QrCorrectionLevelQ, // unsupported
    QrCorrectionLevelH, // Highest level of error correction
} QrCorrectionLevel;

typedef enum {
    QrModeUnspecified,
    QrModeNumeric, // unsupported
    QrModeAlphaNum, // 0–9, A–Z (upper-case only), space, $, %, *, +, -, ., /, :
    QrModeBinary, // unsupported
    QrModeKanji, // unsupported
} QrMode;

#define MAX_ALPHANUM_LEN 468
typedef struct {
    uint32_t counter; // testing value
    QrVersion version;
    QrCorrectionLevel level;
    QrMode mode;
    char data[MAX_ALPHANUM_LEN + 1];
    uint16_t len;
} QrState;

typedef struct {
    InputKey key;
    InputType type;
} QrEvent;

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

    // Printing a constant string value
    canvas_draw_str_aligned(canvas, 128, 0, AlignRight, AlignTop, "Testing...");
    // Print an integer as a string
    char count_buffer[7 + 2 + 1]; // "Count: " + 2 digits + \0
    snprintf(count_buffer, sizeof(count_buffer), "Count: %02lu", qr_state->counter % 100);
    canvas_draw_str_aligned(canvas, 128, 10, AlignRight, AlignTop, count_buffer);

    if(qr_state->version == QrVersion1) {
        // QR Code
        for(uint16_t i = 0; i < 21; i++) {
            for(uint16_t j = 0; j < 21; j++) {
                if((j + i) % 2 == 0) {
                    canvas_draw_box(canvas, i*3, j*3, 3, 3);
                }
            }
        }
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
    qr_state->counter = 0;
    qr_state->version = QrVersion1;
    qr_state->level = QrCorrectionLevelH;
    qr_state->mode = QrModeAlphaNum;
    strncpy(qr_state->data, "MIP100609", 10);
    qr_state->len = 9;
    
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

                // update state
                switch (event.type)
                {
                case InputTypePress:
                    qr_state->counter = 1;
                    break;
                case InputTypeRepeat:
                    qr_state->counter++;
                    break;
                case InputTypeRelease:
                    qr_state->counter = 0;
                    break;
                case InputTypeShort:
                    break;
                case InputTypeLong:
                    break;
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
