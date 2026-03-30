"""
CANBUS Transmit - Module for transmitting CAN bus messages.
"""

import can


class CANBusTransmitter:
    """Handles transmitting messages on a CAN bus interface."""

    def __init__(self, interface: str, channel: str, bitrate: int = 500000):
        """
        Initialize the CAN bus transmitter.

        :param interface: CAN interface type (e.g. 'socketcan', 'kvaser', 'virtual')
        :param channel: CAN channel name (e.g. 'can0', 'vcan0')
        :param bitrate: CAN bus bitrate in bits per second (default 500000)
        """
        self.interface = interface
        self.channel = channel
        self.bitrate = bitrate
        self.bus = None

    def connect(self) -> None:
        """Open connection to the CAN bus."""
        self.bus = can.interface.Bus(
            interface=self.interface,
            channel=self.channel,
            bitrate=self.bitrate,
        )

    def disconnect(self) -> None:
        """Close the CAN bus connection."""
        if self.bus is not None:
            self.bus.shutdown()
            self.bus = None

    def transmit(self, arbitration_id: int, data: bytes, is_extended_id: bool = False) -> None:
        """
        Transmit a CAN message.

        :param arbitration_id: CAN message identifier (11-bit standard or 29-bit extended)
        :param data: Message payload, up to 8 bytes
        :param is_extended_id: Use 29-bit extended frame format (default False)
        :raises RuntimeError: If the bus is not connected
        :raises ValueError: If data exceeds 8 bytes
        """
        if self.bus is None:
            raise RuntimeError("CAN bus is not connected. Call connect() first.")
        if len(data) > 8:
            raise ValueError(f"CAN data must be at most 8 bytes, got {len(data)}.")

        message = can.Message(
            arbitration_id=arbitration_id,
            data=data,
            is_extended_id=is_extended_id,
        )
        self.bus.send(message)

    def __enter__(self):
        self.connect()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.disconnect()
        return False
