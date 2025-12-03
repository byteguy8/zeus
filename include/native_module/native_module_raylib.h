#ifndef NATIVE_RAYLIB_H
#define NATIVE_RAYLIB_H
#ifdef RAYLIB

#include "vm/vmu.h"
#include "vm/vm_factory.h"

#include <limits.h>
#include <raylib.h>

static Color color_from_value(Value value, uint8_t param, const char *name, VM *vm){
    RecordObj *color_record_obj = validate_value_record_arg(value, param, name, vm);
    char r = (char)validate_value_int_range(
        vmu_record_get_attr(1, "r", color_record_obj, vm),
        0,
        UCHAR_MAX,
        "Illegal color value",
        vm
    );
    char g = (char)validate_value_int_range(
        vmu_record_get_attr(1, "g", color_record_obj, vm),
        0,
        UCHAR_MAX,
        "Illegal color value",
        vm
    );
    char b = (char)validate_value_int_range(
        vmu_record_get_attr(1, "b", color_record_obj, vm),
        0,
        UCHAR_MAX,
        "Illegal color value",
        vm
    );
    char a = (char)validate_value_int_range(
        vmu_record_get_attr(1, "a", color_record_obj, vm),
        0,
        UCHAR_MAX,
        "Illegal color value",
        vm
    );

    return (Color){
        .r = r,
        .g = g,
        .b = b,
        .a = a,
    };
}

NativeModule *raylib_native_module = NULL;

Value native_fn_raylib_init_window(uint8_t argsc, Value *values, Value target, void *context){
    int64_t width = validate_value_int_range_arg(values[0], 1, "width", 1, INT_MAX, VMU_VM);
    int64_t height = validate_value_int_range_arg(values[1], 2, "height", 1, INT_MAX, VMU_VM);
    StrObj *title_str_obj = validate_value_str_arg(values[2], 3, "title", VMU_VM);

    InitWindow(width, height, title_str_obj->buff);

    return EMPTY_VALUE;
}

Value native_fn_raylib_set_target_fps(uint8_t argsc, Value *values, Value target, void *context){
    int fps = (int)validate_value_int_range_arg(values[0], 1, "fps", 1, INT_MAX, VMU_VM);
    SetTargetFPS(fps);
    return EMPTY_VALUE;
}

Value native_fn_raylib_close_window(uint8_t argsc, Value *values, Value target, void *context){
    CloseWindow();
    return EMPTY_VALUE;
}

Value native_fn_raylib_window_should_close(uint8_t argsc, Value *values, Value target, void *context){
    return BOOL_VALUE(WindowShouldClose());
}

Value native_fn_raylib_begin_drawing(uint8_t argsc, Value *values, Value target, void *context){
    BeginDrawing();
    return EMPTY_VALUE;
}

Value native_fn_raylib_end_drawing(uint8_t argsc, Value *values, Value target, void *context){
    EndDrawing();
    return EMPTY_VALUE;
}

Value native_fn_raylib_get_mouse_x(uint8_t argsc, Value *values, Value target, void *context){
    Vector2 mouse_position = GetMousePosition();
    return FLOAT_VALUE((double)mouse_position.x);
}

Value native_fn_raylib_get_mouse_y(uint8_t argsc, Value *values, Value target, void *context){
    Vector2 mouse_position = GetMousePosition();
    return FLOAT_VALUE((double)mouse_position.y);
}

Value native_fn_raylib_is_mouse_button_down(uint8_t argsc, Value *values, Value target, void *context){
    int button = (int)validate_value_int_range_arg(values[0], 1, "button", INT_MIN, INT_MAX, VMU_VM);
    return BOOL_VALUE(IsMouseButtonDown(button));
}

Value native_fn_raylib_is_mouse_button_released(uint8_t argsc, Value *values, Value target, void *context){
    int button = (int)validate_value_int_range_arg(values[0], 1, "button", INT_MIN, INT_MAX, VMU_VM);
    return BOOL_VALUE(IsMouseButtonReleased(button));
}

Value native_fn_raylib_is_mouse_button_pressed(uint8_t argsc, Value *values, Value target, void *context){
    int button = (int)validate_value_int_range_arg(values[0], 1, "button", INT_MIN, INT_MAX, VMU_VM);
    return BOOL_VALUE(IsMouseButtonPressed(button));
}

