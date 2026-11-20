#ifdef BOARD_AMOLED_216_C6
#include "wifi_sync.h"
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>

static constexpr uint16_t port = 43621;
static constexpr size_t nonce_size = 12;
static constexpr size_t tag_size = 16;
static WiFiUDP udp;
static uint8_t key[32];
static uint8_t packet[512 + nonce_size + tag_size];
static char payload[513];
static bool configured;
static bool listening;
static bool ota_started;
static unsigned packets_received;
static unsigned packets_decrypted;
static unsigned replies_sent;

static bool decode_key(const char* hex) {
    if (strlen(hex) != sizeof(key) * 2) return false;
    uint8_t decoded[sizeof(key)];
    for (size_t i = 0; i < sizeof(key); ++i) {
        char byte[] = {hex[2*i], hex[2*i+1], 0};
        if (!isxdigit(static_cast<unsigned char>(byte[0])) ||
            !isxdigit(static_cast<unsigned char>(byte[1]))) return false;
        decoded[i] = strtoul(byte, nullptr, 16);
    }
    memcpy(key, decoded, sizeof(key));
    return true;
}

void wifi_sync_init() {
    Preferences prefs;
    if (!prefs.begin("wifi-sync", true)) return;
    String config = prefs.getString("config");
    prefs.end();
    JsonDocument doc;
    if (deserializeJson(doc, config)) return;
    const char* ssid = doc["ssid"] | "";
    configured = ssid[0] && decode_key(doc["key"] | "");
    if (!configured) return;
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid, doc["password"] | "");
}

bool wifi_sync_configured() { return configured; }
bool wifi_sync_online() { return WiFi.status() == WL_CONNECTED; }

static void ota_tick() {
    if (!ota_started) {
        const uint8_t context[] = "Clawdmeter OTA";
        uint8_t digest[32];
        auto* sha256 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
        if (mbedtls_md_hmac(sha256, key, sizeof(key), context, sizeof(context) - 1, digest)) return;
        char password[65];
        for (size_t i = 0; i < sizeof(digest); ++i) snprintf(password + 2 * i, 3, "%02x", digest[i]);
        ArduinoOTA.setMdnsEnabled(false).setPassword(password);
        ArduinoOTA.onEnd([]() { Serial.println("OTA update verified"); });
        ArduinoOTA.begin();
        ota_started = true;
    }
    ArduinoOTA.handle();
}

void wifi_sync_configure(const char* json) {
    JsonDocument doc;
    Preferences prefs;
    if (deserializeJson(doc, json) || !doc["ssid"].is<const char*>() ||
        !doc["password"].is<const char*>() || strlen(doc["ssid"].as<const char*>()) == 0 ||
        strlen(doc["ssid"].as<const char*>()) > 32 || strlen(doc["password"].as<const char*>()) > 64 ||
        !doc["key"].is<const char*>() || strlen(doc["key"].as<const char*>()) != 64 ||
        !prefs.begin("wifi-sync", false)) {
        Serial.println("WIFI_CONFIG_ERROR");
        return;
    }
    bool saved = prefs.putString("config", json) != 0;
    prefs.end();
    Serial.println(saved ? "WIFI_CONFIG_SAVED" : "WIFI_CONFIG_ERROR");
    Serial.flush();
    if (saved) ESP.restart();
}

void wifi_sync_replace_key(const char* hex) {
    Preferences prefs;
    if (!configured || !prefs.begin("wifi-sync", false)) {
        Serial.println("WIFI_KEY_ERROR");
        return;
    }
    JsonDocument doc;
    const String current = prefs.getString("config");
    uint8_t previous_key[sizeof(key)];
    memcpy(previous_key, key, sizeof(key));
    bool saved = false;
    if (!deserializeJson(doc, current) && decode_key(hex)) {
        doc["key"] = String(hex);
        String updated;
        serializeJson(doc, updated);
        saved = prefs.putString("config", updated) != 0;
    }
    prefs.end();
    if (!saved) memcpy(key, previous_key, sizeof(key));
    if (saved && ota_started) {
        ArduinoOTA.end();
        ota_started = false;
    }
    Serial.println(saved ? "WIFI_KEY_SAVED" : "WIFI_KEY_ERROR");
}

void wifi_sync_status() {
    if (wifi_sync_online()) {
        Serial.printf("WIFI_READY %s %s\n", WiFi.localIP().toString().c_str(),
                      WiFi.subnetMask().toString().c_str());
    } else {
        Serial.println(configured ? "WIFI_CONNECTING" : "WIFI_UNCONFIGURED");
    }
    Serial.printf("WIFI_STATS %d %u %u %u\n", listening, packets_received, packets_decrypted, replies_sent);
}

const char* wifi_sync_receive() {
    if (!wifi_sync_online()) {
        if (ota_started) ArduinoOTA.end();
        ota_started = false;
        if (listening) udp.stop();
        listening = false;
        return nullptr;
    }
    ota_tick();
    if (!listening) {
        static uint32_t last_begin = 0;
        if (millis() - last_begin < 1000) return nullptr;
        last_begin = millis();
        listening = udp.begin(port);
        if (!listening) return nullptr;
        wifi_sync_status();
    }
    int size = udp.parsePacket();
    if (size <= 0) return nullptr;
    ++packets_received;
    const int received = udp.read(packet, sizeof(packet));
    if (size <= int(nonce_size + tag_size) || size > int(sizeof(packet)) || received != size) {
        udp.clear();
        return nullptr;
    }
    const size_t length = size - nonce_size - tag_size;
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    int result = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (!result) {
        result = mbedtls_gcm_auth_decrypt(&gcm, length, packet, nonce_size, nullptr, 0,
                    packet + nonce_size + length, tag_size, packet + nonce_size,
                    reinterpret_cast<uint8_t*>(payload));
    }
    mbedtls_gcm_free(&gcm);
    if (result) return nullptr;
    ++packets_decrypted;
    payload[length] = 0;
    return payload;
}

void wifi_sync_ack(const char* status) {
    uint8_t reply[32];
    uint8_t authenticated[nonce_size + 320];
    const size_t length = status ? strlen(status) : 0;
    if (length > sizeof(authenticated) - nonce_size) return;
    memcpy(authenticated, packet, nonce_size);
    if (length) memcpy(authenticated + nonce_size, status, length);
    const auto* digest = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (mbedtls_md_hmac(digest, key, sizeof(key), authenticated, nonce_size + length, reply)) return;
    if (!udp.beginPacket(udp.remoteIP(), udp.remotePort())) return;
    udp.write(reply, sizeof(reply));
    if (length) udp.write(reinterpret_cast<const uint8_t*>(status), length);
    if (!udp.endPacket()) return;
    ++replies_sent;
    Serial.println("Wi-Fi limits applied");
}
#endif
