#pragma once
namespace rrpc {
// No token, account login, SDK download, or game assets are needed.
bool presence_start(const char* application_id);
void presence_context(int context); // 0 menus, 1 racing, 2 paused
void presence_stop();
const char* presence_status();
}