Value native_fn_raylib_begin_scissor_mode(uint8_t argsc, Value *values, Value target, void *context){
    int x = (int)validate_value_int_range_arg(values[0], 1, "x", INT_MIN, INT_MAX, VMU_VM);
    int y = (int)validate_value_int_range_arg(values[1], 2, "y", INT_MIN, INT_MAX, VMU_VM);
    int w = (int)validate_value_int_range_arg(values[2], 3, "w", INT_MIN, INT_MAX, VMU_VM);
    int h = (int)validate_value_int_range_arg(values[3], 4, "h", INT_MIN, INT_MAX, VMU_VM);

    BeginScissorMode(x, y, w, h);

    return EMPTY_VALUE;
}

Value native_fn_raylib_end_scissor_mode(uint8_t argsc, Value *values, Value target, void *context){
    EndScissorMode();
    return EMPTY_VALUE;
}

Value native_fn_raylib_clear_background(uint8_t argsc, Value *values, Value target, void *context){
    Color color = color_from_value(values[0], 1, "background color", VMU_VM);
    ClearBackground(color);
    return EMPTY_VALUE;
}

Value native_fn_raylib_draw_rectangle(uint8_t argsc, Value *values, Value target, void *context){
    int x = (int)validate_value_int_range_arg(values[0], 1, "x", INT_MIN, INT_MAX, VMU_VM);
    int y = (int)validate_value_int_range_arg(values[1], 2, "y", INT_MIN, INT_MAX, VMU_VM);
    int w = (int)validate_value_int_range_arg(values[2], 3, "w", INT_MIN, INT_MAX, VMU_VM);
    int h = (int)validate_value_int_range_arg(values[3], 4, "h", INT_MIN, INT_MAX, VMU_VM);
    Color color = color_from_value(values[4], 5, "fill color", VMU_VM);

    DrawRectangle(x, y, w, h, color);

    return EMPTY_VALUE;
}

Value native_fn_raylib_draw_circle(uint8_t argsc, Value *values, Value target, void *context){
    int x = (int)validate_value_int_range_arg(values[0], 1, "x", INT_MIN, INT_MAX, VMU_VM);
    int y = (int)validate_value_int_range_arg(values[1], 2, "y", INT_MIN, INT_MAX, VMU_VM);
    float r = (float)validate_value_float_arg(values[2], 3, "radius", VMU_VM);
    Color color = color_from_value(values[3], 4, "color", VMU_VM);

    DrawCircle(x, y, r, color);

    return EMPTY_VALUE;
}

Value native_fn_raylib_draw_rectangle_lines(uint8_t argsc, Value *values, Value target, void *context){
    int x = (int)validate_value_int_range_arg(values[0], 1, "x", INT_MIN, INT_MAX, VMU_VM);
    int y = (int)validate_value_int_range_arg(values[1], 2, "y", INT_MIN, INT_MAX, VMU_VM);
    int w = (int)validate_value_int_range_arg(values[2], 3, "w", INT_MIN, INT_MAX, VMU_VM);
    int h = (int)validate_value_int_range_arg(values[3], 4, "h", INT_MIN, INT_MAX, VMU_VM);
    Color color = color_from_value(values[4], 5, "lines color", VMU_VM);

    DrawRectangleLines(x, y, w, h, color);

    return EMPTY_VALUE;
}

Value native_fn_raylib_get_frame_time(uint8_t argsc, Value *values, Value target, void *context){
    return FLOAT_VALUE((double)GetFrameTime());
}

Value native_fn_raylib_is_key_down(uint8_t argsc, Value *values, Value target, void *context){
    int key = (int)validate_value_int_range_arg(values[0], 1, "x", INT_MIN, INT_MAX, VMU_VM);
    return BOOL_VALUE(IsKeyDown(key));
}

Value native_fn_raylib_is_key_pressed(uint8_t argsc, Value *values, Value target, void *context){
    int key = (int)validate_value_int_range_arg(values[0], 1, "x", INT_MIN, INT_MAX, VMU_VM);
    return BOOL_VALUE(IsKeyPressed(key));
}

Value native_fn_raylib_set_random_seed(uint8_t argsc, Value *values, Value target, void *context){
    unsigned int seed = (unsigned int)validate_value_int_range_arg(values[0], 1, "seed", 0, UINT_MAX, VMU_VM);
    SetRandomSeed(seed);
    return EMPTY_VALUE;
}

Value native_fn_raylib_get_random_value(uint8_t argsc, Value *values, Value target, void *context){
    int min = (int)validate_value_int_range_arg(values[0], 1, "min", INT_MIN, INT_MAX, VMU_VM);
    int max = (int)validate_value_int_range_arg(values[1], 2, "max", INT_MIN, INT_MAX, VMU_VM);
    return INT_VALUE((int64_t)GetRandomValue(min, max));
}

