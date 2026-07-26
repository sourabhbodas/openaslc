---
name: 'Hardware / Board Support Request '
about: For users requesting support for a new SBC (e.g., BeagleBone, VisionFive, Orange
  Pi) or I/O protocol (Modbus, CAN bus, I2C/SPI).
title: "[HW]"
labels: ''
assignees: ''

---

name: "📟 New Hardware / Board Support"
description: Request support for a new SBC, GPIO driver, or industrial I/O module
title: "[Hardware]: "
labels: ["hardware", "enhancement"]
body:
  - type: textarea
    id: board_details
    attributes:
      label: Target Hardware / Chipset Details
      description: Specify the board model, SoC, or I/O controller (e.g., STM32, ESP32, Rockchip, MCP23017 GPIO expander).
      placeholder: Details on the hardware...
    validations:
      required: true

  - type: dropdown
    id: interface_type
    attributes:
      label: Interface / Bus Type
      options:
        - Native Onboard GPIO (Linux libgpiod)
        - SPI / I2C
        - Industrial Bus (Modbus RTU/TCP, CAN bus, EtherCAT)
        - Custom Bus / Serial
    validations:
      required: true

  - type: textarea
    id: use_case
    attributes:
      label: Use Case & Motivation
      description: Why is this hardware support useful for OpenASLC control logic?
    validations:
      required: false
