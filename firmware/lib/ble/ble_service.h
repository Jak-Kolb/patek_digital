
#ifndef BLE_SERVER_H
#define BLE_SERVER_H

#include <NimBLEDevice.h>
#include <Arduino.h>
#include <cstdint>
#include <ctime>
#include <functional>
#include <string>

namespace consolidate {
struct ConsolidatedRecord;
}

class BLEServerClass {
public:
  void begin();
  void set_erase_callback(const std::function<void()>& cb);
  void set_time_sync_callback(const std::function<void(time_t)>& cb);
  void set_transfer_start_callback(const std::function<void()>& cb);
  void set_transfer_complete_callback(const std::function<void()>& cb);
  bool notify(const uint8_t* data, size_t length);
  bool notify_string(const std::string& payload);

private:
  NimBLEServer* pServer = nullptr;
  NimBLECharacteristic* pNotifyCharacteristic = nullptr;
  NimBLECharacteristic* pControlCharacteristic = nullptr;
  bool deviceConnected = false;

  std::function<void()> onErase_;
  std::function<void(time_t)> onTimeSync_;
  std::function<void()> onTransferStart_;
  std::function<void()> onTransferComplete_;

  class ServerCallbacks : public NimBLEServerCallbacks {
  public:
    ServerCallbacks(BLEServerClass* parent) : pParent(parent) {}

    void onConnect(NimBLEServer* pServer) override {
      pParent->deviceConnected = true;
      Serial.println("BLE device connected.");
    }

    void onDisconnect(NimBLEServer* pServer) override {
      pParent->deviceConnected = false;
      Serial.println("BLE device disconnected.");
    }

  private:
    BLEServerClass* pParent;
  };

  ServerCallbacks serverCallbacks{this};

  class ControlCallbacks : public NimBLECharacteristicCallbacks {
  public:
    ControlCallbacks(BLEServerClass* parent) : pParent(parent) {}
    void onWrite(NimBLECharacteristic* characteristic) override;

  private:
    BLEServerClass* pParent;
  };

  ControlCallbacks controlCallbacks{this};

  void handle_command(const std::string& command);
  void stream_all_records();
  bool send_record_packet(const consolidate::ConsolidatedRecord& record);
  void send_ack(const char* label);
  bool wait_for_notify_complete(uint32_t timeout_ms);
  void handle_notify_status(NimBLECharacteristicCallbacks::Status status, int code);

  class NotifyCallbacks : public NimBLECharacteristicCallbacks {
  public:
    explicit NotifyCallbacks(BLEServerClass* parent) : pParent(parent) {}

    void onStatus(NimBLECharacteristic* /*characteristic*/, Status status, int code) override {
      if (pParent != nullptr) {
        pParent->handle_notify_status(status, code);
      }
    }

  private:
    BLEServerClass* pParent;
  };

  NotifyCallbacks notifyCallbacks{this};
  volatile bool notifyPending = false;
  bool lastNotifyOk = true;
};

extern BLEServerClass bleServer;

#endif


