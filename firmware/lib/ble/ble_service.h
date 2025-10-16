#ifndef BLE_SERVER_H
#define BLE_SERVER_H

#include <NimBLEDevice.h>
#include <Arduino.h>
#include <functional>
#include <cstdint>

namespace consolidate {
  struct ConsolidatedRecord;
}

class BLEServerClass {
public:
  void begin();
  
  // Callback setters
  void set_erase_callback(const std::function<void()>& cb);
  void set_time_sync_callback(const std::function<void(time_t)>& cb);
  void set_transfer_start_callback(const std::function<void()>& cb);
  void set_transfer_complete_callback(const std::function<void()>& cb);

private:
  NimBLEServer* pServer = nullptr;
  NimBLECharacteristic* pNotifyCharacteristic = nullptr;
  NimBLECharacteristic* pControlCharacteristic = nullptr;
  bool deviceConnected = false;

  // Callbacks
  std::function<void()> onErase_;
  std::function<void(time_t)> onTimeSync_;
  std::function<void()> onTransferStart_;
  std::function<void()> onTransferComplete_;

  // Internal methods
  void handle_command(const std::string& command);
  void stream_all_records();
  bool send_record_packet(const consolidate::ConsolidatedRecord& record);
  void send_ack(const char* label);
  bool notify(const uint8_t* data, size_t length);

  // Connection callbacks
  class ServerCallbacks : public NimBLEServerCallbacks {
  public:
    ServerCallbacks(BLEServerClass* parent) : pParent(parent) {}
    void onConnect(NimBLEServer* pServer) override;
    void onDisconnect(NimBLEServer* pServer) override;
  private:
    BLEServerClass* pParent;
  };

  // Control characteristic callbacks
  class ControlCallbacks : public NimBLECharacteristicCallbacks {
  public:
    ControlCallbacks(BLEServerClass* parent) : pParent(parent) {}
    void onWrite(NimBLECharacteristic* characteristic) override;
  private:
    BLEServerClass* pParent;
  };

  ServerCallbacks serverCallbacks{this};
  ControlCallbacks controlCallbacks{this};
};

extern BLEServerClass bleServer;

#endif
