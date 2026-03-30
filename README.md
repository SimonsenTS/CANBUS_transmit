# CANBUS_transmit
Transmit board of CAN commands

## Overview

`CANBusTransmitter` is a Python module for transmitting messages on a CAN bus using the [python-can](https://python-can.readthedocs.io/) library.

## Requirements

```
pip install -r requirements.txt
```

## Usage

```python
from src.canbus_transmit import CANBusTransmitter

# Using as a context manager (recommended)
with CANBusTransmitter(interface='socketcan', channel='can0', bitrate=500000) as tx:
    # Standard 11-bit frame
    tx.transmit(arbitration_id=0x123, data=b'\x01\x02\x03')

    # Extended 29-bit frame
    tx.transmit(arbitration_id=0x1FFFFFFF, data=b'\xDE\xAD\xBE\xEF', is_extended_id=True)

# Manual connect/disconnect
tx = CANBusTransmitter(interface='socketcan', channel='can0')
tx.connect()
tx.transmit(arbitration_id=0x200, data=b'\xFF\x00')
tx.disconnect()
```

### Parameters

| Parameter | Description | Default |
|-----------|-------------|---------|
| `interface` | CAN interface type (`socketcan`, `kvaser`, `virtual`, …) | required |
| `channel` | CAN channel name (`can0`, `vcan0`, …) | required |
| `bitrate` | Bus speed in bits/s | `500000` |

### `transmit(arbitration_id, data, is_extended_id=False)`

| Parameter | Description |
|-----------|-------------|
| `arbitration_id` | CAN message identifier (11-bit standard or 29-bit extended) |
| `data` | Payload bytes, max 8 bytes |
| `is_extended_id` | Set `True` for 29-bit extended frame format |

## Running Tests

```
python -m pytest tests/ -v
```
