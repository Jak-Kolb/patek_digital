const scanButton = document.getElementById("scan-button");
const networkStatus = document.getElementById("network-status");
const networkResults = document.getElementById("network-results");
const bleConnectButton = document.getElementById("ble-connect-button");
const bleDisconnectButton = document.getElementById("ble-disconnect-button");
const bleStatus = document.getElementById("ble-status");
const bleControls = document.getElementById("ble-controls");
const responseLog = document.getElementById("ble-response-log");
// Updated to match firmware UUIDs in app_config.h
const BLE_SERVICE_UUID = "12345678-1234-5678-1234-56789abc0000";
const BLE_DATA_CHAR_UUID = "12345678-1234-5678-1234-56789abc1001";
const BLE_CONTROL_CHAR_UUID = "12345678-1234-5678-1234-56789abc1002";
// Use data characteristic for notifications
const BLE_CHARACTERISTIC_UUID = BLE_DATA_CHAR_UUID;

function canonicalUuid(uuidLike) {
  if (!uuidLike) {
    return null;
  }

  if (typeof uuidLike === "string") {
    return uuidLike.toLowerCase();
  }

  if (typeof uuidLike === "number") {
    const hex = uuidLike.toString(16).padStart(4, "0");
    return `0000${hex}-0000-1000-8000-00805f9b34fb`;
  }

  return `${uuidLike}`;
}

const bleSession = {
  device: null,
  server: null,
  service: null,
  dataCharacteristic: null,
  controlCharacteristic: null,
};

const responseHistory = [];

function toggleConnectionButtons(connected) {
  if (bleConnectButton) {
    if (connected) {
      bleConnectButton.classList.add("hidden");
    } else {
      bleConnectButton.classList.remove("hidden");
    }
  }

  if (bleDisconnectButton) {
    if (connected) {
      bleDisconnectButton.classList.remove("hidden");
    } else {
      bleDisconnectButton.classList.add("hidden");
    }
  }
}

function showBleControls(visible) {
  if (!bleControls) {
    return;
  }

  if (visible) {
    bleControls.classList.remove("hidden");
  } else {
    bleControls.classList.add("hidden");
  }
  toggleConnectionButtons(visible);
}

function appendResponse(direction, message) {
  if (!responseLog) {
    return;
  }

  const timestamp = new Date().toLocaleTimeString();
  const prefix =
    direction === "send"
      ? "→"
      : direction === "error"
      ? "⚠"
      : direction === "info"
      ? "ℹ"
      : "←";
  const entry = `[${timestamp}] ${prefix} ${message}`;
  responseHistory.push(entry);

  while (responseHistory.length > 200) {
    responseHistory.shift();
  }

  responseLog.textContent = responseHistory.join("\n");
  responseLog.scrollTop = responseLog.scrollHeight;
}

function formatNotification(dataView) {
  if (!(dataView instanceof DataView)) {
    return String(dataView);
  }

  const view = new DataView(
    dataView.buffer,
    dataView.byteOffset,
    dataView.byteLength
  );
  const bytes = new Uint8Array(view.buffer, view.byteOffset, view.byteLength);
  if (!bytes.length) {
    return "(no data)";
  }

  const hex = Array.from(bytes)
    .map((value) => value.toString(16).padStart(2, "0"))
    .join(" ");

  const parts = [`hex ${hex}`];

  if (bytes.length >= 4) {
    const little = view.getUint32(0, true);
    const big = view.getUint32(0, false);
    parts.push(`u32-le ${little}`);
    if (big !== little) {
      parts.push(`u32-be ${big}`);
    }
  }

  try {
    const text = new TextDecoder("utf-8", { fatal: false }).decode(bytes);
    const printable = text.replace(/[\u0000-\u001F\u007F]/g, "").trim();
    if (printable) {
      parts.push(`text "${printable}"`);
    }
  } catch (error) {
    // Ignore UTF-8 decode issues and keep numeric formats only.
  }

  return parts.join(" | ");
}

function resetBleSession(message) {
  if (bleSession.dataCharacteristic) {
    try {
      bleSession.dataCharacteristic.removeEventListener(
        "characteristicvaluechanged",
        handleNotification
      );
      bleSession.dataCharacteristic.stopNotifications().catch(() => undefined);
    } catch (error) {
      // Ignore cleanup errors
    }
  }

  if (bleSession.device) {
    bleSession.device.removeEventListener(
      "gattserverdisconnected",
      handleGattDisconnect
    );
  }

  bleSession.device = null;
  bleSession.server = null;
  bleSession.service = null;
  bleSession.dataCharacteristic = null;
  bleSession.controlCharacteristic = null;

  showBleControls(false);

  if (message) {
    bleStatus.textContent = message;
    appendResponse("info", message);
  }
}

function handleNotification(event) {
  const formatted = formatNotification(event.target.value);
  appendResponse("receive", formatted);
}

function renderNetworkDevices(devices) {
  networkResults.innerHTML = "";

  if (!devices.length) {
    const noneItem = document.createElement("li");
    noneItem.className = "device-item";
    noneItem.textContent = "None";
    networkResults.appendChild(noneItem);
    return;
  }

  devices.forEach((device) => {
    const item = document.createElement("li");
    item.className = "device-item";

    const title = document.createElement("strong");
    title.textContent = device.ip;

    const meta = document.createElement("p");
    meta.className = "device-meta";
    meta.textContent = `${device.mac} • ${device.interface}`;

    item.appendChild(title);
    item.appendChild(meta);
    networkResults.appendChild(item);
  });
}

