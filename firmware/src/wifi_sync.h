#pragma once
#ifdef BOARD_AMOLED_216_C6

void wifi_sync_init();
bool wifi_sync_configured();
bool wifi_sync_online();
void wifi_sync_configure(const char* json);
void wifi_sync_replace_key(const char* hex);
void wifi_sync_status();
const char* wifi_sync_receive();
void wifi_sync_ack(const char* status = nullptr);

#endif
