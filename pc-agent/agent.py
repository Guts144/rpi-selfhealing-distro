#!/usr/bin/env python3
"""
PC AI Diagnostic & Remediation Agent
Hybrid companion running on Ubuntu ThinkPad communicating with
Raspberry Pi 4 embedded self-healing node via HiveMQ MQTT Broker.
"""

import sys
import os
import json
import time
import threading
import http.server
import socketserver
import paho.mqtt.client as mqtt
from colorama import init, Fore, Style
from tabulate import tabulate

init(autoreset=True)

# Configuration defaults (auto-loaded from constants.txt if available)
def load_constants():
    conf = {
        "BROKER_URI": "tcp://broker.hivemq.com:1883",
        "CLIENT_ID": "thinkpad-ai-agent",
        "USERNAME": "",
        "PASSWORD": "",
        "TOPIC_TELEMETRY": "rpi4/selfhealing/telemetry",
        "TOPIC_EVENTS": "rpi4/selfhealing/events",
        "TOPIC_COMMANDS": "rpi4/selfhealing/commands"
    }
    candidates = [
        "constants.txt",
        "../layers/meta-selfhealing/recipes-core/local-agent/files/constants.txt"
    ]
    for path in candidates:
        if os.path.exists(path):
            with open(path, "r") as f:
                for line in f:
                    line = line.strip()
                    if not line or line.startswith("#") or "=" not in line:
                        continue
                    k, v = line.split("=", 1)
                    conf[k.strip()] = v.strip()
            break
    return conf

_CFG = load_constants()

# Extract host and port from BROKER_URI
_uri = _CFG["BROKER_URI"].replace("tcp://", "").replace("ssl://", "")
if ":" in _uri:
    DEFAULT_BROKER, _p = _uri.split(":", 1)
    DEFAULT_PORT = int(_p)
else:
    DEFAULT_BROKER = _uri
    DEFAULT_PORT = 1883

TOPIC_TELEMETRY = _CFG["TOPIC_TELEMETRY"]
TOPIC_EVENTS = _CFG["TOPIC_EVENTS"]
TOPIC_COMMANDS = _CFG["TOPIC_COMMANDS"]

class AnomalyDetector:
    """Agentic AI engine analyzing telemetry and suggesting embedded remediations."""
    
    @staticmethod
    def analyze(data: dict) -> list:
        findings = []
        
        # 1. Thermal check
        cpu_temp = data.get("cpu_temp_c", 0.0)
        if cpu_temp > 80.0:
            findings.append((
                "CRITICAL",
                f"CPU temperature is {cpu_temp:.1f}°C (Exceeds thermal throttling limit 80°C)",
                "Check physical heatsink/fan; consider throttling CPU frequency governor."
            ))
        elif cpu_temp > 70.0:
            findings.append((
                "WARNING",
                f"CPU temperature elevated at {cpu_temp:.1f}°C",
                "Monitor load; avoid running heavy compile tasks directly on target."
            ))

        # 2. Memory check
        ram_pct = data.get("ram_usage_percent", 0.0)
        if ram_pct > 90.0:
            findings.append((
                "CRITICAL",
                f"RAM usage critically high at {ram_pct:.1f}%",
                "OOM killer risk; inspect memory footprint of running services."
            ))
        elif ram_pct > 80.0:
            findings.append((
                "WARNING",
                f"RAM usage high at {ram_pct:.1f}%",
                "Check for potential memory leak in daemons."
            ))

        # 3. Wi-Fi Link Quality
        rssi = data.get("wifi_rssi_dbm", 0)
        if rssi < -80 and rssi != 0:
            findings.append((
                "WARNING",
                f"Wi-Fi RSSI degraded ({rssi} dBm)",
                "Target is in weak RF zone; packet re-transmits likely; reposition antenna."
            ))

        # 4. Network Self-Healing Frequency
        failures = data.get("consecutive_net_failures", 0)
        events = data.get("healing_events", [])
        if failures > 0:
            findings.append((
                "WARNING",
                f"Device experienced {failures} network recovery attempt(s)",
                "Local agent is actively triggering progressive network healer ladder."
            ))
        
        # 5. Boot slot verification
        slot = data.get("active_slot", "unknown")
        if slot == "unknown":
            findings.append((
                "INFO",
                "Active RAUC boot slot is undetermined",
                "Verify U-Boot cmdline passing 'rauc.slot=A|B'."
            ))

        return findings

