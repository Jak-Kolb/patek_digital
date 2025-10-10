const express = require("express");
const cors = require("cors");
const path = require("path");
const os = require("os");
const util = require("util");
const { exec } = require("child_process");
const ping = require("ping");

const execAsync = util.promisify(exec);
const app = express();
const PORT = process.env.PORT || 5173;

// Known Espressif (ESP32) MAC address prefixes (OUIs)
const ESPRESSIF_OUIS = new Set([
  "24:0A:C4",
  "30:AE:A4",
  "34:86:5D",
  "3C:71:BF",
  "40:22:D8",
  "44:1D:64", // Additional ESP32 OUI
  "84:CC:A8",
  "84:F7:03",
  "A4:CF:12",
  "B4:E6:2D",
  "BC:DD:C2",
  "C4:4F:33",
  "CC:50:E3",
  "D8:A0:1D",
  "EC:62:60",
  "EC:94:CB",
  "EC:FA:BC",
  "F4:12:FA",
]);

app.use(cors());
app.use(express.json({ limit: "32kb" }));
app.use(express.static(path.join(__dirname, "public")));

function normalizeMac(mac) {
  return mac
    .replace(/-/g, ":")
    .split(":")
    .filter(Boolean)
    .map((segment) => segment.padStart(2, "0"))
    .join(":")
    .toUpperCase();
}

function looksLikeEspressif(mac) {
  const normalized = normalizeMac(mac);
  const prefix = normalized.split(":").slice(0, 3).join(":");
  return ESPRESSIF_OUIS.has(prefix);
}

function parseArpTable(raw) {
  const entries = [];
  const lineRegex = /\((?<ip>[^)]+)\) at (?<mac>[^ ]+) on (?<iface>[^ ]+)/;

  raw.split("\n").forEach((line) => {
    const match = line.match(lineRegex);
    if (!match) {
      return;
    }

    const { ip, mac, iface } = match.groups;
    if (!ip || !mac || mac.toLowerCase() === "incomplete") {
      return;
    }

    const normalizedMac = normalizeMac(mac);
    const isEspressif = looksLikeEspressif(mac);

    entries.push({
      ip,
      mac: normalizedMac,
      interface: iface,
      vendor: isEspressif ? "Espressif (ESP32)" : "Unknown",
      isEsp32: isEspressif,
    });
  });

  return entries;
}

function getLocalSubnets() {
  const interfaces = os.networkInterfaces();
  const seen = new Map();

  Object.entries(interfaces).forEach(([iface, configs]) => {
    configs
      .filter((cfg) => cfg.family === "IPv4" && !cfg.internal)
      .forEach((cfg) => {
        const parts = cfg.address.split(".");
        if (parts.length === 4) {
          const base = parts.slice(0, 3).join(".");
          const key = `${iface}-${base}`;
          if (!seen.has(key)) {
            seen.set(key, {
              interface: iface,
              base,
              address: cfg.address,
            });
          }
        }
      });
  });

  return Array.from(seen.values());
}

async function pingSubnet(subnet) {
  const ips = [];
  for (let host = 1; host <= 254; host += 1) {
    const candidate = `${subnet.base}.${host}`;
    if (candidate === subnet.address) {
      continue; // skip the host we are already on
    }
    ips.push(candidate);
  }

  const concurrency = 25;
  let index = 0;

  async function worker() {
    while (true) {
      const currentIndex = index;
      index += 1;
      if (currentIndex >= ips.length) {
        break;
      }

      const current = ips[currentIndex];
      try {
        await ping.promise.probe(current, {
          timeout: 1,
        });
      } catch (error) {
        // Ignore individual ping errors; absence of reply is expected for most hosts
      }
    }
  }

  const workers = Array.from({ length: concurrency }, () => worker());
  await Promise.all(workers);
}

async function refreshArpCache(subnets) {
  if (!subnets.length) {
    return;
  }

  await Promise.all(subnets.map(pingSubnet));
}

async function collectEsp32Devices() {
  try {
    const subnets = getLocalSubnets();
    if (!subnets.length) {
      return {
        success: true,
        devices: [],
        scannedSubnets: [],
        message: "No active network interfaces detected.",
      };
    }

    await refreshArpCache(subnets);

    const { stdout } = await execAsync("arp -a");
    const entries = parseArpTable(stdout);
    const espDevices = entries.filter((entry) => entry.isEsp32);

    return {
      success: true,
      devices: espDevices,
      scannedSubnets: Array.from(
        new Set(subnets.map((net) => `${net.base}.0/24`))
      ),
    };
  } catch (error) {
    return {
      success: false,
      error: error.message || "Unknown error while scanning network.",
    };
  }
}

app.get("/api/scan", async (_req, res) => {
  const result = await collectEsp32Devices();

  if (!result.success) {
    return res.status(500).json({
      success: false,
      message: "Failed to scan network for ESP32 devices.",
      error: result.error,
    });
  }

  return res.json({
    success: true,
    devices: result.devices,
    scannedSubnets: result.scannedSubnets,
    message: result.devices.length
      ? `Found ${result.devices.length} ESP32 device(s).`
      : "No ESP32 devices found on the scanned subnets.",
  });
});

app.get("*", (_req, res) => {
  res.sendFile(path.join(__dirname, "public", "index.html"));
});

app.listen(PORT, () => {
  // eslint-disable-next-line no-console
  console.log(`Test app listening on http://localhost:${PORT}`);
});