async function scanNetwork() {
  try {
    scanButton.disabled = true;
    networkStatus.textContent = "Scanning local network for ESP32 devices…";
    networkResults.innerHTML = "";

    const response = await fetch(`/api/scan?ts=${Date.now()}`);
    if (!response.ok) {
      throw new Error("Request failed");
    }

    const payload = await response.json();
    const scannedLabel =
      Array.isArray(payload.scannedSubnets) && payload.scannedSubnets.length
        ? ` Scanned: ${payload.scannedSubnets.join(", ")}`
        : "";
    networkStatus.textContent =
      (payload.message || "Scan complete.") + scannedLabel;
    renderNetworkDevices(payload.devices || []);
  } catch (error) {
    networkStatus.textContent = `Scan failed: ${error.message || error}`;
    renderNetworkDevices([]);
  } finally {
    scanButton.disabled = false;
  }
}

function handleGattDisconnect() {
  resetBleSession("Disconnected from ESP32 device.");
}

async function connectBle() {
  if (!navigator.bluetooth) {
    bleStatus.textContent =
      "Web Bluetooth is not supported in this browser. Try Chrome on desktop with HTTPS.";
    return;
  }

  try {
    bleConnectButton.disabled = true;
    bleStatus.textContent = "Requesting ESP32 over BLE…";

    const primaryUuid = canonicalUuid(BLE_SERVICE_UUID);
    const device = await navigator.bluetooth.requestDevice({
      filters: [
        { namePrefix: "ESP32" },
        { name: "ESP32-DataNode" },
        { services: [primaryUuid] },
      ],
      optionalServices: ["battery_service", primaryUuid],
    });

    device.addEventListener("gattserverdisconnected", handleGattDisconnect);

    const server = await device.gatt.connect();

    let service;
    try {
      service = await server.getPrimaryService(primaryUuid);
    } catch (serviceError) {
      const services = await server.getPrimaryServices();
      const available = services
        .map((svc) => canonicalUuid(svc.uuid))
        .join("\n");
      appendResponse(
        "error",
        `Service ${primaryUuid} not found. Available services:\n${
          available || "(none)"
        }`
      );
      throw serviceError;
    }

    const dataCharacteristic = await service.getCharacteristic(
      canonicalUuid(BLE_DATA_CHAR_UUID)
    );

    const controlCharacteristic = await service.getCharacteristic(
      canonicalUuid(BLE_CONTROL_CHAR_UUID)
    );

    await dataCharacteristic.startNotifications();
    dataCharacteristic.addEventListener(
      "characteristicvaluechanged",
      handleNotification
    );

    bleSession.device = device;
    bleSession.server = server;
    bleSession.service = service;
    bleSession.dataCharacteristic = dataCharacteristic;
    bleSession.controlCharacteristic = controlCharacteristic;

    showBleControls(true);
    bleStatus.textContent = `Connected to ${device.name || device.id}`;
    appendResponse("info", `Connected to ${device.name || device.id}`);
  } catch (error) {
    bleStatus.textContent = `BLE connection failed: ${error.message || error}`;
    appendResponse("error", `BLE connection failed: ${error.message || error}`);
    resetBleSession();
  } finally {
    bleConnectButton.disabled = false;
  }
}

async function disconnectBle() {
  try {
    bleDisconnectButton.disabled = true;
    bleStatus.textContent = "Disconnecting from ESP32…";

    if (bleSession.device && bleSession.device.gatt.connected) {
      await bleSession.device.gatt.disconnect();
      appendResponse("info", "Manually disconnected from device");
    }

    resetBleSession("Disconnected from ESP32 device.");
  } catch (error) {
    bleStatus.textContent = `Disconnect failed: ${error.message || error}`;
    appendResponse("error", `Disconnect failed: ${error.message || error}`);
  } finally {
    bleDisconnectButton.disabled = false;
  }
}

async function sendBleCommand(command) {
  if (!bleSession.controlCharacteristic) {
    appendResponse("error", "Not connected to ESP32");
    return;
  }

  try {
    const encoder = new TextEncoder();
    const data = encoder.encode(command);
    await bleSession.controlCharacteristic.writeValue(data);
    appendResponse("send", `Command: ${command}`);
  } catch (error) {
    appendResponse("error", `Failed to send command: ${error.message}`);
  }
}

async function sendListCommand() {
  await sendBleCommand("LIST");
}

async function sendSendCommand() {
  appendResponse("info", "Requesting data transfer from ESP32...");
  await sendBleCommand("SEND");
}

async function sendEraseCommand() {
  if (!confirm("Are you sure you want to erase all data on the ESP32?")) {
    return;
  }
  appendResponse("info", "Sending ERASE command...");
  await sendBleCommand("ERASE");
}

async function syncTime() {
  const epochSeconds = Math.floor(Date.now() / 1000);
  await sendBleCommand(`TIME:${epochSeconds}`);
}

scanButton.addEventListener("click", scanNetwork);
bleConnectButton.addEventListener("click", connectBle);
bleDisconnectButton.addEventListener("click", disconnectBle);

// Add event listeners for BLE command buttons
document.getElementById("ble-sync-time")?.addEventListener("click", syncTime);
document.getElementById("ble-list")?.addEventListener("click", sendListCommand);
document.getElementById("ble-send")?.addEventListener("click", sendSendCommand);
document
  .getElementById("ble-erase")
  ?.addEventListener("click", sendEraseCommand);

document.addEventListener("DOMContentLoaded", () => {
  networkStatus.textContent =
    "Press scan to search for ESP32 devices on your network.";
  bleStatus.textContent = "Not connected.";
  showBleControls(false);
  responseLog.textContent = "";
});