class LocalFileServer(threading.Thread):
    """Background HTTP server for serving .raucb bundles to RPi."""
    def __init__(self, port=8080, directory="."):
        super().__init__(daemon=True)
        self.port = port
        self.directory = directory
        self.server = None

    def run(self):
        handler = lambda *args: http.server.SimpleHTTPRequestHandler(*args, directory=self.directory)
        try:
            with socketserver.TCPServer(("", self.port), handler) as httpd:
                self.server = httpd
                httpd.serve_forever()
        except Exception as e:
            print(f"[HTTP Server] Error: {e}")

class PcAgent:
    def __init__(self, broker: str, port: int):
        self.broker = broker
        self.port = port
        self.client = mqtt.Client(client_id="thinkpad-ai-agent", clean_session=True)
        if _CFG.get("USERNAME") and _CFG.get("PASSWORD"):
            self.client.username_pw_set(_CFG["USERNAME"], _CFG["PASSWORD"])
        if port == 8883 or _CFG["BROKER_URI"].startswith("ssl://"):
            self.client.tls_set()
        self.latest_telemetry = {}
        self.latest_events = []
        self.connected = False
        self.lock = threading.Lock()

        self.client.on_connect = self.on_connect
        self.client.on_message = self.on_message
        self.client.on_disconnect = self.on_disconnect

    def on_connect(self, client, userdata, flags, rc):
        if rc == 0:
            self.connected = True
            print(Fore.GREEN + f"[+] Connected to HiveMQ Broker ({self.broker}:{self.port})")
            client.subscribe([(TOPIC_TELEMETRY, 1), (TOPIC_EVENTS, 1)])
            print(Fore.CYAN + f"[+] Subscribed to {TOPIC_TELEMETRY} and {TOPIC_EVENTS}")
        else:
            print(Fore.RED + f"[-] Connection failed with code {rc}")

    def on_disconnect(self, client, userdata, rc):
        self.connected = False
        print(Fore.YELLOW + f"[!] Disconnected from broker (rc={rc})")

    def on_message(self, client, userdata, msg):
        try:
            payload = json.loads(msg.payload.decode("utf-8"))
            with self.lock:
                if msg.topic == TOPIC_TELEMETRY:
                    self.latest_telemetry = payload
                elif msg.topic == TOPIC_EVENTS:
                    self.latest_events.append(payload)
                    if len(self.latest_events) > 20:
                        self.latest_events.pop(0)
                    print(Fore.MAGENTA + f"\n[RPI EVENT] {json.dumps(payload, indent=2)}")
        except Exception as e:
            print(Fore.RED + f"[-] Error decoding MQTT message: {e}")

    def send_command(self, cmd_dict: dict):
        payload = json.dumps(cmd_dict)
        self.client.publish(TOPIC_COMMANDS, payload, qos=1)
        print(Fore.CYAN + f"[->] Sent command to RPi: {payload}")

    def show_dashboard(self):
        with self.lock:
            data = dict(self.latest_telemetry)

        if not data:
            print(Fore.YELLOW + "[!] No telemetry received from Raspberry Pi yet.")
            return

        table = [
            ["Node ID", data.get("device_id", "N/A")],
            ["Active RAUC Slot", Fore.GREEN + str(data.get("active_slot", "N/A"))],
            ["Active Medium", Fore.CYAN + str(data.get("active_medium", "N/A"))],
            ["Network Status", Fore.GREEN if data.get("network_status") == "ONLINE" else Fore.RED + str(data.get("network_status", "N/A"))],
            ["Uptime (sec)", str(data.get("uptime_sec", 0))],
            ["CPU Temperature", f"{data.get('cpu_temp_c', 0.0):.1f} °C"],
            ["RAM Usage", f"{data.get('ram_usage_percent', 0.0):.1f} %"],
            ["Load Avg (1m, 5m, 15m)", f"{data.get('load_1m', 0):.2f}, {data.get('load_5m', 0):.2f}, {data.get('load_15m', 0):.2f}"],
            ["Wi-Fi Signal (RSSI)", f"{data.get('wifi_rssi_dbm', 0)} dBm"],
            ["Consecutive Failures", str(data.get("consecutive_net_failures", 0))]
        ]

        print("\n" + "=" * 55)
        print(Fore.CYAN + Style.BRIGHT + "   RASPBERRY PI 4 HYBRID TELEMETRY DASHBOARD   ")
        print("=" * 55)
        print(tabulate(table, headers=["Metric", "Value"], tablefmt="grid"))

        # Agentic AI Anomaly Evaluation
        findings = AnomalyDetector.analyze(data)
        if findings:
            print("\n" + Fore.YELLOW + Style.BRIGHT + "--- AI DIAGNOSTIC FINDINGS & ACTIONS ---")
            for severity, reason, suggestion in findings:
                color = Fore.RED if severity == "CRITICAL" else (Fore.YELLOW if severity == "WARNING" else Fore.BLUE)
                print(f"[{color}{severity}{Style.RESET_ALL}] {reason}")
                print(f"  -> {Fore.GREEN}Action: {suggestion}")
        else:
            print("\n" + Fore.GREEN + "[AI Agent] All telemetry metrics nominal. System operating healthy.")

        events = data.get("healing_events", [])
        if events:
            print("\n" + Fore.BLUE + Style.BRIGHT + "--- RECENT EMBEDDED SELF-HEALING HISTORY ---")
            ev_table = [[e.get("timestamp"), f"Stage {e.get('stage')}", e.get("action"), "OK" if e.get("success") else "FAILED"] for e in events[-5:]]
            print(tabulate(ev_table, headers=["Timestamp", "Stage", "Action Taken", "Result"], tablefmt="simple"))

    def start(self):
        print(Fore.BLUE + f"[*] Connecting to HiveMQ Broker: {self.broker}:{self.port}...")
        self.client.connect_async(self.broker, self.port, 60)
        self.client.loop_start()

        # Allow initial handshake
        time.sleep(1)

        print("\n" + Fore.GREEN + Style.BRIGHT + "================================================")
        print(Fore.GREEN + Style.BRIGHT + "   ThinkPad PC AI Agent Interactive Console     ")
        print(Fore.GREEN + Style.BRIGHT + "================================================")
        print("Available Commands:")
        print("  status         - Display live health dashboard & AI diagnostics")
        print("  ping           - Test round-trip latency with embedded node")
        print("  heal           - Trigger autonomous network self-heal cycle on RPi")
        print("  mark_good      - Manually confirm active RAUC boot slot is good")
        print("  ota <url>      - Install RAUC update bundle (.raucb) on RPi")
        print("  reboot         - Reboot Raspberry Pi node")
        print("  host <dir>     - Start local HTTP server to host .raucb bundle")
        print("  help           - Show command menu")
        print("  exit / quit    - Terminate PC AI Agent\n")

        while True:
            try:
                cmd_line = input(Fore.YELLOW + "ai-agent> " + Style.RESET_ALL).strip()
                if not cmd_line:
                    continue

                parts = cmd_line.split()
                cmd = parts[0].lower()

                if cmd in ["exit", "quit"]:
                    print("[*] Shutting down PC Agent...")
                    self.client.loop_stop()
                    self.client.disconnect()
                    sys.exit(0)
                elif cmd == "status":
                    self.show_dashboard()
                elif cmd == "ping":
                    self.send_command({"command": "ping"})
                elif cmd == "heal":
                    self.send_command({"command": "heal"})
                elif cmd == "mark_good":
                    self.send_command({"command": "mark_good"})
                elif cmd == "reboot":
                    self.send_command({"command": "reboot"})
                elif cmd == "ota":
                    if len(parts) < 2:
                        print(Fore.RED + "Usage: ota <http_bundle_url>")
                    else:
                        url = parts[1]
                        self.send_command({"command": "rauc_install", "url": url})
                elif cmd == "host":
                    dir_path = parts[1] if len(parts) > 1 else "."
                    server = LocalFileServer(port=8080, directory=dir_path)
                    server.start()
                    print(Fore.GREEN + f"[+] Hosting bundles at http://0.0.0.0:8080 from '{dir_path}'")
                elif cmd == "help":
                    print("Commands: status, ping, heal, mark_good, ota <url>, reboot, host <dir>, exit")
                else:
                    print(Fore.RED + f"Unknown command '{cmd}'. Type 'help' for options.")
            except (KeyboardInterrupt, EOFError):
                print("\n[*] Exiting...")
                self.client.loop_stop()
                self.client.disconnect()
                break

if __name__ == "__main__":
    broker = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_BROKER
    port = int(sys.argv[2]) if len(sys.argv) > 2 else DEFAULT_PORT
    agent = PcAgent(broker, port)
    agent.start()
