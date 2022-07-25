#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdlib.h>

typedef struct {
    uint32_t counter;
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
    canvas_draw_str(canvas, 37, 31, "Testing...");
    // Print an integer as a string
    char count_buffer[7 + 2 + 1]; // "Count: " + 2 digits + \0
    snprintf(count_buffer, sizeof(count_buffer), "Count: %lu", qr_state->counter);
    canvas_draw_str(canvas, 37, 41, count_buffer);

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
    osMessageQueuePut(event_queue, (QrEvent*)&qr_event, 0, osWaitForever);
}

int32_t qr_code_displayer(void* p) {
    // unknown
    UNUSED(p);

    // setup event queue
    osMessageQueueId_t event_queue = osMessageQueueNew(8, sizeof(InputEvent), NULL);

    // variable to track plugin state
    QrState* qr_state = (QrState*)malloc(sizeof(QrState));
    if(qr_state == NULL) {
        // Memory allocation failed!
        return 255; // should be osMemoryError
    }
    qr_state->counter = 0;
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

    QrEvent qr_event;
    for(bool processing = true; processing;) {
        // We can wait forever since our app only changes on input
        osStatus_t event_status = osMessageQueueGet(event_queue, &qr_event, NULL, osWaitForever);

        if(event_status == osOK) {
            if(qr_event.key == InputKeyBack) {
                // Quit if back is pressed
                processing = false;
            } else {
                // get state mutex here
                QrState* qr_state = (QrState*)acquire_mutex_block(&state_mutex);

                // update state
                switch (qr_event.type)
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