Value native_fn_raylib_draw_text(uint8_t argsc, Value *values, Value target, void *context){
    StrObj *text_str_obj = validate_value_str_arg(values[0], 1, "text", VMU_VM);
    int x = (int)validate_value_int_range_arg(values[1], 2, "x", INT_MIN, INT_MAX, VMU_VM);
    int y = (int)validate_value_int_range_arg(values[2], 3, "y", INT_MIN, INT_MAX, VMU_VM);
    int font_size = (int)validate_value_int_range_arg(values[3], 4, "font size", INT_MIN, INT_MAX, VMU_VM);
    Color color = color_from_value(values[4], 5, "color", VMU_VM);

    DrawText(text_str_obj->buff, x, y, font_size, color);

    return INT_VALUE((int64_t)GetRandomValue(x, y));
}

Value native_fn_raylib_measure_text(uint8_t argsc, Value *values, Value target, void *context){
    StrObj *text_str_obj = validate_value_str_arg(values[0], 1, "text", VMU_VM);
    int font_size = (int)validate_value_int_range_arg(values[1], 2, "font size", INT_MIN, INT_MAX, VMU_VM);

    return INT_VALUE((int64_t)MeasureText(text_str_obj->buff, font_size));
}

void raylib_module_init(const Allocator *allocator){
    raylib_native_module = vm_factory_native_module_create(allocator, "raylib");

    vm_factory_native_module_add_value(raylib_native_module, "MOUSE_BUTTON_LEFT", INT_VALUE(MOUSE_BUTTON_LEFT));
    vm_factory_native_module_add_value(raylib_native_module, "MOUSE_BUTTON_RIGHT", INT_VALUE(MOUSE_BUTTON_RIGHT));
    vm_factory_native_module_add_value(raylib_native_module, "MOUSE_BUTTON_MIDDLE", INT_VALUE(MOUSE_BUTTON_MIDDLE));
    vm_factory_native_module_add_value(raylib_native_module, "MOUSE_BUTTON_SIDE", INT_VALUE(MOUSE_BUTTON_SIDE));
    vm_factory_native_module_add_value(raylib_native_module, "MOUSE_BUTTON_EXTRA", INT_VALUE(MOUSE_BUTTON_EXTRA));
    vm_factory_native_module_add_value(raylib_native_module, "MOUSE_BUTTON_FORWARD", INT_VALUE(MOUSE_BUTTON_FORWARD));
    vm_factory_native_module_add_value(raylib_native_module, "MOUSE_BUTTON_BACK", INT_VALUE(MOUSE_BUTTON_BACK));

    vm_factory_native_module_add_value(raylib_native_module, "KEY_NULL", INT_VALUE(KEY_NULL));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_APOSTROPHE", INT_VALUE(KEY_APOSTROPHE));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_COMMA", INT_VALUE(KEY_COMMA));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_MINUS", INT_VALUE(KEY_MINUS));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_PERIOD", INT_VALUE(KEY_PERIOD));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_SLASH", INT_VALUE(KEY_SLASH));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_ZERO", INT_VALUE(KEY_ZERO));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_ONE", INT_VALUE(KEY_ONE));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_TWO", INT_VALUE(KEY_TWO));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_THREE", INT_VALUE(KEY_THREE));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_FOUR", INT_VALUE(KEY_FOUR));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_FIVE", INT_VALUE(KEY_FIVE));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_SIX", INT_VALUE(KEY_SIX));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_SEVEN", INT_VALUE(KEY_SEVEN));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_EIGHT", INT_VALUE(KEY_EIGHT));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_NINE", INT_VALUE(KEY_NINE));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_SEMICOLON", INT_VALUE(KEY_SEMICOLON));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_EQUAL", INT_VALUE(KEY_EQUAL));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_A", INT_VALUE(KEY_A));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_B", INT_VALUE(KEY_B));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_C", INT_VALUE(KEY_C));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_D", INT_VALUE(KEY_D));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_E", INT_VALUE(KEY_E));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_F", INT_VALUE(KEY_F));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_G", INT_VALUE(KEY_G));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_H", INT_VALUE(KEY_H));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_I", INT_VALUE(KEY_I));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_J", INT_VALUE(KEY_J));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_K", INT_VALUE(KEY_K));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_L", INT_VALUE(KEY_L));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_M", INT_VALUE(KEY_M));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_N", INT_VALUE(KEY_N));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_O", INT_VALUE(KEY_O));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_P", INT_VALUE(KEY_P));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_Q", INT_VALUE(KEY_Q));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_R", INT_VALUE(KEY_R));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_S", INT_VALUE(KEY_S));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_T", INT_VALUE(KEY_T));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_U", INT_VALUE(KEY_U));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_V", INT_VALUE(KEY_V));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_W", INT_VALUE(KEY_W));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_X", INT_VALUE(KEY_X));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_Y", INT_VALUE(KEY_Y));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_Z", INT_VALUE(KEY_Z));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_LEFT_BRACKET", INT_VALUE(KEY_LEFT_BRACKET));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_BACKSLASH", INT_VALUE(KEY_BACKSLASH));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_RIGHT_BRACKET", INT_VALUE(KEY_RIGHT_BRACKET));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_GRAVE", INT_VALUE(KEY_GRAVE));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_SPACE", INT_VALUE(KEY_SPACE));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_ESCAPE", INT_VALUE(KEY_ESCAPE));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_ENTER", INT_VALUE(KEY_ENTER));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_TAB", INT_VALUE(KEY_TAB));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_BACKSPACE", INT_VALUE(KEY_BACKSPACE));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_INSERT", INT_VALUE(KEY_INSERT));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_DELETE", INT_VALUE(KEY_DELETE));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_RIGHT", INT_VALUE(KEY_RIGHT));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_LEFT", INT_VALUE(KEY_LEFT));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_DOWN", INT_VALUE(KEY_DOWN));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_UP", INT_VALUE(KEY_UP));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_PAGE_UP", INT_VALUE(KEY_PAGE_UP));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_PAGE_DOWN", INT_VALUE(KEY_PAGE_DOWN));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_HOME", INT_VALUE(KEY_HOME));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_END", INT_VALUE(KEY_END));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_CAPS_LOCK", INT_VALUE(KEY_CAPS_LOCK));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_SCROLL_LOCK", INT_VALUE(KEY_SCROLL_LOCK));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_NUM_LOCK", INT_VALUE(KEY_NUM_LOCK));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_PRINT_SCREEN", INT_VALUE(KEY_PRINT_SCREEN));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_PAUSE", INT_VALUE(KEY_PAUSE));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_F1", INT_VALUE(KEY_F1));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_F2", INT_VALUE(KEY_F2));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_F3", INT_VALUE(KEY_F3));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_F4", INT_VALUE(KEY_F4));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_F5", INT_VALUE(KEY_F5));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_F6", INT_VALUE(KEY_F6));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_F7", INT_VALUE(KEY_F7));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_F8", INT_VALUE(KEY_F8));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_F9", INT_VALUE(KEY_F9));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_F10", INT_VALUE(KEY_F10));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_F11", INT_VALUE(KEY_F11));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_F12", INT_VALUE(KEY_F12));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_LEFT_SHIFT", INT_VALUE(KEY_LEFT_SHIFT));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_LEFT_CONTROL", INT_VALUE(KEY_LEFT_CONTROL));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_LEFT_ALT", INT_VALUE(KEY_LEFT_ALT));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_LEFT_SUPER", INT_VALUE(KEY_LEFT_SUPER));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_RIGHT_SHIFT", INT_VALUE(KEY_RIGHT_SHIFT));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_RIGHT_CONTROL", INT_VALUE(KEY_RIGHT_CONTROL));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_RIGHT_ALT", INT_VALUE(KEY_RIGHT_ALT));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_RIGHT_SUPER", INT_VALUE(KEY_RIGHT_SUPER));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_KB_MENU", INT_VALUE(KEY_KB_MENU));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_KP_0", INT_VALUE(KEY_KP_0));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_KP_1", INT_VALUE(KEY_KP_1));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_KP_2", INT_VALUE(KEY_KP_2));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_KP_3", INT_VALUE(KEY_KP_3));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_KP_4", INT_VALUE(KEY_KP_4));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_KP_5", INT_VALUE(KEY_KP_5));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_KP_6", INT_VALUE(KEY_KP_6));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_KP_7", INT_VALUE(KEY_KP_7));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_KP_8", INT_VALUE(KEY_KP_8));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_KP_9", INT_VALUE(KEY_KP_9));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_KP_DECIMAL", INT_VALUE(KEY_KP_DECIMAL));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_KP_DIVIDE", INT_VALUE(KEY_KP_DIVIDE));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_KP_MULTIPLY", INT_VALUE(KEY_KP_MULTIPLY));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_KP_SUBTRACT", INT_VALUE(KEY_KP_SUBTRACT));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_KP_ADD", INT_VALUE(KEY_KP_ADD));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_KP_ENTER", INT_VALUE(KEY_KP_ENTER));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_KP_EQUAL", INT_VALUE(KEY_KP_EQUAL));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_BACK", INT_VALUE(KEY_BACK));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_MENU", INT_VALUE(KEY_MENU));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_VOLUME_UP", INT_VALUE(KEY_VOLUME_UP));
    vm_factory_native_module_add_value(raylib_native_module, "KEY_VOLUME_DOWN", INT_VALUE(KEY_VOLUME_DOWN));
