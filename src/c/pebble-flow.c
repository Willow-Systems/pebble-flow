#include "./pebble-flow.h"

static Window *s_window;
static DictationSession *s_dictation_session;
//holder char for dictation session initialization
static char s_sent_message[512];
static int s_num_messages;
//array of message bubbles
static MessageBubble s_message_bubbles[MAX_MESSAGES];
//array of text layers that hold the bounds of each message bubble
static TextLayer *s_text_layers[MAX_MESSAGES];
//all text layers are added as children to the scroll layer
static ScrollLayer *s_scroll_layer;
//create a layer to draw the assistant animation
static Layer *s_canvas_layer;
static AppTimer *s_timer;
//create a boolean that determines whether or not the animation should be animated
static bool animated = false;

int get_num_messages() {
  return s_num_messages;
}

MessageBubble *get_message_by_id(int id) {
  return &s_message_bubbles[id];
}

//creates a text layer that is appropriately sized AND placed for the message.
TextLayer *render_new_bubble(int index, GRect bounds) {
  //get text from message array
  char *text = s_message_bubbles[index].text;
  //set font
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_18);
  //set bold if message is from user
  if (s_message_bubbles[index].is_user){
    font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  } 
  //create a holder GRect to use to measure the size of the final textlayer for the message all integers like '5' and '10' are for padding
  GRect shrinking_rect = GRect(5, 0, bounds.size.w - 10, 2000);
  GSize text_size = graphics_text_layout_get_content_size(text, font, shrinking_rect, GTextOverflowModeWordWrap, GTextAlignmentLeft);
  GRect text_bounds = bounds;

  //set the starting y bounds to the height of the total bounds which was passed into this function by the other functions
  text_bounds.origin.y = bounds.size.h;
  text_bounds.size.h = text_size.h + 5;

  //creates the textlayer with the bounds caluclated above. Size AND position are set this way. This is how we get all the messages stacked on top of each other like a chat
  TextLayer *text_layer = text_layer_create(text_bounds);
  text_layer_set_overflow_mode(text_layer, GTextOverflowModeWordWrap);
  text_layer_set_background_color(text_layer, GColorClear);

  //set alignment depending on whether the message was made by the user or the assistant
  if (s_message_bubbles[index].is_user){
    text_layer_set_text_alignment(text_layer, GTextAlignmentRight);
  } else {
    text_layer_set_text_alignment(text_layer, GTextAlignmentLeft);
  }

  //do not get creative here
  text_layer_set_font(text_layer, font);
  text_layer_set_text(text_layer, text);

  return text_layer;
}

//this function loops through all messages and passes their data to the render_new_bubble function which actually draws each textlayer
static void draw_message_bubbles(){
  //get bounds of the pebble
  Layer *window_layer = window_get_root_layer(s_window);

  //this bounds GRect is very important. It gets modified on each pass through the loop. As new bubbles are created, the bounds are adjusted. The bounds supply information both for the actual size of the scroll layer but also for positioning of each textlayer
  //again, adjusted for padding with 5 and -10
  GRect bounds = GRect(5, 0, (layer_get_bounds(window_layer).size.w - 10), 5);

    //for each message, render a new bubble (stored in the text layers array), adjust the important bounds object, and update the scroll layer's size
    for(int index = 0; index < s_num_messages; index++) {
      //render the bubble for the message
      s_text_layers[index] = render_new_bubble(index, bounds);
      //adjust bounds based on the height of the bubble rendered
      bounds.size.h = bounds.size.h + text_layer_get_content_size(s_text_layers[index]).h + 10;
      //set scroll layer content size so everything is shown and is scrollable
      scroll_layer_set_content_size(s_scroll_layer, bounds.size);
      //add the newly rendered bubble to the scroll layer. Again, its position was set by passing the bounds to the render_new_bubble function
      scroll_layer_add_child(s_scroll_layer, text_layer_get_layer(s_text_layers[index]));
    }
  //after the bubbles have been drawn and added to the scroll layer, this function scrolls the scroll layer to the given offset. In this case: the bottom of the screen (to the y coordinate of the bounds object)
  scroll_layer_set_content_offset(s_scroll_layer, GPoint(0,-bounds.size.h), true);
}

//adds a new message to the messages array but does not render anything
static void add_new_message(char *text, bool is_user){
  //safeguard to prevent too many bubbles being added
  //I have no experience managing memory so review here would be great. let's get as many messages as possible!
  if(s_num_messages < MAX_MESSAGES && strlen(text) > 0) {
    //set the message text and the is_user bool
    strncpy(s_message_bubbles[s_num_messages].text, text, MAX_MESSAGE_LENGTH - 1);
    s_message_bubbles[s_num_messages].is_user = is_user;
    //increment the number of messages int. This int is used throughout the other functions
    s_num_messages++;
  } else {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Failed to add message to bubble list; exceeded maximum number of bubbles");
  }
}

static void animate(){
  //duration for all dots
  uint16_t duration = 250;
}

//callback for the timer to check to see whether or not we should play the animation.
static void check_animate(){
  if(animated){
    animate();
    app_timer_register(1250, check_animate, NULL);
  }
}

//standard dictation callback
static void handle_transcription(char *transcription_text) {
  //makes a dictionary which is used to send the phone app the results of the transcription
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);

  //writes the transcription and the HYPER mode state to the dictionary
  dict_write_cstring(iter, 0, transcription_text);

  //sends the dictionary through an appmessage to the pebble phone app
  app_message_outbox_send();

  //adds the transcription to the messages array so it is displayed as a bubble. true indicates that this message is from a user
  add_new_message(transcription_text, true);

  //update the view so new message is drawn
  draw_message_bubbles();

  //start the animation
  animated = true;
  app_timer_register(250, check_animate, NULL);

}

