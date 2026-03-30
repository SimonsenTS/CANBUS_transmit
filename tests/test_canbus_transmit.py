"""
Unit tests for CANBusTransmitter.
"""

import unittest
from unittest.mock import MagicMock, patch, call
import can

import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'src'))

from canbus_transmit import CANBusTransmitter


class TestCANBusTransmitter(unittest.TestCase):

    def _make_transmitter(self, interface='virtual', channel='vcan0', bitrate=500000):
        return CANBusTransmitter(interface=interface, channel=channel, bitrate=bitrate)

    # ------------------------------------------------------------------
    # connect / disconnect
    # ------------------------------------------------------------------

    @patch('canbus_transmit.can.interface.Bus')
    def test_connect_creates_bus(self, MockBus):
        tx = self._make_transmitter()
        tx.connect()
        MockBus.assert_called_once_with(interface='virtual', channel='vcan0', bitrate=500000)
        self.assertIsNotNone(tx.bus)

    @patch('canbus_transmit.can.interface.Bus')
    def test_disconnect_shuts_down_bus(self, MockBus):
        mock_bus = MockBus.return_value
        tx = self._make_transmitter()
        tx.connect()
        tx.disconnect()
        mock_bus.shutdown.assert_called_once()
        self.assertIsNone(tx.bus)

    def test_disconnect_when_not_connected_is_safe(self):
        tx = self._make_transmitter()
        # Should not raise
        tx.disconnect()

    # ------------------------------------------------------------------
    # transmit
    # ------------------------------------------------------------------

    @patch('canbus_transmit.can.interface.Bus')
    def test_transmit_sends_message(self, MockBus):
        mock_bus = MockBus.return_value
        tx = self._make_transmitter()
        tx.connect()
        tx.transmit(arbitration_id=0x123, data=b'\x01\x02\x03')
        mock_bus.send.assert_called_once()
        sent_msg = mock_bus.send.call_args[0][0]
        self.assertIsInstance(sent_msg, can.Message)
        self.assertEqual(sent_msg.arbitration_id, 0x123)
        self.assertEqual(bytes(sent_msg.data), b'\x01\x02\x03')
        self.assertFalse(sent_msg.is_extended_id)

    @patch('canbus_transmit.can.interface.Bus')
    def test_transmit_extended_frame(self, MockBus):
        mock_bus = MockBus.return_value
        tx = self._make_transmitter()
        tx.connect()
        tx.transmit(arbitration_id=0x1FFFFFFF, data=b'\xFF', is_extended_id=True)
        sent_msg = mock_bus.send.call_args[0][0]
        self.assertTrue(sent_msg.is_extended_id)
        self.assertEqual(sent_msg.arbitration_id, 0x1FFFFFFF)

    def test_transmit_raises_when_not_connected(self):
        tx = self._make_transmitter()
        with self.assertRaises(RuntimeError):
            tx.transmit(arbitration_id=0x100, data=b'\x00')

    @patch('canbus_transmit.can.interface.Bus')
    def test_transmit_raises_on_oversized_data(self, MockBus):
        tx = self._make_transmitter()
        tx.connect()
        with self.assertRaises(ValueError):
            tx.transmit(arbitration_id=0x100, data=b'\x00' * 9)

    @patch('canbus_transmit.can.interface.Bus')
    def test_transmit_max_data_length(self, MockBus):
        mock_bus = MockBus.return_value
        tx = self._make_transmitter()
        tx.connect()
        # Exactly 8 bytes should succeed
        tx.transmit(arbitration_id=0x100, data=b'\xAA' * 8)
        mock_bus.send.assert_called_once()

    @patch('canbus_transmit.can.interface.Bus')
    def test_transmit_empty_data(self, MockBus):
        mock_bus = MockBus.return_value
        tx = self._make_transmitter()
        tx.connect()
        tx.transmit(arbitration_id=0x100, data=b'')
        sent_msg = mock_bus.send.call_args[0][0]
        self.assertEqual(len(sent_msg.data), 0)

    # ------------------------------------------------------------------
    # context manager
    # ------------------------------------------------------------------

    @patch('canbus_transmit.can.interface.Bus')
    def test_context_manager_connects_and_disconnects(self, MockBus):
        mock_bus = MockBus.return_value
        with CANBusTransmitter(interface='virtual', channel='vcan0') as tx:
            self.assertIsNotNone(tx.bus)
        mock_bus.shutdown.assert_called_once()
        self.assertIsNone(tx.bus)

    @patch('canbus_transmit.can.interface.Bus')
    def test_context_manager_disconnects_on_exception(self, MockBus):
        mock_bus = MockBus.return_value
        try:
            with CANBusTransmitter(interface='virtual', channel='vcan0') as tx:
                raise ValueError("test error")
        except ValueError:
            pass
        mock_bus.shutdown.assert_called_once()


if __name__ == '__main__':
    unittest.main()