//---------------------------------------------------  WINDOW-RELATED FUNCTIONS  ---------------------------------------------------//
    vm_factory_native_module_add_native_fn(raylib_native_module, "init_window", 3, native_fn_raylib_init_window);
    vm_factory_native_module_add_native_fn(raylib_native_module, "close_window", 0, native_fn_raylib_close_window);
    vm_factory_native_module_add_native_fn(raylib_native_module, "window_should_close", 0, native_fn_raylib_window_should_close);
//--------------------------------------------------  DRAWING-RELATED FUNCTIONS  ---------------------------------------------------//
    vm_factory_native_module_add_native_fn(raylib_native_module, "clear_background", 1, native_fn_raylib_clear_background);
    vm_factory_native_module_add_native_fn(raylib_native_module, "begin_drawing", 0, native_fn_raylib_begin_drawing);
    vm_factory_native_module_add_native_fn(raylib_native_module, "end_drawing", 0, native_fn_raylib_end_drawing);
    vm_factory_native_module_add_native_fn(raylib_native_module, "begin_scissor_mode", 4, native_fn_raylib_begin_scissor_mode);
    vm_factory_native_module_add_native_fn(raylib_native_module, "end_scissor_mode", 0, native_fn_raylib_end_scissor_mode);
//---------------------------------------------------  TIMING-RELATED FUNCTIONS  ---------------------------------------------------//
    vm_factory_native_module_add_native_fn(raylib_native_module, "set_target_fps", 1, native_fn_raylib_set_target_fps);
    vm_factory_native_module_add_native_fn(raylib_native_module, "get_frame_time", 0, native_fn_raylib_get_frame_time);
