# ThinkPad PC AI Companion Agent

Hybrid telemetry monitor and command console for your Raspberry Pi 4 self-healing node.

## Quick Start on Ubuntu ThinkPad

1. **Install dependencies**:
   ```bash
   cd ~/rpi-selfhealing-distro/pc-agent
   python3 -m venv venv
   source venv/bin/activate
   pip install -r requirements.txt
   ```

2. **Run the AI Agent**:
   ```bash
   python3 agent.py
   ```
   *(Defaults to `broker.hivemq.com:1883`. To use a private HiveMQ cluster: `python3 agent.py <broker_host> <port>`)*

3. **Deploying RAUC OTA updates over the air**:
   From the interactive prompt:
   - Host the generated `.raucb` file:
     ```bash
     ai-agent> host ../build/tmp/deploy/images/raspberrypi4-64/
     ```
   - Trigger the update on Raspberry Pi:
     ```bash
     ai-agent> ota http://<YOUR_THINKPAD_IP>:8080/selfhealing-bundle-raspberrypi4-64.raucb
     ```
