# smart-water-cooled-bed-controller
An open-source, IoT-enabled fluid thermal management system engineered to actively regulate mattress sleep temperatures. Powered by an ESP32 microcontroller, dual isolated liquid loops, a custom high-power KiCad PCB handling up to 18A continuous current, and a local responsive Web dashboard.

---

## System Overview & Architecture

The system uses a 4-module parallel thermoelectric (Peltier) array to chill a continuous fluid loop running through tubing a mattress topper. A seperate, high-flow exhaust loop pulls waste heat into a 360mm radiator to maintain thermal efficiency without acoustic disruption.

```text
                                 [ 12V 29A Mean Well SMPS ]
                                              │
                    ┌─────────────────────────┴─────────────────────────┐
                    ▼                                                   ▼
       [ 12V-to-5V Buck Converter ]                       [ 4x 7.5A Fused Power Rails ]
                    │                                                   │
                    ▼                                                   ▼
           [ ESP32 Controller ] ──(3.3V Logic)──► [ 4x IRLZ44N N-Channel MOSFETs ]
             │              │                                           │
 (25 kHz PWM)│              └──(1-Wire Bus)──┐                          ▼
             ▼                               ▼                  [ 4x Peltier Modules ]
   [ 2x DDC Fluid Pumps ]          [ 2x DS18B20 Probes ]          (Cooling Engine)
             │                               │                          │
             ▼                               ▼                          ▼
   [ Dual Fluid Circuits ] ◄─────────────────┴──────────────────────────┘
```

## Technical Specifications

| Specification | Implementation | 
| :--- | :--- |
| **Microcontroller** | ESP32-WROOM-32 (240 MHz Dual-Core, 2.4 GHz SoftAP) |
| **Power Supply Unit** | Mean Well LRS-340-12 (12V DC, 29A, 348W continuous) |
| **Cooling Engine** | 4x Parallel Thermoelectric Modules (TEC1-12706 / CP Series) |
| **Current Handling** | 18.0A continuous load across 4 distributed 7.5 A fused sub-channels |
| **Switching Architecture** | Low-Side N-channel Logic MOSFETs (IRLZ44N) with 10kΩ gate anchors |
| **Temperature Telemetry** | Dual Dallas 1-wire DS18B20 waterproof probes (4.7kΩ pull-up) |
| **Fluid Management** | Dual isolated DDC PWM pumps + 360mm copper-core radiator |
| **Control Logic** | Non-blocking ±0.5°C hysteresis with 55°C hot-loop thermal runaway trip |

---

## Custom Hardware & PCB Engineering (KiCad)

The power management shield was designed in **KiCad** to solve high-current thermal stress and signal isolation:

* **Distributed Power Architecture:** Instead of routing 18A through a single trace , the board splits current into 4 - 4.5A branches (2 copper traces each) protected by individual 7.5A ATO blade fuses.
* **Trace Sizing (IPC-2221):** Power traces are routed at 3.5mm width on 1oz copper with teardrops on high-stress terminal pads to eliminate delamination risks.
* **Unified Ground Plane:** A full-coverage bottom-layer copper pour ties the 12V DC ground and 5V ESP32 reference together, eliminating gate-switching floating transients.
* **Gate Protection:** Features a 10kΩ pull-down resistor to prevent floating gates during bootup, plus individual 150Ω gate resistors on each MOSFET to eliminate ringing reflections.

---

## Firmware & Software Architecture

The firmware is written in C++ for the ESP32 platform:

* **Embedded SoftAP Web Dashboard:** Hosts a zero-dependency HTML/CSS/JavaScript control dashboard served directly from flash memory. No internet connection or cloud service is required.
* **Hysteresis Temperature Loop:** Enforces a ±0.5°C switching window to prevent short-cycling the MOSFETs and damaging the Peltier ceramic layers.
* **Hardware Watchdog Interlocks:**
  * Auto-shuts off the MOSFETs if the hot-loop sensor exceeds **55.0°C**.
  * Emergency stops if a 1-Wire sensor disconnects (reading `-127.0°C` or `85.0°C`).

---

## Key Engineering Decisions & Trade-Offs

1. **Parallel Array vs. Cascaded (Stacked) Modules:**
   * *Trade-off:* Stacking modules increases temperature difference ($\Delta T$) but drastically chokes heat capacity ($Q_c$) because the outer Peltier must absorb the inner unit's electrical waste heat.
   * *Decision:* Used a 4-module parallel array to maximize continuous wattage removal from the bed loop.
2. **Dual Isolated Fluid Loops:**
   * *Trade-off:* Requires two pumps and separate plumbing lines.
   * *Decision:* Prevents mixing chilled bed fluid with high-temperature radiator exhaust fluid, maximizing heat dissipation.
3. **All-Aluminum Metallurgy & Closed-Cell Insulation:**
   * *Decision:* Standardized on aluminum water blocks and fittings to prevent galvanic corrosion, and insulated cold lines with neoprene to eliminate condensation on internal electronics.

---

## Hardware Bring-Up & Validation Protocol

The physical system was brought up following a 5-phase staged protocol:

1. **Cold Continuity Verification:** Multimeter checks for shorts across 12V and GND screw terminals.
2. **Buck Calibration:** Regulated onboard converter output to **5.00V DC** before inserting the ESP32.
3. **Gate Switching Verification:** Validated 0.0V (OFF) and 3.3V (ON) logic levels on MOSFET gates.
4. **24-Hour Hydrodynamic Leak Test:** Fluid loops pressurized and circulated with electronics isolated.
5. **Full-Load Thermal Soak:** Validated 18A continuous draw under full cooling engagement.
  