//----------------------------------------------  INPUT-RELATED FUNCTIONS: KEYBOARD  -----------------------------------------------//
    vm_factory_native_module_add_native_fn(raylib_native_module, "is_key_pressed", 1, native_fn_raylib_is_key_pressed);
    vm_factory_native_module_add_native_fn(raylib_native_module, "is_key_down", 1, native_fn_raylib_is_key_down);
//------------------------------------------------  INPUT-RELATED FUNCTIONS:MOUSE  -------------------------------------------------//
    vm_factory_native_module_add_native_fn(raylib_native_module, "is_mouse_button_pressed", 1, native_fn_raylib_is_mouse_button_pressed);
    vm_factory_native_module_add_native_fn(raylib_native_module, "is_mouse_button_down", 1, native_fn_raylib_is_mouse_button_down);
    vm_factory_native_module_add_native_fn(raylib_native_module, "is_mouse_button_released", 1, native_fn_raylib_is_mouse_button_released);
    vm_factory_native_module_add_native_fn(raylib_native_module, "get_mouse_x", 0, native_fn_raylib_get_mouse_x);
    vm_factory_native_module_add_native_fn(raylib_native_module, "get_mouse_y", 0, native_fn_raylib_get_mouse_y);
//------------------------------------------------  BASIC SHAPES DRAWING FUNCTIONS  ------------------------------------------------//
    vm_factory_native_module_add_native_fn(raylib_native_module, "draw_rectangle", 5, native_fn_raylib_draw_rectangle);
    vm_factory_native_module_add_native_fn(raylib_native_module, "draw_rectangle_lines", 5, native_fn_raylib_draw_rectangle_lines);
    vm_factory_native_module_add_native_fn(raylib_native_module, "draw_circle", 4, native_fn_raylib_draw_circle);

    vm_factory_native_module_add_native_fn(raylib_native_module, "set_random_seed", 1, native_fn_raylib_set_random_seed);
    vm_factory_native_module_add_native_fn(raylib_native_module, "get_random_value", 2, native_fn_raylib_get_random_value);
    vm_factory_native_module_add_native_fn(raylib_native_module, "draw_text", 5, native_fn_raylib_draw_text);
    vm_factory_native_module_add_native_fn(raylib_native_module, "measure_text", 2, native_fn_raylib_measure_text);
}

#endif
#endif