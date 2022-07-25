#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdlib.h>

typedef struct {
    uint32_t counter;
} QrState;

/**
 * @brief Updates whay is drawn to display when view_port_update is called
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
    char count_buffer[7 + 2]; // len("Count: ") == 7 so max counter value displayed will be 99
    snprintf(count_buffer, sizeof(count_buffer), "Count: %u", qr_state->counter);
    canvas_draw_str(canvas, 37, 41, count_buffer);

    // release state mutex
    release_mutex((ValueMutex*)ctx, qr_state);
}

static void qr_input(InputEvent* input_event, osMessageQueueId_t event_queue) {
    furi_assert(event_queue);

    osMessageQueuePut(event_queue, &input_event, 0, osKernelSysTickMicroSec(500));
}

osStatus_t qr_code_displayer(void* p) {
    // unknown
    UNUSED(p);
    // seed for random numbers
    srand(DWT->CYCCNT);

    // setup event queue
    osMessageQueueId_t event_queue = osMessageQueueNew(8, sizeof(InputEvent), NULL);

    // variable to track plugin state
    QrState* qr_state = (QrState*)malloc(sizeof(QrState));
    if(qr_state == NULL) {
        // Memory allocation failed!
        return osErrorNoMemory;
    }
    qr_state->counter = 0;
    // setup mutex for locking state in callbacks
    ValueMutex state_mutex;
    if(!init_mutex(&state_mutex, qr_state, sizeof(QrState))) {
        FURI_LOG_E("QrCodeDisplayer", "cannot create mutex\r\n");
        free(qr_state);
        return osError;
    }

    // setup view port with functions called on draw and hardware inputs
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, qr_draw, &state_mutex);
    view_port_input_callback_set(view_port, qr_input, event_queue);

    // open GUI and register view_port
    Gui* gui = furi_record_open("gui");
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    InputEvent input_event;
    for(bool processing = true; processing;) {
        osStatus_t event_status = osMessageQueueGet(event_queue, &input_event, NULL, 100);

        // get state mutex here
        QrState* qr_state = (QrState*)acquire_mutex_block(&state_mutex);

        if(event_status == osOK) {
            // key press events
            if(input_event.key == InputKeyOk 
                && input_event.type == InputTypeRepeat) 
            {
                qr_state->counter = input_event.sequence;
                view_port_update(view_port);
            }
        } else {
            // event timeout
        }

        // release state mutex
        release_mutex(&state_mutex, qr_state);
    }

    view_port_enabled_set(view_port, false);
    gui_remove_view_port(gui, view_port);
    furi_record_close("gui");
    view_port_free(view_port);
    osMessageQueueDelete(event_queue);
    // delete_mutex and free malloc'd state variable
    delete_mutex(&state_mutex);
    free(qr_state);

    return osOK;
}