static void dictation_session_callback(DictationSession *session, DictationSessionStatus status, char *transcription, void *context) {
  if(status == DictationSessionStatusSuccess) {
    handle_transcription(transcription);
  } else {
    //handle error
  }
}

//starts dictation if the user short presses select.
static void select_callback(ClickRecognizerRef recognizer, void *context) {
  dictation_session_start(s_dictation_session);
}

//open pre-filled action menu
static void long_select_callback(ClickRecognizerRef recognizer, void *context) {
  //future feature
}

//this click config provider is not really used
static void prv_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_callback);
}

//click config provider for the scroll window, setting both short and long select pushes.
static void prv_scroll_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_callback);
  window_long_click_subscribe(BUTTON_ID_SELECT, 500, long_select_callback, NULL);
}

//when the phone app sends the pebble app an appmessage, this function is called.
static void in_received_handler(DictionaryIterator *iter, void *context) {
  Tuple *response_tuple = dict_find( iter, ActionResponse );
  if (response_tuple) {
    //if an appmessage has a ActionResponse key, read the value and add a new message. Since this is the assistant, the is_user flag is set to false
    add_new_message(response_tuple->value->cstring, false);
    //stop animation since the assistant responded
    animated = false;
    //gotta update the view
    draw_message_bubbles();
  } else {
    //basically this should never happen
    add_new_message("tuple error", false);
    animated = false;
    draw_message_bubbles();
  }
}

static GPath *s_water_path = NULL;

//operate on the first 9, leave stationary the last 2

static const GPathInfo WATER_PATH_INFO = {
  .num_points = 11,
  .points = (GPoint []) {{-5, 26}, {10, 32}, {30, 25}, {50, 32}, {70, 25}, {80, 32}, {120, 23}, {135, 30}, {150, 26}, {150, 45}, {-5, 45}}
};

static void s_canvas_layer_update_proc(Layer *layer, GContext *ctx) {
  // Fill the path:
  graphics_context_set_fill_color(ctx, GColorPictonBlue);
  gpath_draw_filled(ctx, s_water_path);
  // Stroke the path:
  graphics_context_set_stroke_width(ctx, 2);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  gpath_draw_outline(ctx, s_water_path);
}


static void in_dropped_handler(AppMessageResult reason, void *context){
  //handle failed message
  add_new_message("Message was dropped between the phone and the Pebble", false);
  draw_message_bubbles();
  animated = false;
}

static void prv_window_load(Window *window) {
  //root layer
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  //create dictation and set confirmation mode to false
  s_dictation_session = dictation_session_create(sizeof(s_sent_message), dictation_session_callback, NULL);
  dictation_session_enable_confirmation(s_dictation_session, false);

  //create scroll layer with shortened root layer bounds ( to accommodate bouncing dots )
  GRect scroll_bounds = GRect(0, 0, bounds.size.w, bounds.size.h - 40);
  s_scroll_layer = scroll_layer_create(scroll_bounds);
  // Set the scrolling content size
  scroll_layer_set_content_size(s_scroll_layer, GSize(scroll_bounds.size.w, scroll_bounds.size.h-2));
  //we could make the scroll layer page instead of single click scroll but sometimes that leads to UI problems
  scroll_layer_set_paging(s_scroll_layer, false);
  //hide content indicator shadow
  scroll_layer_set_shadow_hidden(s_scroll_layer, true);
  // set the click config provider of the scroll layer. This adds the short and long select press callbacks. it silently preserves scrolling functionality
  scroll_layer_set_callbacks(s_scroll_layer, (ScrollLayerCallbacks){
    .click_config_provider = prv_scroll_click_config_provider
  });
  //take the scroll layer's click config and place it onto the main window
  scroll_layer_set_click_config_onto_window(s_scroll_layer, window);

  //add scroll layer to the root window
  layer_add_child(window_layer, scroll_layer_get_layer(s_scroll_layer));

  //make waves dog; hang 10 you know?
  s_water_path = gpath_create(&WATER_PATH_INFO);

  // Create the holding layer for the assistant animation layer
  GRect anim_bounds = GRect(0, bounds.size.h - 40, bounds.size.w, 40);
  s_canvas_layer = layer_create(anim_bounds);
  layer_set_update_proc(s_canvas_layer, s_canvas_layer_update_proc);
  //add canvas to window
  layer_add_child(window_layer, s_canvas_layer);

  //start dictation on first launch
  // dictation_session_start(s_dictation_session);

  //testing
  add_new_message("test test test test test test test test test test test test test test test test test test ", true);
  add_new_message("test test test test test test test test test test test test test test test test test test test test test test test test test test test test test test ", false);
  draw_message_bubbles();

}

static void prv_window_unload(Window *window) {
  dictation_session_destroy(s_dictation_session);
  layer_destroy(s_canvas_layer);
  scroll_layer_destroy(s_scroll_layer);
  //probably need to destroy other stuff here like the s_message_bubbles and the s_text_layers arrays
}

static void prv_init(void) {
  s_window = window_create();
  window_set_click_config_provider(s_window, prv_click_config_provider);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload,
  });

  //instantiate appmessages
  app_message_register_inbox_received(in_received_handler);
  app_message_register_inbox_dropped(in_dropped_handler);
  app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());

  const bool animated = true;
  window_stack_push(s_window, animated);
}

static void prv_deinit(void) {
  window_destroy(s_window);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}

