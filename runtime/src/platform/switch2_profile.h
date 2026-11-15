#pragma once
namespace switch2::profile {
void register_menu();  // before overlay extension registry is sealed
void poll(bool menu_open);  // render thread, once per overlay frame, including closed menu
bool active();
// Session-only A/B options. Menu and draw preparation run on the render thread.
bool reuse_vertices();
bool specialize_indices();
bool uniform_copy_plan();
bool verify_uniform_copy_plan();
}
